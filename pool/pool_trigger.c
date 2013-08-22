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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 06/19/2013
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"
#include "lib/assert.h"
#include "lib/misc.h"        /* M0_IN() */
#include "lib/memory.h"
#include "fop/fop.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "rpc/rpclib.h"
#include "lib/getopts.h"
#include "mero/init.h"
#include "pool/pool.h"
#include "pool/pool_fops.h"


/*#define RPC_ASYNC_MODE*/

struct m0_net_domain     cl_ndom;
struct m0_dbenv          cl_dbenv;
struct m0_cob_domain     cl_cdom;
struct m0_rpc_client_ctx cl_ctx;

enum {
	MAX_RPCS_IN_FLIGHT = 10,
	MAX_RPC_SLOTS_NR   = 10,
	MAX_FILES_NR       = 10,
	MAX_SERVERS        = 1024
};

const char *poolmach_state_name[] = {
	[M0_PNDS_ONLINE]          = "Online",
	[M0_PNDS_FAILED]          = "Failed",
	[M0_PNDS_OFFLINE]         = "Offline",
	[M0_PNDS_SNS_REPAIRING]   = "SNS Repairing",
	[M0_PNDS_SNS_REPAIRED]    = "SNS Repaired",
	[M0_PNDS_SNS_REBALANCING] = "SNS Rebalancing",
	[M0_PNDS_SNS_REBALANCED]  = "SNS Rebalanced",
};

const char *cl_ep_addr;
const char *srv_ep_addr[MAX_SERVERS];
const char *dbname = "sr_cdb";

struct rpc_ctx {
	struct m0_rpc_conn    ctx_conn;
	struct m0_rpc_session ctx_session;
	const char           *ctx_sep;
};


static int poolmach_client_init(void)
{
	int    rc;

	rc = m0_net_domain_init(&cl_ndom, &m0_net_lnet_xprt, &m0_addb_proc_ctx);
	if (rc != 0)
		return rc;

	cl_ctx.rcx_net_dom            = &cl_ndom;
	cl_ctx.rcx_local_addr         = cl_ep_addr;
	cl_ctx.rcx_remote_addr        = srv_ep_addr[0];
	cl_ctx.rcx_db_name            = dbname;
	cl_ctx.rcx_dbenv              = &cl_dbenv;
	cl_ctx.rcx_nr_slots           = MAX_RPC_SLOTS_NR;
	cl_ctx.rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT;

	rc = m0_rpc_client_start(&cl_ctx);
	if (rc != 0)
		m0_net_domain_fini(&cl_ndom);
	return rc;
}

static void poolmach_client_fini(void)
{
	int rc;

	rc = m0_rpc_client_stop(&cl_ctx);
	if (rc != 0)
		M0_LOG(M0_DEBUG, "Failed to stop client");

	m0_net_domain_fini(&cl_ndom);
}

static int poolmach_rpc_ctx_init(struct rpc_ctx *ctx, const char *sep)
{
	ctx->ctx_sep = sep;
	return m0_rpc_client_connect(&ctx->ctx_conn,
				     &ctx->ctx_session,
				     &cl_ctx.rcx_rpc_machine,
				     sep,
				     MAX_RPCS_IN_FLIGHT,
				     MAX_RPC_SLOTS_NR);
}

static void poolmach_rpc_ctx_fini(struct rpc_ctx *ctx)
{
	int rc;

	rc = m0_rpc_session_destroy(&ctx->ctx_session, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_DEBUG, "Failed to destroy session to %s",
				 ctx->ctx_sep);
	rc = m0_rpc_conn_destroy(&ctx->ctx_conn, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_DEBUG, "Failed to destroy connection to %s",
				 ctx->ctx_sep);
}

#ifdef RPC_ASYNC_MODE
static int poolmach_rpc_post(struct m0_fop *fop,
			     struct m0_rpc_session *session,
			     const struct m0_rpc_item_ops *ri_ops,
			     m0_time_t  deadline)
{
	struct m0_rpc_item *item;

	M0_PRE(fop != NULL);
	M0_PRE(session != NULL);

	item              = &fop->f_item;
	item->ri_ops      = ri_ops;
	item->ri_session  = session;
	item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline = deadline;
	item->ri_resend_interval = M0_TIME_NEVER;

	return m0_rpc_post(item);
}
#endif

struct m0_mutex poolmach_wait_mutex;
struct m0_chan  poolmach_wait;
int32_t         srv_cnt = 0;

extern struct m0_fop_type trigger_fop_fopt;

static void trigger_rpc_item_reply_cb(struct m0_rpc_item *item)
{
	static int32_t srv_rep_cnt = 0;
	struct m0_fop *rep_fop;

	M0_PRE(item != NULL);

	if (item->ri_error == 0) {
		rep_fop = m0_rpc_item_to_fop(item->ri_reply);
		M0_ASSERT(rep_fop->f_type == &m0_fop_poolmach_query_rep_fopt ||
			  rep_fop->f_type == &m0_fop_poolmach_set_rep_fopt);

		if (rep_fop->f_type == &m0_fop_poolmach_query_rep_fopt) {
			struct m0_fop_poolmach_query_rep *query_fop_rep;
			query_fop_rep = m0_fop_data(rep_fop);
			fprintf(stderr, "Query: rc = %d, state=%d\n",
					(int)query_fop_rep->fpq_rc,
					(int)query_fop_rep->fpq_state);
		} else {
			struct m0_fop_poolmach_set_rep *set_fop_rep;
			set_fop_rep = m0_fop_data(rep_fop);
			fprintf(stderr, "Set: rc = %d\n",
					(int)set_fop_rep->fps_rc);
		}
	}

	M0_CNT_INC(srv_rep_cnt);
	if (srv_rep_cnt == srv_cnt) {
		m0_mutex_lock(&poolmach_wait_mutex);
		m0_chan_signal(&poolmach_wait);
		m0_mutex_unlock(&poolmach_wait_mutex);
	}
	fprintf(stderr, "Got reply %d out of %d\n", srv_rep_cnt, srv_cnt);
}

const struct m0_rpc_item_ops poolmach_fop_rpc_item_ops = {
	.rio_replied = trigger_rpc_item_reply_cb,
};

void print_help()
{
	fprintf(stdout,
"-O Q(uery) or S(et): Query device state or Set device state\n"
"-T (D)evice or (N)ode\n"
"-I device_index or node_index\n"
"[-s device_state]: if Set, the device state. The states supported are:\n"
"    0:    M0_PNDS_ONLINE\n"
"    1:    M0_PNDS_FAILED\n"
"    2:    M0_PNDS_OFFLINE\n"
"-C Client_end_point\n"
"-S Server_end_point [-S Server_end_point ]: max number is %d\n", MAX_SERVERS);
}

int main(int argc, char *argv[])
{
	struct rpc_ctx        *ctxs;
	struct m0_clink        poolmach_clink;
	const char            *op = NULL;
	const char            *type = NULL;
	uint32_t               device_index = -1;
	uint32_t               device_state = -1;
	m0_time_t              start;
	m0_time_t              delta;
	int                    rc;
	int                    rc2 = 0;
	int                    i;

	rc = M0_GETOPTS("poolmach", argc, argv,
			M0_STRINGARG('O',
				     "Q(uery) or S(et)",
				     LAMBDA(void, (const char *str) {
						op = str;
						if (op[0] != 'Q' &&
						    op[0] != 'q' &&
						    op[0] != 'S' &&
						    op[0] != 's')
							rc2 = -EINVAL;
					}
				     )
				    ),
			M0_STRINGARG('T',
				     "D(evice) or N(ode)",
				     LAMBDA(void, (const char *str) {
						type = str;
						if (type[0] != 'D' &&
						    type[0] != 'd' &&
						    type[0] != 'N' &&
						    type[0] != 'n')
							rc2 = -EINVAL;
					}
				     )
				    ),
			M0_FORMATARG('I', "device index", "%u", &device_index),
			M0_FORMATARG('s', "device state", "%u", &device_state),
			M0_STRINGARG('C', "Client endpoint",
				     LAMBDA(void, (const char *str) {
						cl_ep_addr = str;
					}
				     )
				    ),
			M0_STRINGARG('S', "Server endpoint",
				     LAMBDA(void, (const char *str) {
						srv_ep_addr[srv_cnt] = str;
						++srv_cnt;
						if (srv_cnt >= MAX_SERVERS)
							rc2 = -E2BIG;
					}
				     )
				    ),
			);
	if (rc != 0 || rc2 != 0) {
		print_help();
		return rc;
	}

	if (op == NULL         || type == NULL       || device_index == -1 ||
	    cl_ep_addr == NULL || srv_cnt == 0       ||
	    ((op[0] == 'S' || op[0] == 's') &&
	     (device_state < 0 || device_state > 2))  ) {
		print_help();
		return -EINVAL;
	}

	rc = m0_init();
	if (rc != 0)
		return rc;

	rc = poolmach_client_init();
	if (rc != 0)
		return rc;

	m0_mutex_init(&poolmach_wait_mutex);
	m0_chan_init(&poolmach_wait, &poolmach_wait_mutex);
	m0_clink_init(&poolmach_clink, NULL);

	M0_ALLOC_ARR(ctxs, srv_cnt);
	if (ctxs == NULL)
		return -ENOMEM;

	for (i = 1; i < srv_cnt; ++i) {
		/* connection to srv_ep_addr[0] is establish in
		 * poolmach_client_init() already */
		rc = poolmach_rpc_ctx_init(&ctxs[i], srv_ep_addr[i]);
		if (rc != 0)
			return rc;
	}

	m0_mutex_lock(&poolmach_wait_mutex);
	m0_clink_add(&poolmach_wait, &poolmach_clink);
	m0_mutex_unlock(&poolmach_wait_mutex);
	start = m0_time_now();
	for (i = 0; i < srv_cnt; ++i) {
		struct m0_fop         *req;
		struct m0_rpc_session *session;

		if (i == 0)
			session = &cl_ctx.rcx_session;
		else
			session = &ctxs[i].ctx_session;

		if (op[0] == 'Q' || op[0] == 'q') {
			struct m0_fop_poolmach_query *query_fop;

			req = m0_fop_alloc(&m0_fop_poolmach_query_fopt, NULL);
			if (req == NULL)
				return -ENOMEM;
			query_fop = m0_fop_data(req);
			if (type[0] == 'N' || type[0] == 'n')
				query_fop->fpq_type  = M0_POOL_NODE;
			else
				query_fop->fpq_type  = M0_POOL_DEVICE;
			query_fop->fpq_index = device_index;
		} else {
			struct m0_fop_poolmach_set   *set_fop;
			req = m0_fop_alloc(&m0_fop_poolmach_set_fopt, NULL);
			if (req == NULL)
				return -ENOMEM;
			set_fop = m0_fop_data(req);
			if (type[0] == 'N' || type[0] == 'n')
				set_fop->fps_type  = M0_POOL_NODE;
			else
				set_fop->fps_type  = M0_POOL_DEVICE;
			set_fop->fps_index = device_index;
			set_fop->fps_state = device_state;
		}

		fprintf(stderr, "sending/posting to %s\n", srv_ep_addr[i]);
#ifdef RPC_ASYNC_MODE
		rc = poolmach_rpc_post(req, session,
				       &poolmach_fop_rpc_item_ops,
				       0);
#else
		rc = m0_rpc_client_call(req, session, NULL, 0);
		if (rc != 0) {
			m0_fop_put(req);
			return rc;
		}

		if (op[0] == 'Q' || op[0] == 'q') {
			struct m0_fop_poolmach_query_rep *query_fop_rep;
			struct m0_fop *rep;
			const char    *name;
			uint32_t       state;
			rep = m0_rpc_item_to_fop(req->f_item.ri_reply);
			query_fop_rep = m0_fop_data(rep);
			state = query_fop_rep->fpq_state;
			name = (state >= 0 &&
				state < ARRAY_SIZE(poolmach_state_name))?
					poolmach_state_name[state]:
					"N/A";

			fprintf(stderr, "Query got reply: rc = %d, "
					"state = %d (%s)\n",
					(int)query_fop_rep->fpq_rc,
					(int)state,
					name);
		} else {
			struct m0_fop_poolmach_set_rep *set_fop_rep;
			struct m0_fop *rep;
			rep = m0_rpc_item_to_fop(req->f_item.ri_reply);
			set_fop_rep = m0_fop_data(rep);
			fprintf(stderr, "Set got reply: rc = %d\n",
					(int)set_fop_rep->fps_rc);
		}
#endif
		m0_fop_put(req);
		if (rc != 0)
			return rc;
	}

#ifdef RPC_ASYNC_MODE
	m0_chan_wait(&poolmach_clink);
#endif

	delta = m0_time_sub(m0_time_now(), start);
	printf("Time: %lu.%2.2lu sec\n", (unsigned long)m0_time_seconds(delta),
			(unsigned long)m0_time_nanoseconds(delta) * 100 /
			M0_TIME_ONE_BILLION);
	for (i = 1; i < srv_cnt; ++i)
		poolmach_rpc_ctx_fini(&ctxs[i]);
	poolmach_client_fini();
	m0_fini();

	return rc;
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
