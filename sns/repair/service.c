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

#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"

#include "sns/repair/cm.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "lib/finject.h"

/**
  @defgroup SNSRepairSVC Copy machine service
  @ingroup SNSRepairCM

  @{
*/

const struct c2_addb_loc sns_repair_addb_loc = {
        .al_name = "sns repair"
};

const struct c2_addb_ctx_type sns_repair_addb_ctx_type = {
        .act_name = "sns repair"
};

C2_ADDB_EV_DEFINE(svc_init_fail, "svc_init_fail",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(service_start_fail, "service_start_fail",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(agents_create_fail, "agents_create_fail",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(config_fetch_fail, "config_fetch_fail",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);


/** Copy machine service type operations.*/
static int service_allocate(struct c2_reqh_service_type *stype,
			    struct c2_reqh_service **service);

static const struct c2_reqh_service_type_ops service_type_ops = {
	.rsto_service_allocate = service_allocate,
};

/** Copy machine service operations.*/
static int service_start(struct c2_reqh_service *service);
static void service_stop(struct c2_reqh_service *service);
static void service_fini(struct c2_reqh_service *service);

static const struct c2_reqh_service_ops service_ops = {
	.rso_start = service_start,
	.rso_stop  = service_stop,
	.rso_fini  = service_fini
};

C2_CM_TYPE_DECLARE(sns_repair, &service_type_ops, "sns_repair");

extern const struct c2_cm_ops cm_ops;
extern const struct c2_cm_sw_ops sw_ops;

static int service_allocate(struct c2_reqh_service_type *stype,
			    struct c2_reqh_service **service)
{
	struct c2_sns_repair_cm   *rmach;
	struct c2_cm              *cm;
	struct c2_cm_type         *cm_type;
	int                        rc;

	C2_ENTRY();
	C2_PRE(stype != NULL && service != NULL);

	C2_ALLOC_PTR(rmach);
	if (rmach != NULL) {
		cm = &rmach->rc_cm;
		cm_type = container_of(stype, struct c2_cm_type, ct_stype);
		*service = &cm->cm_service;
		(*service)->rs_type = stype;
		(*service)->rs_ops = &service_ops;
		c2_addb_ctx_init(&cm->cm_addb, &sns_repair_addb_ctx_type,
		                 &c2_addb_global_ctx);
		rc = c2_cm_init(cm, cm_type, &cm_ops, &sw_ops);
		if (rc != 0) {
			C2_ADDB_ADD(&cm->cm_addb, &sns_repair_addb_loc,
			            svc_init_fail,
				    "c2_cm_init", rc);
			c2_addb_ctx_fini(&cm->cm_addb);
			c2_free(rmach);
		}
	} else
		rc = -ENOMEM;

	C2_LEAVE();
	return rc;
}

static int service_start(struct c2_reqh_service *service)
{
	int           rc;
	struct c2_cm *cm;

	C2_ENTRY();
	C2_PRE(service != NULL);

	cm = container_of(service, struct c2_cm, cm_service);
	rc = c2_cm_start(cm);
	if (rc != 0)
		C2_ADDB_ADD(&cm->cm_addb, &sns_repair_addb_loc,
			    service_start_fail,
			    "c2_cm_start", rc);

	C2_LEAVE();
	return rc;
}

static void service_stop(struct c2_reqh_service *service)
{
	C2_ENTRY();
	C2_PRE(service != NULL);
	C2_LEAVE();
}

static void service_fini(struct c2_reqh_service *service)
{
	struct c2_cm            *mach;
	struct c2_sns_repair_cm *smach;

	C2_ENTRY();
	C2_PRE(service != NULL);

	mach = container_of(service, struct c2_cm, cm_service);
	c2_cm_fini(mach);
	smach = container_of(mach, struct c2_sns_repair_cm, rc_cm);
	c2_free(smach);
	C2_LEAVE();
}

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
