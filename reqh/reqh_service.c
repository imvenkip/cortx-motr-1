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

/**
   Temporary magic for reqh service
 */
enum {
	/* Hex conversion of "rhserv" */
	RST_MAGIC = 0x726873657276
};

/**
   Checks consistency of service type instance
 */
static bool reqh_service_type_invariant(struct c2_reqh_service_type *stype)
{
	return stype != NULL && stype->rst_ops != NULL &&
		stype->rst_magic == RST_MAGIC;
}

bool c2_reqh_service_invariant(struct c2_reqh_service *service)
{
	return service != NULL && service->rs_uuid[0] != 0 &&
		service->rs_ops != NULL && service->rs_type != NULL &&
		service->rs_reqh != NULL && service->rs_magic == RST_MAGIC;
}

struct c2_list *c2_reqh_service_list_get(void)
{
	struct c2_list *stypes;

	stypes = &rstypes;

	return stypes;
}

struct c2_reqh_service_type *c2_reqh_service_type_find(const char *sname)
{
	struct c2_reqh_service_type *stype;
	struct c2_reqh_service_type *stype_next;
        bool                         found = false;

	C2_PRE(sname != NULL);

        c2_list_for_each_entry_safe(&rstypes, stype, stype_next,
			struct c2_reqh_service_type, rst_linkage) {
                if (strcmp(stype->rst_name, sname) == 0) {
                        found = true;
                        break;
                }
        }

        if (!found)
                stype = NULL;

	C2_POST(ergo(found == true, strcmp(stype->rst_name, sname) == 0 &&
		reqh_service_type_invariant(stype)));

        return stype;
}

void c2_reqh_service_start(struct c2_reqh_service *service, struct c2_reqh *reqh)
{
	C2_PRE(service != NULL && reqh != NULL);

	/* Adds service to reqh's service list */
        c2_list_add_tail(&reqh->rh_services, &service->rs_linkage);
	service->rs_reqh = reqh;
	service->rs_state = RH_SERVICE_STARTED;
}

void c2_reqh_service_stop(struct c2_reqh_service *service)
{

	C2_PRE(service != NULL);

	C2_ASSERT(c2_reqh_service_invariant(service));

	service->rs_state = RH_SERVICE_STOPPED;
	c2_list_del(&service->rs_linkage);
}

int c2_reqh_service_init(struct c2_reqh_service *service,
			const char *service_name)
{
	C2_PRE(service != NULL && service_name != NULL);

	/**
	   Generating service uuid with service name and
	   timestamp.
	 */
	C2_SET_ARR0(service->rs_uuid);
	sprintf(service->rs_uuid, "%s%lu", service_name,
					c2_time_now());

	service->rs_magic = RST_MAGIC;
	c2_list_link_init(&service->rs_linkage);

	return 0;
}

void c2_reqh_service_fini(struct c2_reqh_service *service)
{
	C2_PRE(service != NULL);

	c2_list_link_fini(&service->rs_linkage);
}

int c2_reqh_service_type_init(struct c2_reqh_service_type *rstype,
	struct c2_reqh_service_type_ops *rst_ops, const char *service_name)
{
        C2_PRE(rstype != NULL);

	rstype->rst_name = service_name;
	rstype->rst_ops = rst_ops;
        c2_list_link_init(&rstype->rst_linkage);
	rstype->rst_magic = RST_MAGIC;
        c2_mutex_lock(&rstypes_mutex);
        c2_list_add_tail(&rstypes, &rstype->rst_linkage);
        c2_mutex_unlock(&rstypes_mutex);

	return 0;
}

void c2_reqh_service_type_fini(struct c2_reqh_service_type *rstype)
{
	c2_list_del(&rstype->rst_linkage);
	c2_list_link_fini(&rstype->rst_linkage);
	rstype->rst_magic = 0;
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
