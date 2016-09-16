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
 *
 * Original creation date: 11-Apr-2014
 * Modified by Sining Wu from m0t1fs/linux_kernel/fsync.c on 23-Jun-2015.
 */

#include "clovis/clovis_addb.h"
#include "clovis/clovis.h"
#include "clovis/clovis_internal.h"
#include "clovis/osync.h"               /* clovis_osync_interactions */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CLOVIS
#include "lib/trace.h"
#include "mdservice/fsync_fops.h"       /* m0_fop_fsync_mds_fopt */
#include "fop/fom_generic.h"            /* m0_rpc_item_is_generic_reply_fop */
#include "lib/memory.h"                 /* m0_alloc, m0_free */
#include "lib/tlist.h"
#include "lib/hash.h"                   /* m0_htable */
#include "file/file.h"
#include "mero/magic.h"                 /* M0_T1FS_FFW_TLIST_MAGIC? */
#include "pool/pool.h"                  /* pools_common_svc_ctx_tl */

/* TODO: use Clovis-defined magic values */
M0_TL_DESCR_DEFINE(opf, "clovis_osync_fop_wrappers pending fsync-fops",
                   static, struct clovis_osync_fop_wrapper, ofw_tlink,
                   ofw_tlink_magic, M0_T1FS_FFW_TLIST_MAGIC1,
                   M0_T1FS_FFW_TLIST_MAGIC2);

M0_TL_DEFINE(opf, static, struct clovis_osync_fop_wrapper);

/* OSPTI -> Object's Service to Pending Transaction Id list */
M0_TL_DESCR_DEFINE(ospti, "m0_reqh_service_txid pending an object", M0_INTERNAL,
			struct m0_reqh_service_txid,
			stx_tlink, stx_link_magic,
			M0_T1FS_INODE_PTI_MAGIC1, M0_T1FS_INODE_PTI_MAGIC2);

M0_TL_DEFINE(ospti, M0_INTERNAL, struct m0_reqh_service_txid);


/**
 * Ugly abstraction of clovis_osync interactions with wider mero code
 * - purely to facilitate unit testing
 */
struct clovis_osync_interactions osi = {
	.post_rpc       = &m0_rpc_post,
	.wait_for_reply = &m0_rpc_item_wait_for_reply,
	/* fini is for requests, allocated in a bigger structure */
	.fop_fini       = &m0_fop_fini,
	/* put is for replies, allocated by a lower layer */
	.fop_put        = &m0_fop_put_lock,
};

/**
 * Cleans-up a fop.
 */
static void clovis_osync_fop_cleanup(struct m0_ref *ref)
{
	struct m0_fop                   *fop;
	struct clovis_osync_fop_wrapper *ofw;

	M0_ENTRY();
	M0_PRE(osi.fop_fini != NULL);

	fop = container_of(ref, struct m0_fop, f_ref);
	osi.fop_fini(fop);

	ofw = container_of(fop, struct clovis_osync_fop_wrapper, ofw_fop);
	m0_free(ofw);

	M0_LEAVE("clovis_osync_fop_cleanup");
}

/**
 * Creates and sends an fsync fop from the provided m0_reqh_service_txid.
 * Allocates and returns the fop wrapper at @ofw_out on success,
 * which is freed on the last m0_fop_put().
 */
int clovis_osync_request_create(struct m0_reqh_service_txid      *stx,
                                struct clovis_osync_fop_wrapper **ofw_out,
                                enum m0_fsync_mode                mode)
{
	int                              rc;
	struct m0_fop                   *fop;
	struct m0_rpc_item              *item;
	struct m0_fop_fsync             *ffd;
	struct m0_fop_type              *fopt;
	struct clovis_osync_fop_wrapper *ofw;

	M0_ENTRY();

	M0_ALLOC_PTR(ofw);
	if (ofw == NULL)
		return M0_ERR(-ENOMEM);

	rc = m0_rpc_session_validate(&stx->stx_service_ctx->sc_rlink.rlk_sess);
	if (rc != 0) {
		m0_free(ofw);
		return M0_ERR_INFO(rc, "Service tx session invalid");
	}

	if (stx->stx_service_ctx->sc_type == M0_CST_MDS)
		fopt = &m0_fop_fsync_mds_fopt;
	else if (stx->stx_service_ctx->sc_type == M0_CST_IOS)
		fopt = &m0_fop_fsync_ios_fopt;
	else
		M0_IMPOSSIBLE("invalid service type:%d",
			      stx->stx_service_ctx->sc_type);

	/* store the pending txid reference with the fop */
	ofw->ofw_stx = stx;

	fop = &ofw->ofw_fop;
	m0_fop_init(fop, fopt, NULL, &clovis_osync_fop_cleanup);
	rc = m0_fop_data_alloc(fop);
	if (rc != 0) {
		m0_free(ofw);
		return M0_ERR_INFO(rc, "Allocating osync fop data failed.");
	}

	ffd = m0_fop_data(fop);
	ffd->ff_be_remid = stx->stx_tri;
	ffd->ff_fsync_mode = mode;

	/*
	 *  We post the rpc_item directly so that this is asyncronous.
	 *  Prepare the fop as an rpc item
	 */
	item = &fop->f_item;
	item->ri_session = &stx->stx_service_ctx->sc_rlink.rlk_sess;
	item->ri_prio = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline = 0;
	item->ri_nr_sent_max = CLOVIS_RPC_MAX_RETRIES;
	item->ri_resend_interval = CLOVIS_RPC_RESEND_INTERVAL;

	rc = osi.post_rpc(item);
	if (rc != 0) {
		osi.fop_fini(fop);
		return M0_ERR_INFO(rc, "Calling m0_rpc_post() failed.");
	}

	*ofw_out = ofw;
	M0_LEAVE();
	return M0_RC(0);
}

static void fsync_stx_update(struct m0_reqh_service_txid *stx, uint64_t txid,
			     struct m0_mutex *lock)
{
	M0_PRE(stx != NULL);
	M0_PRE(lock != NULL);

	m0_mutex_lock(lock);
	if (stx->stx_tri.tri_txid <= txid) {
		/*
		 * Our transaction got committed update the record to be
		 * ignored in the future.
		 */
		M0_SET0(&stx->stx_tri);
	}
	/*
	 * Else the stx_maximum_txid got increased while we were waiting, it
	 * is acceptable for fsync to return as long as the
	 * correct-at-time-of-sending txn was committed (which the caller
	 * should assert).
	 */
	m0_mutex_unlock(lock);
}

/**
 * Waits for a reply to an fsync fop and process it.
 * Cleans-up the fop allocated in clovis_osync_request_create.
 *
 * obj may be NULL if the reply is only likely to touch the super block.
 * csb may be NULL, iff obj is specified.
 *
 */
int clovis_osync_reply_process(struct m0_clovis                *m0c,
                               struct m0_clovis_obj            *obj,
                               struct clovis_osync_fop_wrapper *ofw)
{
	int                         rc;
	uint64_t                    reply_txid;
	struct m0_fop              *fop;
	struct m0_rpc_item         *item;
	struct m0_fop_fsync        *ffd;
	struct m0_fop_fsync_rep    *ffr;
	struct m0_reqh_service_ctx *service;

	M0_ENTRY();

	if (m0c == NULL)
		m0c = m0_clovis__obj_instance(obj);
	M0_PRE(m0c != NULL);

	fop = &ofw->ofw_fop;
	item = &fop->f_item;

	rc = osi.wait_for_reply(item, m0_time_from_now(CLOVIS_RPC_TIMEOUT, 0));
	if (rc != 0)
		goto out;

	/* get the {fop,reply} data */
	ffd = m0_fop_data(fop);
	M0_ASSERT(ffd != NULL);
	ffr = m0_fop_data(m0_rpc_item_to_fop(item->ri_reply));
	M0_ASSERT(ffr != NULL);

	rc = ffr->ffr_rc;
	if (rc != 0) {
		M0_LOG(M0_ERROR, "reply rc=%d", rc);
		goto out;
	}

	/* Is this a valid reply to our request */
	reply_txid = ffr->ffr_be_remid.tri_txid;
	if (reply_txid < ffd->ff_be_remid.tri_txid) {
		/* Error (most likely) caused by an ioservice. */
		rc = M0_ERR(-EIO);
		M0_LOG(M0_ERROR, "Commited transaction is smaller "
					    "than that requested.");
		goto out;
	}

	if (obj != NULL)
		fsync_stx_update(ofw->ofw_stx, reply_txid,
				 &obj->ob_pending_tx_lock);

	service = ofw->ofw_stx->stx_service_ctx;

	/*
	 * check the super block too, super block txid_record
	 * is embedded in the m0_reqh_service_ctx struct
	 */
	fsync_stx_update(&service->sc_max_pending_tx, reply_txid,
			 &service->sc_max_pending_tx_lock);

out:
	osi.fop_put(fop);

	return M0_RC(rc);
}

/**
 * clovis sync core sends an fsync-fop to a list of services, then blocks,
 * waiting for replies. This is implemented as two loops.
 * The 'fop sending loop', generates and posts fops, adding them to a list
 * of pending fops. This is all done while holding the
 * m0_clovis_obj::ob_pending_tx_lock. The 'reply receiving loop'
 * works over the list of pending fops, waiting for a reply for each one.
 * It acquires the m0_clovis_obj::ob_pending_tx_map_lock only
 * when necessary.
 */
int clovis_osync_core(struct m0_clovis_obj *obj, enum m0_fsync_mode mode)
{
	int                              rc;
	int                              saved_error = 0;
	struct m0_tl                     pending_fops;
	struct m0_reqh_service_txid     *iter;
	struct clovis_osync_fop_wrapper *ofw;

	M0_ENTRY();

	M0_PRE(obj != NULL);

	m0_tlist_init(&opf_tl, &pending_fops);

	/*
	 * find the inode's list services with pending transactions
	 * for each entry, send an fsync fop.
	 * This is the fop sending loop.
	 */
	m0_mutex_lock(&obj->ob_pending_tx_lock);
	m0_tl_for(ospti, &obj->ob_pending_tx, iter) {
		/*
		 * send an fsync fop for
		 * iter->stx_maximum_txid to iter->stx_service_ctx
		 */

		/* Check if this service has any pending transactions. */
		if (iter->stx_tri.tri_txid == 0)
			continue;

		/* Create and send a request */
		rc = clovis_osync_request_create(iter, &ofw, mode);
		if (rc != 0) {
			saved_error = rc;
			break;
		} else
			/* Add to list of pending fops  */
			opf_tlink_init_at(ofw, &pending_fops);
	} m0_tl_endfor;
	m0_mutex_unlock(&obj->ob_pending_tx_lock);

	/*
	 * At this point we may have sent some fops, but stopped when one
	 * failed - collect all the replies before returning.
	 */

	/* This is the fop-reply receiving loop. */
	m0_tl_teardown(opf, &pending_fops, ofw) {
		/* Get and process the reply. */
		rc = clovis_osync_reply_process(NULL, obj, ofw);
		saved_error = saved_error ? : rc;
	}

	M0_LEAVE();
	return saved_error;
}

/**
 * Updates a m0_reqh_service_txid with the specified be_tx_remid
 * if the struct m0_be_tx_remid::tri_txid > the stored value
 * obj may be NULL if the update has no associated inode.
 * m0c may be NULL, iff obj is specified. (XXX: it seems we can remove it?)
 */
void clovis_osync_record_update(struct m0_reqh_service_ctx *service,
                                struct m0_clovis           *m0c,
                                struct m0_clovis_obj       *obj,
                                struct m0_be_tx_remid      *btr)
{
	struct m0_reqh_service_txid *stx = NULL;

	M0_ENTRY();

	M0_PRE(service != NULL);
	if (m0c == NULL)
		m0c = m0_clovis__obj_instance(obj);
	M0_PRE(m0c != NULL);

	/* Updates pending transaction number in the inode */
	if (obj != NULL) {
		/*
		  * TODO: replace this O(N) search with something better.
		  * Embbed the struct m0_reqh_service_txid in a list of
		  * 'services for this inode'? See RB1667
		  */
		/* Find the record for this service */
		m0_mutex_lock(&obj->ob_pending_tx_lock);
		stx = m0_tl_find(ospti, stx,
				 &obj->ob_pending_tx,
				 stx->stx_service_ctx == service);

		if (stx != NULL) {
			if (btr->tri_txid > stx->stx_tri.tri_txid)
				stx->stx_tri = *btr;
		} else {
			/*
			 * not found - add a new record
			 */
			M0_ALLOC_PTR(stx);
			if (stx != NULL) {
				stx->stx_service_ctx = service;
				stx->stx_tri = *btr;

				ospti_tlink_init_at(stx, &obj->ob_pending_tx);
			}
		}
		m0_mutex_unlock(&obj->ob_pending_tx_lock);
	}

	/* update pending transaction number in the Clovis instance */
	m0_mutex_lock(&service->sc_max_pending_tx_lock);
	stx = &service->sc_max_pending_tx;
	/* update the value from the reply_fop */
	if (btr->tri_txid > stx->stx_tri.tri_txid) {
		stx->stx_service_ctx = service;
		stx->stx_tri = *btr;
	}
	m0_mutex_unlock(&service->sc_max_pending_tx_lock);

	M0_LEAVE("Clovis osync record updated.");
}

/**
 * Entry point for osync, calls clovis_osync_core with mode=active
 */
int m0_clovis_obj_sync(struct m0_clovis_obj *obj)
{
	int rc;

	M0_ENTRY();

	M0_PRE(obj != NULL);
	rc = clovis_osync_core(obj, M0_FSYNC_MODE_ACTIVE);

	return M0_RC(rc);
}
M0_EXPORTED(m0_clovis_obj_sync);

/**
 * Entry point for syncing the all pending tx in the Clovis instance.
 * Unlike clovis_osync_core this function acquires the sc_max_pending_tx_lock
 * for each service, as there is not a larger-granularity lock.
 */
int m0_clovis_sync(struct m0_clovis *m0c, int wait)
{
	int                              rc;
	int                              saved_error = 0;
	struct m0_tl                     pending_fops;
	struct m0_reqh_service_txid     *stx;
	struct m0_reqh_service_ctx      *iter;
	struct clovis_osync_fop_wrapper *ofw;

	M0_ENTRY();

	M0_PRE(osi.post_rpc != NULL);
	M0_PRE(osi.wait_for_reply != NULL);
	M0_PRE(osi.fop_fini != NULL);

	m0_tlist_init(&opf_tl, &pending_fops);

	/*
	 *  loop over all services associated with this super block,
	 *  send an fsync fop for those with pending transactions
	 *
	 *  fop sending loop
	 */
	m0_tl_for(pools_common_svc_ctx, &m0c->m0c_pools_common.pc_svc_ctxs,
		  iter) {
		/*
		 * Send an fsync fop for iter->sc_max_pending_txt to iter.
		 */
		m0_mutex_lock(&iter->sc_max_pending_tx_lock);
		stx = &iter->sc_max_pending_tx;

		/*
		 * Check if this service has any pending transactions.
		 * Currently for fsync operations are supported only for
		 * ioservice and mdservice.
		 */
		if (stx->stx_tri.tri_txid == 0 ||
		    !M0_IN(stx->stx_service_ctx->sc_type,
			  (M0_CST_MDS, M0_CST_IOS))) {
			m0_mutex_unlock(&iter->sc_max_pending_tx_lock);
			continue;
		}

		/* Create and send a request */
		rc = clovis_osync_request_create(stx, &ofw,
		                                 M0_FSYNC_MODE_ACTIVE);
		if (rc != 0) {
			saved_error = rc;
			m0_mutex_unlock(&iter->sc_max_pending_tx_lock);
			break;
		} else {
			/* Add to list of pending fops */
			opf_tlink_init_at(ofw, &pending_fops);
		}

		m0_mutex_unlock(&iter->sc_max_pending_tx_lock);
	} m0_tl_endfor;

	/*
	 * At this point we may have sent some fops, but stopped when one
	 * failed - collect all the replies before returning
	 */

	/* reply receiving loop */
	m0_tl_teardown(opf, &pending_fops, ofw) {
		/* get and process the reply */
		rc = clovis_osync_reply_process(m0c, NULL, ofw);
		saved_error = saved_error ? : rc;
	}

	M0_LEAVE();
	return M0_ERR(saved_error);
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
