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
#include "lib/misc.h"        /* M0_IN() */
#include "lib/memory.h"
#include "lib/time.h"

#include "fop/fop.h"
#include "mero/init.h"

#include "rpc/rpclib.h"
#include "rpc/rpc_opcodes.h"

#include "sns/cm/cm.h"
#include "sns/cm/trigger_fop.h"
#include "sns/cm/trigger_fop_xc.h"

#include "repair_cli.h"

struct m0_mutex                 repair_wait_mutex;
struct m0_chan                  repair_wait;
int32_t                         srv_cnt = 0;
int32_t                         srv_rep_cnt = 0;
enum {
	MAX_FAILURES_NR = 10,
};
extern struct m0_rpc_client_ctx cl_ctx;

extern const char *cl_ep_addr;
extern const char *srv_ep_addr[MAX_SERVERS];

extern struct m0_fop_type repair_trigger_fopt;
extern struct m0_fop_type rebalance_trigger_fopt;

M0_INTERNAL int m0_sns_cm_repair_trigger_fop_init(void);
M0_INTERNAL void m0_sns_cm_repair_trigger_fop_fini(void);

M0_INTERNAL int m0_sns_cm_rebalance_trigger_fop_init(void);
M0_INTERNAL void m0_sns_cm_rebalance_trigger_fop_fini(void);

static void trigger_rpc_item_reply_cb(struct m0_rpc_item *item)
{
	struct m0_fop *rep_fop;

	M0_PRE(item != NULL);

	if (item->ri_error == 0) {
		rep_fop = m0_rpc_item_to_fop(item->ri_reply);
		M0_ASSERT(M0_IN(m0_fop_opcode(rep_fop),
					(M0_SNS_REPAIR_TRIGGER_REP_OPCODE,
					 M0_SNS_REBALANCE_TRIGGER_REP_OPCODE)));
	}
	M0_CNT_INC(srv_rep_cnt);
	if (srv_rep_cnt == srv_cnt) {
		m0_mutex_lock(&repair_wait_mutex);
		m0_chan_signal(&repair_wait);
		m0_mutex_unlock(&repair_wait_mutex);
	}
}

const struct m0_rpc_item_ops trigger_fop_rpc_item_ops = {
	.rio_replied = trigger_rpc_item_reply_cb,
};


int main(int argc, char *argv[])
{
	struct trigger_fop    *treq;
	struct rpc_ctx        *ctxs;
	struct m0_rpc_session *session;
	struct m0_clink        repair_clink;
	uint32_t               op;
	uint64_t               fail_idx[MAX_FAILURES_NR];
	m0_time_t              start;
	m0_time_t              delta;
	int                    fail_cnt = 0;
	int32_t                n = 0;
	int                    rc;
	int                    i;
	int                    j;

	rc = M0_GETOPTS("repair", argc, argv,
			M0_FORMATARG('O',
				     "Operation, i.e. SNS_REPAIR = 2 or SNS_REBALANCE = 4",
				     "%u", &op),
			M0_FORMATARG('N', "Number of failed devices", "%d", &n),
			M0_NUMBERARG('F', "Failure devices",
				LAMBDA(void, (int64_t fdv)
					{
					fail_idx[fail_cnt] = fdv;
					fail_cnt++;
					})),
			M0_STRINGARG('C', "Client endpoint",
				LAMBDA(void, (const char *str){
						cl_ep_addr = str;
					})),
			M0_STRINGARG('S', "Server endpoint",
				LAMBDA(void, (const char *str){
					srv_ep_addr[srv_cnt] = str;
					++srv_cnt;
					})),
			);
	if (rc != 0)
		return rc;

	rc = m0_init();
	M0_ASSERT(rc == 0);
	rc = m0_sns_cm_repair_trigger_fop_init();
	M0_ASSERT(rc == 0);
	rc = m0_sns_cm_rebalance_trigger_fop_init();
	M0_ASSERT(rc == 0);
	repair_client_init();

	m0_mutex_init(&repair_wait_mutex);
	m0_chan_init(&repair_wait, &repair_wait_mutex);
	m0_clink_init(&repair_clink, NULL);
	M0_ALLOC_ARR(ctxs, srv_cnt);
	M0_ASSERT(ctxs != NULL);
	for (i = 0; i < srv_cnt; ++i) {
		rc = repair_rpc_ctx_init(&ctxs[i], srv_ep_addr[i]);
		M0_ASSERT(rc == 0);
	}
	m0_mutex_lock(&repair_wait_mutex);
	m0_clink_add(&repair_wait, &repair_clink);
	m0_mutex_unlock(&repair_wait_mutex);
	start = m0_time_now();
	for (i = 0; i < srv_cnt; ++i) {
		struct m0_fop *fop = NULL;
		if (op == SNS_REPAIR)
			fop = m0_fop_alloc(&repair_trigger_fopt, NULL);
		else if (op == SNS_REBALANCE)
			fop = m0_fop_alloc(&rebalance_trigger_fopt, NULL);
		treq = m0_fop_data(fop);
		treq->fdata.fd_nr = n;
		M0_ALLOC_ARR(treq->fdata.fd_index, n);
		M0_ASSERT(treq->fdata.fd_index != NULL);

		for (j = 0; j < n; ++j)
			treq->fdata.fd_index[j] = fail_idx[j];
		treq->op = op;
		session = &ctxs[i].ctx_session;
		rc = repair_rpc_post(fop, session,
				&trigger_fop_rpc_item_ops,
				0 /* deadline */);
		M0_ASSERT(rc == 0);
		m0_fop_put(fop);
	}
	m0_chan_wait(&repair_clink);
	delta = m0_time_sub(m0_time_now(), start);
	printf("Time: %lu.%2.2lu sec\n", (unsigned long)m0_time_seconds(delta),
			(unsigned long)m0_time_nanoseconds(delta) * 100 /
			M0_TIME_ONE_SECOND);
	for (i = 0; i < srv_cnt; ++i)
		repair_rpc_ctx_fini(&ctxs[i]);
	repair_client_fini();
	m0_sns_cm_repair_trigger_fop_fini();
	m0_sns_cm_rebalance_trigger_fop_fini();
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
