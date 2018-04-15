/* -*- C -*- */
/*
 * COPYRIGHT 2018 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 13-Apr-2018
 */

/**
 * @addtogroup be
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "be/fom_thread.h"

#include "lib/memory.h"         /* M0_ALLOC_PTR */
#include "fop/fom_generic.h"    /* M0_FOPH_FINISH */
#include "fop/fom.h"            /* m0_fom_type */
#include "rpc/rpc_opcodes.h"    /* M0_BE_UT_FOM_THREAD_OPCODE */
#include "reqh/reqh_service.h"  /* m0_reqh_service */

static int be_ut_fom_thread_tick(struct m0_fom *fom)
{
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	return M0_FSO_WAIT;
}

static void be_ut_fom_thread_fini(struct m0_fom *fom)
{
}

static size_t be_ut_fom_thread_locality(const struct m0_fom *fom)
{
	return 0;
}

static struct m0_sm_state_descr be_ut_fom_thread_states[M0_FOPH_FINISH + 1] = {
	[M0_FOPH_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "M0_FOPH_INIT",
		.sd_allowed = M0_BITS(M0_FOPH_FINISH),
	},
	[M0_FOPH_FINISH] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "M0_FOPH_FINISH",
		.sd_allowed = 0,
	},
};

static const struct m0_sm_conf be_ut_fom_thread_conf = {
	.scf_name      = "be_ut_fom_thread",
	.scf_nr_states = ARRAY_SIZE(be_ut_fom_thread_states),
	.scf_state     = be_ut_fom_thread_states,
};

static const struct m0_fom_ops be_ut_fom_thread_ops = {
	.fo_fini          = be_ut_fom_thread_fini,
	.fo_tick          = be_ut_fom_thread_tick,
	.fo_home_locality = be_ut_fom_thread_locality,
};

static const struct m0_fom_type_ops be_ut_fom_thread_type_ops = {
	.fto_create = NULL,
};

void m0_be_ut_fom_thread(void)
{
	struct m0_be_fom_thread *fth;
	struct m0_reqh_service  *service;
	struct m0_fom_type      *ftype;
	struct m0_fom           *fom;
	int                      rc;

	M0_ALLOC_PTR(fth);
	M0_ASSERT(fth != NULL);
	M0_ALLOC_PTR(service);
	M0_ASSERT(service != NULL);
	M0_ALLOC_PTR(ftype);
	M0_ASSERT(ftype != NULL);
	M0_ALLOC_PTR(fom);
	M0_ASSERT(fom != NULL);
	m0_fom_type_init(ftype, M0_BE_UT_FOM_THREAD_OPCODE,
			 &be_ut_fom_thread_type_ops, NULL,
			 &be_ut_fom_thread_conf);
	service->rs_type = NULL;
	fom->fo_type = ftype;
	fom->fo_ops = &be_ut_fom_thread_ops;
	fom->fo_service = service;
	rc = m0_be_fom_thread_init(fth, fom);
	M0_ASSERT(rc == 0);
	m0_be_fom_thread_wakeup(fth);
	m0_be_fom_thread_fini(fth);
	m0_fom_type_fini(ftype);
	m0_free(fom);
	m0_free(ftype);
	m0_free(service);
	m0_free(fth);
}


#undef M0_TRACE_SUBSYSTEM

/** @} end of be group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
