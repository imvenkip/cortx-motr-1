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
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 10/31/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include <stdlib.h>

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/getopts.h"
#include "lib/misc.h"           /* M0_IN() */
#include "lib/memory.h"
#include "lib/time.h"
#include "fop/fop.h"
#include "net/lnet/lnet.h"
#include "mero/init.h"

#include "ut/rpc.h"
#include "rpc/rpc.h"
#include "rpc/rpclib.h"

#include "trigger_fop.h"
#include "trigger_fop_xc.h"

enum {
	MAX_RPCS_IN_FLIGHT = 10,
	MAX_RPC_SLOTS_NR   = 2,
	RPC_TIMEOUTS       = 5,
	MAX_FILES_NR       = 10
};

struct m0_net_domain     cl_ndom;
struct m0_dbenv          cl_dbenv;
struct m0_cob_domain     cl_cdom;
struct m0_rpc_client_ctx cl_ctx;

const char *cl_ep_addr;
const char *srv_ep_addr;
const char *dbname = "sr_cdb";
static int cl_cdom_id = 10001;

const struct m0_rpc_item_ops trigger_fop_rpc_item_ops;
extern struct m0_fop_type trigger_fop_fopt;

static void client_init(void)
{
	int rc;

	rc = m0_net_domain_init(&cl_ndom, &m0_net_lnet_xprt, &m0_addb_proc_ctx);
	M0_ASSERT(rc == 0);

	cl_ctx.rcx_net_dom            = &cl_ndom;
	cl_ctx.rcx_local_addr         = cl_ep_addr;
	cl_ctx.rcx_remote_addr        = srv_ep_addr;
	cl_ctx.rcx_db_name            = dbname;
	cl_ctx.rcx_dbenv              = &cl_dbenv;
	cl_ctx.rcx_cob_dom_id         = cl_cdom_id;
	cl_ctx.rcx_cob_dom            = &cl_cdom;
	cl_ctx.rcx_nr_slots           = MAX_RPC_SLOTS_NR;
	cl_ctx.rcx_timeout_s          = RPC_TIMEOUTS;
	cl_ctx.rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT;

	rc = m0_rpc_client_init(&cl_ctx);
	M0_ASSERT(rc == 0);
}

static void client_fini(void)
{
	int rc;

	rc = m0_rpc_client_fini(&cl_ctx);
	M0_ASSERT(rc == 0);

	m0_net_domain_fini(&cl_ndom);
}

int main(int argc, char *argv[])
{
	struct m0_fop      *fop;
	struct trigger_fop *treq;
	int32_t             n = 0;
	uint32_t            op;
	uint32_t            unit_size;
	uint64_t            fdata;
	uint64_t            fsize[MAX_FILES_NR];
	uint64_t            N = 0;
	uint64_t            K = 0;
	uint64_t            P = 0;
	m0_time_t           start;
	m0_time_t           delta;
	int                 file_cnt = 0;
	int                 rc;
	int                 i;

	rc = M0_GETOPTS("repair", argc, argv,
			M0_FORMATARG('O', "Operation, i.e. SNS_REPAIR = 2 or SNS_REBALANCE = 4", "%u", &op),
			M0_FORMATARG('U', "Unit size", "%u", &unit_size),
			M0_FORMATARG('F', "Failure device", "%lu", &fdata),
			M0_FORMATARG('n', "Number of files", "%d", &n),
			M0_NUMBERARG('s', "File size",
				     LAMBDA(void, (int64_t fsz)
				     {
					fsize[file_cnt] = fsz;
					file_cnt++;
				     })),
			M0_FORMATARG('N', "Number of data units", "%lu", &N),
			M0_FORMATARG('K', "Number of parity units", "%lu", &K),
			M0_FORMATARG('P', "Total pool width", "%lu", &P),
			M0_STRINGARG('C', "Client endpoint",
				     LAMBDA(void, (const char *str){
						cl_ep_addr = str;
				     })),
			M0_STRINGARG('S', "Server endpoint",
				     LAMBDA(void, (const char *str){
						srv_ep_addr = str;
				     })),
		       );
	if (rc != 0)
		return rc;

	M0_ASSERT(P >= N + 2 * K);
	rc = m0_init();
	M0_ASSERT(rc == 0);
	rc = m0_sns_repair_trigger_fop_init();
	M0_ASSERT(rc == 0);
	client_init();

	fop = m0_fop_alloc(&trigger_fop_fopt, NULL);
	treq = m0_fop_data(fop);
	treq->fdata = fdata;
	M0_ASSERT(n == file_cnt);
	M0_ALLOC_ARR(treq->fsize.f_size, file_cnt);
	M0_ASSERT(treq->fsize.f_size != NULL);
	for (i = 0; i < file_cnt; ++i)
		treq->fsize.f_size[i] = fsize[i];
	treq->fsize.f_nr = file_cnt;

	treq->N = N;
	treq->K = K;
	treq->P = P;
	treq->unit_size = unit_size;
	treq->op = op;
	start = m0_time_now();
	rc = m0_rpc_client_call(fop, &cl_ctx.rcx_session,
				&trigger_fop_rpc_item_ops,
				0 /* deadline */,
				M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	delta = m0_time_sub(m0_time_now(), start);
	printf("Time: %lu.%2.2lu sec\n", (unsigned long)m0_time_seconds(delta),
	       (unsigned long)m0_time_nanoseconds(delta) * 100 / M0_TIME_ONE_BILLION);

	m0_fop_put(fop);
	client_fini();
	m0_sns_repair_trigger_fop_fini();
	m0_fini();

	return rc;
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
