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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSREPAIR
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/finject.h"

#include "reqh/reqh_service.h"
#include "sns/repair/st/trigger_fop.h"
#include "sns/repair/cm.h"

/**
  @defgroup SNSRepairSVC SNS Repair service
  @ingroup SNSRepairCM

  @{
*/

const struct m0_addb_loc sns_repair_addb_loc = {
        .al_name = "sns repair"
};

const struct m0_addb_ctx_type sns_repair_addb_ctx_type = {
        .act_name = "sns repair"
};

M0_ADDB_EV_DEFINE(svc_init_fail, "svc_init_fail",
                  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);
M0_ADDB_EV_DEFINE(service_start_fail, "service_start_fail",
                  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);
M0_ADDB_EV_DEFINE(config_fetch_fail, "config_fetch_fail",
                  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);


/** Copy machine service type operations.*/
static int service_allocate(struct m0_reqh_service_type *stype,
			    struct m0_reqh_service **service);

static const struct m0_reqh_service_type_ops service_type_ops = {
	.rsto_service_allocate = service_allocate,
};

/** Copy machine service operations.*/
static int service_start(struct m0_reqh_service *service);
static void service_stop(struct m0_reqh_service *service);
static void service_fini(struct m0_reqh_service *service);

static const struct m0_reqh_service_ops service_ops = {
	.rso_start = service_start,
	.rso_stop  = service_stop,
	.rso_fini  = service_fini
};

M0_CM_TYPE_DECLARE(sns_repair, &service_type_ops, "sns_repair");

extern const struct m0_cm_ops cm_ops;

/**
 * Allocates and initialises SNS Repair copy machine.
 * This allocates struct m0_sns_repair_cm and invokes m0_cm_init() to initialise
 * m0_sns_repair_cm::rc_base.
 */
static int service_allocate(struct m0_reqh_service_type *stype,
			    struct m0_reqh_service **service)
{
	struct m0_sns_repair_cm   *rmach;
	struct m0_cm              *cm;
	struct m0_cm_type         *cm_type;
	int                        rc;

	M0_ENTRY("stype: %p", stype);
	M0_PRE(stype != NULL && service != NULL);

	M0_ALLOC_PTR(rmach);
	if (rmach != NULL) {
		cm = &rmach->rc_base;
		cm_type = container_of(stype, struct m0_cm_type, ct_stype);
		*service = &cm->cm_service;
		(*service)->rs_type = stype;
		(*service)->rs_ops = &service_ops;
		m0_addb_ctx_init(&cm->cm_addb, &sns_repair_addb_ctx_type,
		                 &m0_addb_global_ctx);
		rc = m0_cm_init(cm, cm_type, &cm_ops);
		if (rc != 0) {
			M0_ADDB_ADD(&cm->cm_addb, &sns_repair_addb_loc,
			            svc_init_fail,
				    "m0_cm_init", rc);
			m0_addb_ctx_fini(&cm->cm_addb);
			m0_free(rmach);
		}
	} else
		rc = -ENOMEM;

	M0_LEAVE("rmach: %p service: %p", rmach, *service);
	return rc;
}

/**
 * Registers SNS Repair specific FOP types.
 */
static int service_start(struct m0_reqh_service *service)
{
	struct m0_cm *cm;
	int           rc;

	M0_ENTRY("service: %p", service);
	M0_PRE(service != NULL);

        /* XXX Register SNS Repair FOP types */
	cm = container_of(service, struct m0_cm, cm_service);
	rc = m0_cm_setup(cm);
	if (rc != 0)
		M0_ADDB_ADD(&cm->cm_addb, &sns_repair_addb_loc,
			    service_start_fail,
			    "m0_cm_start", rc);

	/* Build sns repair trigger fop. */
	if (rc == 0)
		rc = m0_sns_repair_trigger_fop_init();

	M0_LEAVE();
	return rc;
}

/**
 * Destroys SNS Repair specific FOP types and stops the copy machine.
 */
static void service_stop(struct m0_reqh_service *service)
{
	struct m0_cm *cm;

	M0_ENTRY("service: %p", service);
	M0_PRE(service != NULL);

        /* XXX Destroy SNS Repair FOP types and finlise copy machine. */
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
 * Destorys SNS Repair copy machine.
 */
static void service_fini(struct m0_reqh_service *service)
{
	struct m0_cm            *cm;
	struct m0_sns_repair_cm *sns_cm;

	M0_ENTRY("service: %p", service);
	M0_PRE(service != NULL);

	cm = container_of(service, struct m0_cm, cm_service);
	sns_cm = cm2sns(cm);
	m0_free(sns_cm);

	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

/** @} SNSRepairSVC */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
