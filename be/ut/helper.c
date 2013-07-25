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

#include "be/ut/helper.h"
#include "be/tx_group_fom.h"

#include "stob/stob.h"	/* m0_stob_id */
#include "stob/linux.h"	/* m0_linux_stob_domain_locate */
#include "dtm/dtm.h"	/* m0_dtx_init */
#include "rpc/rpclib.h"	/* m0_rpc_server_start */

#include <stdlib.h>	/* system */
#include <sys/stat.h>	/* mkdir */
#include <sys/types.h>	/* mkdir */

#define BE_UT_H_STORAGE_DIR "./__seg_ut_stob"

enum {
	BE_UT_H_DOM_ID   = 42,
	BE_UT_H_STOB_ID  = 42,
	BE_UT_H_SEG_SIZE = 0x1000000  /* 16 MiB */
};

struct m0_sm_group ut__txs_sm_group;

static struct m0_stob_id be_ut_h_stob_id = {
	.si_bits = M0_UINT128(0, BE_UT_H_STOB_ID)
};

static struct m0_net_xprt *g_xprt = &m0_net_lnet_xprt;

/* XXX Code duplication! The same code exists in conf/ut/confc.c. */
static int service_start(struct m0_rpc_server_ctx *sctx)
{
	int rc;

	rc = m0_net_xprt_init(g_xprt);
	if (rc != 0)
		return rc;

	rc = m0_rpc_server_start(sctx);
	if (rc != 0)
		m0_net_xprt_fini(g_xprt);
	return rc;
}

static void service_stop(struct m0_rpc_server_ctx *sctx)
{
	m0_rpc_server_stop(sctx);
	m0_net_xprt_fini(g_xprt);
}

void m0_be_ut_seg_storage_fini(void)
{
	int rc = system("rm -rf " BE_UT_H_STORAGE_DIR);
	M0_ASSERT(rc == 0);
}

void m0_be_ut_seg_storage_init(void)
{
	int rc;

	m0_be_ut_seg_storage_fini();

	rc = mkdir(BE_UT_H_STORAGE_DIR, 0700);
	M0_ASSERT(rc == 0);

	rc = mkdir(BE_UT_H_STORAGE_DIR "/o", 0700);
	M0_ASSERT(rc == 0);
}

void m0_be_ut_seg_initialize(struct m0_be_ut_h *h, bool stob_create)
{
	int rc;

	rc = m0_linux_stob_domain_locate(BE_UT_H_STORAGE_DIR, &h->buh_dom);
	M0_ASSERT(rc == 0);
	m0_dtx_init(&h->buh_dtx);
	if (!stob_create) {
		m0_stob_init(&h->buh_stob_, &be_ut_h_stob_id, h->buh_dom);
		h->buh_stob = &h->buh_stob_;
	} else {
		rc = m0_stob_create_helper(h->buh_dom, &h->buh_dtx,
					   &be_ut_h_stob_id, &h->buh_stob);
		M0_ASSERT(rc == 0);
	}
	m0_be_seg_init(&h->buh_seg, h->buh_stob, &h->buh_be);
}

void m0_be_ut_seg_finalize(struct m0_be_ut_h *h, bool stob_put)
{
	m0_be_seg_fini(&h->buh_seg);
	if (stob_put)
		m0_stob_put(h->buh_stob);
	m0_dtx_fini(&h->buh_dtx);
	h->buh_dom->sd_ops->sdo_fini(h->buh_dom);
}

void m0_be_ut_seg_create(struct m0_be_ut_h *h)
{
	int rc;

	m0_be_ut_seg_storage_init();
	m0_be_ut_seg_initialize(h, false);
	rc = m0_be_seg_create(&h->buh_seg, BE_UT_H_SEG_SIZE);
	M0_ASSERT(rc == 0);
}

void m0_be_ut_seg_destroy(struct m0_be_ut_h *h)
{
	int rc;

	rc = m0_be_seg_destroy(&h->buh_seg);
	M0_ASSERT(rc == 0);
	m0_be_ut_seg_finalize(h, false);
	m0_be_ut_seg_storage_fini();
}

void m0_be_ut_seg_create_open(struct m0_be_ut_h *h)
{
	int rc;

	m0_be_ut_seg_create(h);
	rc = m0_be_seg_open(&h->buh_seg);
	M0_ASSERT(rc == 0);
}

void m0_be_ut_seg_close_destroy(struct m0_be_ut_h *h)
{
	m0_be_seg_close(&h->buh_seg);
	m0_be_ut_seg_destroy(h);
}

void m0_be_ut_h_init(struct m0_be_ut_h *h)
{
	int                      rc;
	struct m0_rpc_machine   *mach;
#define NAME(ext) "be-ut" ext
	char                    *argv[] = {
		NAME(""), "-r", "-p", "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", NAME("_addb.stob"), "-w", "10",
		"-e", "lnet:0@lo:12345:34:1", "-s", "be-tx-service"
	};
	struct m0_rpc_server_ctx sctx = {
		.rsx_xprts         = &g_xprt,
		.rsx_xprts_nr      = 1,
		.rsx_argv          = argv,
		.rsx_argc          = ARRAY_SIZE(argv),
		.rsx_log_file_name = NAME(".log")
	};
#undef NAME

	*h = (struct m0_be_ut_h){ .buh_rpc_svc = sctx };

	rc = service_start(&h->buh_rpc_svc);
	M0_ASSERT(rc == 0);
	mach = m0_mero_to_rmach(&h->buh_rpc_svc.rsx_mero_ctx);
	M0_ASSERT(mach->rm_reqh != NULL);

	/*
	m0_be_init(&h->buh_be);
	rc = m0_be_tx_engine_start(&h->buh_be.b_tx_engine, mach->rm_reqh);
	M0_ASSERT(rc == 0);
	*/

	m0_be_ut_seg_create_open(h);
	h->buh_allocator = &h->buh_seg.bs_allocator;
	rc = m0_be_allocator_init(h->buh_allocator, &h->buh_seg);
	M0_ASSERT(rc == 0);

	rc = m0_be_allocator_create(h->buh_allocator, NULL /* XXX FIXME */);
	M0_ASSERT(rc == 0);
}

void m0_be_ut_h_fini(struct m0_be_ut_h *h)
{
	/* m0_be_tx_engine_stop(&h->buh_be.b_tx_engine); */
	service_stop(&h->buh_rpc_svc);
	/*
	 * Allocator and segment should be finalised _after_ reqh.
	 * Max knows why.
	 */
	// m0_be_fini(&h->buh_be);
#if 0 /* XXX FIXME: m0_be_allocator_{create,destroy}() need a transaction.
       * Max knows what to do about it. */
	m0_be_allocator_destroy(h->buh_allocator, NULL);
#endif
	m0_be_allocator_fini(h->buh_allocator);
	m0_be_ut_seg_close_destroy(h);
}

void m0_be_ut_h_seg_reload(struct m0_be_ut_h *h)
{
	int rc;

	m0_be_seg_close(&h->buh_seg);
	rc = m0_be_seg_open(&h->buh_seg);
	M0_ASSERT(rc == 0);
}

static void be_ut_h_persistent(const struct m0_be_tx *tx)
{
}

static void be_ut_h_discarded(const struct m0_be_tx *tx)
{
}

void m0_be_ut_h_tx_init(struct m0_be_tx *tx, struct m0_be_ut_h *h)
{
	/*
	m0_be_tx_init(tx, ++h->buh_tid, &h->buh_be, &ut__txs_sm_group,
		      be_ut_h_persistent, be_ut_h_discarded, true, NULL, NULL);
		      */
}

void m0_be_ut_backend_init(struct m0_be_ut_backend *ut_be)
{
	int                      rc;
#define NAME(ext) "be-ut" ext
	static char		*argv[] = {
		NAME(""), "-r", "-p", "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", NAME("_addb.stob"), "-w", "10",
		"-e", "lnet:0@lo:12345:34:1", "-s", "be-tx-service"
	};
	struct m0_reqh		*reqh;

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
				.bec_log_size = 1 << 24,
				.bec_group_size_max =
					M0_BE_TX_CREDIT(200000, 1 << 22),
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
	m0_be_tx_init(tx, 0, &ut_be->but_dom, &ut__txs_sm_group,
		      be_ut_h_persistent, be_ut_h_discarded, true, NULL, NULL);
}

static void be_ut_seg_init(struct m0_be_ut_seg *ut_seg,
			   bool stob_create,
			   m0_bcount_t size)
{
	int rc;

	rc = system("rm -rf " BE_UT_H_STORAGE_DIR);
	M0_ASSERT(rc == 0);
	rc = mkdir(BE_UT_H_STORAGE_DIR, 0700);
	M0_ASSERT(rc == 0);
	rc = mkdir(BE_UT_H_STORAGE_DIR "/o", 0700);
	M0_ASSERT(rc == 0);

	rc = m0_linux_stob_domain_locate(BE_UT_H_STORAGE_DIR, &ut_seg->bus_dom);
	M0_ASSERT(rc == 0);
	m0_dtx_init(&ut_seg->bus_dtx);
	if (!stob_create) {
		m0_stob_init(&ut_seg->bus_stob_, &be_ut_h_stob_id,
			     ut_seg->bus_dom);
		ut_seg->bus_stob = &ut_seg->bus_stob_;
	} else {
		rc = m0_stob_create_helper(ut_seg->bus_dom, &ut_seg->bus_dtx,
					   &be_ut_h_stob_id, &ut_seg->bus_stob);
		M0_ASSERT(rc == 0);
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
}

void m0_be_ut_seg_fini(struct m0_be_ut_seg *ut_seg)
{
	be_ut_seg_fini(ut_seg, false);
}

void m0_be_ut_seg_reload(struct m0_be_ut_seg *ut_seg)
{
	int rc;

	m0_be_seg_close(&ut_seg->bus_seg);
	rc = m0_be_seg_open(&ut_seg->bus_seg);
	M0_ASSERT(rc == 0);
}

void m0_be_ut_seg_allocator_init(struct m0_be_ut_seg *ut_seg)
{
	int rc;

	ut_seg->bus_allocator = &ut_seg->bus_seg.bs_allocator;

	rc = m0_be_allocator_init(ut_seg->bus_allocator, &ut_seg->bus_seg);
	M0_ASSERT(rc == 0);
	rc = m0_be_allocator_create(ut_seg->bus_allocator, /* XXX */ NULL);
	M0_ASSERT(rc == 0);
}

void m0_be_ut_seg_allocator_fini(struct m0_be_ut_seg *ut_seg)
{
	m0_be_allocator_destroy(ut_seg->bus_allocator, /* XXX */ NULL);
	m0_be_allocator_fini(ut_seg->bus_allocator);
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
