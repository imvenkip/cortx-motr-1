/* -*- C -*- */
/*
 * COPYRIGHT 2016 SEAGATE LLC
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE LLC,
 * ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original authors: Juan   Gonzalez <juan.gonzalez@seagate.com>
 *                   James  Morse    <james.s.morse@seagate.com>
 *                   Sining Wu       <sining.wu@seagate.com>
 * Revision:         Pratik Shinde   <pratik.shinde@seagate.com>
 * Original creation date: 3-Nov-2014
 *
 * Original 'm0t1fs' author: Anand Vidwansa <anand_vidwansa@xyratex.com>
 */

#include "clovis/clovis.h"
#include "clovis/clovis_internal.h"
#include "clovis/clovis_addb.h"
#include "clovis/pg.h"
#include "clovis/io.h"

#include "lib/memory.h"          /* m0_alloc, m0_free */
#include "lib/errno.h"           /* ENOMEM */
#include "lib/atomic.h"          /* m0_atomic_{inc,dec,get} */
#include "rpc/rpc_machine_internal.h"	/* m0_rpc_machine_lock */
#include "ioservice/io_device.h" /* M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH */
#include "fop/fom_generic.h"     /* m0_rpc_item_generic_reply_rc */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CLOVIS
#include "lib/trace.h"           /* M0_LOG */

#define CLOVIS_OSYNC             /* [Experimental] Clovis Object Sync */
#ifdef  CLOVIS_OSYNC
#include "clovis/osync.h"
#endif

/*
 * No initialisation for iofop_bobtype as it isn't const,
 * iofop_bobtype is initialised as a list type.
 */
struct m0_bob_type iofop_bobtype;
M0_BOB_DEFINE(M0_INTERNAL, &iofop_bobtype, ioreq_fop);

/**
 * Definition for a list of io fops, used to group fops that
 * belong to the same clovis:operation
 */
M0_TL_DESCR_DEFINE(iofops, "List of IO fops", M0_INTERNAL,
		   struct ioreq_fop, irf_link, irf_magic,
		   M0_CLOVIS_IOFOP_MAGIC, M0_CLOVIS_TIOREQ_MAGIC);
M0_TL_DEFINE(iofops,  M0_INTERNAL, struct ioreq_fop);

/**
 * This is heavily based on m0t1fs/linux_kernel/file.c::io_req_fop_invariant
 */
M0_INTERNAL bool ioreq_fop_invariant(const struct ioreq_fop *fop)
{
	return M0_RC(fop != NULL &&
	             _0C(ioreq_fop_bob_check(fop)) &&
	             _0C(fop->irf_tioreq      != NULL) &&
	             _0C(fop->irf_ast.sa_cb   != NULL) &&
	             _0C(fop->irf_ast.sa_mach != NULL));
}

/**
 * Callback to re-synchronise the pool machine, called when a server responds
 * with M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH.
 * This is heavily based on m0t1fs/linux_kernel/file.c::failure_vector_mismatch
 *
 * @param irfop The failed fop - used to extract the 'current' failure vector
 *              version number.
 */
static inline void failure_vector_mismatch(struct ioreq_fop *irfop)
{
	uint32_t                     i = 0;
	int                          c_count;
	struct m0_fop               *reply;
	struct m0_rpc_item          *reply_item;
	struct m0_fop_cob_rw_reply  *rw_reply;
	struct m0_fv_version        *reply_version;
	struct m0_fv_updates        *reply_updates;
	struct m0_clovis_op_io      *ioo;
	struct m0_poolmach_versions *cli;
	struct m0_poolmach_versions *srv;
	struct m0_poolmach_event    *event;
	struct m0_poolmach          *pm;

	M0_ENTRY();

	M0_PRE(irfop != NULL);

	ioo = bob_of(irfop->irf_tioreq->ti_nwxfer, struct m0_clovis_op_io,
		     ioo_nwxfer, &ioo_bobtype);
	pm = clovis_ioo_to_poolmach(ioo);
	M0_ASSERT(pm != NULL);

	reply_item    = irfop->irf_iofop.if_fop.f_item.ri_reply;
	reply         = m0_rpc_item_to_fop(reply_item);
	rw_reply      = io_rw_rep_get(reply);
	reply_version = &rw_reply->rwr_fv_version;
	reply_updates = &rw_reply->rwr_fv_updates;
	srv           = (struct m0_poolmach_versions *)reply_version;
	cli = &pm->pm_state->pst_version;

	M0_LOG(M0_DEBUG, ">>>VERSION MISMATCH!");
	m0_poolmach_version_dump(cli);
	m0_poolmach_version_dump(srv);

	/*
	 * Retrieve the latest server version and
	 * updates and apply to the client's copy.
	 * When -EAGAIN is return, this system
	 * call will be restarted.
	 */
	while (i < reply_updates->fvu_count) {
		c_count = pm->pm_state->pst_version.pvn_version[PVE_READ];
		if (c_count == reply_version->fvv_read)
			break;

		event = (struct m0_poolmach_event*)&reply_updates->fvu_events[i];
		m0_poolmach_event_dump(event);
		m0_poolmach_state_transit(pm, event, NULL);
		i++;
	}

	M0_LOG(M0_DEBUG, "<<<VERSION MISMATCH!");

	M0_LEAVE();
}

/**
 * AST-Callback for the rpc layer when it receives a reply fop.
 * This is heavily based on m0t1fs/linux_kernel/file.c::io_bottom_half
 *
 * @param grp The state-machine-group/locality that is processing this callback.
 * @param ast The AST that triggered this callback, used to find the
 *            IO operation.
 */
static void clovis_io_bottom_half(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	int                          rc;
	uint64_t                     actual_bytes = 0;
	struct m0_clovis            *instance;
	struct m0_clovis_op         *op;
	struct m0_clovis_op_io      *ioo;
	struct nw_xfer_request      *xfer;
	struct m0_io_fop            *iofop;
	struct ioreq_fop            *irfop;
	struct target_ioreq         *tioreq;
	struct m0_fop               *reply_fop = NULL;
	struct m0_rpc_item          *req_item;
	struct m0_rpc_item          *reply_item;
	struct m0_rpc_bulk	    *rbulk;
	struct m0_fop_cob_rw_reply  *rw_reply;
	struct m0_fop_generic_reply *gen_rep;

	M0_ENTRY("sm_group %p sm_ast %p", grp, ast);

	M0_PRE(grp != NULL);
	M0_PRE(ast != NULL);

	irfop  = bob_of(ast, struct ioreq_fop, irf_ast, &iofop_bobtype);
	iofop  = &irfop->irf_iofop;
	tioreq = irfop->irf_tioreq;
	xfer   = tioreq->ti_nwxfer;

	ioo    = bob_of(xfer, struct m0_clovis_op_io, ioo_nwxfer, &ioo_bobtype);
	op     = &ioo->ioo_oo.oo_oc.oc_op;
	instance = m0_clovis__op_instance(op);
	M0_PRE(instance != NULL);
	M0_PRE(M0_IN(irfop->irf_pattr, (PA_DATA, PA_PARITY)));
	M0_PRE(M0_IN(ioreq_sm_state(ioo),
		     (IRS_READING, IRS_WRITING,
		      IRS_DEGRADED_READING, IRS_DEGRADED_WRITING,
		      IRS_FAILED)));

	/* Check errors in rpc items of an IO reqest and its reply. */
	rbulk      = &iofop->if_rbulk;
	req_item   = &iofop->if_fop.f_item;
	reply_item = req_item->ri_reply;
	rc         = req_item->ri_error;
	if (reply_item != NULL) {
		reply_fop = m0_rpc_item_to_fop(reply_item);
		rc = rc?: m0_rpc_item_generic_reply_rc(reply_item);
	}
	if (rc < 0 || reply_item == NULL) {
		M0_ASSERT(ergo(reply_item == NULL, rc != 0));
		M0_LOG(M0_ERROR, "[%p] rpc item %p rc=%d", ioo, req_item, rc);
		goto ref_dec;
	}
	M0_ASSERT(!m0_rpc_item_is_generic_reply_fop(reply_item));
	M0_ASSERT(m0_is_io_fop_rep(reply_fop));

	/* Check errors in an IO request's reply. */
	gen_rep = m0_fop_data(m0_rpc_item_to_fop(reply_item));
	rw_reply = io_rw_rep_get(reply_fop);
	ioo->ioo_sns_state = rw_reply->rwr_repair_done;
	M0_LOG(M0_DEBUG, "[%p] item %p[%u], reply received = %d, "
			 "sns state = %d", ioo, req_item,
			 req_item->ri_type->rit_opcode, rc, ioo->ioo_sns_state);

	rc = gen_rep->gr_rc;
	rc = rc ?: rw_reply->rwr_rc;

	/*
	 * ##TODO: A cleaner approach to ignore
	 *         M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH error
	 *         will be handeled as part of MERO-1502.
	 *
	 * if (rc == M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH) {
	 *	M0_ASSERT(rw_reply != NULL);
	 *	M0_LOG(M0_FATAL, "[%p] item %p, VERSION_MISMATCH received on "
	 *	FID_F, req, req_item, FID_P(&tioreq->ti_fid));
	 *	failure_vector_mismatch(irfop);
	 * }
	 */
	irfop->irf_reply_rc = rc;


#ifdef CLOVIS_OSYNC
	/* Update pending transaction number */
	clovis_osync_record_update(
		m0_reqh_service_ctx_from_session(reply_item->ri_session),
		instance, ioo->ioo_obj, &rw_reply->rwr_mod_rep.fmr_remid);
#endif

ref_dec:
	/* For whatever reason, io didn't complete successfully.
	 * Reduce expected read bulk count */
	if (rc < 0 && m0_is_read_fop(&iofop->if_fop))
		m0_atomic64_sub(&xfer->nxr_rdbulk_nr,
				m0_rpc_bulk_buf_length(rbulk));

	/* Propogate the error up as many stashed-rc layers as we can */
	if (tioreq->ti_rc == 0)
		tioreq->ti_rc = rc;

	/*
	 * Note: this is not necessary mean that this is 'real' error in the
	 * case of CROW is used (object is only created when it is first
	 * write)
	 */
	if (xfer->nxr_rc == 0 && rc != 0) {
		xfer->nxr_rc = rc;
		M0_LOG(M0_DEBUG, "[%p] rc %d, tioreq->ti_rc %d, "
				 "nwxfer rc = %d @"FID_F,
				 ioo, rc, tioreq->ti_rc,
				 xfer->nxr_rc, FID_P(&tioreq->ti_fid));
	}

	/*
	 * Sining: don't set the ioo_rc utill replies come back from  dgmode
	 * IO.
	 */
	if (ioo->ioo_rc == 0 && ioo->ioo_dgmode_io_sent == true)
		ioo->ioo_rc = rc;

	if (irfop->irf_pattr == PA_DATA)
		tioreq->ti_databytes += rbulk->rb_bytes;
	else
		tioreq->ti_parbytes += rbulk->rb_bytes;

	M0_LOG(M0_INFO, "[%p] fop %p, Returned no of bytes = %llu, "
	       "expected = %llu",
	       ioo, &iofop->if_fop, (unsigned long long)actual_bytes,
	       (unsigned long long)rbulk->rb_bytes);

	/* Drops reference on reply fop. */
	m0_fop_put0_lock(&iofop->if_fop);
	m0_fop_put0_lock(reply_fop);
	m0_atomic64_dec(&instance->m0c_pending_io_nr);

	m0_mutex_lock(&xfer->nxr_lock);
	m0_atomic64_dec(&xfer->nxr_iofop_nr);
	if (m0_atomic64_get(&xfer->nxr_iofop_nr) == 0 &&
	    m0_atomic64_get(&xfer->nxr_rdbulk_nr) == 0) {
		m0_sm_state_set(&ioo->ioo_sm,
				(M0_IN(ioreq_sm_state(ioo),
				       (IRS_READING, IRS_DEGRADED_READING)) ?
					IRS_READ_COMPLETE:IRS_WRITE_COMPLETE));

		/* post an ast to run iosm_handle_executed */
		ioo->ioo_ast.sa_cb = ioo->ioo_ops->iro_iosm_handle_executed;
		m0_sm_ast_post(ioo->ioo_oo.oo_sm_grp, &ioo->ioo_ast);
	}
	m0_mutex_unlock(&xfer->nxr_lock);

	M0_LOG(M0_DEBUG, "[%p] irfop=%p bulk=%p "FID_F
	       " Pending fops = %"PRIu64" bulk=%"PRIu64,
	       ioo, irfop, rbulk, FID_P(&tioreq->ti_fid),
	       m0_atomic64_get(&xfer->nxr_iofop_nr),
	       m0_atomic64_get(&xfer->nxr_rdbulk_nr));

	M0_LEAVE();
}

/**
 * Callback for the rpc layer when it receives a reply fop. This schedules
 * io_bottom_half.
 * This is heavily based on m0t1fs/linux_kernel/file.c::io_rpc_item_cb
 *
 * @param item The rpc item for which a reply was received.
 */
static void io_rpc_item_cb(struct m0_rpc_item *item)
{
	struct m0_fop          *fop;
	struct m0_fop          *rep_fop;
	struct m0_io_fop       *iofop;
	struct ioreq_fop       *reqfop;
	struct m0_clovis_op_io *ioo;

	M0_PRE(item != NULL);
	M0_ENTRY("rpc_item %p", item);

	fop    = m0_rpc_item_to_fop(item);
	iofop  = container_of(fop, struct m0_io_fop, if_fop);
	reqfop = bob_of(iofop, struct ioreq_fop, irf_iofop, &iofop_bobtype);
	ioo    = bob_of(reqfop->irf_tioreq->ti_nwxfer, struct m0_clovis_op_io,
			ioo_nwxfer, &ioo_bobtype);
	/*
	 * NOTE: RPC errors are handled in io_bottom_half(), which is called
	 * by reqfop->irf_ast.
	 */

	/*
	 * Acquires a reference on IO reply fop since its contents
	 * are needed for policy decisions in io_bottom_half().
	 * io_bottom_half() takes care of releasing the reference.
	 */
	if (item->ri_reply != NULL) {
		rep_fop = m0_rpc_item_to_fop(item->ri_reply);
		m0_fop_get(rep_fop);
	}
	M0_LOG(M0_INFO, "ioreq_fop %p, target_ioreq %p io_request %p",
	       reqfop, reqfop->irf_tioreq, ioo);

	m0_fop_get(&reqfop->irf_iofop.if_fop);
	m0_sm_ast_post(ioo->ioo_sm.sm_grp, &reqfop->irf_ast);

	M0_LEAVE();
}

/*
 * io_rpc_item_cb can not be directly invoked from io fops code since it
 * leads to build dependency of ioservice code over kernel code (kernel clovis).
 * Hence, a new m0_rpc_item_ops structure is used for fops dispatched
 * by clovis io requests in all cases.
 */
static const struct m0_rpc_item_ops clovis_item_ops = {
	.rio_replied = io_rpc_item_cb,
};

/**
 * Callback for the rpc layer when it receives a completed bulk transfer.
 * This is heavily based on m0t1fs/linux_kernel/file.c::client_passive_recv
 *
 * @param evt A network event summary from the rpc layer.
 */
static void client_passive_recv(const struct m0_net_buffer_event *evt)
{
	struct m0_rpc_bulk     *rbulk;
	struct m0_rpc_bulk_buf *buf;
	struct m0_net_buffer   *nb;
	struct m0_io_fop       *iofop;
	struct ioreq_fop       *reqfop;
	struct m0_clovis_op_io *ioo;
	uint32_t                req_sm_state;

	M0_ENTRY();

	M0_PRE(evt != NULL);
	M0_PRE(evt->nbe_buffer != NULL);

	nb = evt->nbe_buffer;
	buf = (struct m0_rpc_bulk_buf *)nb->nb_app_private;
	rbulk = buf->bb_rbulk;
	M0_LOG(M0_DEBUG, "PASSIVE recv, e=%p status=%d, len=%"PRIu64" rbulk=%p",
	       evt, evt->nbe_status, evt->nbe_length, rbulk);

	iofop  = container_of(rbulk, struct m0_io_fop, if_rbulk);
	reqfop = bob_of(iofop, struct ioreq_fop, irf_iofop, &iofop_bobtype);
	ioo    = bob_of(reqfop->irf_tioreq->ti_nwxfer, struct m0_clovis_op_io,
			ioo_nwxfer, &ioo_bobtype);

	M0_ASSERT(m0_is_read_fop(&iofop->if_fop));
	M0_LOG(M0_DEBUG,
	       "irfop=%p "FID_F" Pending fops = %"PRIu64"bulk = %"PRIu64,
	       reqfop, FID_P(&reqfop->irf_tioreq->ti_fid),
	       m0_atomic64_get(&ioo->ioo_nwxfer.nxr_iofop_nr),
	       m0_atomic64_get(&ioo->ioo_nwxfer.nxr_rdbulk_nr) - 1);

	/*
	 * buf will be released in this callback. But rbulk is still valid
	 * after that.
	 */
	m0_rpc_bulk_default_cb(evt);
	if (evt->nbe_status != 0)
		return;

	/* Set io request's state*/
	m0_mutex_lock(&ioo->ioo_nwxfer.nxr_lock);

	req_sm_state = ioreq_sm_state(ioo);
	if (req_sm_state != IRS_READ_COMPLETE &&
	    req_sm_state != IRS_WRITE_COMPLETE) {
		/*
		 * It is possible that io_bottom_half() has already
		 * reduced the nxr_rdbulk_nr to 0 by this time, due to FOP
		 * receiving some error.
		 */

		if (m0_atomic64_get(&ioo->ioo_nwxfer.nxr_rdbulk_nr) > 0)
			m0_atomic64_dec(&ioo->ioo_nwxfer.nxr_rdbulk_nr);
		if (m0_atomic64_get(&ioo->ioo_nwxfer.nxr_iofop_nr) == 0 &&
		    m0_atomic64_get(&ioo->ioo_nwxfer.nxr_rdbulk_nr) == 0) {
			m0_sm_state_set(&ioo->ioo_sm,
				        (M0_IN(ioreq_sm_state(ioo),
					       (IRS_READING,
						IRS_DEGRADED_READING)) ?
				        IRS_READ_COMPLETE :
				        IRS_WRITE_COMPLETE));

			/* post an ast to run iosm_handle_executed */
			ioo->ioo_ast.sa_cb =
				ioo->ioo_ops->iro_iosm_handle_executed;
			m0_sm_ast_post(ioo->ioo_oo.oo_sm_grp, &ioo->ioo_ast);
		}
	}
	m0_mutex_unlock(&ioo->ioo_nwxfer.nxr_lock);

	M0_LEAVE();
}

/** Callbacks for the RPC layer to inform clovis of events */
const struct m0_net_buffer_callbacks clovis_client_buf_bulk_cb  = {
	.nbc_cb = {
		[M0_NET_QT_PASSIVE_BULK_SEND] = m0_rpc_bulk_default_cb,
		[M0_NET_QT_PASSIVE_BULK_RECV] = client_passive_recv,
		[M0_NET_QT_ACTIVE_BULK_RECV]  = m0_rpc_bulk_default_cb,
		[M0_NET_QT_ACTIVE_BULK_SEND]  = m0_rpc_bulk_default_cb
	}
};

/**
 * This is heavily based on m0t1fs/linux_kernel/file.c::iofop_async_submit
 */
M0_INTERNAL int ioreq_fop_async_submit(struct m0_io_fop      *iofop,
				       struct m0_rpc_session *session)
{
	int                   rc;
	struct m0_fop_cob_rw *rwfop;

	M0_ENTRY("m0_io_fop %p m0_rpc_session %p", iofop, session);

	M0_PRE(iofop != NULL);
	M0_PRE(session != NULL);

	rwfop = io_rw_get(&iofop->if_fop);
	M0_ASSERT(rwfop != NULL);

	rc = m0_rpc_bulk_store(&iofop->if_rbulk, session->s_conn,
			       rwfop->crw_desc.id_descs,
			       &clovis_client_buf_bulk_cb);
	if (rc != 0)
		goto out;

	iofop->if_fop.f_item.ri_session = session;
	iofop->if_fop.f_item.ri_nr_sent_max = CLOVIS_RPC_MAX_RETRIES;
	iofop->if_fop.f_item.ri_resend_interval = CLOVIS_RPC_RESEND_INTERVAL;
	rc = m0_rpc_post(&iofop->if_fop.f_item);
	M0_LOG(M0_INFO, "IO fops submitted to rpc, rc = %d", rc);

	/*
	 * Ignoring error from m0_rpc_post() so that the subsequent fop
	 * submission goes on. This is to ensure that the ioreq gets into dgmode
	 * subsequently without exiting from the healthy mode IO itself.
	 */
	return M0_RC(0);

out:
	/*
	 * In case error is encountered either by m0_rpc_bulk_store() or
	 * queued net buffers, if any, will be deleted at io_req_fop_release.
	 */
	return M0_RC(rc);
}

/* Finds out pargrp_iomap from array of such structures in m0_clovis_op_ioo. */
static void ioreq_pgiomap_find(struct m0_clovis_op_io *ioo,
			       uint64_t                grpid,
			       uint64_t               *cursor,
			       struct pargrp_iomap   **out)
{
	uint64_t id;

	M0_PRE(ioo    != NULL);
	M0_PRE(out    != NULL);
	M0_PRE(cursor != NULL);
	M0_PRE(*cursor < ioo->ioo_iomap_nr);
	M0_ENTRY("group_id = %3"PRIu64", cursor = %3"PRIu64, grpid, *cursor);

	for (id = *cursor; id < ioo->ioo_iomap_nr; ++id)
		if (ioo->ioo_iomaps[id]->pi_grpid == grpid) {
			*out = ioo->ioo_iomaps[id];
			*cursor = id;
			break;
		}

	M0_POST(id < ioo->ioo_iomap_nr);
	M0_LEAVE();
}

/**
 * This is heavily based on m0t1fs/linux_kernel/file.c::io_req_fop_dgmode_read.
 */
M0_INTERNAL int ioreq_fop_dgmode_read(struct ioreq_fop *irfop)
{
	int                         rc;
	uint32_t                    cnt;
	uint32_t                    seg;
	uint32_t                    seg_nr;
	uint64_t                    grpid;
	uint64_t                    pgcur = 0;
	m0_bindex_t                *index;
	struct m0_clovis_op_io     *ioo;
	struct m0_rpc_bulk         *rbulk;
	struct pargrp_iomap        *map = NULL;
	struct m0_rpc_bulk_buf     *rbuf;

	M0_PRE(irfop != NULL);
	M0_ENTRY("target fid = "FID_F, FID_P(&irfop->irf_tioreq->ti_fid));

	ioo    = bob_of(irfop->irf_tioreq->ti_nwxfer, struct m0_clovis_op_io,
			ioo_nwxfer, &ioo_bobtype);
	rbulk     = &irfop->irf_iofop.if_rbulk;

	m0_tl_for (rpcbulk, &rbulk->rb_buflist, rbuf) {

		index  = rbuf->bb_zerovec.z_index;
		seg_nr = rbuf->bb_zerovec.z_bvec.ov_vec.v_nr;

		for (seg = 0; seg < seg_nr; ) {

			grpid = pargrp_id_find(index[seg], ioo, irfop);
			for (cnt = 1, ++seg; seg < seg_nr; ++seg) {

				M0_ASSERT(ergo(seg > 0, index[seg] >
					       index[seg - 1]));
				M0_ASSERT(addr_is_network_aligned(
						(void *)index[seg]));

				if (grpid ==
				    pargrp_id_find(index[seg], ioo, irfop))
					++cnt;
				else
					break;
			}

			ioreq_pgiomap_find(ioo, grpid, &pgcur, &map);
			M0_ASSERT(map != NULL);
			rc = map->pi_ops->pi_dgmode_process(map,
					irfop->irf_tioreq, &index[seg - cnt],
					cnt);
			if (rc != 0)
				return M0_ERR(rc);
		}
	} m0_tl_endfor;
	return M0_RC(0);
}

/**
 * Releases an io fop, freeing the network buffers.
 * This is heavily based on m0t1fs/linux_kernel/file.c::io_req_fop_release
 *
 * @param ref A fop reference to release.
 */
static void ioreq_fop_release(struct m0_ref *ref)
{
	struct m0_fop          *fop;
	struct m0_io_fop       *iofop;
	struct ioreq_fop       *reqfop;
	struct m0_fop_cob_rw   *rwfop;
	struct m0_rpc_bulk     *rbulk;
	struct nw_xfer_request *xfer;
	struct m0_rpc_machine  *rmach;
	struct m0_rpc_item     *item;

	M0_ENTRY("ref %p", ref);
	M0_PRE(ref != NULL);

	fop    = container_of(ref, struct m0_fop, f_ref);
	rmach  = m0_fop_rpc_machine(fop);
	iofop  = container_of(fop, struct m0_io_fop, if_fop);
	reqfop = bob_of(iofop, struct ioreq_fop, irf_iofop, &iofop_bobtype);
	rbulk  = &iofop->if_rbulk;
	xfer   = reqfop->irf_tioreq->ti_nwxfer;
	item   = &fop->f_item;

	/*
	 * Release the net buffers if rpc bulk object is still dirty.
	 * And wait on channel till all net buffers are deleted from
	 * transfer machine.
	 */
	m0_mutex_lock(&xfer->nxr_lock);
	m0_mutex_lock(&rbulk->rb_mutex);
	if (!m0_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist)) {
		struct m0_clink clink;
		size_t          buf_nr;
		size_t          non_queued_buf_nr;

		m0_clink_init(&clink, NULL);
		m0_clink_add(&rbulk->rb_chan, &clink);
		buf_nr = rpcbulk_tlist_length(&rbulk->rb_buflist);
		non_queued_buf_nr = m0_rpc_bulk_store_del_unqueued(rbulk);
		m0_mutex_unlock(&rbulk->rb_mutex);

		m0_rpc_bulk_store_del(rbulk);
		M0_LOG(M0_DEBUG, "fop %p, %p[%u], bulk %p, buf_nr %llu, "
		       "non_queued_buf_nr %llu", &iofop->if_fop, item,
		       item->ri_type->rit_opcode, rbulk,
		       (unsigned long long)buf_nr,
		       (unsigned long long)non_queued_buf_nr);

		if (m0_is_read_fop(&iofop->if_fop))
			m0_atomic64_sub(&xfer->nxr_rdbulk_nr,
				        non_queued_buf_nr);
		if (item->ri_sm.sm_state == M0_RPC_ITEM_UNINITIALISED)
			/* rio_replied() is not invoked for this item. */
			m0_atomic64_dec(&xfer->nxr_iofop_nr);
		m0_mutex_unlock(&xfer->nxr_lock);

		/*
		 * If there were some queued net bufs which had to be deleted,
		 * then it is required to wait for their callbacks.
		 */
		if (buf_nr > non_queued_buf_nr) {
			/*
			 * rpc_machine_lock may be needed from nlx_tm_ev_worker
			 * thread, which is going to wake us up. So we should
			 * release it to avoid deadlock.
			 */
			m0_rpc_machine_unlock(rmach);
			m0_chan_wait(&clink);
			m0_rpc_machine_lock(rmach);
		}
		m0_clink_del_lock(&clink);
		m0_clink_fini(&clink);
	} else {
		m0_mutex_unlock(&rbulk->rb_mutex);
		m0_mutex_unlock(&xfer->nxr_lock);
	}
	M0_ASSERT(m0_rpc_bulk_is_empty(rbulk));

	rwfop = io_rw_get(&iofop->if_fop);
	M0_ASSERT(rwfop != NULL);
	ioreq_fop_fini(reqfop);
	/* see ioreq_fop_fini(). */
	ioreq_fop_bob_fini(reqfop);
	m0_io_fop_fini(iofop);
	m0_free(reqfop);

	M0_LEAVE();
}

/**
 * This is heavily based on m0t1fs/linux_kernel/file.c::io_req_fop_fini
 */
M0_INTERNAL int ioreq_fop_init(struct ioreq_fop    *fop,
			       struct target_ioreq *ti,
			       enum page_attr       pattr)
{
	int                     rc;
	struct m0_fop_type     *fop_type;
	struct m0_clovis_op_io *ioo;
	struct m0_fop_cob_rw   *rwfop;

	M0_ENTRY("ioreq_fop %p, target_ioreq %p", fop, ti);

	M0_PRE(fop != NULL);
	M0_PRE(ti  != NULL);
	M0_PRE(M0_IN(pattr, (PA_DATA, PA_PARITY)));

	ioo = bob_of(ti->ti_nwxfer, struct m0_clovis_op_io, ioo_nwxfer,
		     &ioo_bobtype);
	M0_ASSERT(M0_IN(ioreq_sm_state(ioo),
			(IRS_READING, IRS_DEGRADED_READING,
			 IRS_WRITING, IRS_DEGRADED_WRITING)));

	ioreq_fop_bob_init(fop);
	iofops_tlink_init(fop);
	fop->irf_pattr     = pattr;
	fop->irf_tioreq    = ti;
	fop->irf_reply_rc  = 0;
	fop->irf_ast.sa_cb = clovis_io_bottom_half;
	fop->irf_ast.sa_mach = &ioo->ioo_sm;

	fop_type = M0_IN(ioreq_sm_state(ioo),
			 (IRS_WRITING, IRS_DEGRADED_WRITING)) ?
		   &m0_fop_cob_writev_fopt : &m0_fop_cob_readv_fopt;
	rc  = m0_io_fop_init(&fop->irf_iofop, &ioo->ioo_oo.oo_fid,
			     fop_type, ioreq_fop_release);
	if (rc == 0) {
		/*
		 * Currently m0_io_fop_init sets CROW flag for a READ op.
		 * Diable the flag to force ioservice to return -ENOENT for
		 * non-existing objects. (Temporary solution)
		 */
		if (ioo->ioo_oo.oo_oc.oc_op.op_code == M0_CLOVIS_OC_READ) {
			rwfop = io_rw_get(&fop->irf_iofop.if_fop);
			rwfop->crw_flags &= ~M0_IO_FLAG_CROW;
		}

		/*
		 * Changes ri_ops of rpc item so as to execute clovis's own
		 * callback on receiving a reply.
		 */
		fop->irf_iofop.if_fop.f_item.ri_ops = &clovis_item_ops;
	}

	M0_POST(ergo(rc == 0, ioreq_fop_invariant(fop)));
	return M0_RC(rc);
}

/**
 * This is heavily based on m0t1fs/linux_kernel/file.c::io_req_fop_fini
 */
M0_INTERNAL void ioreq_fop_fini(struct ioreq_fop *fop)
{
	M0_ENTRY("ioreq_fop %p", fop);

	M0_PRE(ioreq_fop_invariant(fop));

	/*
	 * IO fop is finalized (m0_io_fop_fini()) through rpc sessions code
	 * using m0_rpc_item::m0_rpc_item_ops::rio_free().
	 * see m0_io_item_free().
	 */

	iofops_tlink_fini(fop);

	/*
	 * ioreq_bob_fini() is not done here so that struct ioreq_fop
	 * can be retrieved from struct m0_rpc_item using bob_of() and
	 * magic numbers can be checked.
	 */

	fop->irf_tioreq = NULL;
	fop->irf_ast.sa_cb = NULL;
	fop->irf_ast.sa_mach = NULL;

	M0_LEAVE();
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
