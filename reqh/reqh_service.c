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

#include "lib/rwlock.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/time.h"
#include "lib/misc.h" /* M0_SET_ARR0 */
#include "lib/trace.h" /* m0_console_printf */
#include "lib/finject.h" /* M0_FI_ENABLED */
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "mero/magic.h"

/**
   @addtogroup reqhservice
   @{
 */

/**
   static global list of service types.
   Holds struct m0_reqh_service_type instances linked via
   m0_reqh_service_type::rst_linkage.

   @see struct m0_reqh_service_type
 */
static struct m0_tl rstypes;

/** Protects access to list rstypes. */
static struct m0_rwlock rstypes_rwlock;

M0_TL_DESCR_DEFINE(rstypes, "reqh service types", static,
                   struct m0_reqh_service_type, rst_linkage, rst_magix,
                   M0_REQH_SVC_TYPE_MAGIC, M0_REQH_SVC_HEAD_MAGIC);

M0_TL_DEFINE(rstypes, static, struct m0_reqh_service_type);

static struct m0_bob_type rstypes_bob;
M0_BOB_DEFINE(static, &rstypes_bob, m0_reqh_service_type);

M0_INTERNAL bool m0_reqh_service_invariant(const struct m0_reqh_service *svc)
{
	return m0_reqh_service_bob_check(svc) &&
	M0_IN(svc->rs_state, (M0_RST_INITIALISING, M0_RST_INITIALISED,
				M0_RST_STARTING, M0_RST_STARTED,
				M0_RST_STOPPING)) &&
	svc->rs_type != NULL && svc->rs_ops != NULL &&
	ergo(M0_IN(svc->rs_state, (M0_RST_INITIALISED, M0_RST_STARTING,
				   M0_RST_STARTED, M0_RST_STOPPING)),
	     svc->rs_uuid[0] != 0 && svc->rs_reqh != NULL) &&
	ergo(M0_IN(svc->rs_state, (M0_RST_STARTED, M0_RST_STOPPING)),
	     m0_reqh_svc_tlist_contains(&svc->rs_reqh->rh_services, svc));
}
M0_EXPORTED(m0_reqh_service_invariant);

M0_INTERNAL struct m0_reqh_service_type *
m0_reqh_service_type_find(const char *sname)
{
	struct m0_reqh_service_type *t;

	M0_PRE(sname != NULL);

	m0_rwlock_read_lock(&rstypes_rwlock);
        m0_tl_for(rstypes, &rstypes, t) {
		M0_ASSERT(m0_reqh_service_type_bob_check(t));
                if (strcmp(t->rst_name, sname) == 0)
			break;
        } m0_tl_endfor;
	m0_rwlock_read_unlock(&rstypes_rwlock);

        return t;
}

M0_INTERNAL int m0_reqh_service_allocate(struct m0_reqh_service_type *stype,
					 struct m0_reqh_service **service)
{
	int rc;

	M0_PRE(stype != NULL && service != NULL);

        rc = stype->rst_ops->rsto_service_allocate(stype, service);
        if (rc == 0) {
		m0_reqh_service_bob_init(*service);
		M0_ASSERT(m0_reqh_service_invariant(*service));
	}

	return rc;
}

M0_INTERNAL int m0_reqh_service_start(struct m0_reqh_service *service)
{
	int             rc;
	unsigned        key;
	struct m0_reqh *reqh;

	M0_PRE(m0_reqh_service_invariant(service));

	reqh = service->rs_reqh;
	service->rs_state = M0_RST_STARTING;
	rc = service->rs_ops->rso_start(service);
	if (rc == 0) {
		m0_rwlock_write_lock(&reqh->rh_rwlock);
		m0_reqh_svc_tlist_add_tail(&reqh->rh_services, service);
		service->rs_state = M0_RST_STARTED;
		M0_ASSERT(m0_reqh_service_invariant(service));
		key = service->rs_type->rst_key;
		M0_ASSERT(reqh->rh_key[key] == NULL);
		reqh->rh_key[key] = service;
		m0_rwlock_write_unlock(&reqh->rh_rwlock);
        } else
		service->rs_state = M0_RST_FAILED;

	return rc;
}

M0_INTERNAL void m0_reqh_service_stop(struct m0_reqh_service *service)
{
	struct m0_reqh *reqh;
	unsigned        key;

	M0_ASSERT(m0_reqh_service_invariant(service));

	reqh = service->rs_reqh;
	service->rs_state = M0_RST_STOPPING;
	service->rs_ops->rso_stop(service);
	m0_rwlock_write_lock(&reqh->rh_rwlock);
	m0_reqh_svc_tlist_del(service);
	service->rs_state = M0_RST_STOPPED;
	key = service->rs_type->rst_key;
	M0_ASSERT(reqh->rh_key[key] == service);
	reqh->rh_key[key] = NULL;
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
}

M0_INTERNAL void m0_reqh_service_init(struct m0_reqh_service *service,
				      struct m0_reqh *reqh)
{
	const char *sname;

	M0_PRE(service != NULL && reqh != NULL &&
		service->rs_state == M0_RST_INITIALISING);

	/*
	   Generating service uuid with service name and timestamp.
	 */
	M0_SET_ARR0(service->rs_uuid);
	sname = service->rs_type->rst_name;
	snprintf(service->rs_uuid, M0_REQH_SERVICE_UUID_SIZE, "%s:%llu", sname,
		(unsigned long long)m0_time_now());
	service->rs_state = M0_RST_INITIALISED;
	service->rs_reqh  = reqh;
	m0_reqh_svc_tlink_init(service);
	m0_mutex_init(&service->rs_mutex);
	M0_POST(m0_reqh_service_invariant(service));
}

M0_INTERNAL void m0_reqh_service_fini(struct m0_reqh_service *service)
{
	M0_PRE(service != NULL && (service->rs_state == M0_RST_STOPPED ||
		service->rs_state == M0_RST_FAILED) &&
		m0_reqh_service_bob_check(service));

	m0_reqh_service_bob_fini(service);
	m0_reqh_svc_tlink_fini(service);
	service->rs_ops->rso_fini(service);
}

int m0_reqh_service_type_register(struct m0_reqh_service_type *rstype)
{
        M0_PRE(rstype != NULL);
	M0_PRE(!m0_reqh_service_is_registered(rstype->rst_name));

	if (M0_FI_ENABLED("fake_error"))
		return -EINVAL;

	m0_reqh_service_type_bob_init(rstype);
	m0_rwlock_write_lock(&rstypes_rwlock);
	rstype->rst_key = m0_reqh_key_init();
	rstypes_tlink_init_at_tail(rstype, &rstypes);
	m0_rwlock_write_unlock(&rstypes_rwlock);

	return 0;
}

void m0_reqh_service_type_unregister(struct m0_reqh_service_type *rstype)
{
	M0_PRE(rstype != NULL && m0_reqh_service_type_bob_check(rstype));

	rstypes_tlink_del_fini(rstype);
	m0_reqh_service_type_bob_fini(rstype);
}

M0_INTERNAL int m0_reqh_service_types_length(void)
{
	return rstypes_tlist_length(&rstypes);
}

M0_INTERNAL void m0_reqh_service_list_print(void)
{
	struct m0_reqh_service_type *stype;

        m0_tl_for(rstypes, &rstypes, stype) {
                M0_ASSERT(m0_reqh_service_type_bob_check(stype));
                m0_console_printf(" %s\n", stype->rst_name);
        } m0_tl_endfor;
}

M0_INTERNAL bool m0_reqh_service_is_registered(const char *sname)
{
        return !m0_tl_forall(rstypes, stype, &rstypes,
                                strcasecmp(stype->rst_name, sname) != 0);
}

M0_INTERNAL int m0_reqh_service_types_init(void)
{
	rstypes_tlist_init(&rstypes);
	m0_bob_type_tlist_init(&rstypes_bob, &rstypes_tl);
	m0_rwlock_init(&rstypes_rwlock);

	return 0;
}
M0_EXPORTED(m0_reqh_service_types_init);

M0_INTERNAL void m0_reqh_service_types_fini(void)
{
	rstypes_tlist_fini(&rstypes);
	m0_rwlock_fini(&rstypes_rwlock);
}
M0_EXPORTED(m0_reqh_service_types_fini);

M0_INTERNAL struct m0_reqh_service *m0_reqh_service_find(const struct
							 m0_reqh_service_type
							 *st,
							 struct m0_reqh *reqh)
{
	M0_PRE(st != NULL && reqh != NULL);

	return reqh->rh_key[st->rst_key];
}
M0_EXPORTED(m0_reqh_service_find);

/** @} endgroup reqhservice */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
