/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 05/08/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/rwlock.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/time.h"
#include "lib/misc.h" /* C2_SET_ARR0 */
#include "lib/trace.h" /* c2_console_printf */
>>>>>>> 1) Addressed Nikita's comments, 2) changed mutexes to rwlocks inorder to allow concurrent read, 3) reduced scope of service types list (c2_rstypes -> rstypes) to private to reqh_service.c, 4) provided interfaces for protected access to rstypes list. 5) changed reqh shutdown logic, removed sleep, instead using c2_chan_wait, 6) Few more misc changes.

#include "reqh/reqh.h"
#include "reqh/reqh_service.h"

/**
   @addtogroup reqh
   @{
 */

/**
   static global list of service types.
   Holds struct c2_reqh_service_type instances linked via
   c2_reqh_service_type::rst_linkage.

   @see struct c2_reqh_service_type
 */
static struct c2_tl rstypes;

/** Protects access to list rstypes. */
static struct c2_rwlock rstypes_rwlock;

C2_TL_DESCR_DEFINE(rstypes, "reqh service types", static,
                   struct c2_reqh_service_type, rst_linkage, rst_magix,
                   C2_RHS_MAGIX, C2_RHS_MAGIX_HEAD);

C2_TL_DEFINE(rstypes, static, struct c2_reqh_service_type);

static struct c2_bob_type rstypes_bob;
C2_BOB_DEFINE(static, &rstypes_bob, c2_reqh_service_type);

bool c2_reqh_service_invariant(const struct c2_reqh_service *svc)
{
	return c2_reqh_service_bob_check(svc) &&
	C2_IN(svc->rs_state, (C2_RST_INITIALISING, C2_RST_INITIALISED,
				C2_RST_STARTING, C2_RST_STARTED,
				C2_RST_STOPPING)) &&
	svc->rs_type != NULL && svc->rs_ops != NULL &&
	ergo(svc->rs_state == C2_RST_INITIALISED ||
		svc->rs_state == C2_RST_STARTING ||
		svc->rs_state == C2_RST_STARTED ||
		svc->rs_state == C2_RST_STOPPING, svc->rs_uuid[0] != 0 &&
		svc->rs_reqh != NULL) &&
	ergo(svc->rs_state == C2_RST_STARTED ||
		svc->rs_state == C2_RST_STOPPING,
		c2_reqh_svc_tlist_contains(&svc->rs_reqh->rh_services, svc));
}

struct c2_reqh_service_type *c2_reqh_service_type_find(const char *sname)
{
	struct c2_reqh_service_type *stype;

	C2_PRE(sname != NULL);

	c2_rwlock_read_lock(&rstypes_rwlock);
        c2_tl_for(rstypes, &rstypes, stype) {
		C2_ASSERT(c2_reqh_service_type_bob_check(stype));
                if (strcmp(stype->rst_name, sname) == 0) {
			c2_rwlock_read_unlock(&rstypes_rwlock);
                        return stype;
		}
        } c2_tl_endfor;
	c2_rwlock_read_unlock(&rstypes_rwlock);

        return stype;
}

int c2_reqh_service_locate(struct c2_reqh_service_type *stype,
                              struct c2_reqh_service **service)
{
	int rc;

	C2_PRE(stype != NULL && service != NULL);

        rc = stype->rst_ops->rsto_service_locate(stype, service);
        if (rc == 0) {
		c2_reqh_service_bob_init(*service);
		C2_ASSERT(c2_reqh_service_invariant(*service));
	}

	return rc;
}

int c2_reqh_service_start(struct c2_reqh_service *service)
{
	int             rc;
	struct c2_reqh *reqh;

	C2_PRE(c2_reqh_service_invariant(service));

	reqh = service->rs_reqh;
	service->rs_state = C2_RST_STARTING;
	rc = service->rs_ops->rso_start(service);
	if (rc == 0) {
		c2_rwlock_write_lock(&reqh->rh_rwlock);
		c2_reqh_svc_tlist_add_tail(&reqh->rh_services, service);
		service->rs_state = C2_RST_STARTED;
		C2_ASSERT(c2_reqh_service_invariant(service));
		c2_rwlock_write_unlock(&reqh->rh_rwlock);
        } else
		service->rs_state = C2_RST_FAILED;

	return rc;
}

void c2_reqh_service_stop(struct c2_reqh_service *service)
{
	struct c2_reqh *reqh;

	C2_ASSERT(c2_reqh_service_invariant(service));

	reqh = service->rs_reqh;
	service->rs_state = C2_RST_STOPPING;
	service->rs_ops->rso_stop(service);
	c2_rwlock_write_lock(&reqh->rh_rwlock);
	c2_reqh_svc_tlist_del(service);
	c2_rwlock_write_unlock(&reqh->rh_rwlock);
	service->rs_state = C2_RST_STOPPED;
}

void c2_reqh_service_init(struct c2_reqh_service *service, struct c2_reqh *reqh)
{
	const char *sname;

	C2_PRE(service != NULL && reqh != NULL &&
		service->rs_state == C2_RST_INITIALISING);

	/*
	   Generating service uuid with service name and timestamp.
	 */
	C2_SET_ARR0(service->rs_uuid);
	sname = service->rs_type->rst_name;
	snprintf(service->rs_uuid, C2_REQH_SERVICE_UUID_SIZE, "%s:%lu", sname,
								c2_time_now());
	service->rs_state = C2_RST_INITIALISED;
	service->rs_reqh  = reqh;
	c2_reqh_svc_tlink_init(service);
	c2_mutex_init(&service->rs_mutex);
	C2_POST(c2_reqh_service_invariant(service));
}

void c2_reqh_service_fini(struct c2_reqh_service *service)
{
	C2_PRE(service != NULL && (service->rs_state == C2_RST_STOPPED ||
		service->rs_state == C2_RST_FAILED) &&
		c2_reqh_service_bob_check(service));

	c2_reqh_service_bob_fini(service);
	c2_reqh_svc_tlink_fini(service);
	service->rs_ops->rso_fini(service);
}

int c2_reqh_service_type_register(struct c2_reqh_service_type *rstype)
{
        C2_PRE(rstype != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

	c2_reqh_service_type_bob_init(rstype);
	c2_rwlock_write_lock(&rstypes_rwlock);
	rstypes_tlink_init_at_tail(rstype, &rstypes);
	c2_rwlock_write_unlock(&rstypes_rwlock);

	return 0;
}

void c2_reqh_service_type_unregister(struct c2_reqh_service_type *rstype)
{
	C2_PRE(rstype != NULL && c2_reqh_service_type_bob_check(rstype));

	rstypes_tlink_del_fini(rstype);
	c2_reqh_service_type_bob_fini(rstype);
}

int c2_reqh_service_types_length(void)
{
	return rstypes_tlist_length(&rstypes);
}

void c2_reqh_service_list_print(void)
{
	struct c2_reqh_service_type *stype;

        c2_tl_for(rstypes, &rstypes, stype) {
                C2_ASSERT(c2_reqh_service_type_bob_check(stype));
                c2_console_printf(" %s\n", stype->rst_name);
        } c2_tl_endfor;
}

bool c2_reqh_service_is_registered(const char *sname)
{
        return !c2_tl_forall(rstypes, stype, &rstypes,
                                strcasecmp(stype->rst_name, sname) != 0);
}

int c2_reqh_service_types_init(void)
{
	rstypes_tlist_init(&rstypes);
	c2_bob_type_tlist_init(&rstypes_bob, &rstypes_tl);
	c2_rwlock_init(&rstypes_rwlock);

	return 0;
}

void c2_reqh_service_types_fini(void)
{
	rstypes_tlist_fini(&rstypes);
	c2_rwlock_fini(&rstypes_rwlock);
}

/** @} endgroup reqh */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

