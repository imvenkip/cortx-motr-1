/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 23/11/2012
 */

/**
   @addtogroup confd_dfspec
   @{
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "colibri/magic.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "conf/confd.h"
#include "conf/conf_fop.h"

static struct c2_addb_ctx confd_addb_ctx;

static const struct c2_addb_loc confd_addb_loc = {
	.al_name = "confd-service"
};

static const struct c2_addb_ctx_type confd_addb_ctx_type = {
	.act_name = "confd-service"
};

static int confd_allocate(struct c2_reqh_service_type *stype,
			  struct c2_reqh_service **service);
static void confd_fini(struct c2_reqh_service *service);

static int confd_start(struct c2_reqh_service *service);
static void confd_stop(struct c2_reqh_service *service);

/**
 * Conf Service type operations.
 */
static const struct c2_reqh_service_type_ops confd_type_ops = {
	.rsto_service_allocate = confd_allocate
};

/**
 * Confd Service operations.
 */
static const struct c2_reqh_service_ops confd_ops = {
	.rso_start = confd_start,
	.rso_stop  = confd_stop,
	.rso_fini  = confd_fini
};

C2_REQH_SERVICE_TYPE_DECLARE(c2_confd_type, &confd_type_ops, "confd-service");

C2_INTERNAL int c2_confd_service_register(void)
{
	c2_reqh_service_type_register(&c2_confd_type);
	return c2_conf_fops_init();
}

C2_INTERNAL void c2_confd_service_unregister(void)
{
	c2_reqh_service_type_unregister(&c2_confd_type);
	c2_conf_fops_fini();
}

/**
 * Allocates and initiates Confd Service instance.
 * This operation allocates & initiates service instance with its operation
 * vector.
 *
 * @param stype    service type
 * @param service  pointer to service instance
 *
 * @pre stype != NULL && service != NULL
 */
static int confd_allocate(struct c2_reqh_service_type *stype,
			  struct c2_reqh_service **service)
{
	struct c2_confd *confd;

	C2_ENTRY();
	C2_PRE(stype != NULL && service != NULL);

	c2_addb_ctx_init(&confd_addb_ctx, &confd_addb_ctx_type,
			 &c2_addb_global_ctx);

	C2_ALLOC_PTR_ADDB(confd, &confd_addb_ctx, &confd_addb_loc);
	if (confd == NULL)
		return -ENOMEM;

	confd->c_magic = C2_REQH_CONFD_SERVICE_MAGIC;

	*service = &confd->c_reqh;
	(*service)->rs_type = stype;
	(*service)->rs_ops = &confd_ops;

	C2_RETURN(0);
}

/**
 * Finalise Conf Service instance.
 * This operation finalises service instance and de-allocate it.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void confd_fini(struct c2_reqh_service *service)
{
	C2_ENTRY();
	C2_PRE(service != NULL);

	c2_addb_ctx_fini(&confd_addb_ctx);
	c2_free(container_of(service, struct c2_confd, c_reqh));
	C2_LEAVE();
}

/**
 * Start Conf Service.
 * - Mount local storage
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static int confd_start(struct c2_reqh_service *service)
{
	C2_ENTRY();
	C2_PRE(service != NULL);

	/* XXX TODO */

	C2_RETURN(0);
}

/**
 * Stops Conf Service.
 * - Umount local storage
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void confd_stop(struct c2_reqh_service *service)
{
	C2_ENTRY();
	C2_PRE(service != NULL);

	/* XXX TODO */

	C2_LEAVE();
}

#undef C2_TRACE_SUBSYSTEM

/** @} endgroup confd_dfspec */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
