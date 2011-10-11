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

struct c2_list rstypes;
struct c2_mutex rstypes_mutex;

bool c2_reqh_service_invariant(struct c2_reqh_service *service)
{
	if (service == NULL)
		return false;

	switch (service->rs_phase) {
	case RH_SERVICE_INITIALISED:
		return service->rs_uuid[0] != 0 &&
			service->rs_magic == C2_RST_MAGIC &&
			service->rs_state == RH_SERVICE_READY &&
			service->rs_reqh != NULL;
	case RH_SERVICE_STARTING:
		return	service->rs_state == RH_SERVICE_READY;
	case RH_SERVICE_STARTED:
		return service->rs_ops != NULL && service->rs_type != NULL &&
			service->rs_state == RH_SERVICE_RUNNING &&
			c2_list_contains(&service->rs_reqh->rh_services,
						&service->rs_linkage);
	case RH_SERVICE_STOPPING:
		return service->rs_state == RH_SERVICE_RUNNING &&
			service->rs_reqh != NULL &&
			c2_list_contains(&service->rs_reqh->rh_services,
						&service->rs_linkage);
	default:
		return false;
	}
}

struct c2_reqh_service_type *c2_reqh_service_type_find(const char *sname)
{
	struct c2_reqh_service_type *stype;

	C2_PRE(sname != NULL);

        c2_list_for_each_entry(&rstypes, stype, struct c2_reqh_service_type,
								rst_linkage) {
                if (strcmp(stype->rst_name, sname) == 0)
                        return stype;
        }

        return NULL;
}

int c2_reqh_service_start(struct c2_reqh_service *service)
{

	C2_PRE(service != NULL);

	C2_ASSERT(c2_reqh_service_invariant(service));

	/* Adds service to reqh's service list */
        c2_list_add_tail(&service->rs_reqh->rh_services,
					&service->rs_linkage);
	service->rs_phase = RH_SERVICE_STARTED;
	service->rs_state = RH_SERVICE_RUNNING;
	C2_POST(c2_reqh_service_invariant(service));

	return 0;
}

void c2_reqh_service_stop(struct c2_reqh_service *service)
{
	C2_ASSERT(c2_reqh_service_invariant(service));

	c2_list_del(&service->rs_linkage);
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

	service->rs_magic = C2_RST_MAGIC;
	service->rs_phase = RH_SERVICE_INITIALISED;
	service->rs_state = RH_SERVICE_READY;
	service->rs_reqh  = reqh;
	c2_list_link_init(&service->rs_linkage);
	c2_mutex_init(&service->rs_mutex);
	C2_POST(c2_reqh_service_invariant(service));

	return 0;
}

void c2_reqh_service_fini(struct c2_reqh_service *service)
{
	C2_PRE(service != NULL && (service->rs_phase == RH_SERVICE_STOPPED ||
				service->rs_phase == RH_SERVICE_FAILED));

	c2_list_link_fini(&service->rs_linkage);
}

int c2_reqh_service_type_register(struct c2_reqh_service_type *rstype)
{
        C2_PRE(rstype != NULL && rstype->rst_magic == C2_RST_MAGIC);

        c2_list_link_init(&rstype->rst_linkage);
        c2_mutex_lock(&rstypes_mutex);
        c2_list_add_tail(&rstypes, &rstype->rst_linkage);
        c2_mutex_unlock(&rstypes_mutex);

	return 0;
}

void c2_reqh_service_type_unregister(struct c2_reqh_service_type *rstype)
{
	C2_PRE(rstype != NULL);

	c2_list_del(&rstype->rst_linkage);
	c2_list_link_fini(&rstype->rst_linkage);
}

int c2_reqh_service_types_init()
{
	c2_list_init(&rstypes);
	c2_mutex_init(&rstypes_mutex);

	return 0;
}

void c2_reqh_service_types_fini()
{
	c2_list_fini(&rstypes);
	c2_mutex_fini(&rstypes_mutex);
}
