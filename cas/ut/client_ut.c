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
 * Original author: Leonid Nikulin <leonid.nikulin@seagate.com>
 * Original creation date: 12-Apr-2016
 */

/**
 * @addtogroup cas
 *
 * @{
 */
#include "conf/ut/common.h"            /* ENDPOINT*/
#include "conf/ut/rpc_helpers.h"       /* m0_ut_rpc_machine_start */
#include "rpc/rpclib.h"                /* m0_rpc_server_ctx */
#include "lib/finject.h"
#include "ut/misc.h"                   /* M0_UT_PATH */
#include "ut/ut.h"
#include "cas/client.h"
#include "lib/finject.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CAS

#define SERVER_LOG_FILE_NAME       "cas_server.log"
#define IFID(x, y) M0_FID_TINIT('i', (x), (y))
#define SQR(x) ((x) * (x))

extern const struct m0_tl_descr ndoms_descr;

enum {
	/**
	 * @todo Greater number of indices produces -E2BIG error in idx-deleteN
	 * test case.
	 */
	COUNT = 24,
	COUNT_TREE = 10
};
M0_BASSERT(COUNT % 2 == 0);

struct async_wait {
	struct m0_clink     aw_clink;
	struct m0_semaphore aw_cb_wait;
	bool                aw_done;
};

/* Client context */
struct cl_ctx {
	/* Client network domain.*/
	struct m0_net_domain     cl_ndom;
	/* Client rpc context.*/
	struct m0_rpc_client_ctx cl_rpc_ctx;
	struct async_wait        cl_wait;
};

enum { MAX_RPCS_IN_FLIGHT = 10 };
/* Configures mero environment with given parameters. */
static char *cas_startup_cmd[] = { "m0d", "-T", "linux",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-H", "0@lo:12345:34:1",
				"-w", "10",
				"-F",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_SRC_PATH("cas/ut/conf.xc")};

static const char         *cdbnames[] = { "cas1" };
static const char      *cl_ep_addrs[] = { "0@lo:12345:34:2" };
static const char     *srv_ep_addrs[] = { "0@lo:12345:34:1" };
static struct m0_net_xprt *cs_xprts[] = { &m0_net_lnet_xprt };

static struct cl_ctx            casc_ut_cctx;
static struct m0_rpc_server_ctx casc_ut_sctx = {
		.rsx_xprts            = cs_xprts,
		.rsx_xprts_nr         = ARRAY_SIZE(cs_xprts),
		.rsx_argv             = cas_startup_cmd,
		.rsx_argc             = ARRAY_SIZE(cas_startup_cmd),
		.rsx_log_file_name    = SERVER_LOG_FILE_NAME
};

static int cas_client_init(struct cl_ctx *cctx, const char *cl_ep_addr,
			   const char *srv_ep_addr, const char* dbname,
			   struct m0_net_xprt *xprt)
{
	int                       rc;
	struct m0_rpc_client_ctx *cl_rpc_ctx;

	M0_PRE(cctx != NULL && cl_ep_addr != NULL && srv_ep_addr != NULL &&
	       dbname != NULL && xprt != NULL);

	rc = m0_net_domain_init(&cctx->cl_ndom, xprt);
	M0_UT_ASSERT(rc == 0);

	m0_semaphore_init(&cctx->cl_wait.aw_cb_wait, 0);
	cctx->cl_wait.aw_done          = false;
	cl_rpc_ctx = &cctx->cl_rpc_ctx;

	cl_rpc_ctx->rcx_net_dom            = &cctx->cl_ndom;
	cl_rpc_ctx->rcx_local_addr         = cl_ep_addr;
	cl_rpc_ctx->rcx_remote_addr        = srv_ep_addr;
	cl_rpc_ctx->rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT;
	cl_rpc_ctx->rcx_fid                = &g_process_fid;

	rc = m0_rpc_client_start(cl_rpc_ctx);
	M0_UT_ASSERT(rc == 0);

	return rc;
}

static void cas_client_fini(struct cl_ctx *cctx)
{
	int rc;
	rc = m0_rpc_client_stop(&cctx->cl_rpc_ctx);
	M0_UT_ASSERT(rc == 0);
	m0_net_domain_fini(&cctx->cl_ndom);
	m0_semaphore_fini(&cctx->cl_wait.aw_cb_wait);
}

static void casc_ut_init(struct m0_rpc_server_ctx *sctx,
			 struct cl_ctx            *cctx)
{
	int rc;
	rc = m0_rpc_server_start(sctx);
	M0_UT_ASSERT(rc == 0);
	rc = cas_client_init(cctx, cl_ep_addrs[0],
			      srv_ep_addrs[0], cdbnames[0],
			      cs_xprts[0]);
	M0_UT_ASSERT(rc == 0);
}

static void casc_ut_fini(struct m0_rpc_server_ctx *sctx,
			 struct cl_ctx            *cctx)
{
	cas_client_fini(cctx);
	m0_rpc_server_stop(sctx);
}

static bool casc_chan_cb(struct m0_clink *clink)
{
	struct async_wait *aw = container_of(clink, struct async_wait,
					     aw_clink);
	struct m0_sm      *sm = container_of(clink->cl_chan, struct m0_sm,
					     sm_chan);

	if (sm->sm_state == CASREQ_REPLIED) {
		aw->aw_done = true;
		m0_semaphore_up(&aw->aw_cb_wait);
	}
	return true;
}

static int ut_idx_create_wrp(struct cl_ctx            *cctx,
			     const struct m0_fid      *ids,
			     uint64_t                  ids_nr,
			     m0_chan_cb_t              cb,
			     struct m0_cas_rec_reply *rep)
{
	struct m0_cas_req       req;
	struct m0_chan         *chan;
	int                     rc;
	uint64_t                i;

	/* start operation */
	M0_SET0(&req);
	m0_cas_req_init(&req, &cctx->cl_rpc_ctx.rcx_session,
			m0_locality0_get()->lo_grp);
	chan = &req.ccr_sm.sm_chan;
	M0_UT_ASSERT(chan != NULL);
	m0_clink_init(&cctx->cl_wait.aw_clink, cb);
	m0_clink_add_lock(chan, &cctx->cl_wait.aw_clink);

	m0_cas_req_lock(&req);
	rc = m0_cas_index_create(&req, ids, ids_nr, NULL);
	/* wait results */
	if (rc == 0) {
		if (cb != NULL) {
			m0_cas_req_unlock(&req);
			m0_semaphore_timeddown(&cctx->cl_wait.aw_cb_wait,
					       m0_time_from_now(5, 0));
			M0_UT_ASSERT(cctx->cl_wait.aw_done);
			cctx->cl_wait.aw_done = false;
			m0_cas_req_lock(&req);
		}
		else
			m0_cas_req_wait(&req, M0_BITS(CASREQ_REPLIED),
					M0_TIME_NEVER);
		rc = m0_cas_req_generic_rc(&req);
		if (rc == 0) {
			M0_UT_ASSERT(m0_cas_req_nr(&req) == ids_nr);
			for (i = 0; i < ids_nr; i++)
				m0_cas_index_create_rep(&req, i, &rep[i]);
		}
	}
	m0_cas_req_unlock(&req);
	m0_clink_del_lock(&cctx->cl_wait.aw_clink);
	m0_cas_req_fini_lock(&req);
	m0_clink_fini(&cctx->cl_wait.aw_clink);
	return rc;
}

static int ut_idx_create_async(struct cl_ctx            *cctx,
			       const struct m0_fid      *ids,
			       uint64_t                  ids_nr,
			       m0_chan_cb_t              cb,
			       struct m0_cas_rec_reply *rep)
{
	M0_UT_ASSERT(cb != NULL);
	return ut_idx_create_wrp(cctx, ids, ids_nr, cb, rep);
}

static int ut_idx_create(struct cl_ctx            *cctx,
			 const struct m0_fid      *ids,
			 uint64_t                  ids_nr,
			 struct m0_cas_rec_reply *rep)
{
	return ut_idx_create_wrp(cctx, ids, ids_nr, NULL, rep);
}

static int ut_lookup_idx(struct cl_ctx           *cctx,
			 const struct m0_fid     *ids,
			 uint64_t                 ids_nr,
			 struct m0_cas_rec_reply *rep)
{
	struct m0_cas_req  req;
	struct m0_chan    *chan;
	int                rc;
	uint64_t           i;

	/* start operation */
	M0_SET0(&req);
	m0_cas_req_init(&req, &cctx->cl_rpc_ctx.rcx_session,
			m0_locality0_get()->lo_grp);
	chan = &req.ccr_sm.sm_chan;
	M0_UT_ASSERT(chan != NULL);
	m0_clink_init(&cctx->cl_wait.aw_clink, NULL);
	m0_clink_add_lock(chan, &cctx->cl_wait.aw_clink);

	m0_cas_req_lock(&req);
	rc = m0_cas_index_lookup(&req, ids, ids_nr);
	if (rc == 0) {
		/* wait results */
		m0_cas_req_wait(&req, M0_BITS(CASREQ_REPLIED), M0_TIME_NEVER);
		rc = m0_cas_req_generic_rc(&req);
		if (rc == 0)
			for (i = 0; i < ids_nr; i++)
				m0_cas_index_lookup_rep(&req, i, &rep[i]);
	}
	m0_cas_req_unlock(&req);
	m0_clink_del_lock(&cctx->cl_wait.aw_clink);
	m0_cas_req_fini_lock(&req);
	m0_clink_fini(&cctx->cl_wait.aw_clink);
	return rc;
}

static int ut_idx_delete(struct cl_ctx           *cctx,
			 const struct m0_fid     *ids,
			 uint64_t                 ids_nr,
			 struct m0_cas_rec_reply *rep)
{
	struct m0_cas_req  req;
	struct m0_chan    *chan;
	int                rc;
	uint64_t           i;

	/* start operation */
	M0_SET0(&req);
	m0_cas_req_init(&req, &cctx->cl_rpc_ctx.rcx_session,
			m0_locality0_get()->lo_grp);
	chan = &req.ccr_sm.sm_chan;
	M0_UT_ASSERT(chan != NULL);
	m0_clink_init(&cctx->cl_wait.aw_clink, NULL);
	m0_clink_add_lock(chan, &cctx->cl_wait.aw_clink);

	m0_cas_req_lock(&req);
	rc = m0_cas_index_delete(&req, ids, ids_nr, NULL);
	if (rc == 0) {
		/* wait results */
		m0_cas_req_wait(&req, M0_BITS(CASREQ_REPLIED), M0_TIME_NEVER);
		rc = m0_cas_req_generic_rc(&req);
		if (rc == 0)
			for (i = 0; i < ids_nr; i++)
				m0_cas_index_delete_rep(&req, i, &rep[i]);
	}
	m0_cas_req_unlock(&req);
	m0_clink_del_lock(&cctx->cl_wait.aw_clink);
	m0_cas_req_fini_lock(&req);
	m0_clink_fini(&cctx->cl_wait.aw_clink);
	return rc;
}

static int ut_idx_list(struct cl_ctx             *cctx,
		       const struct m0_fid       *start_fid,
		       uint64_t                   ids_nr,
		       uint64_t                  *rep_count,
		       struct m0_cas_ilist_reply *rep)
{
	struct m0_cas_req  req;
	struct m0_chan    *chan;
	int                rc;
	uint64_t           i;

	/* start operation */
	M0_SET0(&req);
	m0_cas_req_init(&req, &cctx->cl_rpc_ctx.rcx_session,
			m0_locality0_get()->lo_grp);
	chan = &req.ccr_sm.sm_chan;
	M0_UT_ASSERT(chan != NULL);
	m0_clink_init(&cctx->cl_wait.aw_clink, NULL);
	m0_clink_add_lock(chan, &cctx->cl_wait.aw_clink);

	m0_cas_req_lock(&req);
	rc = m0_cas_index_list(&req, start_fid, ids_nr);
	if (rc == 0) {
		/* wait results */
		m0_cas_req_wait(&req, M0_BITS(CASREQ_REPLIED), M0_TIME_NEVER);
		rc = m0_cas_req_generic_rc(&req);
		if (rc == 0) {
			*rep_count = m0_cas_req_nr(&req);
			for (i = 0; i < *rep_count; i++)
				m0_cas_index_list_rep(&req, i, &rep[i]);
		}
	}
	m0_cas_req_unlock(&req);
	m0_clink_del_lock(&cctx->cl_wait.aw_clink);
	m0_cas_req_fini_lock(&req);
	m0_clink_fini(&cctx->cl_wait.aw_clink);
	return rc;
}

static int ut_rec_put(struct cl_ctx            *cctx,
		      struct m0_cas_id         *index,
		      const struct m0_bufvec   *keys,
		      const struct m0_bufvec   *values,
		      struct m0_cas_rec_reply *rep)
{
	struct m0_cas_req  req;
	struct m0_chan    *chan;
	int                rc;
	uint64_t           i;

	/* start operation */
	M0_SET0(&req);
	m0_cas_req_init(&req, &cctx->cl_rpc_ctx.rcx_session,
			m0_locality0_get()->lo_grp);
	chan = &req.ccr_sm.sm_chan;
	M0_UT_ASSERT(chan != NULL);
	m0_clink_init(&cctx->cl_wait.aw_clink, NULL);
	m0_clink_add_lock(chan, &cctx->cl_wait.aw_clink);

	m0_cas_req_lock(&req);
	rc = m0_cas_put(&req, index, keys, values, NULL);
	if (rc == 0) {
		/* wait results */
		m0_cas_req_wait(&req, M0_BITS(CASREQ_REPLIED), M0_TIME_NEVER);
		rc = m0_cas_req_generic_rc(&req);
		if (rc == 0)
			for (i = 0; i < keys->ov_vec.v_nr; i++)
				m0_cas_put_rep(&req, i, &rep[i]);
	}
	m0_cas_req_unlock(&req);
	m0_clink_del_lock(&cctx->cl_wait.aw_clink);
	m0_cas_req_fini_lock(&req);
	m0_clink_fini(&cctx->cl_wait.aw_clink);
	return rc;
}

static void ut_get_rep_clear(struct m0_cas_get_reply *rep, uint32_t nr)
{
	uint32_t i;

	for (i = 0; i < nr; i++)
		m0_free(rep[i].cge_val.b_addr);
}

static int ut_rec_get(struct cl_ctx           *cctx,
		      struct m0_cas_id        *index,
		      const struct m0_bufvec  *keys,
		      struct m0_cas_get_reply *rep)
{
	struct m0_cas_req  req;
	struct m0_chan    *chan;
	int                rc;
	uint64_t           i;

	/* start operation */
	M0_SET0(&req);
	m0_cas_req_init(&req, &cctx->cl_rpc_ctx.rcx_session,
			m0_locality0_get()->lo_grp);
	chan = &req.ccr_sm.sm_chan;
	M0_UT_ASSERT(chan != NULL);
	m0_clink_init(&cctx->cl_wait.aw_clink, NULL);
	m0_clink_add_lock(chan, &cctx->cl_wait.aw_clink);

	m0_cas_req_lock(&req);
	rc = m0_cas_get(&req, index, keys);
	if (rc == 0) {
		/* wait results */
		m0_cas_req_wait(&req, M0_BITS(CASREQ_REPLIED), M0_TIME_NEVER);
		rc = m0_cas_req_generic_rc(&req);
		if (rc == 0) {
			M0_UT_ASSERT(m0_cas_req_nr(&req) == keys->ov_vec.v_nr);
			for (i = 0; i < keys->ov_vec.v_nr; i++) {
				m0_cas_get_rep(&req, i, &rep[i]);
				/*
				 * Lock value in memory, because it will be
				 * deallocated after m0_cas_req_fini().
				 */
				m0_cas_rep_mlock(&req, i);
			}
		}
	}
	m0_cas_req_unlock(&req);
	m0_clink_del_lock(&cctx->cl_wait.aw_clink);
	m0_cas_req_fini_lock(&req);
	m0_clink_fini(&cctx->cl_wait.aw_clink);
	return rc;
}

static void ut_next_rep_clear(struct m0_cas_next_reply *rep, uint64_t nr)
{
	uint64_t i;

	for (i = 0; i < nr; i++) {
		m0_free(rep[i].cnp_key.b_addr);
		m0_free(rep[i].cnp_val.b_addr);
		M0_SET0(&rep[i]);
	}
}

static int ut_next_rec(struct cl_ctx            *cctx,
		       struct m0_cas_id         *index,
		       struct m0_bufvec         *start_keys,
		       uint32_t                 *recs_nr,
		       struct m0_cas_next_reply *rep,
		       uint64_t                 *count)
{
	struct m0_cas_req  req;
	struct m0_chan    *chan;
	int                rc;
	uint64_t           i;

	/* start operation */
	M0_SET0(&req);
	m0_cas_req_init(&req, &cctx->cl_rpc_ctx.rcx_session,
			m0_locality0_get()->lo_grp);
	chan = &req.ccr_sm.sm_chan;
	M0_UT_ASSERT(chan != NULL);
	m0_clink_init(&cctx->cl_wait.aw_clink, NULL);
	m0_clink_add_lock(chan, &cctx->cl_wait.aw_clink);

	m0_cas_req_lock(&req);
	rc = m0_cas_next(&req, index, start_keys, recs_nr);
	if (rc == 0) {
		/* wait results */
		m0_cas_req_wait(&req, M0_BITS(CASREQ_REPLIED), M0_TIME_NEVER);
		rc = m0_cas_req_generic_rc(&req);
		if (rc == 0) {
			*count = m0_cas_req_nr(&req);
			for (i = 0; i < *count; i++) {
				m0_cas_next_rep(&req, i, &rep[i]);
				/*
				 * Lock key/value in memory, because they will
				 * be deallocated after m0_cas_req_fini().
				 */
				m0_cas_rep_mlock(&req, i);
			}
		}
	}
	m0_cas_req_unlock(&req);
	m0_clink_del_lock(&cctx->cl_wait.aw_clink);
	m0_cas_req_fini_lock(&req);
	m0_clink_fini(&cctx->cl_wait.aw_clink);
	return rc;
}

static int ut_del_rec(struct cl_ctx           *cctx,
		      struct m0_cas_id        *index,
		      struct m0_bufvec        *keys,
		      struct m0_cas_rec_reply *rep)
{
	struct m0_cas_req  req;
	struct m0_chan    *chan;
	int                rc;
	uint64_t           i;

	/* start operation */
	M0_SET0(&req);
	m0_cas_req_init(&req, &cctx->cl_rpc_ctx.rcx_session,
			m0_locality0_get()->lo_grp);
	chan = &req.ccr_sm.sm_chan;
	M0_UT_ASSERT(chan != NULL);
	m0_clink_init(&cctx->cl_wait.aw_clink, NULL);
	m0_clink_add_lock(chan, &cctx->cl_wait.aw_clink);

	m0_cas_req_lock(&req);
	rc = m0_cas_del(&req, index, keys, NULL);
	if (rc == 0) {
		/* wait results */
		m0_cas_req_wait(&req, M0_BITS(CASREQ_REPLIED), M0_TIME_NEVER);
		rc = m0_cas_req_generic_rc(&req);
		if (rc == 0) {
			M0_UT_ASSERT(m0_cas_req_nr(&req) == keys->ov_vec.v_nr);
			for (i = 0; i < keys->ov_vec.v_nr; i++)
				m0_cas_del_rep(&req, i, rep);
		}
	}
	m0_cas_req_unlock(&req);
	m0_clink_del_lock(&cctx->cl_wait.aw_clink);
	m0_cas_req_fini_lock(&req);
	m0_clink_fini(&cctx->cl_wait.aw_clink);
	return rc;
}

static void idx_create(void)
{
	struct m0_cas_rec_reply rep       = { 0 };
	const struct m0_fid     ifid      = IFID(2, 3);
	const struct m0_fid     ifid_fake = IFID(2, 4);
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == 0);
	rc = ut_lookup_idx(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == 0);
	rc = ut_lookup_idx(&casc_ut_cctx, &ifid_fake, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == -ENOENT);

	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_create_fail(void)
{
	struct m0_cas_rec_reply rep  = { 0 };
	const struct m0_fid     ifid = IFID(2, 3);
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);
	m0_fi_enable_once("cas_index_op_prepare", "cas_alloc_fail");
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == -ENOMEM);
	rc = ut_lookup_idx(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == -ENOENT);
	m0_fi_enable_once("cas_buf_get", "cas_alloc_fail");
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == -ENOMEM);
	rc = ut_lookup_idx(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == -ENOENT);
	m0_fi_enable_once("cas_index_op_prepare", "cas_alloc_fail");
	rc = ut_lookup_idx(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == -ENOMEM);
	m0_fi_enable_once("cas_buf_get", "cas_alloc_fail");
	rc = ut_lookup_idx(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == -ENOMEM);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_create_a(void)
{
	struct m0_cas_rec_reply rep  = { 0 };
	const struct m0_fid     ifid = IFID(2, 3);
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	rc = ut_idx_create_async(&casc_ut_cctx, &ifid, 1, casc_chan_cb, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == 0);

	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_create_n(void)
{
	struct m0_cas_rec_reply rep[COUNT];
	struct m0_fid           ifid[COUNT];
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	m0_forall(i, COUNT, ifid[i] = IFID(2, 3 + i), true);
	/* create several indices */
	rc = ut_idx_create(&casc_ut_cctx, ifid, COUNT, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	rc = ut_lookup_idx(&casc_ut_cctx, ifid, COUNT, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));

	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_delete(void)
{
	struct m0_cas_rec_reply rep  = { 0 };
	const struct m0_fid     ifid = IFID(2, 3);
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	rc = ut_idx_delete(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	rc = ut_lookup_idx(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == -ENOENT);

	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_delete_fail(void)
{
	struct m0_cas_rec_reply rep  = { 0 };
	const struct m0_fid     ifid = IFID(2, 3);
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	m0_fi_enable_once("cas_index_op_prepare", "cas_alloc_fail");
	rc = ut_idx_delete(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == -ENOMEM);
	rc = ut_lookup_idx(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == 0);
	m0_fi_enable_once("cas_buf_get", "cas_alloc_fail");
	rc = ut_idx_delete(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == -ENOMEM);
	rc = ut_lookup_idx(&casc_ut_cctx, &ifid, 1, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.crr_rc == 0);

	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_delete_n(void)
{
	struct m0_cas_rec_reply rep[COUNT];
	struct m0_fid           ifid[COUNT];
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	m0_forall(i, COUNT, ifid[i] = IFID(2, 3 + i), true);
	/* create several indices*/
	rc = ut_idx_create(&casc_ut_cctx, ifid, COUNT, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	rc = ut_lookup_idx(&casc_ut_cctx, ifid, COUNT, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));

	rc = ut_idx_delete(&casc_ut_cctx, ifid, COUNT, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	rc = ut_lookup_idx(&casc_ut_cctx, ifid, COUNT, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == -ENOENT));

	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_tree_insert(void)
{
	struct m0_cas_get_reply rep[COUNT_TREE];
	struct m0_cas_rec_reply rec_rep[COUNT_TREE];
	struct m0_fid           ifid[COUNT_TREE];
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;
	int                     i;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	/* initialize data */
	M0_SET_ARR0(rep);
	rc = m0_bufvec_alloc(&keys, COUNT_TREE, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT_TREE, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == COUNT_TREE);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, COUNT_TREE, ifid[i] = IFID(2, 3 + i), true);
	/* create several indices */
	rc = ut_idx_create(&casc_ut_cctx, ifid, COUNT_TREE, rec_rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, rec_rep[i].crr_rc == 0));
	rc = ut_lookup_idx(&casc_ut_cctx, ifid, COUNT_TREE, rec_rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, rec_rep[i].crr_rc == 0));

	/* insert several records into each index */
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, index.ci_fid = ifid[i],
					 rc = ut_rec_put(&casc_ut_cctx, &index,
							 &keys, &values,
							 rec_rep),
					 rc == 0));
	/* get all data */
	m0_forall(i, COUNT_TREE, *(uint64_t*)values.ov_buf[i] = 0, true);
	for (i = 0; i < COUNT_TREE; i++) {
		index.ci_fid = ifid[i];
		rc = ut_rec_get(&casc_ut_cctx, &index, &keys, rep);
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(m0_forall(j, COUNT_TREE,
			       *(uint64_t*)rep[j].cge_val.b_addr == j * j));
		ut_get_rep_clear(rep, COUNT_TREE);
	}
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_tree_delete(void)
{
	struct m0_cas_rec_reply rep[COUNT_TREE];
	struct m0_fid           ifid[COUNT_TREE];
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	/* initialize data */
	M0_SET_ARR0(rep);
	rc = m0_bufvec_alloc(&keys, COUNT_TREE, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT_TREE, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == COUNT_TREE);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, COUNT_TREE, ifid[i] = IFID(2, 3 + i), true);
	/* create several indices */
	rc = ut_idx_create(&casc_ut_cctx, ifid, COUNT_TREE, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, rep[i].crr_rc == 0));
	rc = ut_lookup_idx(&casc_ut_cctx, ifid, COUNT_TREE, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, rep[i].crr_rc == 0));

	/* insert several records into each index */
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, index.ci_fid = ifid[i],
					 rc = ut_rec_put(&casc_ut_cctx, &index,
							 &keys, &values,
							 rep),
					 rc == 0));

	/* delete all trees */
	rc = ut_idx_delete(&casc_ut_cctx, ifid, COUNT_TREE, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, rep[i].crr_rc == 0));

	rc = ut_lookup_idx(&casc_ut_cctx, ifid, COUNT_TREE, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, rep[i].crr_rc == -ENOENT));

	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_tree_delete_fail(void)
{
	struct m0_cas_rec_reply rep[COUNT_TREE];
	struct m0_cas_get_reply get_rep[COUNT_TREE];
	struct m0_fid           ifid[COUNT_TREE];
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;
	int                     i;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	/* initialize data */
	M0_SET_ARR0(rep);
	rc = m0_bufvec_alloc(&keys, COUNT_TREE, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT_TREE, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == COUNT_TREE);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, COUNT_TREE, ifid[i] = IFID(2, 3 + i), true);
	/* create several indices */
	rc = ut_idx_create(&casc_ut_cctx, ifid, COUNT_TREE, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, rep[i].crr_rc == 0));
	rc = ut_lookup_idx(&casc_ut_cctx, ifid, COUNT_TREE, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, rep[i].crr_rc == 0));

	/* insert several records into each index */
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, index.ci_fid = ifid[i],
					 rc = ut_rec_put(&casc_ut_cctx, &index,
							 &keys, &values,
							 rep),
					 rc == 0));

	/* delete all trees */
	m0_fi_enable_once("cas_index_op_prepare", "cas_alloc_fail");
	rc = ut_idx_delete(&casc_ut_cctx, ifid, COUNT_TREE, rep);
	M0_UT_ASSERT(rc == -ENOMEM);

	rc = ut_lookup_idx(&casc_ut_cctx, ifid, COUNT_TREE, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT_TREE, rep[i].crr_rc == 0));

	/* get all data */
	m0_forall(i, COUNT_TREE, *(uint64_t*)values.ov_buf[i] = 0, true);
	for (i = 0; i < COUNT_TREE; i++) {
		index.ci_fid = ifid[i];
		rc = ut_rec_get(&casc_ut_cctx, &index, &keys, get_rep);
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(m0_forall(j, COUNT_TREE,
			       *(uint64_t*)get_rep[j].cge_val.b_addr == j * j));
		ut_get_rep_clear(get_rep, COUNT_TREE);
	}

	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_list(void)
{
	struct m0_cas_rec_reply   rep[COUNT];
	struct m0_cas_ilist_reply rep_list[COUNT + 2];
	struct m0_fid             ifid[COUNT];
	uint64_t                  rep_count;
	int                       rc;
	int                       i;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	m0_forall(i, COUNT, ifid[i] = IFID(2, 3 + i), true);
	/* Create several indices. */
	rc = ut_idx_create(&casc_ut_cctx, ifid, COUNT, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	/* Get list of indices from start. */
	rc = ut_idx_list(&casc_ut_cctx, &ifid[0], COUNT, &rep_count, rep_list);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, m0_fid_eq(&rep_list[i].clr_fid,
						   &ifid[i])));
	/* Get list of indices from another position. */
	rc = ut_idx_list(&casc_ut_cctx, &ifid[COUNT / 2], COUNT,
			 &rep_count, rep_list);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count >= COUNT / 2 + 1); /* 1 for -ENOENT record */
	M0_UT_ASSERT(m0_forall(i, COUNT / 2,
				rep_list[i].clr_rc == 0 &&
				m0_fid_eq(&rep_list[i].clr_fid,
					  &ifid[i + COUNT / 2])));
	M0_UT_ASSERT(rep_list[COUNT / 2].clr_rc == -ENOENT);
	/**
	 * Get list of indices from the end. Should contain two records:
	 * the last index and -ENOENT record.
	 */
	rc = ut_idx_list(&casc_ut_cctx, &ifid[COUNT - 1], COUNT,
			 &rep_count, rep_list);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count >= 2);
	M0_UT_ASSERT(m0_fid_eq(&rep_list[0].clr_fid, &ifid[COUNT - 1]));
	M0_UT_ASSERT(rep_list[1].clr_rc == -ENOENT);

	/* Get list of indices from start (provide m0_cas_meta_fid). */
	rc = ut_idx_list(&casc_ut_cctx, &m0_cas_meta_fid, COUNT + 2,
			 &rep_count, rep_list);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count == COUNT + 2);
	M0_UT_ASSERT(m0_fid_eq(&rep_list[0].clr_fid, &m0_cas_meta_fid));
	for (i = 1; i < COUNT + 1; i++)
		M0_UT_ASSERT(m0_fid_eq(&rep_list[i].clr_fid, &ifid[i-1]));
	M0_UT_ASSERT(rep_list[COUNT + 1].clr_rc == -ENOENT);

	/* Delete all indices. */
	rc = ut_idx_delete(&casc_ut_cctx, ifid, COUNT, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	/* Get list - should be empty. */
	rc = ut_idx_list(&casc_ut_cctx, &ifid[0], COUNT, &rep_count, rep_list);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count >= 1);
	M0_UT_ASSERT(rep_list[0].clr_rc == -ENOENT);

	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void idx_list_fail(void)
{
	struct m0_cas_rec_reply   rep[COUNT];
	struct m0_cas_ilist_reply rep_list[COUNT];
	struct m0_fid             ifid[COUNT];
	uint64_t                  rep_count;
	int                       rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	m0_forall(i, COUNT, ifid[i] = IFID(2, 3 + i), true);
	/* create several indices */
	rc = ut_idx_create(&casc_ut_cctx, ifid, COUNT, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	/* get list of indices from start */
	rc = ut_idx_list(&casc_ut_cctx, &ifid[0], COUNT, &rep_count, rep_list);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, m0_fid_eq(&rep_list[i].clr_fid,
						   &ifid[i])));
	/* get failed cases for list */
	m0_fi_enable_once("cas_index_op_prepare", "cas_alloc_fail");
	rc = ut_idx_list(&casc_ut_cctx, &ifid[0], COUNT, &rep_count, rep_list);
	M0_UT_ASSERT(rc == -ENOMEM);
	m0_fi_enable_once("cas_buf_get", "cas_alloc_fail");
	rc = ut_idx_list(&casc_ut_cctx, &ifid[0], COUNT, &rep_count, rep_list);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, i == 0 ?
					 rep_list[i].clr_rc == -ENOMEM :
					 rep_list[i].clr_rc == -EPROTO));

	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static bool next_rep_equals(const struct m0_cas_next_reply *rep,
			    uint64_t                        key,
			    uint64_t                        val)
{
	return *(uint64_t *)rep->cnp_key.b_addr == key &&
	       *(uint64_t *)rep->cnp_val.b_addr == val;
}

static void next(void)
{
	struct m0_cas_rec_reply   rep[COUNT];
	struct m0_cas_next_reply  next_rep[COUNT];
	const struct m0_fid       ifid = IFID(2, 3);
	struct m0_cas_id          index;
	struct m0_bufvec          keys;
	struct m0_bufvec          values;
	uint64_t                 *start_key_val;
	struct m0_bufvec          start_key;
	uint32_t                  recs_nr;
	uint64_t                  rep_count;
	int                       rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	M0_SET_ARR0(next_rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == COUNT);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	/* insert index and records */
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	/* clear result set */
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = 0,
					*(uint64_t*)values.ov_buf[i] = 0,
					true));
	rc = m0_bufvec_alloc(&start_key, 1, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	start_key_val = start_key.ov_buf[0];
	/* perform next for all records */
	recs_nr = COUNT;
	*start_key_val = 0;
	rc = ut_next_rec(&casc_ut_cctx, &index, &start_key, &recs_nr, next_rep,
			 &rep_count);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count == recs_nr);
	M0_UT_ASSERT(m0_forall(i, rep_count, next_rep[i].cnp_rc == 0));
	M0_UT_ASSERT(m0_forall(i, rep_count, next_rep_equals(&next_rep[i],
							     i, i * i)));

	ut_next_rep_clear(next_rep, rep_count);

	/* perform next for small rep */
	recs_nr = COUNT / 2;
	rc = ut_next_rec(&casc_ut_cctx, &index, &start_key, &recs_nr, next_rep,
			 &rep_count);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count == COUNT / 2);
	M0_UT_ASSERT(m0_forall(i, rep_count, next_rep[i].cnp_rc == 0));
	M0_UT_ASSERT(m0_forall(i, rep_count, next_rep_equals(&next_rep[i],
			       i, i * i)));
	ut_next_rep_clear(next_rep, rep_count);

	/* perform next for half records */
	*start_key_val = COUNT / 2;
	recs_nr = COUNT;
	rc = ut_next_rec(&casc_ut_cctx, &index, &start_key, &recs_nr, next_rep,
			 &rep_count);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count <= recs_nr);
	M0_UT_ASSERT(m0_forall(i, COUNT / 2, next_rep[i].cnp_rc == 0));
	M0_UT_ASSERT(m0_forall(i, COUNT / 2, next_rep_equals(&next_rep[i],
			COUNT / 2 + i, (COUNT / 2 + i) * (COUNT / 2 + i))));
	M0_UT_ASSERT(next_rep[COUNT / 2].cnp_rc == -ENOENT);
	ut_next_rep_clear(next_rep, rep_count);

	/* perform next for empty result set */
	*start_key_val = COUNT;
	recs_nr = COUNT;
	rc = ut_next_rec(&casc_ut_cctx, &index, &start_key, &recs_nr,
			 next_rep, &rep_count);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count >= 1);
	M0_UT_ASSERT(next_rep[0].cnp_rc == -ENOENT);
	ut_next_rep_clear(next_rep, rep_count);

	m0_bufvec_free(&start_key);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void next_fail(void)
{
	struct m0_cas_rec_reply  rep[COUNT];
	struct m0_cas_next_reply next_rep[COUNT];
	const struct m0_fid      ifid = IFID(2, 3);
	struct m0_cas_id         index;
	struct m0_bufvec         keys;
	struct m0_bufvec         values;
	struct m0_bufvec         start_key;
	uint32_t                 recs_nr;
	uint64_t                 rep_count;
	int                      rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	M0_SET_ARR0(next_rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == COUNT);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	/* insert index and records */
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	/* clear result set */
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = 0,
					*(uint64_t*)values.ov_buf[i] = 0,
					true));
	/* perform next for all records */
	recs_nr = COUNT;
	rc = m0_bufvec_alloc(&start_key, 1, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	*(uint64_t *)start_key.ov_buf[0] = 0;
	m0_fi_enable_once("cas_records_op_prepare", "cas_alloc_fail");
	rc = ut_next_rec(&casc_ut_cctx, &index, &start_key, &recs_nr, next_rep,
			 &rep_count);
	M0_UT_ASSERT(rc == -ENOMEM);
	m0_fi_enable_once("cas_buf_get", "cas_alloc_fail");
	rc = ut_next_rec(&casc_ut_cctx, &index, &start_key, &recs_nr,
			 next_rep, &rep_count);
	M0_UT_ASSERT(rc == -ENOMEM);

	m0_bufvec_free(&start_key);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void next_multi(void)
{
	struct m0_cas_rec_reply  rep[COUNT];
	struct m0_cas_next_reply next_rep[COUNT];
	const struct m0_fid      ifid = IFID(2, 3);
	struct m0_cas_id         index;
	struct m0_bufvec         keys;
	struct m0_bufvec         values;
	struct m0_bufvec         start_keys;
	uint32_t                 recs_nr[3];
	uint64_t                 rep_count;
	int                      rc;
	int                      i;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	M0_SET_ARR0(next_rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == COUNT);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	/* insert index and records */
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	/* clear result set */
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = 0,
					*(uint64_t*)values.ov_buf[i] = 0,
					true));
	/*
	 * Perform next for three keys: first, middle and last.
	 */
	rc = m0_bufvec_alloc(&start_keys, 3, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	*(uint64_t *)start_keys.ov_buf[0] = 0;
	*(uint64_t *)start_keys.ov_buf[1] = COUNT / 2;
	*(uint64_t *)start_keys.ov_buf[2] = COUNT;
	recs_nr[0] = COUNT / 2 - 1;;
	recs_nr[1] = COUNT / 2 - 1;
	recs_nr[2] = 1;
	rc = ut_next_rec(&casc_ut_cctx, &index, &start_keys, recs_nr, next_rep,
			 &rep_count);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep_count == COUNT - 1);
	M0_UT_ASSERT(m0_forall(i, COUNT - 2, next_rep[i].cnp_rc == 0));
	M0_UT_ASSERT(next_rep[COUNT - 2].cnp_rc == -ENOENT);
	M0_UT_ASSERT(m0_forall(i, COUNT / 2 - 1, next_rep_equals(&next_rep[i],
								 i, i * i)));
	for (i = COUNT / 2 - 1; i < COUNT - 2; i++) {
		M0_UT_ASSERT(next_rep_equals(&next_rep[i], i + 1,
					     (i + 1) * (i + 1)));
	}
	ut_next_rep_clear(next_rep, rep_count);

	m0_bufvec_free(&start_keys);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void put(void)
{
	struct m0_cas_rec_reply rep[COUNT];
	const struct m0_fid     ifid = IFID(2, 3);
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr != 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));

	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void put_fail(void)
{
	struct m0_cas_rec_reply rep[COUNT];
	const struct m0_fid     ifid = IFID(2, 3);
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr != 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;

	m0_fi_enable_once("cas_records_op_prepare", "cas_alloc_fail");
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == -ENOMEM);
	m0_fi_enable_once("cas_buf_get", "cas_alloc_fail");
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == -ENOMEM);

	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void upd(void)
{
	struct m0_cas_rec_reply  rep[COUNT];
	struct m0_cas_get_reply  get_rep[COUNT];
	const struct m0_fid      ifid = IFID(2, 3);
	struct m0_cas_id         index;
	struct m0_bufvec         keys;
	struct m0_bufvec         values;
	int                      rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	M0_SET_ARR0(get_rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr != 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	/* Insert new records */
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);
	/* update several records */
	m0_forall(i, values.ov_vec.v_nr / 3,
		  *(uint64_t*)values.ov_buf[i] = COUNT * COUNT, true);
	keys.ov_vec.v_nr /= 3;
	values.ov_vec.v_nr = keys.ov_vec.v_nr;
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	values.ov_vec.v_nr = keys.ov_vec.v_nr = COUNT;
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, i < values.ov_vec.v_nr / 3 ?
				rep[i].crr_rc == -EEXIST : rep[i].crr_rc == 0));

	m0_forall(i, values.ov_vec.v_nr,
		 (*(uint64_t*)values.ov_buf[i] = 0, true));
	/* check selected values*/
	rc = ut_rec_get(&casc_ut_cctx, &index, &keys, get_rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, keys.ov_vec.v_nr,
			       *(uint64_t*)get_rep[i].cge_val.b_addr == i * i));
	ut_get_rep_clear(get_rep, keys.ov_vec.v_nr);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}


static void del(void)
{
	struct m0_cas_rec_reply rep[COUNT];
	struct m0_cas_get_reply get_rep[COUNT];
	const struct m0_fid     ifid = IFID(2, 3);
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	M0_SET_ARR0(get_rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr != 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	/* Insert new records */
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);
	/* Delete all records */
	rc = ut_del_rec(&casc_ut_cctx, &index, &keys, rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep[i].crr_rc == 0));
	/* check selected values - must be empty*/
	m0_forall(i, values.ov_vec.v_nr,
		 (*(uint64_t*)values.ov_buf[i] = 0, true));
	rc = ut_rec_get(&casc_ut_cctx, &index, &keys, get_rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, get_rep[i].cge_rc == -ENOENT));
	ut_get_rep_clear(get_rep, COUNT);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void del_fail(void)
{
	struct m0_cas_rec_reply rep[COUNT];
	struct m0_cas_get_reply get_rep[COUNT];
	const struct m0_fid     ifid = IFID(2, 3);
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	M0_SET_ARR0(get_rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr != 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	/* Insert new records */
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);
	/* Delete all records */
	m0_fi_enable_once("cas_records_op_prepare", "cas_alloc_fail");
	rc = ut_del_rec(&casc_ut_cctx, &index, &keys, rep);
	M0_UT_ASSERT(rc == -ENOMEM);
	m0_fi_enable_once("cas_buf_get", "cas_alloc_fail");
	rc = ut_del_rec(&casc_ut_cctx, &index, &keys, rep);
	M0_UT_ASSERT(rc == -ENOMEM);
	/* check selected values - must be empty*/
	m0_forall(i, values.ov_vec.v_nr,
		 (*(uint64_t*)values.ov_buf[i] = 0, true));
	rc = ut_rec_get(&casc_ut_cctx, &index, &keys, get_rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, get_rep[i].cge_rc == 0));
	ut_get_rep_clear(get_rep, COUNT);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}
static void del_n(void)
{
	struct m0_cas_rec_reply rep[COUNT];
	struct m0_cas_get_reply get_rep[COUNT];
	const struct m0_fid     ifid = IFID(2, 3);
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	M0_SET_ARR0(get_rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr != 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == COUNT);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	/* Insert new records */
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);
	/* Delete several records */
	keys.ov_vec.v_nr /= 3;
	rc = ut_del_rec(&casc_ut_cctx, &index, &keys, rep);
	M0_UT_ASSERT(rc == 0);
	/* restore old count value */
	keys.ov_vec.v_nr = COUNT;
	/* check selected values - some records not found*/
	m0_forall(i, values.ov_vec.v_nr,
		 (*(uint64_t*)values.ov_buf[i] = 0, true));
	rc = ut_rec_get(&casc_ut_cctx, &index, &keys, get_rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT,
			       (i < COUNT / 3) ? get_rep[i].cge_rc == -ENOENT :
			       rep[i].crr_rc == 0 &&
			       *(uint64_t*)get_rep[i].cge_val.b_addr == i * i));
	ut_get_rep_clear(get_rep, COUNT);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void get(void)
{
	struct m0_cas_rec_reply rep[COUNT];
	struct m0_cas_get_reply get_rep[COUNT];
	const struct m0_fid     ifid = IFID(2, 3);
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	M0_SET_ARR0(get_rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == COUNT);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	/* Insert new records */
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);

	m0_forall(i, values.ov_vec.v_nr / 3,
		 (*(uint64_t*)values.ov_buf[i] = 0, true));
	/* check selected values */
	rc = ut_rec_get(&casc_ut_cctx, &index, &keys, get_rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, keys.ov_vec.v_nr,
		     *(uint64_t*)get_rep[i].cge_val.b_addr == i * i));
	ut_get_rep_clear(get_rep, keys.ov_vec.v_nr);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}

static void get_fail(void)
{
	struct m0_cas_rec_reply rep[COUNT];
	struct m0_cas_get_reply get_rep[COUNT];
	const struct m0_fid     ifid = IFID(2, 3);
	struct m0_cas_id        index;
	struct m0_bufvec        keys;
	struct m0_bufvec        values;
	int                     rc;

	casc_ut_init(&casc_ut_sctx, &casc_ut_cctx);

	M0_SET_ARR0(rep);
	M0_SET_ARR0(get_rep);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(&values, COUNT, sizeof(uint64_t));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(keys.ov_vec.v_nr == COUNT);
	M0_UT_ASSERT(keys.ov_vec.v_nr == values.ov_vec.v_nr);
	m0_forall(i, keys.ov_vec.v_nr, (*(uint64_t*)keys.ov_buf[i]   = i,
					*(uint64_t*)values.ov_buf[i] = i * i,
					true));
	rc = ut_idx_create(&casc_ut_cctx, &ifid, 1, rep);
	M0_UT_ASSERT(rc == 0);
	index.ci_fid = ifid;
	/* Insert new records */
	rc = ut_rec_put(&casc_ut_cctx, &index, &keys, &values, rep);
	M0_UT_ASSERT(rc == 0);

	m0_forall(i, values.ov_vec.v_nr / 3,
		 (*(uint64_t*)values.ov_buf[i] = 0, true));
	/* check selected values */
	m0_fi_enable_once("cas_records_op_prepare", "cas_alloc_fail");
	rc = ut_rec_get(&casc_ut_cctx, &index, &keys, get_rep);
	M0_UT_ASSERT(rc == -ENOMEM);
	m0_fi_enable_once("cas_buf_get", "cas_alloc_fail");
	rc = ut_rec_get(&casc_ut_cctx, &index, &keys, get_rep);
	M0_UT_ASSERT(rc == -ENOMEM);
	m0_bufvec_free(&keys);
	m0_bufvec_free(&values);
	casc_ut_fini(&casc_ut_sctx, &casc_ut_cctx);
}


struct m0_ut_suite cas_client_ut = {
	.ts_name   = "cas-client",
	.ts_owners = "Leonid",
	.ts_init   = NULL,
	.ts_fini   = NULL,
	.ts_tests  = {
		{ "idx-create",             idx_create,             "Leonid" },
		{ "idx-create-fail",        idx_create_fail,        "Leonid" },
		{ "idx-create-async",       idx_create_a,           "Leonid" },
		{ "idx-delete",             idx_delete,             "Leonid" },
		{ "idx-delete-fail",        idx_delete_fail,        "Leonid" },
		{ "idx-createN",            idx_create_n,           "Leonid" },
		{ "idx-deleteN",            idx_delete_n,           "Leonid" },
		{ "idx-list",               idx_list,               "Leonid" },
		{ "idx-list-fail",          idx_list_fail,          "Leonid" },
		{ "next",                   next,                   "Leonid" },
		{ "next-fail",              next_fail,              "Leonid" },
		{ "next-multi",             next_multi,             "Egor"   },
		{ "put",                    put,                    "Leonid" },
		{ "put-fail",               put_fail,               "Leonid" },
		{ "get",                    get,                    "Leonid" },
		{ "get-fail",               get_fail,               "Leonid" },
		{ "upd",                    upd,                    "Leonid" },
		{ "del",                    del,                    "Leonid" },
		{ "del-fail",               del_fail,               "Leonid" },
		{ "delN",                   del_n,                  "Leonid" },
		{ "idx-tree-insert",        idx_tree_insert,        "Leonid" },
		{ "idx-tree-delete",        idx_tree_delete,        "Leonid" },
		{ "idx-tree-delete-fail",   idx_tree_delete_fail,   "Leonid" },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM

/** @} end of cas group */

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
