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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 09/25/2012
 * Restructured by: Rohan Puri <Rohan_Puri@xyratex.com>
 * Restructured Date: 12/13/2012
 */

#include "cm/ut/common_service.h"
#include "mero/setup.h"

struct m0_reqh           cm_ut_reqh;
struct m0_cm_cp          cm_ut_cp;
struct m0_cm             cm_ut;
struct m0_reqh_service  *cm_ut_service;

static int cm_ut_service_start(struct m0_reqh_service *service)
{
	struct  m0_cm *cm;

	cm = container_of(service, struct m0_cm, cm_service);
	return m0_cm_setup(cm);
}

static void cm_ut_service_stop(struct m0_reqh_service *service)
{
	struct m0_cm *cm = container_of(service, struct m0_cm, cm_service);
	m0_cm_fini(cm);
}

static void cm_ut_service_fini(struct m0_reqh_service *service)
{
	cm_ut_service = NULL;
	M0_SET0(&cm_ut);
}

static const struct m0_reqh_service_ops cm_ut_service_ops = {
	.rso_start = cm_ut_service_start,
	.rso_stop  = cm_ut_service_stop,
	.rso_fini  = cm_ut_service_fini
};

static void cm_cp_ut_free(struct m0_cm_cp *cp)
{
}

static bool cm_cp_ut_invariant(const struct m0_cm_cp *cp)
{
	return true;
}

static const struct m0_cm_cp_ops cm_cp_ut_ops = {
	.co_invariant = cm_cp_ut_invariant,
	.co_free = cm_cp_ut_free
};

static struct m0_cm_cp* cm_ut_cp_alloc(struct m0_cm *cm)
{
	cm_ut_cp.c_ops = &cm_cp_ut_ops;
	return &cm_ut_cp;
}

static int cm_ut_setup(struct m0_cm *cm)
{
	return 0;
}

static int cm_ut_start(struct m0_cm *cm)
{
	return 0;
}

static int cm_ut_stop(struct m0_cm *cm)
{
	return 0;
}

static int cm_ut_data_next(struct m0_cm *cm, struct m0_cm_cp *cp)
{
	return -ENODATA;
}

static void cm_ut_fini(struct m0_cm *cm)
{
}

static void cm_ut_complete(struct m0_cm *cm)
{
}

static const struct m0_cm_ops cm_ut_ops = {
	.cmo_setup     = cm_ut_setup,
	.cmo_start     = cm_ut_start,
	.cmo_stop      = cm_ut_stop,
	.cmo_cp_alloc  = cm_ut_cp_alloc,
	.cmo_data_next = cm_ut_data_next,
	.cmo_complete  = cm_ut_complete,
	.cmo_fini      = cm_ut_fini
};

static int cm_ag_ut_fini(struct m0_cm_aggr_group *ag)
{
	return 0;
}

static uint64_t cm_ag_ut_local_cp_nr(const struct m0_cm_aggr_group *ag)
{
	return CM_UT_LOCAL_CP_NR;
}

const struct m0_cm_aggr_group_ops cm_ag_ut_ops = {
	.cago_fini = cm_ag_ut_fini,
	.cago_local_cp_nr = cm_ag_ut_local_cp_nr,
};

static int cm_ut_service_allocate(struct m0_reqh_service **service,
				  struct m0_reqh_service_type *stype,
				  struct m0_reqh_context *rctx)
{
	struct m0_cm *cm = &cm_ut;

	*service = &cm->cm_service;
	(*service)->rs_ops = &cm_ut_service_ops;
	(*service)->rs_state = M0_RST_INITIALISING;

	return m0_cm_init(cm, container_of(stype, struct m0_cm_type, ct_stype),
			  &cm_ut_ops);
}

static const struct m0_reqh_service_type_ops cm_ut_service_type_ops = {
	.rsto_service_allocate = cm_ut_service_allocate,
};

M0_CM_TYPE_DECLARE(cm_ut, &cm_ut_service_type_ops, "cm_ut",
		   &m0_addb_ct_ut_service);

struct m0_mero         mero = { .cc_pool_width = 3 };
struct m0_reqh_context rctx = { .rc_mero = &mero };

void cm_ut_service_alloc_init()
{
	int rc;
	/* Internally calls m0_cm_init(). */
	M0_ASSERT(cm_ut_service == NULL);
	rc = m0_reqh_service_allocate(&cm_ut_service, &cm_ut_cmt.ct_stype,
	                              &rctx);
	M0_ASSERT(rc == 0);
	m0_reqh_service_init(cm_ut_service, &cm_ut_reqh);
}

void cm_ut_service_cleanup()
{
        m0_reqh_service_stop(cm_ut_service);
        m0_reqh_service_fini(cm_ut_service);
}

M0_ADDB_CT(m0_addb_ct_ut_service, M0_ADDB_CTXID_UT_SERVICE, "hi", "low");
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
