/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 02/27/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/memory.h"
#include "lib/trace.h"

#include "rpc/rpclib.h"

#include "cm/proxy.h"
#include "sns/sns_addb.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/sns_cp_onwire.h"
#include "sns/cm/cm_utils.h"

#include "fop/fop.h"
#include "fop/fom.h"
#include "net/net.h"
#include "rpc/item.h"
#include "rpc/session.h"
#include "rpc/conn.h"
#include "rpc/rpc_machine_internal.h"

/**
 * @addtogroup SNSCMCP
 * @{
 */

static void cp_reply_received(struct m0_rpc_item *item);

/*
 * Over-ridden rpc item ops, required to send notification to the copy packet
 * send phase that reply has been received and the copy packet can be finalised.
 */
static const struct m0_rpc_item_ops cp_item_ops = {
        .rio_replied = cp_reply_received,
};

/* Creates indexvec structure based on number of segments and segment size. */
static int indexvec_prepare(struct m0_io_indexvec *iv, m0_bindex_t idx,
                            uint32_t seg_nr, size_t seg_size)
{
        int i;

        M0_PRE(iv != NULL);

	SNS_ALLOC_ARR(iv->ci_iosegs, seg_nr, &m0_sns_cp_addb_ctx,
		      CP_INDEXVEC_PREPARE);
        if (iv->ci_iosegs == NULL) {
                m0_free(iv);
                return -ENOMEM;
        }

        iv->ci_nr = seg_nr;
        for (i = 0; i < seg_nr; ++i) {
                iv->ci_iosegs[i].ci_index = idx;
                iv->ci_iosegs[i].ci_count = seg_size;
                idx += seg_size;
        }
        return 0;
}

static struct m0_cm *cm_get(struct m0_fom *tfom)
{
        return container_of(tfom->fo_service, struct m0_cm, cm_service);
}

/* Returns total number of segments in onwire copy packet structure. */
static uint32_t seg_nr_get(const struct m0_sns_cpx *sns_cpx, uint32_t ivec_nr)
{
        int      i;
        uint32_t seg_nr = 0;

        M0_PRE(sns_cpx != NULL);

        for (i = 0; i < ivec_nr; ++i)
                seg_nr += sns_cpx->scx_ivecs.cis_ivecs[i].ci_nr;

        return seg_nr;
}

/* Converts onwire copy packet structure to in-memory copy packet structure. */
static void snscpx_to_snscp(const struct m0_sns_cpx *sns_cpx,
                            struct m0_sns_cm_cp *sns_cp)
{
        struct m0_cm_ag_id       ag_id;
        struct m0_cm            *cm;
        struct m0_cm_aggr_group *ag;

        M0_PRE(sns_cp != NULL);
        M0_PRE(sns_cpx != NULL);

        sns_cp->sc_sid.si_bits.u_hi = sns_cpx->scx_sid.f_container;
        sns_cp->sc_sid.si_bits.u_lo = sns_cpx->scx_sid.f_key;
	sns_cp->sc_cobfid = sns_cpx->scx_sid;
	sns_cp->sc_failed_idx = sns_cpx->scx_failed_idx;

        sns_cp->sc_index =
                sns_cpx->scx_ivecs.cis_ivecs[0].ci_iosegs[0].ci_index;

        sns_cp->sc_base.c_prio = sns_cpx->scx_cp.cpx_prio;

        m0_cm_ag_id_copy(&ag_id, &sns_cpx->scx_cp.cpx_ag_id);

        cm = cm_get(&sns_cp->sc_base.c_fom);
        m0_cm_lock(cm);
        ag = m0_cm_aggr_group_locate(cm, &ag_id, true);
	M0_ASSERT(ag != NULL);
        m0_cm_unlock(cm);
        sns_cp->sc_base.c_ag = ag;

        sns_cp->sc_base.c_ag_cp_idx = sns_cpx->scx_cp.cpx_ag_cp_idx;
        m0_bitmap_init(&sns_cp->sc_base.c_xform_cp_indices,
                       ag->cag_cp_global_nr);
        m0_bitmap_load(&sns_cpx->scx_cp.cpx_bm,
			&sns_cp->sc_base.c_xform_cp_indices);

        sns_cp->sc_base.c_buf_nr = 0;
        sns_cp->sc_base.c_data_seg_nr = seg_nr_get(sns_cpx,
                                                   sns_cpx->scx_ivecs.cis_nr);
}

/* Converts in-memory copy packet structure to onwire copy packet structure. */
static int snscp_to_snscpx(struct m0_sns_cm_cp *sns_cp,
                           struct m0_sns_cpx *sns_cpx)
{
        struct m0_net_buffer    *nbuf;
        struct m0_cm_cp         *cp;
        uint32_t                 nbuf_seg_nr;
        uint32_t                 tmp_seg_nr;
        uint32_t                 nb_idx = 0;
        uint32_t                 nb_cnt;
        uint64_t                 offset;
        int                      rc;
        int                      i;

        M0_PRE(sns_cp != NULL);
        M0_PRE(sns_cpx != NULL);

        cp = &sns_cp->sc_base;

        sns_cpx->scx_sid.f_container = sns_cp->sc_sid.si_bits.u_hi;
        sns_cpx->scx_sid.f_key = sns_cp->sc_sid.si_bits.u_lo;
	sns_cpx->scx_failed_idx = sns_cp->sc_failed_idx;
        sns_cpx->scx_cp.cpx_prio = cp->c_prio;
        sns_cpx->scx_phase = M0_CCP_SEND;
        m0_cm_ag_id_copy(&sns_cpx->scx_cp.cpx_ag_id, &cp->c_ag->cag_id);
        sns_cpx->scx_cp.cpx_ag_cp_idx = cp->c_ag_cp_idx;
        m0_bitmap_onwire_init(&sns_cpx->scx_cp.cpx_bm,
			      cp->c_ag->cag_cp_global_nr);
        m0_bitmap_store(&cp->c_xform_cp_indices, &sns_cpx->scx_cp.cpx_bm);

        offset = sns_cp->sc_index;
        nb_cnt = cp->c_buf_nr;
        SNS_ALLOC_ARR(sns_cpx->scx_ivecs.cis_ivecs, nb_cnt, &m0_sns_cp_addb_ctx,
		      CP_TO_CPX_IVEC);
        if (sns_cpx->scx_ivecs.cis_ivecs == NULL) {
                rc = -ENOMEM;
                goto out;
        }

        tmp_seg_nr = cp->c_data_seg_nr;
        m0_tl_for(cp_data_buf, &cp->c_buffers, nbuf) {
                nbuf_seg_nr = min32(nbuf->nb_pool->nbp_seg_nr, tmp_seg_nr);
                tmp_seg_nr -= nbuf_seg_nr;
                rc = indexvec_prepare(&sns_cpx->scx_ivecs.
                                               cis_ivecs[nb_idx],
                                               offset,
                                               nbuf_seg_nr,
                                               nbuf->nb_pool->nbp_seg_size);
                if (rc != 0 )
                        goto cleanup;

                offset += nbuf_seg_nr * nbuf->nb_pool->nbp_seg_size;
                M0_CNT_INC(nb_idx);
        } m0_tl_endfor;
        sns_cpx->scx_ivecs.cis_nr = nb_idx;
        sns_cpx->scx_cp.cpx_desc.id_nr = nb_idx;

        SNS_ALLOC_ARR(sns_cpx->scx_cp.cpx_desc.id_descs,
                      sns_cpx->scx_cp.cpx_desc.id_nr, &m0_sns_cp_addb_ctx,
		      CP_TO_CPX_DESC);
        if (sns_cpx->scx_cp.cpx_desc.id_descs == NULL) {
                rc = -ENOMEM;
                goto cleanup;
        }

        goto out;

cleanup:
        for (i = 0; i < nb_idx; ++i)
                m0_free(&sns_cpx->scx_ivecs.cis_ivecs[nb_idx]);
        m0_free(sns_cpx->scx_ivecs.cis_ivecs);
        m0_bitmap_onwire_fini(&sns_cpx->scx_cp.cpx_bm);
out:
        return rc;
}

static void cp_reply_received(struct m0_rpc_item *req_item)
{
        struct m0_fop           *req_fop;
        struct m0_sns_cm_cp     *scp;
	struct m0_rpc_item      *rep_item;
	struct m0_cm_cp_fop     *cp_fop;
	struct m0_fop           *rep_fop;
	struct m0_sns_cpx_reply *sns_cpx_rep;

        rep_item = req_item->ri_reply;
	rep_fop = m0_rpc_item_to_fop(rep_item);
	sns_cpx_rep = m0_fop_data(rep_fop);
	if (sns_cpx_rep->scr_cp_rep.cr_rc == 0) {
		req_fop = m0_rpc_item_to_fop(req_item);
		cp_fop = container_of(req_fop, struct m0_cm_cp_fop, cf_fop);
		scp = cp2snscp(cp_fop->cf_cp);
		m0_fom_wakeup(&scp->sc_base.c_fom);
	}
}

static void cp_fop_release(struct m0_ref *ref)
{
	struct m0_cm_cp_fop  *cp_fop;
        struct m0_fop        *fop = container_of(ref, struct m0_fop, f_ref);

	cp_fop = container_of(fop, struct m0_cm_cp_fop, cf_fop);
	M0_ASSERT(cp_fop != NULL);
        m0_fop_fini(fop);
	m0_free(cp_fop);
}

M0_INTERNAL int m0_sns_cm_cp_send(struct m0_cm_cp *cp, struct m0_fop_type *ft)
{
        struct m0_sns_cm_cp    *sns_cp;
        struct m0_sns_cpx      *sns_cpx;
        struct m0_rpc_bulk_buf *rbuf;
        struct m0_net_domain   *ndom;
        struct m0_net_buffer   *nbuf;
	uint32_t                nbuf_seg_nr;
	uint32_t                tmp_seg_nr;
        struct m0_rpc_session  *session;
	struct m0_cm_cp_fop    *cp_fop;
        struct m0_fop          *fop;
        struct m0_rpc_item     *item;
        uint64_t                offset;
        int                     rc;
        int                     i;

        M0_PRE(cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_SEND);
        M0_PRE(cp->c_cm_proxy != NULL);

	m0_sns_cm_cp_addb_log(cp);

	SNS_ALLOC_PTR(cp_fop, &m0_sns_cp_addb_ctx, CP_SEND_FOP);
	if (cp_fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}
        sns_cp = cp2snscp(cp);
        fop = &cp_fop->cf_fop;
        m0_fop_init(fop, ft, NULL, cp_fop_release);
        rc = m0_fop_data_alloc(fop);
        if (rc  != 0) {
		m0_fop_fini(fop);
		m0_free(cp_fop);
                goto out;
	}

        sns_cpx = m0_fop_data(fop);
        M0_PRE(sns_cpx != NULL);
	cp_fop->cf_cp = cp;
        rc = snscp_to_snscpx(sns_cp, sns_cpx);
        if (rc != 0)
                goto out;

	m0_mutex_lock(&cp->c_cm_proxy->px_mutex);
        session = &cp->c_cm_proxy->px_session;
        ndom = session->s_conn->c_rpc_machine->rm_tm.ntm_dom;
	m0_mutex_unlock(&cp->c_cm_proxy->px_mutex);

        offset = sns_cp->sc_index;
	tmp_seg_nr = cp->c_data_seg_nr;
        m0_tl_for(cp_data_buf, &cp->c_buffers, nbuf) {
		nbuf_seg_nr = min32(nbuf->nb_pool->nbp_seg_nr, tmp_seg_nr);
		tmp_seg_nr -= nbuf_seg_nr;
                rc = m0_rpc_bulk_buf_add(&cp->c_bulk,
                                         nbuf_seg_nr,
                                         ndom, NULL, &rbuf);
                if (rc != 0 || rbuf == NULL)
                        goto out;

                for (i = 0; i < nbuf_seg_nr; ++i) {
                        rc = m0_rpc_bulk_buf_databuf_add(rbuf,
                                        nbuf->nb_buffer.ov_buf[i],
                                        nbuf->nb_buffer.ov_vec.v_count[i],
                                        offset, ndom);
                        offset += nbuf->nb_buffer.ov_vec.v_count[i];
                        if (rc != 0)
                                goto out;
                }
        } m0_tl_endfor;

        m0_mutex_lock(&cp->c_bulk.rb_mutex);
        m0_rpc_bulk_qtype(&cp->c_bulk, M0_NET_QT_PASSIVE_BULK_SEND);
        m0_mutex_unlock(&cp->c_bulk.rb_mutex);

        rc = m0_rpc_bulk_store(&cp->c_bulk, session->s_conn,
                               sns_cpx->scx_cp.cpx_desc.id_descs);
        if (rc != 0)
                goto out;

        item  = m0_fop_to_rpc_item(fop);
        item->ri_ops = &cp_item_ops;
        item->ri_session = session;
        item->ri_prio  = M0_RPC_ITEM_PRIO_MID;
        item->ri_deadline = 0;

        rc = m0_rpc_post(item);
	m0_fop_put(fop);
out:
        if (rc != 0) {
		SNS_ADDB_FUNCFAIL(rc, &m0_sns_cp_addb_ctx, CP_SEND);
                if (&cp->c_bulk != NULL)
                        m0_rpc_bulk_buflist_empty(&cp->c_bulk);
                m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_FINI);
        } else
		m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_SEND_WAIT);

        return M0_FSO_WAIT;
}

M0_INTERNAL int m0_sns_cm_cp_send_wait(struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp     *scp;
	struct m0_net_end_point *ep;
	struct m0_rpc_bulk      *rbulk = &cp->c_bulk;

	M0_PRE(cp != NULL);

	M0_LOG(M0_DEBUG, "rbulk rc: %d", rbulk->rb_rc);

	scp = cp2snscp(cp);
	ep = cp->c_cm_proxy->px_conn.c_rpcchan->rc_destep;
	return cp->c_ops->co_phase_next(cp);
}

M0_INTERNAL void m0_sns_cm_buf_available(struct m0_net_buffer_pool *pool)
{
}

static int cp_buf_acquire(struct m0_cm_cp *cp)
{
        struct m0_sns_cm_cp *sns_cp;
        struct m0_sns_cpx   *sns_cpx;
        struct m0_cm        *cm;
        struct m0_sns_cm    *sns_cm;
	int                  rc;

        M0_PRE(cp != NULL);

        sns_cpx = m0_fop_data(cp->c_fom.fo_fop);
        sns_cp = cp2snscp(cp);
        snscpx_to_snscp(sns_cpx, sns_cp);
        cm = cm_get(&cp->c_fom);
        sns_cm = cm2sns(cm);

	m0_cm_lock(cm);
        rc =  m0_sns_cm_buf_attach(&sns_cm->sc_ibp.sb_bp, cp);
	m0_cm_unlock(cm);

	return rc;
}

M0_INTERNAL int m0_sns_cm_cp_recv_init(struct m0_cm_cp *cp)
{
        struct m0_sns_cpx      *sns_cpx;
        struct m0_rpc_bulk_buf *rbuf;
        struct m0_net_domain   *ndom;
        struct m0_net_buffer   *nbuf;
        struct m0_rpc_bulk     *rbulk;
        struct m0_rpc_session  *session;
        uint32_t                nbuf_idx = 0;
        int                     rc;

        M0_PRE(cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_RECV_INIT);

	rc = cp_buf_acquire(cp);
	if (rc != 0)
		goto out;

        sns_cpx = m0_fop_data(cp->c_fom.fo_fop);
        M0_PRE(sns_cpx != NULL);

        session = cp->c_fom.fo_fop->f_item.ri_session;
        ndom = session->s_conn->c_rpc_machine->rm_tm.ntm_dom;
        rbulk = &cp->c_bulk;

        m0_tl_for(cp_data_buf, &cp->c_buffers, nbuf) {
                nbuf->nb_buffer.ov_vec.v_nr =
                        sns_cpx->scx_ivecs.cis_ivecs[nbuf_idx].ci_nr;
                rc = m0_rpc_bulk_buf_add(rbulk,
                                sns_cpx->scx_ivecs.cis_ivecs[nbuf_idx].ci_nr,
                                ndom, nbuf , &rbuf);
                if (rc != 0 || rbuf == NULL)
                        goto out;
                M0_CNT_INC(nbuf_idx);
        } m0_tl_endfor;

        m0_mutex_lock(&rbulk->rb_mutex);
        m0_rpc_bulk_qtype(rbulk, M0_NET_QT_ACTIVE_BULK_RECV);
        m0_fom_wait_on(&cp->c_fom, &rbulk->rb_chan, &cp->c_fom.fo_cb);
        m0_mutex_unlock(&rbulk->rb_mutex);

        rc = m0_rpc_bulk_load(rbulk, session->s_conn,
                              sns_cpx->scx_cp.cpx_desc.id_descs);
        if (rc != 0) {
                m0_mutex_lock(&rbulk->rb_mutex);
                m0_fom_callback_cancel(&cp->c_fom.fo_cb);
                m0_mutex_unlock(&rbulk->rb_mutex);
                m0_rpc_bulk_buflist_empty(rbulk);
                m0_fom_phase_move(&cp->c_fom, rc, M0_FOPH_FAILURE);
                return M0_FSO_AGAIN;
        }

out:
        if (rc != 0) {
		SNS_ADDB_FUNCFAIL(rc, &m0_sns_cp_addb_ctx, CP_RECV_INIT);
                m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_FINI);
		return M0_FSO_WAIT;
	} else
		return cp->c_ops->co_phase_next(cp);
}

M0_INTERNAL int m0_sns_cm_cp_recv_wait(struct m0_cm_cp *cp,
				       struct m0_fop_type *ft)
{
        struct m0_rpc_bulk      *rbulk;
        struct m0_fop           *fop;
        struct m0_sns_cpx_reply *sns_cpx_rep;
	struct m0_cm_aggr_group *sw_lo_ag;
	struct m0_cm_aggr_group *sw_hi_ag;
	struct m0_cm            *cm;
        int                      rc;

        M0_PRE(cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_RECV_WAIT);

	m0_sns_cm_cp_addb_log(cp);
        rbulk = &cp->c_bulk;

        m0_mutex_lock(&rbulk->rb_mutex);
        if (rbulk->rb_rc != 0)
                m0_fom_phase_move(&cp->c_fom, rbulk->rb_rc, M0_FOPH_FAILURE);
        m0_mutex_unlock(&rbulk->rb_mutex);

        fop = m0_fop_alloc(ft, NULL);
        if (fop == NULL) {
                rc = -ENOMEM;
                goto out;
        } else
                rc = 0;
        sns_cpx_rep = m0_fop_data(fop);
        sns_cpx_rep->scr_cp_rep.cr_rc = rbulk->rb_rc;

	cm = cm_get(&cp->c_fom);
	m0_cm_lock(cm);
	sw_lo_ag = m0_cm_ag_lo(cm);
	sw_hi_ag = m0_cm_ag_hi(cm);
	m0_cm_unlock(cm);
        rc = m0_rpc_reply_post(&cp->c_fom.fo_fop->f_item, &fop->f_item);
        m0_fop_put(fop);
out:
        if (rc != 0) {
		SNS_ADDB_FUNCFAIL(rc, &m0_sns_cp_addb_ctx, CP_RECV_WAIT);
                m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_FINI);
                return M0_FSO_WAIT;
        }
        return cp->c_ops->co_phase_next(cp);
}

M0_INTERNAL int m0_sns_cm_cp_sw_check(struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp *scp         = cp2snscp(cp);
	struct m0_fid        fid;
	struct m0_cm        *cm          = cm_get(&cp->c_fom);
	struct m0_cm_proxy  *cm_proxy;
	const char          *remote_rep;
	int                  rc;

	M0_PRE(cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_SW_CHECK);

	fid.f_container = scp->sc_sid.si_bits.u_hi;
	fid.f_key       = scp->sc_sid.si_bits.u_lo;
	remote_rep = m0_sns_cm_tgt_ep(cm, &fid);
	M0_ASSERT(remote_rep != NULL);
	if (cp->c_cm_proxy == NULL) {
		m0_cm_lock(cm);
		cm_proxy = m0_cm_proxy_locate(cm, remote_rep);
		m0_cm_unlock(cm);
		M0_ASSERT(cm_proxy != NULL);
		cp->c_cm_proxy = cm_proxy;
	} else
		cm_proxy = cp->c_cm_proxy;

	if (m0_cm_proxy_agid_is_in_sw(cm_proxy, &cp->c_ag->cag_id))
		rc = cp->c_ops->co_phase_next(cp);
	else {
		m0_cm_proxy_cp_add(cm_proxy, cp);
		rc = M0_FSO_WAIT;
	}

	return rc;
}

/** @} SNSCMCP */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
