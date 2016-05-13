/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 7-Apr-2016
 */


/**
 * @addtogroup ha
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "ha/link.h"
#include "ut/ut.h"

#include "lib/time.h"           /* m0_time_now */
#include "lib/arith.h"          /* m0_rnd64 */
#include "lib/memory.h"         /* M0_ALLOC_PTR */
#include "reqh/reqh_service.h"  /* m0_reqh_service */
#include "ut/threads.h"         /* M0_UT_THREADS_DEFINE */
#include "ha/ut/helper.h"       /* m0_ha_ut_rpc_ctx */
#include "ha/link_service.h"    /* m0_ha_link_service_init */

struct ha_ut_link_ctx {
	struct m0_ha_link                ulc_link;
	struct m0_ha_link_cfg            ulc_cfg;
	struct m0_ha_ut_rpc_session_ctx  ulc_session_ctx;
};

static void ha_ut_link_init(struct ha_ut_link_ctx   *link_ctx,
                            struct m0_ha_ut_rpc_ctx *rpc_ctx,
                            struct m0_reqh_service  *hl_service,
                            struct m0_uint128       *id_local,
                            struct m0_uint128       *id_remote,
                            bool                     tag_even)
{
	int rc;

	m0_ha_ut_rpc_session_ctx_init(&link_ctx->ulc_session_ctx, rpc_ctx);
	link_ctx->ulc_cfg = (struct m0_ha_link_cfg){
		.hlc_reqh           = &rpc_ctx->hurc_reqh,
		.hlc_reqh_service   = hl_service,
		.hlc_rpc_session    = &link_ctx->ulc_session_ctx.husc_session,
		.hlc_link_id_local  = *id_local,
		.hlc_link_id_remote = *id_remote,
		.hlc_tag_even       = tag_even,
	};
	rc = m0_ha_link_init(&link_ctx->ulc_link, &link_ctx->ulc_cfg);
	M0_UT_ASSERT(rc == 0);
	m0_ha_link_start(&link_ctx->ulc_link);
}

static void ha_ut_link_fini(struct ha_ut_link_ctx *link_ctx)
{
	m0_ha_link_stop(&link_ctx->ulc_link);
	m0_ha_link_fini(&link_ctx->ulc_link);
	m0_ha_ut_rpc_session_ctx_fini(&link_ctx->ulc_session_ctx);
}

void m0_ha_ut_link_usecase(void)
{
	struct m0_ha_ut_rpc_ctx *rpc_ctx;
	struct m0_reqh_service  *hl_service;
	struct ha_ut_link_ctx   *ctx1;
	struct ha_ut_link_ctx   *ctx2;
	struct m0_ha_link       *hl1;
	struct m0_ha_link       *hl2;
	struct m0_uint128        id1 = M0_UINT128(0, 0);
	struct m0_uint128        id2 = M0_UINT128(0, 1);
	struct m0_ha_msg        *msg;
	struct m0_ha_msg        *msg_recv;
	uint64_t                 tag;
	uint64_t                 tag1;
	int                      rc;

	M0_ALLOC_PTR(rpc_ctx);
	M0_UT_ASSERT(rpc_ctx != NULL);
	m0_ha_ut_rpc_ctx_init(rpc_ctx);
	rc = m0_ha_link_service_init(&hl_service, &rpc_ctx->hurc_reqh);
	M0_UT_ASSERT(rc == 0);
	M0_ALLOC_PTR(ctx1);
	M0_UT_ASSERT(ctx1 != NULL);
	ha_ut_link_init(ctx1, rpc_ctx, hl_service, &id1, &id2, true);
	M0_ALLOC_PTR(ctx2);
	M0_UT_ASSERT(ctx2 != NULL);
	ha_ut_link_init(ctx2, rpc_ctx, hl_service, &id2, &id1, false);
	hl1 = &ctx1->ulc_link;
	hl2 = &ctx2->ulc_link;

	/* One way transmission. Message is sent from hl1 to hl2.  */
	M0_ALLOC_PTR(msg);
	*msg = (struct m0_ha_msg){
		.hm_fid            = M0_FID_INIT(1, 2),
		.hm_source_process = M0_FID_INIT(3, 4),
		.hm_source_service = M0_FID_INIT(5, 6),
		.hm_time           = m0_time_now(),
		.hm_data = {
			.hed_type = M0_HA_MSG_STOB_IOQ,
			.u.hed_stob_ioq = {
				.sie_conf_sdev = M0_FID_INIT(7, 8),
				.sie_size      = 0x100,
			},
		},
	};
	m0_ha_link_send(hl1, msg, &tag);
	m0_ha_link_wait_arrival(hl2);
	msg_recv = m0_ha_link_recv(hl2, &tag1);
	M0_UT_ASSERT(msg_recv != NULL);
	M0_UT_ASSERT(tag == tag1);
	M0_UT_ASSERT(msg_recv->hm_tag == tag1);
	M0_UT_ASSERT(m0_ha_msg_eq(msg_recv, msg));
	m0_ha_link_delivered(hl2, msg_recv);
	m0_ha_link_wait_delivery(hl1, tag);
	m0_free(msg);

	ha_ut_link_fini(ctx2);
	m0_free(ctx2);
	ha_ut_link_fini(ctx1);
	m0_free(ctx1);
	m0_ha_link_service_fini(hl_service);
	m0_ha_ut_rpc_ctx_fini(rpc_ctx);
	m0_free(rpc_ctx);
};

enum {
	HA_UT_THREAD_PAIR_NR = 0x10,
	HA_UT_MSG_PER_THREAD = 0x40,
};

struct ha_ut_link_mt_test {
	struct m0_ha_ut_rpc_ctx *ulmt_ctx;
	struct m0_reqh_service  *ulmt_hl_service;
	struct ha_ut_link_ctx    ulmt_link_ctx;
	struct m0_uint128        ulmt_id_local;
	struct m0_uint128        ulmt_id_remote;
	bool                     ulmt_tag_even;
	struct m0_semaphore      ulmt_start_done;
	struct m0_semaphore      ulmt_start_wait;
	struct m0_ha_msg        *ulmt_msgs_out;
	struct m0_ha_msg        *ulmt_msgs_in;
	uint64_t                *ulmt_tags_out;
	uint64_t                *ulmt_tags_in;
};

static void ha_ut_link_mt_thread(void *param)
{
	struct ha_ut_link_mt_test  *test = param;
	struct m0_ha_link          *hl;
	struct m0_ha_msg          **msgs;
	struct m0_ha_msg           *msg;
	int                         i;
	int                         j;

	ha_ut_link_init(&test->ulmt_link_ctx, test->ulmt_ctx,
			test->ulmt_hl_service, &test->ulmt_id_local,
			&test->ulmt_id_remote, test->ulmt_tag_even);
	/* barrier with the main thread */
	m0_semaphore_up(&test->ulmt_start_wait);
	m0_semaphore_down(&test->ulmt_start_done);

	hl = &test->ulmt_link_ctx.ulc_link;
	for (i = 0; i < HA_UT_MSG_PER_THREAD; ++i) {
		m0_ha_link_send(hl, &test->ulmt_msgs_out[i],
				&test->ulmt_tags_out[i]);
	}
	i = 0;
	M0_ALLOC_ARR(msgs, HA_UT_MSG_PER_THREAD);
	M0_UT_ASSERT(msgs != NULL);
	while (i < HA_UT_MSG_PER_THREAD) {
		m0_ha_link_wait_arrival(hl);
		j = i;
		while (1) {
			M0_UT_ASSERT(i <= HA_UT_MSG_PER_THREAD);
			msg = m0_ha_link_recv(hl, &test->ulmt_tags_in[i]);
			if (msg == NULL)
				break;
			M0_UT_ASSERT(i < HA_UT_MSG_PER_THREAD);
			msgs[i] = msg;
			test->ulmt_msgs_in[i] = *msg;
			++i;
		}
		M0_ASSERT(j < i);
		for ( ; j < i; ++j)
			m0_ha_link_delivered(hl, msgs[j]);
	}
	m0_free(msgs);
	for (i = 0; i < HA_UT_MSG_PER_THREAD; ++i)
		m0_ha_link_wait_delivery(hl, test->ulmt_tags_out[i]);

	ha_ut_link_fini(&test->ulmt_link_ctx);
}

M0_UT_THREADS_DEFINE(ha_ut_link_mt, &ha_ut_link_mt_thread);

void m0_ha_ut_link_multithreaded(void)
{
	struct m0_ha_ut_rpc_ctx   *rpc_ctx;
	struct m0_reqh_service    *hl_service;
	struct ha_ut_link_mt_test *tests;
	struct ha_ut_link_mt_test *test1;
	struct ha_ut_link_mt_test *test2;
	struct m0_uint128          id1;
	struct m0_uint128          id2;
	uint64_t                   seed = 42;
	int                        rc;
	int                        i;
	int                        j;

	M0_ALLOC_PTR(rpc_ctx);
	M0_UT_ASSERT(rpc_ctx != NULL);
	m0_ha_ut_rpc_ctx_init(rpc_ctx);
	rc = m0_ha_link_service_init(&hl_service, &rpc_ctx->hurc_reqh);
	M0_UT_ASSERT(rc == 0);
	M0_ALLOC_ARR(tests, HA_UT_THREAD_PAIR_NR * 2);
	M0_UT_ASSERT(tests != NULL);

	for (i = 0; i < HA_UT_THREAD_PAIR_NR; ++i) {
		id1 = M0_UINT128(i * 2,     m0_rnd64(&seed));
		id2 = M0_UINT128(i * 2 + 1, m0_rnd64(&seed));
		tests[i * 2] = (struct ha_ut_link_mt_test){
			.ulmt_id_local   = id1,
			.ulmt_id_remote  = id2,
			.ulmt_tag_even   = true,
		};
		tests[i * 2 + 1] = (struct ha_ut_link_mt_test){
			.ulmt_id_local   = id2,
			.ulmt_id_remote  = id1,
			.ulmt_tag_even   = false,
		};
	}
	for (i = 0; i < HA_UT_THREAD_PAIR_NR * 2; ++i) {
		tests[i].ulmt_ctx        = rpc_ctx;
		tests[i].ulmt_hl_service = hl_service;
		rc = m0_semaphore_init(&tests[i].ulmt_start_done, 0);
		M0_UT_ASSERT(rc == 0);
		rc = m0_semaphore_init(&tests[i].ulmt_start_wait, 0);
		M0_UT_ASSERT(rc == 0);
		M0_ALLOC_ARR(tests[i].ulmt_msgs_out, HA_UT_MSG_PER_THREAD);
		M0_UT_ASSERT(tests[i].ulmt_msgs_out != NULL);
		M0_ALLOC_ARR(tests[i].ulmt_msgs_in,  HA_UT_MSG_PER_THREAD);
		M0_UT_ASSERT(tests[i].ulmt_msgs_in != NULL);
		for (j = 0; j < HA_UT_MSG_PER_THREAD; ++j) {
			tests[i].ulmt_msgs_out[j] = (struct m0_ha_msg){
				.hm_fid = M0_FID_INIT(m0_rnd64(&seed),
				                      m0_rnd64(&seed)),
				.hm_source_process = M0_FID_INIT(
					 m0_rnd64(&seed), m0_rnd64(&seed)),
				.hm_source_service = M0_FID_INIT(
					 m0_rnd64(&seed), m0_rnd64(&seed)),
				.hm_time = m0_time_now(),
				.hm_data = {
					.hed_type = M0_HA_MSG_STOB_IOQ,
				}
			};
		}
		M0_ALLOC_ARR(tests[i].ulmt_tags_out, HA_UT_MSG_PER_THREAD);
		M0_UT_ASSERT(tests[i].ulmt_tags_out != NULL);
		M0_ALLOC_ARR(tests[i].ulmt_tags_in, HA_UT_MSG_PER_THREAD);
		M0_UT_ASSERT(tests[i].ulmt_tags_in != NULL);
	}
	M0_UT_THREADS_START(ha_ut_link_mt, HA_UT_THREAD_PAIR_NR * 2, tests);
	/* barrier with all threads */
	for (i = 0; i < HA_UT_THREAD_PAIR_NR * 2; ++i)
		m0_semaphore_down(&tests[i].ulmt_start_wait);
	for (i = 0; i < HA_UT_THREAD_PAIR_NR * 2; ++i)
		m0_semaphore_up(&tests[i].ulmt_start_done);
	M0_UT_THREADS_STOP(ha_ut_link_mt);

	for (i = 0; i < HA_UT_THREAD_PAIR_NR; ++i) {
		test1 = &tests[i * 2];
		test2 = &tests[i * 2 + 1];
		for (j = 0; j < HA_UT_MSG_PER_THREAD; ++j) {
			M0_UT_ASSERT(test1->ulmt_tags_in[j] ==
				     test2->ulmt_tags_out[j]);
			M0_UT_ASSERT(test1->ulmt_tags_out[j] ==
				     test2->ulmt_tags_in[j]);
			M0_UT_ASSERT(m0_ha_msg_eq(&test1->ulmt_msgs_in[j],
			                          &test2->ulmt_msgs_out[j]));
			M0_UT_ASSERT(m0_ha_msg_eq(&test1->ulmt_msgs_out[j],
			                          &test2->ulmt_msgs_in[j]));
		}
	}
	for (i = 0; i < HA_UT_THREAD_PAIR_NR * 2; ++i) {
		m0_free(tests[i].ulmt_tags_in);
		m0_free(tests[i].ulmt_tags_out);
		m0_free(tests[i].ulmt_msgs_in);
		m0_free(tests[i].ulmt_msgs_out);
		m0_semaphore_fini(&tests[i].ulmt_start_wait);
		m0_semaphore_fini(&tests[i].ulmt_start_done);
	}
	m0_free(tests);
	m0_ha_link_service_fini(hl_service);
	m0_ha_ut_rpc_ctx_fini(rpc_ctx);
	m0_free(rpc_ctx);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of ha group */

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
