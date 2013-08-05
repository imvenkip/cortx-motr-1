/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 5-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "lib/memory.h"		/* m0_alloc */
#include "lib/misc.h"		/* M0_BITS */

#include "be/ut/helper.h"	/* m0_be_ut_backend */

#include "stob/stob.h"		/* m0_stob_id */
#include "stob/linux.h"		/* m0_linux_stob_domain_locate */
#include "dtm/dtm.h"		/* m0_dtx_init */
#include "rpc/rpclib.h"		/* m0_rpc_server_start */

#include <stdlib.h>		/* system */
#include <sys/stat.h>		/* mkdir */
#include <sys/types.h>		/* mkdir */

#define BE_UT_H_STORAGE_DIR "./__seg_ut_stob"

struct m0_sm_group ut__txs_sm_group;

void m0_be_ut_backend_init(struct m0_be_ut_backend *ut_be)
{
#define NAME(ext) "be-ut" ext
	static char    *argv[] = {
		NAME(""), "-r", "-p", "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", NAME("_addb.stob"), "-w", "10",
		"-e", "lnet:0@lo:12345:34:1", "-s", "be-tx-service"
	};
	struct m0_reqh *reqh;
	int             rc;

	*ut_be = (struct m0_be_ut_backend) {
		.but_net_xprt = &m0_net_lnet_xprt,
		.but_rpc_sctx = {
			.rsx_xprts         = &ut_be->but_net_xprt,
			.rsx_xprts_nr      = 1,
			.rsx_argv          = argv,
			.rsx_argc          = ARRAY_SIZE(argv),
			.rsx_log_file_name = NAME(".log")
		},
		.but_dom_cfg = {
			.bc_engine = {
				.bec_group_nr = 1,
				.bec_log_size = 1 << 27,
				.bec_tx_size_max =
					M0_BE_TX_CREDIT(1 << 20, 1 << 26),
				.bec_group_size_max =
					M0_BE_TX_CREDIT(1 << 21, 1 << 27),
				.bec_group_tx_max = 20,
				.bec_group_fom_reqh = NULL,
			},
		},
	};
#undef NAME

	rc = m0_net_xprt_init(ut_be->but_net_xprt);
	M0_ASSERT(rc == 0);
	rc = m0_rpc_server_start(&ut_be->but_rpc_sctx);
	M0_ASSERT(rc == 0);

	reqh = m0_mero_to_rmach(&ut_be->but_rpc_sctx.rsx_mero_ctx)->rm_reqh;
	M0_ASSERT(reqh != NULL);
	ut_be->but_dom_cfg.bc_engine.bec_group_fom_reqh = reqh;

	rc = m0_be_domain_init(&ut_be->but_dom, &ut_be->but_dom_cfg);
	M0_ASSERT(rc == 0);

	m0_sm_group_lock(&ut__txs_sm_group); /* XXX fix it using fom-simple */
}

void m0_be_ut_backend_fini(struct m0_be_ut_backend *ut_be)
{
	m0_sm_group_unlock(&ut__txs_sm_group);	/* XXX FIXME */
	m0_be_domain_fini(&ut_be->but_dom);
	m0_rpc_server_stop(&ut_be->but_rpc_sctx);
	m0_net_xprt_fini(ut_be->but_net_xprt);
}

void m0_be_ut_backend_tx_init(struct m0_be_ut_backend *ut_be,
			      struct m0_be_tx *tx)
{
	m0_be_tx_init(tx, 0, &ut_be->but_dom, &ut__txs_sm_group, NULL, NULL,
		      NULL, NULL);
}

static void
be_ut_seg_init(struct m0_be_ut_seg *ut_seg, bool stob_create, m0_bcount_t size)
{
	struct m0_stob_id stob_id = { .si_bits = M0_UINT128(0, 42) };
	int               rc;

	rc = system("rm -rf " BE_UT_H_STORAGE_DIR);
	M0_ASSERT(rc == 0);
	rc = mkdir(BE_UT_H_STORAGE_DIR, 0700);
	M0_ASSERT(rc == 0);
	rc = mkdir(BE_UT_H_STORAGE_DIR "/o", 0700);
	M0_ASSERT(rc == 0);

	rc = m0_linux_stob_domain_locate(BE_UT_H_STORAGE_DIR, &ut_seg->bus_dom);
	M0_ASSERT(rc == 0);
	m0_dtx_init(&ut_seg->bus_dtx);
	if (stob_create) {
		rc = m0_stob_create_helper(ut_seg->bus_dom, &ut_seg->bus_dtx,
					   &stob_id, &ut_seg->bus_stob);
		M0_ASSERT(rc == 0);
	} else {
		m0_stob_init(&ut_seg->bus_stob_, &stob_id, ut_seg->bus_dom);
		ut_seg->bus_stob = &ut_seg->bus_stob_;
	}

	m0_be_seg_init(&ut_seg->bus_seg, ut_seg->bus_stob, /* XXX */ NULL);
	rc = m0_be_seg_create(&ut_seg->bus_seg, size);
	M0_ASSERT(rc == 0);
	rc = m0_be_seg_open(&ut_seg->bus_seg);
	M0_ASSERT(rc == 0);
}

static void be_ut_seg_fini(struct m0_be_ut_seg *ut_seg, bool stob_destroy)
{
	int rc;

	m0_be_seg_close(&ut_seg->bus_seg);
	rc = m0_be_seg_destroy(&ut_seg->bus_seg);
	M0_ASSERT(rc == 0);
	m0_be_seg_fini(&ut_seg->bus_seg);
	if (stob_destroy)
		m0_stob_put(ut_seg->bus_stob);
	m0_dtx_fini(&ut_seg->bus_dtx);
	ut_seg->bus_dom->sd_ops->sdo_fini(ut_seg->bus_dom);
	if (stob_destroy) {
		rc = system("rm -rf " BE_UT_H_STORAGE_DIR);
		M0_ASSERT(rc == 0);
	}
}

void m0_be_ut_seg_init(struct m0_be_ut_seg *ut_seg, m0_bcount_t size)
{
	be_ut_seg_init(ut_seg, false, size);
	ut_seg->bus_copy = NULL;
}

void m0_be_ut_seg_fini(struct m0_be_ut_seg *ut_seg)
{
	m0_free(ut_seg->bus_copy);
	be_ut_seg_fini(ut_seg, false);
}

static void be_ut_data_save(const char *filename, m0_bcount_t size, void *addr)
{
	size_t	written;
	FILE   *f;

	f = fopen(filename, "w");
	written = fwrite(addr, size, 1, f);
	M0_ASSERT(written == 1);
	fclose(f);
}

void m0_be_ut_seg_check_persistence(struct m0_be_ut_seg *ut_seg)
{
	struct m0_be_seg *seg = &ut_seg->bus_seg;

	if (ut_seg->bus_copy == NULL) {
		ut_seg->bus_copy = m0_alloc(seg->bs_size);
		M0_ASSERT(ut_seg->bus_copy != NULL);
	}

	m0_be_seg__read(&M0_BE_REG_SEG(seg), ut_seg->bus_copy);

	if (memcmp(seg->bs_addr, ut_seg->bus_copy, seg->bs_size) != 0) {
		be_ut_data_save("stob.dat", seg->bs_size, ut_seg->bus_copy);
		be_ut_data_save("memory.dat", seg->bs_size, seg->bs_addr);
		M0_IMPOSSIBLE("Segment data differs from stob data");
	}
}

static void be_ut_seg_allocator_initfini(struct m0_be_ut_seg *ut_seg,
					 struct m0_be_ut_backend *ut_be,
					 bool init)
{
	struct m0_be_allocator *a;
	struct m0_be_tx_credit	credit;
	struct m0_be_tx	        tx;
	int			rc;

	a = ut_seg->bus_allocator = &ut_seg->bus_seg.bs_allocator;

	if (ut_be != NULL) {
		m0_be_ut_backend_tx_init(ut_be, &tx);
		m0_be_tx_credit_init(&credit);
		m0_be_allocator_credit(a, M0_BAO_CREATE, 0, 0, &credit);
		m0_be_tx_prep(&tx, &credit);
		m0_be_tx_open(&tx);
		rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_ACTIVE,
						     M0_BTS_FAILED),
					M0_TIME_NEVER);
		M0_ASSERT(rc == 0);
	}

	if (init) {
		rc = m0_be_allocator_init(a, &ut_seg->bus_seg);
		M0_ASSERT(rc == 0);
		rc = m0_be_allocator_create(a, ut_be == NULL ? NULL : &tx);
		M0_ASSERT(rc == 0);
	} else {
		m0_be_allocator_destroy(a, ut_be == NULL ? NULL : &tx);
		m0_be_allocator_fini(a);
	}

	if (ut_be != NULL) {
		m0_be_tx_close(&tx);
		rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_DONE),
					M0_TIME_NEVER);
		M0_ASSERT(rc == 0);
		m0_be_tx_fini(&tx);
	}
}

void m0_be_ut_seg_allocator_init(struct m0_be_ut_seg *ut_seg,
				 struct m0_be_ut_backend *ut_be)
{
	be_ut_seg_allocator_initfini(ut_seg, ut_be, true);
}

void m0_be_ut_seg_allocator_fini(struct m0_be_ut_seg *ut_seg,
				 struct m0_be_ut_backend *ut_be)
{
	be_ut_seg_allocator_initfini(ut_seg, ut_be, false);
}

#undef M0_TRACE_SUBSYSTEM

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
