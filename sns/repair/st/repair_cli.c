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
#include "lib/misc.h"           /* C2_IN() */
#include "fop/fop.h"
#include "net/lnet/lnet.h"
#include "colibri/init.h"

#include "ut/rpc.h"
#include "rpc/rpc.h"
#include "rpc/rpclib.h"

#include "trigger_fop.h"
#include "trigger_fop_ff.h"

enum {
	MAX_RPCS_IN_FLIGHT = 10,
	MAX_RPC_SLOTS_NR   = 2,
	RPC_TIMEOUTS       = 5
};

struct c2_net_domain     cl_ndom;
struct c2_dbenv          cl_dbenv;
struct c2_cob_domain     cl_cdom;
struct c2_rpc_client_ctx cl_ctx;

const char *cl_ep_addr = "0@lo:12345:34:2";
char *srv_ep_addr;
const char *dbname = "sr_cdb";
static int cl_cdom_id = 10001;

const struct c2_rpc_item_ops trigger_fop_rpc_item_ops;
extern struct c2_fop_type trigger_fop_fopt;

static void client_init(void)
{
	int rc;

	rc = c2_net_domain_init(&cl_ndom, &c2_net_lnet_xprt);
	C2_ASSERT(rc == 0);

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

	rc = c2_rpc_client_init(&cl_ctx);
	C2_ASSERT(rc == 0);
}

static void client_fini(void)
{
	int rc;

	rc = c2_rpc_client_fini(&cl_ctx);
	C2_ASSERT(rc == 0);

	c2_net_domain_fini(&cl_ndom);
}

int main(int argc, char *argv[])
{
	struct c2_fop      *fop;
	struct trigger_fop *treq;
	uint64_t            fdata;
	uint64_t            fsize;
	uint64_t            N = 0;
	uint64_t            K = 0;
	uint64_t            P = 0;
	int                 rc;

	rc = C2_GETOPTS("repair", argc, argv,
			C2_FORMATARG('F', "Failure device", "%lu", &fdata),
			C2_FORMATARG('s', "File size", "%lu", &fsize),
			C2_FORMATARG('N', "Number of data units", "%lu", &N),
			C2_FORMATARG('K', "Number of parity units", "%lu", &K),
			C2_FORMATARG('P', "Total pool width", "%lu", &P),
			C2_STRINGARG('S', "Server endpoint",
				     LAMBDA(void, (const char *str){
						srv_ep_addr = (char*)str;
				     })),
		       );
	if (rc != 0)
		return rc;

	C2_ASSERT(P >= N + 2 * K);
	rc = c2_init();
	C2_ASSERT(rc == 0);
	rc = c2_sns_repair_trigger_fop_init();
	C2_ASSERT(rc == 0);
	client_init();

	fop = c2_fop_alloc(&trigger_fop_fopt, NULL);
	treq = c2_fop_data(fop);
	treq->fdata = fdata;
	treq->fsize = fsize;
	treq->N = N;
	treq->K = K;
	treq->P = P;
	rc = c2_rpc_client_call(fop, &cl_ctx.rcx_session,
				&trigger_fop_rpc_item_ops,
				0 /* deadline */,
				60 /* op timeout */);
	C2_ASSERT(rc == 0);

	client_fini();
	c2_sns_repair_trigger_fop_fini();
	c2_fini();

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
