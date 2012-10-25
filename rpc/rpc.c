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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 *		    Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 04/28/2011
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"     /* C2_IN */
#include "lib/types.h"

#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"

/**
 * @addtogroup rpc
 * @{
 */

C2_INTERNAL int c2_rpc__post_locked(struct c2_rpc_item *item);

const struct c2_addb_ctx_type c2_rpc_addb_ctx_type = {
	.act_name = "rpc"
};

const struct c2_addb_loc c2_rpc_addb_loc = {
	.al_name = "rpc"
};

struct c2_addb_ctx c2_rpc_addb_ctx;


C2_INTERNAL int c2_rpc_init(void)
{
	int rc;

	C2_ENTRY();

	c2_addb_ctx_init(&c2_rpc_addb_ctx, &c2_rpc_addb_ctx_type,
			 &c2_addb_global_ctx);
	rc = c2_rpc_item_type_list_init() ?:
	     c2_rpc_service_module_init() ?:
	     c2_rpc_session_module_init();

	C2_RETURN(rc);
}

C2_INTERNAL void c2_rpc_fini(void)
{
	C2_ENTRY();

	c2_rpc_session_module_fini();
	c2_rpc_service_module_fini();
	c2_rpc_item_type_list_fini();
	c2_addb_ctx_fini(&c2_rpc_addb_ctx);

	C2_LEAVE();
}

C2_INTERNAL int c2_rpc_post(struct c2_rpc_item *item)
{
	struct c2_rpc_machine *machine = item_machine(item);
	int                    rc;
	uint64_t	       item_size;

	C2_ENTRY("item: %p", item);
	C2_PRE(item->ri_session != NULL);

	item_size = c2_rpc_item_size(item);

	c2_rpc_machine_lock(machine);
	C2_ASSERT(item_size <= machine->rm_min_recv_size);
	rc = c2_rpc__post_locked(item);
	c2_rpc_machine_unlock(machine);

	C2_RETURN(rc);
}
C2_EXPORTED(c2_rpc_post);

C2_INTERNAL void c2_rpc_item_get(struct c2_rpc_item *item)
{
	/* XXX TODO */
}
C2_EXPORTED(c2_rpc_item_get);

C2_INTERNAL void c2_rpc_item_put(struct c2_rpc_item *item)
{
	/* XXX TODO */
}
C2_EXPORTED(c2_rpc_item_put);

C2_INTERNAL int c2_rpc__post_locked(struct c2_rpc_item *item)
{
	struct c2_rpc_session *session;

	C2_ENTRY("item: %p", item);
	C2_PRE(item != NULL && item->ri_type != NULL);
	/* XXX Temporary assertion, until bound item posting is supported */
	C2_PRE(c2_rpc_item_is_request(item) && !c2_rpc_item_is_bound(item));
	/*
	 * It is mandatory to specify item_ops, because rpc layer needs
	 * implementation of c2_rpc_item_ops::rio_free() in order to free the
	 * item. Consumer can use c2_fop_default_item_ops if, it is not
	 * interested in implementing other (excluding ->rio_free())
	 * interfaces of c2_rpc_item_ops. See also c2_fop_item_free().
	 */
	C2_ASSERT(item->ri_ops != NULL && item->ri_ops->rio_free != NULL);

	session = item->ri_session;
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(C2_IN(session_state(session), (C2_RPC_SESSION_IDLE,
						 C2_RPC_SESSION_BUSY)));
	C2_ASSERT(c2_rpc_item_size(item) <=
			c2_rpc_session_get_max_item_size(session));
	C2_ASSERT(c2_rpc_machine_is_locked(session_machine(session)));
	/*
	 * This hold will be released when the item is SENT or FAILED.
	 * See rpc/frmops.c:item_done()
	 */
	c2_rpc_session_hold_busy(session);

	item->ri_rpc_time = c2_time_now();
	item->ri_stage = RPC_ITEM_STAGE_FUTURE;
	c2_rpc_item_sm_init(item, &session_machine(session)->rm_sm_grp,
			    C2_RPC_ITEM_OUTGOING);
	c2_rpc_frm_enq_item(session_frm(session), item);
	C2_RETURN(0);
}

C2_INTERNAL int c2_rpc_reply_post(struct c2_rpc_item *request,
				  struct c2_rpc_item *reply)
{
	struct c2_rpc_slot_ref *sref;
	struct c2_rpc_machine  *machine;
	struct c2_rpc_slot     *slot;

	C2_ENTRY("req_item: %p, rep_item: %p", request, reply);
	C2_PRE(request != NULL && reply != NULL);
	C2_PRE(request->ri_stage == RPC_ITEM_STAGE_IN_PROGRESS);
	C2_PRE(request->ri_session != NULL);
	C2_PRE(reply->ri_type != NULL);
	C2_PRE(reply->ri_ops != NULL && reply->ri_ops->rio_free != NULL);
	C2_PRE(c2_rpc_item_size(reply) <=
			c2_rpc_session_get_max_item_size(request->ri_session));
	reply->ri_rpc_time = c2_time_now();
	reply->ri_session  = request->ri_session;

	/* BEWARE: structure instance copy ahead */
	reply->ri_slot_refs[0] = request->ri_slot_refs[0];
	sref = &reply->ri_slot_refs[0];
	/* don't need values of sr_link and sr_ready_link of request item */
	slot_item_tlink_init(reply);

	sref->sr_item = reply;

	reply->ri_prio     = request->ri_prio;
	reply->ri_deadline = 0;
	reply->ri_error    = 0;

	slot = sref->sr_slot;
	machine = session_machine(slot->sl_session);

	c2_rpc_machine_lock(machine);
	c2_rpc_item_sm_init(reply, &machine->rm_sm_grp, C2_RPC_ITEM_OUTGOING);
	/*
	 * This hold will be released when the item is SENT or FAILED.
	 * See rpc/frmops.c:item_done()
	 */
	c2_rpc_session_hold_busy(reply->ri_session);
	__slot_reply_received(slot, request, reply);
	c2_rpc_machine_unlock(machine);
	C2_RETURN(0);
}
C2_EXPORTED(c2_rpc_reply_post);

C2_INTERNAL int c2_rpc_oneway_item_post(const struct c2_rpc_conn *conn,
					struct c2_rpc_item *item)
{
	struct c2_rpc_machine *machine;

	C2_ENTRY("conn: %p, item: %p", conn, item);
	C2_PRE(conn != NULL);
	C2_PRE(item != NULL && c2_rpc_item_is_oneway(item));

	item->ri_rpc_time = c2_time_now();

	machine = conn->c_rpc_machine;
	c2_rpc_machine_lock(machine);
	c2_rpc_item_sm_init(item, &machine->rm_sm_grp, C2_RPC_ITEM_OUTGOING);
	c2_rpc_frm_enq_item(&conn->c_rpcchan->rc_frm, item);
	c2_rpc_machine_unlock(machine);
	C2_RETURN(0);
}

C2_INTERNAL int c2_rpc_reply_timedwait(struct c2_clink *clink,
				       const c2_time_t timeout)
{
	int rc;
	C2_ENTRY("timeout: [%llu:%llu]",
		 (unsigned long long)c2_time_seconds(timeout),
		 (unsigned long long) c2_time_nanoseconds(timeout));
	C2_PRE(clink != NULL);
	C2_PRE(c2_clink_is_armed(clink));

	rc = c2_chan_timedwait(clink, timeout) ? 0 : -ETIMEDOUT;
	C2_RETURN(rc);
}
C2_EXPORTED(c2_rpc_reply_timedwait);


static void rpc_buffer_pool_low(struct c2_net_buffer_pool *bp)
{
	/* Buffer pool is below threshold.  */
}

static const struct c2_net_buffer_pool_ops b_ops = {
	.nbpo_not_empty	      = c2_net_domain_buffer_pool_not_empty,
	.nbpo_below_threshold = rpc_buffer_pool_low,
};

C2_INTERNAL int c2_rpc_net_buffer_pool_setup(struct c2_net_domain *ndom,
					     struct c2_net_buffer_pool
					     *app_pool, uint32_t bufs_nr,
					     uint32_t tm_nr)
{
	int	    rc;
	uint32_t    segs_nr;
	c2_bcount_t seg_size;

	C2_ENTRY("net_dom: %p", ndom);
	C2_PRE(ndom != NULL);
	C2_PRE(app_pool != NULL);
	C2_PRE(bufs_nr != 0);

	seg_size = c2_rpc_max_seg_size(ndom);
	segs_nr  = c2_rpc_max_segs_nr(ndom);
	app_pool->nbp_ops = &b_ops;
	rc = c2_net_buffer_pool_init(app_pool, ndom,
				     C2_NET_BUFFER_POOL_THRESHOLD,
				     segs_nr, seg_size, tm_nr, C2_SEG_SHIFT);
	if (rc != 0)
		C2_RETERR(rc, "net_buf_pool: Initialization");

	c2_net_buffer_pool_lock(app_pool);
	rc = c2_net_buffer_pool_provision(app_pool, bufs_nr);
	c2_net_buffer_pool_unlock(app_pool);
	rc = rc != bufs_nr ? -ENOMEM : 0;
	C2_RETURN(rc);
}
C2_EXPORTED(c2_rpc_net_buffer_pool_setup);

C2_INTERNAL void c2_rpc_net_buffer_pool_cleanup(struct c2_net_buffer_pool
						*app_pool)
{
	C2_PRE(app_pool != NULL);

	c2_net_buffer_pool_fini(app_pool);
}
C2_EXPORTED(c2_rpc_net_buffer_pool_cleanup);

/** @} */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
