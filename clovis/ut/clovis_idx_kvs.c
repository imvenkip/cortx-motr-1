/* -*- C -*- */
/*
 * COPYRIGHT 2016 SEAGATE LLC
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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 30-Apr-2016
 */


#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CLOVIS
#include "lib/trace.h"

#include "lib/misc.h"               /* M0_SRC_PATH */
#include "lib/finject.h"
#include "ut/ut.h"
#include "ut/misc.h"                /* M0_UT_CONF_PROFILE */
#include "rpc/rpclib.h"             /* m0_rpc_server_ctx */
#include "fid/fid.h"
#include "clovis/clovis.h"
#include "clovis/clovis_internal.h"
#include "clovis/clovis_idx.h"

#define WAIT_TIMEOUT               M0_TIME_NEVER
#define SERVER_LOG_FILE_NAME       "cas_server.log"

static struct m0_clovis        *ut_m0c;
static struct m0_clovis_config  ut_m0c_config;

enum {
	MAX_RPCS_IN_FLIGHT = 10,
	CNT = 10,
};

static char *cas_startup_cmd[] = { "m0d", "-T", "linux",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-H", "0@lo:12345:34:1",
				"-w", "10",
				"-F",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_SRC_PATH("cas/ut/conf.xc")};

static const char         *local_ep_addr = "0@lo:12345:34:2";
static const char         *srv_ep_addr   = { "0@lo:12345:34:1" };
static const char         *process_fid   = "<0x7200000000000000:0>";
static struct m0_net_xprt *cs_xprts[]    = { &m0_net_lnet_xprt };

static struct m0_rpc_server_ctx kvs_ut_sctx = {
		.rsx_xprts            = cs_xprts,
		.rsx_xprts_nr         = ARRAY_SIZE(cs_xprts),
		.rsx_argv             = cas_startup_cmd,
		.rsx_argc             = ARRAY_SIZE(cas_startup_cmd),
		.rsx_log_file_name    = SERVER_LOG_FILE_NAME
};

static void idx_kvs_ut_clovis_init()
{
	int rc;

	ut_m0c_config.cc_is_oostore            = false;
	ut_m0c_config.cc_is_read_verify        = false;
	ut_m0c_config.cc_local_addr            = local_ep_addr;
	ut_m0c_config.cc_ha_addr               = srv_ep_addr;
	ut_m0c_config.cc_confd                 = srv_ep_addr;
	ut_m0c_config.cc_profile               = M0_UT_CONF_PROFILE;
	/* Use fake fid, see clovis_initlift_resource_manager(). */
	ut_m0c_config.cc_process_fid           = process_fid;
	ut_m0c_config.cc_tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	ut_m0c_config.cc_max_rpc_msg_size      = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
	ut_m0c_config.cc_idx_service_id        = M0_CLOVIS_IDX_MERO;
	ut_m0c_config.cc_idx_service_conf      = NULL;

	m0_fi_enable_once("clovis_ha_init", "skip-ha-init");
	m0_fi_enable_once("clovis_initlift_addb2", "no-addb2");
	m0_fi_enable("clovis_ha_process_event", "no-link");
	rc = m0_clovis_init(&ut_m0c, &ut_m0c_config, false);
	M0_UT_ASSERT(rc == 0);
	m0_fi_disable("clovis_ha_process_event", "no-link");
	ut_m0c->m0c_mero = m0_get();
}

static void idx_kvs_ut_init()
{
	int rc;

	M0_SET0(&kvs_ut_sctx.rsx_mero_ctx);
	rc = m0_rpc_server_start(&kvs_ut_sctx);
	M0_ASSERT(rc == 0);
	idx_kvs_ut_clovis_init();
}

static void idx_kvs_ut_fini()
{
	m0_fi_enable_once("clovis_ha_fini", "skip-ha-fini");
	m0_fi_enable_once("clovis_initlift_addb2", "no-addb2");
	m0_fi_enable("clovis_ha_process_event", "no-link");
	m0_clovis_fini(&ut_m0c, false);
	m0_fi_disable("clovis_ha_process_event", "no-link");
	m0_rpc_server_stop(&kvs_ut_sctx);
}

static void ut_kvs_init_fini(void)
{
	idx_kvs_ut_init();
	idx_kvs_ut_fini();
}

static int *kvs_rcs_alloc(int count)
{
	int  i;
	int *rcs;

	M0_ALLOC_ARR(rcs, count);
	M0_UT_ASSERT(rcs != NULL);
	for (i = 0; i < count; i++)
		/* Set to some value to assert that UT actually changed rc. */
		rcs[i] = 0x5f;
	return rcs;
}

static void ut_kvs_namei_ops(void)
{
	struct m0_clovis_container realm;
	struct m0_clovis_idx       idx;
	struct m0_clovis_idx       idup;
	struct m0_clovis_idx       idx0;
	struct m0_fid              ifid = M0_FID_TINIT('i', 2, 1);
	struct m0_fid              ifid0 = M0_FID_TINIT('i', 0, 0);
	struct m0_clovis_op       *op = NULL;
	struct m0_bufvec           keys;
	int                       *rcs;
	int                        rc;

	idx_kvs_ut_init();
	m0_clovis_container_init(&realm, NULL, &M0_CLOVIS_UBER_REALM, ut_m0c);
	m0_clovis_idx_init(&idx, &realm.co_realm, (struct m0_uint128 *)&ifid);

	/* Create the index. */
	rc = m0_clovis_entity_create(&idx.in_entity, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_fini(op);
	m0_free0(&op);

	/* Create an index with the same fid once more => -EEXIST. */
	m0_clovis_idx_init(&idup, &realm.co_realm, (struct m0_uint128 *)&ifid);
	rc = m0_clovis_entity_create(&idup.in_entity, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_FAILED), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(op->op_sm.sm_rc == -EEXIST);
	m0_clovis_op_fini(op);
	m0_free0(&op);
	m0_clovis_idx_fini(&idup);

	/* Check that index exists. */
	rc = m0_clovis_idx_op(&idx, M0_CLOVIS_IC_LOOKUP, NULL, NULL, NULL, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_fini(op);
	m0_free0(&op);

	/* List all indices (only one exists). */
	rcs = kvs_rcs_alloc(2);
	rc = m0_bufvec_alloc(&keys, 2, sizeof(struct m0_fid));
	M0_UT_ASSERT(rc == 0);
	m0_clovis_idx_init(&idx0, &realm.co_realm, (struct m0_uint128 *)&ifid0);
	rc = m0_clovis_idx_op(&idx, M0_CLOVIS_IC_LIST, &keys, NULL, rcs, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rcs[0] == 0);
	M0_UT_ASSERT(rcs[1] == -ENOENT);
	M0_UT_ASSERT(keys.ov_vec.v_nr == 2);
	M0_UT_ASSERT(keys.ov_vec.v_count[0] == sizeof(struct m0_fid));
	M0_UT_ASSERT(m0_fid_eq(keys.ov_buf[0], &ifid));
	M0_UT_ASSERT(keys.ov_vec.v_count[1] == sizeof(struct m0_fid));
	M0_UT_ASSERT(!m0_fid_is_set(keys.ov_buf[1]));
	m0_clovis_op_fini(op);
	m0_free0(&op);
	m0_free0(&rcs);
	m0_clovis_idx_fini(&idx0);

	/* Delete the index. */
	rc = m0_clovis_entity_delete(&idx.in_entity, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_fini(op);
	m0_free0(&op);

	/* Delete an index with the same fid once more => -ENOENT. */
	m0_clovis_idx_init(&idup, &realm.co_realm, (struct m0_uint128 *)&ifid);
	rc = m0_clovis_entity_delete(&idup.in_entity, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_FAILED), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(op->op_sm.sm_rc == -ENOENT);
	m0_clovis_op_fini(op);
	m0_free0(&op);
	m0_clovis_idx_fini(&idup);

	m0_clovis_idx_fini(&idx);
	idx_kvs_ut_fini();
}

static uint64_t kvs_key(uint64_t i)
{
	return 100 + i;
}

static uint64_t kvs_val(uint64_t i)
{
	return 100 + i * i;
}

static void ut_kvs_record_ops(void)
{
	struct m0_clovis_container realm;
	struct m0_clovis_idx       idx;
	struct m0_fid              ifid = M0_FID_TINIT('i', 2, 1);
	struct m0_clovis_op       *op = NULL;
	struct m0_bufvec           keys;
	struct m0_bufvec           vals;
	uint64_t                   i;
	bool                       eof;
	uint64_t                   accum;
	uint64_t                   recs_nr;
	uint64_t                   cur_key;
	int                        rc;
	int                       *rcs;

	idx_kvs_ut_init();
	m0_clovis_container_init(&realm, NULL, &M0_CLOVIS_UBER_REALM, ut_m0c);
	m0_clovis_idx_init(&idx, &realm.co_realm, (struct m0_uint128 *)&ifid);

	/* Create index. */
	rc = m0_clovis_entity_create(&idx.in_entity, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_fini(op);
	m0_free0(&op);

	/* Get non-existing key. */
	rcs = kvs_rcs_alloc(1);
	rc = m0_bufvec_alloc(&keys, 1, sizeof(uint64_t)) ?:
	     m0_bufvec_empty_alloc(&vals, 1);
	M0_UT_ASSERT(rc == 0);
	*(uint64_t*)keys.ov_buf[0] = kvs_key(10);
	rc = m0_clovis_idx_op(&idx, M0_CLOVIS_IC_GET, &keys, &vals, rcs, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(vals.ov_buf[0] == NULL);
	M0_UT_ASSERT(vals.ov_vec.v_count[0] == 0);
	M0_UT_ASSERT(rcs[0] == -ENOENT);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&vals);
	m0_clovis_op_fini(op);
	m0_free0(&op);
	m0_free0(&rcs);


	/* Add records to the index. */
	rcs = kvs_rcs_alloc(CNT);
	rc = m0_bufvec_alloc(&keys, CNT, sizeof(uint64_t)) ?:
	     m0_bufvec_alloc(&vals, CNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < keys.ov_vec.v_nr; i++) {
		*(uint64_t *)keys.ov_buf[i] = kvs_key(i);
		*(uint64_t *)vals.ov_buf[i] = kvs_val(i);
	}
	rc = m0_clovis_idx_op(&idx, M0_CLOVIS_IC_PUT, &keys, &vals, rcs, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc  == 0);
	M0_UT_ASSERT(m0_forall(i, CNT, rcs[i] == 0));
	m0_bufvec_free(&keys);
	m0_bufvec_free(&vals);
	m0_clovis_op_fini(op);
	m0_free0(&op);
	m0_free0(&rcs);

	/* Get records from the index by keys. */
	rcs = kvs_rcs_alloc(CNT);
	rc = m0_bufvec_alloc(&keys, CNT, sizeof(uint64_t)) ?:
	     m0_bufvec_empty_alloc(&vals, CNT);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < keys.ov_vec.v_nr; i++)
		*(uint64_t*)keys.ov_buf[i] = kvs_key(i);
	rc = m0_clovis_idx_op(&idx, M0_CLOVIS_IC_GET, &keys, &vals, rcs, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, CNT,
			       rcs[i] == 0 &&
		               *(uint64_t *)vals.ov_buf[i] == kvs_val(i)));
	m0_bufvec_free(&keys);
	m0_bufvec_free(&vals);
	m0_clovis_op_fini(op);
	m0_free0(&op);
	m0_free0(&rcs);

	/* Get records with all existing keys, except the one. */
	rcs = kvs_rcs_alloc(CNT);
	rc = m0_bufvec_alloc(&keys, CNT, sizeof(uint64_t)) ?:
	     m0_bufvec_empty_alloc(&vals, CNT);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < keys.ov_vec.v_nr; i++)
		*(uint64_t*)keys.ov_buf[i] = kvs_key(i);
	*(uint64_t *)keys.ov_buf[5] = kvs_key(999);
	rc = m0_clovis_idx_op(&idx, M0_CLOVIS_IC_GET, &keys, &vals, rcs, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < CNT; i++) {
		if (i != 5) {
			M0_UT_ASSERT(rcs[i] == 0);
			M0_UT_ASSERT(*(uint64_t *)vals.ov_buf[i] == kvs_val(i));
		} else {
			M0_UT_ASSERT(rcs[i] == -ENOENT);
			M0_UT_ASSERT(vals.ov_buf[5] == NULL);
		}
	}
	m0_bufvec_free(&keys);
	m0_bufvec_free(&vals);
	m0_clovis_op_fini(op);
	m0_free0(&op);
	m0_free0(&rcs);

	/* Iterate over all records in the index. */
	rcs = kvs_rcs_alloc(CNT);
	rc = m0_bufvec_empty_alloc(&keys, CNT) ?:
	     m0_bufvec_empty_alloc(&vals, CNT);
	M0_UT_ASSERT(rc == 0);
	keys.ov_buf[0] = m0_alloc(sizeof(uint64_t));
	keys.ov_vec.v_count[0] = sizeof(uint64_t);
	*(uint64_t *)keys.ov_buf[0] = kvs_key(0);
	rc = m0_clovis_idx_op(&idx, M0_CLOVIS_IC_NEXT, &keys, &vals, rcs, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, CNT,
			       rcs[i] == 0 &&
			       *(uint64_t*)keys.ov_buf[i] == kvs_key(i) &&
			       *(uint64_t*)vals.ov_buf[i] == kvs_val(i)));
	m0_bufvec_free(&keys);
	m0_bufvec_free(&vals);
	m0_clovis_op_fini(op);
	m0_free0(&op);
	m0_free0(&rcs);

	/*
	 * Iterate over all records in the index, starting from the beginning
	 * and requesting two records at a time.
	 */
	accum = 0;
	cur_key = 0;
	do {
		rcs = kvs_rcs_alloc(2);
		rc = m0_bufvec_empty_alloc(&keys, 2) ?:
		     m0_bufvec_empty_alloc(&vals, 2);
		M0_UT_ASSERT(rc == 0);
		if (cur_key != 0) {
			keys.ov_buf[0] = m0_alloc(sizeof(uint64_t));
			keys.ov_vec.v_count[0] = sizeof(uint64_t);
			*(uint64_t *)keys.ov_buf[0] = cur_key;
		} else {
			/*
			 * Pass NULL in order to request records starting from
			 * the smallest key.
			 */
			keys.ov_buf[0] = NULL;
			keys.ov_vec.v_count[0] = 0;
		}
		rc = m0_clovis_idx_op(&idx, M0_CLOVIS_IC_NEXT, &keys, &vals,
				      rcs, &op);
		M0_UT_ASSERT(rc == 0);
		m0_clovis_op_launch(&op, 1);
		rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE),
				       WAIT_TIMEOUT);
		M0_UT_ASSERT(rc == 0);
		for (i = 0; i < vals.ov_vec.v_nr && rcs[i] == 0; i++)
			;;
		recs_nr = i;
		eof = recs_nr < keys.ov_vec.v_nr;
		for (i = 0; i < recs_nr; i++) {
			M0_UT_ASSERT(*(uint64_t *)keys.ov_buf[i] ==
				     kvs_key(accum + i));
			M0_UT_ASSERT(*(uint64_t *)vals.ov_buf[i] ==
				     kvs_val(accum + i));
			cur_key = *(uint64_t *)keys.ov_buf[i];
		}
		m0_bufvec_free(&keys);
		m0_bufvec_free(&vals);
		m0_clovis_op_fini(op);
		m0_free0(&op);
		m0_free0(&rcs);
		/*
		 * Starting key is also included in returned number of records,
		 * so extract 1. The only exception is the first request, when
		 * starting key is unknown. It is accounted before accum check
		 * after eof is reached.
		 */
		accum += recs_nr - 1;
	} while (!eof);
	accum++;
	M0_UT_ASSERT(accum == CNT);

	/* Remove the records from the index. */
	rcs = kvs_rcs_alloc(CNT);
	rc = m0_bufvec_alloc(&keys, CNT, sizeof(uint64_t));
	for (i = 0; i < keys.ov_vec.v_nr; i++)
		*(uint64_t *)keys.ov_buf[i] = kvs_key(i);
	rc = m0_clovis_idx_op(&idx, M0_CLOVIS_IC_DEL, &keys, NULL, rcs, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, CNT, rcs[i] == 0));
	m0_bufvec_free(&keys);
	m0_clovis_op_fini(op);
	m0_free0(&op);
	m0_free0(&rcs);

	/* Remove the index. */
	rc = m0_clovis_entity_delete(&idx.in_entity, &op);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_launch(&op, 1);
	rc = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE), WAIT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	m0_clovis_op_fini(op);
	m0_free0(&op);

	m0_clovis_idx_fini(&idx);
	idx_kvs_ut_fini();
}

struct m0_ut_suite ut_suite_clovis_idx_kvs = {
	.ts_name   = "clovis-idx-kvs",
	.ts_owners = "Egor",
	.ts_init   = NULL,
	.ts_fini   = NULL,
	.ts_tests  = {
		{ "init-fini",  ut_kvs_init_fini,  "Egor" },
		{ "namei-ops",  ut_kvs_namei_ops,  "Egor" },
		{ "record-ops", ut_kvs_record_ops, "Egor" },
		{ NULL, NULL }
	}
};

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
