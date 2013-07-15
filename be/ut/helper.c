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

#include "be/ut/helper.h"
#include "be/tx_fom.h"

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
#define NAME(ext) "be-tx-ut" ext
	char                    *argv[] = {
		NAME(""), "-r", "-p", "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", NAME("-addb.stob"), "-w", "10",
		"-e", "lnet:0@lo:12345:34:1", "-s", "be-tx-service",
	};
	struct m0_rpc_server_ctx tx_svc = {
		.rsx_xprts         = &g_xprt,
		.rsx_xprts_nr      = 1,
		.rsx_argv          = argv,
		.rsx_argc          = ARRAY_SIZE(argv),
		.rsx_log_file_name = NAME(".log")
	};
#undef NAME

	*h = (struct m0_be_ut_h){ .buh_rpc_svc = tx_svc };

	rc = service_start(&h->buh_rpc_svc);
	M0_ASSERT(rc == 0);

	h->buh_reqh = m0_mero_to_rmach(&h->buh_rpc_svc.rsx_mero_ctx)->rm_reqh;
	M0_ASSERT(h->buh_reqh != NULL);

	m0_be_init(&h->buh_be);
	rc = m0_be_tx_engine_start(&h->buh_be.b_tx_engine, h->buh_reqh);
	M0_ASSERT(rc == 0);

	m0_be_ut_seg_create_open(h);
	h->buh_a = &h->buh_seg.bs_allocator;
	rc = m0_be_allocator_init(h->buh_a, &h->buh_seg);
	M0_ASSERT(rc == 0);

	rc = m0_be_allocator_create(h->buh_a, NULL /* XXX FIXME */);
	M0_ASSERT(rc == 0);
}

void m0_be_ut_h_fini(struct m0_be_ut_h *h)
{
	m0_be_tx_engine_stop(&h->buh_be.b_tx_engine);
	service_stop(&h->buh_rpc_svc);
	/*
	 * Allocator and segment should be finalised _after_ reqh.
	 * Max knows why.
	 */
	m0_be_fini(&h->buh_be);
#if 0 /* XXX FIXME: m0_be_allocator_{create,destroy}() need a transaction.
       * Max knows what to do about it. */
	rc = m0_be_allocator_destroy(h->buh_a, NULL);
	M0_ASSERT(rc == 0);
#endif
	m0_be_allocator_fini(h->buh_a);
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
	m0_be_tx_init(tx, ++h->buh_tid, &h->buh_be, &ut__txs_sm_group,
		      be_ut_h_persistent, be_ut_h_discarded, true, NULL, NULL);
}

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
