/* -*- C -*- */
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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 16/04/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/finject.h"

#include "reqh/reqh_service.h"
#include "sns/sns_addb.h"
#include "sns/cm/st/trigger_fop.h"
#include "sns/cm/cm.h"

/**
  @defgroup SNSCMSVC SNS copy machine service
  @ingroup SNSCM

  @{
*/

/** Copy machine service type operations.*/
static int svc_allocate(struct m0_reqh_service **service,
			struct m0_reqh_service_type *stype,
			const char *arg);

static const struct m0_reqh_service_type_ops svc_type_ops = {
	.rsto_service_allocate = svc_allocate,
};

/** Copy machine service operations.*/
static int svc_start(struct m0_reqh_service *service);
static void svc_stop(struct m0_reqh_service *service);
static void svc_fini(struct m0_reqh_service *service);

static const struct m0_reqh_service_ops svc_ops = {
	.rso_start = svc_start,
	.rso_stop  = svc_stop,
	.rso_fini  = svc_fini
};

M0_CM_TYPE_DECLARE(sns, &svc_type_ops, "sns_cm", &m0_addb_ct_sns_repair_serv);

extern const struct m0_cm_ops cm_ops;

/**
 * Allocates and initialises SNS copy machine.
 * This allocates struct m0_sns_cm and invokes m0_cm_init() to initialise
 * m0_sns_cm::rc_base.
 */
static int svc_allocate(struct m0_reqh_service **service,
			struct m0_reqh_service_type *stype,
			const char *arg __attribute__((unused)))
{
	struct m0_sns_cm *sns_cm;
	struct m0_cm     *cm;
	int               rc;

	M0_ENTRY("stype: %p", stype);
	M0_PRE(service != NULL && stype != NULL);

	M0_ALLOC_PTR(sns_cm);
	if (sns_cm == NULL)
		M0_RETURN(-ENOMEM);

	cm = &sns_cm->sc_base;

	*service = &cm->cm_service;
	(*service)->rs_ops = &service_ops;

	rc = m0_cm_init(cm, container_of(stype, struct m0_cm_type, ct_stype),
			&cm_ops);
	if (rc != 0)
		m0_free(rmach);

	M0_LOG(M0_DEBUG, "sns_cm: %p service: %p", sns_cm, *service);
	M0_RETURN(rc);
}

/**
 * Registers SNS copy machine specific FOP types.
 */
static int svc_start(struct m0_reqh_service *service)
{
	struct m0_cm *cm;
	int           rc;

	M0_ENTRY("service: %p", service);
	M0_PRE(service != NULL);

        /* XXX Register SNS copy machine FOP types */
	cm = container_of(service, struct m0_cm, cm_service);
	rc = m0_cm_setup(cm) ?: m0_sns_repair_trigger_fop_init();

	M0_LEAVE();
	return rc;
}

/**
 * Destroys SNS copy machine specific FOP types and stops the copy machine.
 */
static void svc_stop(struct m0_reqh_service *service)
{
	struct m0_cm *cm;

	M0_ENTRY("service: %p", service);
	M0_PRE(service != NULL);

        /* XXX Destroy SNS copy machine FOP types and finlise copy machine. */
	cm = container_of(service, struct m0_cm, cm_service);
	/*
	 * Finalise the copy machine as the copy machine as the service is
	 * stopped.
	 */
	m0_cm_fini(cm);
	m0_sns_repair_trigger_fop_fini();

	M0_LEAVE();
}

/**
 * Destorys SNS copy machine copy machine.
 */
static void svc_fini(struct m0_reqh_service *service)
{
	struct m0_cm     *cm;
	struct m0_sns_cm *sns_cm;

	M0_ENTRY("service: %p", service);
	M0_PRE(service != NULL);

	cm = container_of(service, struct m0_cm, cm_service);
	sns_cm = cm2sns(cm);
	m0_free(sns_cm);

	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

/** @} SNSCMSVC */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
