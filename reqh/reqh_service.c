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
#include <config.h>
#endif

#include "lib/mutex.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/time.h"
#include "lib/misc.h" /* C2_SET_ARR0 */

#include "reqh/reqh.h"
#include "reqh/reqh_service.h"

/**
   @addtogroup reqh
   @{
 */

struct c2_tl c2_rstypes;
struct c2_mutex c2_rstypes_mutex;

const struct c2_tl_descr c2_rstypes_descr = C2_TL_DESCR("reqh service types",
                                                         struct c2_reqh_service_type,
                                                         rst_linkage,
                                                         rst_magic,
                                                         C2_RHS_MAGIC,
                                                         C2_RHS_MAGIC);

extern const struct c2_tl_descr c2_rh_sl_descr;
extern const struct c2_tl_descr c2_rh_rpml_descr;

bool c2_reqh_service_invariant(const struct c2_reqh_service *service)
{
	if (service == NULL)
		return false;

	switch (service->rs_phase) {
	case RH_SERVICE_INITIALISED:
		return service->rs_uuid[0] != 0 &&
			service->rs_magic == C2_RHS_MAGIC &&
			service->rs_state == RH_SERVICE_READY &&
			service->rs_reqh != NULL;
	case RH_SERVICE_STARTING:
		return	service->rs_state == RH_SERVICE_READY;
	case RH_SERVICE_STARTED:
		return service->rs_ops != NULL && service->rs_type != NULL &&
			service->rs_state == RH_SERVICE_RUNNING &&
			c2_tlist_contains(&c2_rh_sl_descr,
					&service->rs_reqh->rh_services,
								service);
	case RH_SERVICE_STOPPING:
		return service->rs_state == RH_SERVICE_RUNNING &&
			service->rs_reqh != NULL &&
			c2_tlist_contains(&c2_rh_sl_descr,
					&service->rs_reqh->rh_services,
								service);
	default:
		return false;
	}
}

struct c2_reqh_service_type *c2_reqh_service_type_find(const char *sname)
{
	struct c2_reqh_service_type *stype;

	C2_PRE(sname != NULL);

        c2_tlist_for(&c2_rstypes_descr, &c2_rstypes, stype) {
                if (strcmp(stype->rst_name, sname) == 0)
                        return stype;
        } c2_tlist_endfor;

        return NULL;
}

int c2_reqh_service_start(struct c2_reqh_service *service)
{

	C2_PRE(service != NULL);

	C2_ASSERT(c2_reqh_service_invariant(service));

	/* Adds service to reqh's service list */
        c2_tlist_add_tail(&c2_rh_sl_descr, &service->rs_reqh->rh_services,
							service);
	service->rs_phase = RH_SERVICE_STARTED;
	service->rs_state = RH_SERVICE_RUNNING;
	C2_POST(c2_reqh_service_invariant(service));

	return 0;
}

void c2_reqh_service_stop(struct c2_reqh_service *service)
{
	C2_ASSERT(c2_reqh_service_invariant(service));

	c2_tlist_del(&c2_rh_sl_descr, service);
	service->rs_phase = RH_SERVICE_STOPPED;
}

int c2_reqh_service_init(struct c2_reqh_service *service,
					struct c2_reqh *reqh)
{
	const char *sname;

	C2_PRE(service != NULL && reqh != NULL &&
	service->rs_state == RH_SERVICE_UNDEFINED &&
	service->rs_phase == RH_SERVICE_INITIALISING);

	/**
	   Generating service uuid with service name and
	   timestamp.
	 */
	C2_SET_ARR0(service->rs_uuid);
	sname = service->rs_type->rst_name;
	snprintf(service->rs_uuid, C2_REQH_SERVICE_UUID_SIZE,
				 "%s:%lu", sname, c2_time_now());

	service->rs_magic = C2_RHS_MAGIC;
	service->rs_phase = RH_SERVICE_INITIALISED;
	service->rs_state = RH_SERVICE_READY;
	service->rs_reqh  = reqh;
	c2_tlink_init(&c2_rh_sl_descr, service);
	c2_mutex_init(&service->rs_mutex);
	C2_POST(c2_reqh_service_invariant(service));

	return 0;
}

void c2_reqh_service_fini(struct c2_reqh_service *service)
{
	C2_PRE(service != NULL && (service->rs_phase == RH_SERVICE_STOPPED ||
				service->rs_phase == RH_SERVICE_FAILED));

	c2_tlink_fini(&c2_rh_sl_descr, service);
}

int c2_reqh_service_type_register(struct c2_reqh_service_type *rstype)
{
        C2_PRE(rstype != NULL && rstype->rst_magic == C2_RHS_MAGIC);

        c2_tlink_init(&c2_rstypes_descr, rstype);
        c2_mutex_lock(&c2_rstypes_mutex);
        c2_tlist_add_tail(&c2_rstypes_descr, &c2_rstypes, rstype);
        c2_mutex_unlock(&c2_rstypes_mutex);

	return 0;
}

void c2_reqh_service_type_unregister(struct c2_reqh_service_type *rstype)
{
	C2_PRE(rstype != NULL);

	c2_tlist_del(&c2_rstypes_descr, rstype);
	c2_tlink_fini(&c2_rstypes_descr, rstype);
}

int c2_reqh_service_types_init(void)
{
	c2_tlist_init(&c2_rstypes_descr, &c2_rstypes);
	c2_mutex_init(&c2_rstypes_mutex);

	return 0;
}

void c2_reqh_service_types_fini(void)
{
	c2_tlist_fini(&c2_rstypes_descr, &c2_rstypes);
	c2_mutex_fini(&c2_rstypes_mutex);
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

