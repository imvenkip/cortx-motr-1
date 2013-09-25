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
 * Original creation date: 08/09/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"
#include "lib/memory.h"

#include "reqh/reqh.h"

#include "sns/sns_addb.h"
#include "sns/cm/repair/ag.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm_utils.h"
#include "sns/parity_math.h"

/**
 * @addtogroup SNSCMCP
 * @{
 */

M0_INTERNAL struct m0_sns_cm_repair_ag *
sag2repairag(const struct m0_sns_cm_ag *sag);

/**
 * Splits the merged bufvec into original form, where first bufvec
 * no longer contains the metadata of all the bufvecs in the copy packet.
 */
static int cp_bufvec_split(struct m0_cm_cp *cp)
{
	struct m0_net_buffer       *nbuf_head;
	struct m0_bufvec           *bufvec;
	struct m0_sns_cm_ag        *sag = ag2snsag(cp->c_ag);
	struct m0_sns_cm_repair_ag *rag;
        uint32_t                    new_v_nr;
        m0_bcount_t                *new_v_count;
        uint32_t                    i;

	rag = sag2repairag(sag);
	if (cp->c_buf_nr == 1 ||
	    rag->rag_math.pmi_parity_algo == M0_PARITY_CAL_ALGO_XOR)
		return 0;

	nbuf_head = cp_data_buf_tlist_head(&cp->c_buffers);
        new_v_nr = nbuf_head->nb_pool->nbp_seg_nr;
        SNS_ALLOC_ARR(new_v_count, new_v_nr, &m0_sns_cp_addb_ctx,
		      CP_XFORM_BUFVEC);
        if (new_v_count == NULL)
                return -ENOMEM;

	bufvec = &nbuf_head->nb_buffer;
	for (i = 0; i < new_v_nr; ++i)
		new_v_count[i] = bufvec->ov_vec.v_count[i];

        m0_free(bufvec->ov_vec.v_count);
        bufvec->ov_vec.v_nr = new_v_nr;
        bufvec->ov_vec.v_count = new_v_count;

        return 0;
}

/**
 * XORs the source and destination bufvecs and stores the output in
 * destination bufvec.
 * This implementation assumes that both source and destination bufvecs
 * have same size.
 * @param dst - destination bufvec containing the output of src XOR dest.
 * @param src - source bufvec.
 * @param num_bytes - size of bufvec
 */
static void bufvec_xor(struct m0_bufvec *dst, struct m0_bufvec *src,
		       m0_bcount_t num_bytes)
{
        struct m0_bufvec_cursor s_cur;
        struct m0_bufvec_cursor d_cur;
        m0_bcount_t             frag_size = 0;
        struct m0_buf           src_buf;
        struct m0_buf           dst_buf;

	M0_PRE(dst != NULL);
	M0_PRE(src != NULL);

        m0_bufvec_cursor_init(&s_cur, src);
        m0_bufvec_cursor_init(&d_cur, dst);
        /*
	 * bitwise OR used below to ensure both cursors get moved
         * without short-circuit logic, also why cursor move is before
         * simpler num_bytes check.
         */
        while (!(m0_bufvec_cursor_move(&d_cur, frag_size) |
                 m0_bufvec_cursor_move(&s_cur, frag_size)) &&
               num_bytes > 0) {
                frag_size = min3(m0_bufvec_cursor_step(&d_cur),
                                 m0_bufvec_cursor_step(&s_cur),
                                 num_bytes);
                m0_buf_init(&src_buf, m0_bufvec_cursor_addr(&s_cur), frag_size);
                m0_buf_init(&dst_buf, m0_bufvec_cursor_addr(&d_cur), frag_size);
                m0_parity_math_buffer_xor(&dst_buf, &src_buf);
                num_bytes -= frag_size;
        }
}

static void cp_xor_recover(struct m0_cm_cp *dst_cp, struct m0_cm_cp *src_cp)
{
	struct m0_net_buffer      *src_nbuf;
	struct m0_net_buffer      *dst_nbuf;
	struct m0_net_buffer_pool *nbp;
	uint64_t                   buf_size = 0;
	uint64_t                   total_data_seg_nr;

	M0_PRE(!cp_data_buf_tlist_is_empty(&src_cp->c_buffers));
	M0_PRE(!cp_data_buf_tlist_is_empty(&dst_cp->c_buffers));
	M0_PRE(src_cp->c_buf_nr == dst_cp->c_buf_nr);

	total_data_seg_nr = src_cp->c_data_seg_nr;
	for (src_nbuf = cp_data_buf_tlist_head(&src_cp->c_buffers),
	     dst_nbuf = cp_data_buf_tlist_head(&dst_cp->c_buffers);
	     src_nbuf != NULL && dst_nbuf != NULL;
	     src_nbuf = cp_data_buf_tlist_next(&src_cp->c_buffers, src_nbuf),
	     dst_nbuf = cp_data_buf_tlist_next(&dst_cp->c_buffers, dst_nbuf))
	{
		nbp = src_nbuf->nb_pool;
		if (total_data_seg_nr < nbp->nbp_seg_nr)
			buf_size = total_data_seg_nr * nbp->nbp_seg_size;
		else {
			total_data_seg_nr -= nbp->nbp_seg_nr;
			buf_size = nbp->nbp_seg_nr * nbp->nbp_seg_size;
		}
		bufvec_xor(&dst_nbuf->nb_buffer, &src_nbuf->nb_buffer,
			   buf_size);
	}
}

static void cp_rs_recover(struct m0_cm_cp *src_cp, uint32_t failed_index)
{
	struct m0_sns_cm_cp        *scp;
	struct m0_net_buffer       *nbuf_head;
	struct m0_sns_cm_repair_ag *rag = sag2repairag(ag2snsag(src_cp->c_ag));
	enum m0_sns_ir_block_type   bt;

	nbuf_head = cp_data_buf_tlist_head(&src_cp->c_buffers);
	scp = cp2snscp(src_cp);
	bt = scp->sc_is_local ? M0_SI_BLOCK_LOCAL : M0_SI_BLOCK_REMOTE;
	m0_sns_ir_recover(&rag->rag_ir, &nbuf_head->nb_buffer,
			  &src_cp->c_xform_cp_indices, failed_index, bt);
}

/** Merges the source bitmap to the destination bitmap. */
static void res_cp_bitmap_merge(struct m0_cm_cp *dst, struct m0_cm_cp *src)
{
	int i;

	M0_PRE(dst->c_xform_cp_indices.b_nr <= src->c_xform_cp_indices.b_nr);

	for (i = 0; i < src->c_xform_cp_indices.b_nr; ++i) {
		if (m0_bitmap_get(&src->c_xform_cp_indices, i))
			m0_bitmap_set(&dst->c_xform_cp_indices, i, true);
	}
}

static int res_cp_enqueue(struct m0_cm_cp *cp)
{
	int rc;

	rc = cp_bufvec_split(cp);
	if (rc != 0) {
		SNS_ADDB_FUNCFAIL(rc, &m0_sns_cp_addb_ctx,
				  CP_XFORM_BUFVEC_SPLIT);
		goto out;
	}
	m0_cm_cp_enqueue(cp->c_ag->cag_cm, cp);

out:
	m0_sns_cm_cp_addb_log(cp);
	return rc;
}

static int repair_ag_fc_acc_post(struct m0_sns_cm_repair_ag *rag,
				 struct m0_sns_cm_repair_ag_failure_ctx *fc)
{
	struct m0_sns_cm        *scm;
	struct m0_cm_aggr_group *ag  = &rag->rag_base.sag_base;
	struct m0_cm_cp         *acc = &fc->fc_tgt_acc_cp.sc_base;
        struct m0_cob_domain    *cdom;
	int                      rc;

	scm = cm2sns(ag->cag_cm);
        cdom  = scm->sc_it.si_cob_dom;
	/*
	 * Check if all copy packets are processed at this stage,
	 * For incoming path transformation can be marked as complete
	 * iff bitmap of transformed copy packets "global" to
	 * aggregation group is full.
	 * For outgoing path, iff all "local" copy packets in
	 * aggregation group are transformed, then transformation can
	 * be marked complete.
	 */
	if ((rc = m0_sns_cm_cob_is_local(&fc->fc_tgt_cobfid, cdom)) == 0 &&
	    m0_sns_cm_ag_acc_is_full_with(acc, ag->cag_cp_global_nr -
						rag->rag_base.sag_fnr)) {
			rc = res_cp_enqueue(acc);
			if (rc != 0)
				return rc;
			fc->fc_is_active = true;
	} else if (rc == -ENOENT &&
		   (m0_sns_cm_ag_acc_is_full_with(acc, ag->cag_cp_local_nr))) {
		rc = res_cp_enqueue(acc);
		if (rc != 0)
			return rc;
		fc->fc_is_active = true;
	}

	return rc;
}

/**
 * Transformation function for sns repair.
 * Performs transformation between the accumulator and the given copy packet.
 * Once all the data/parity copy packets are transformed, the accumulator
 * copy packet is posted for further execution. Thus if there exist multiple
 * accumulator copy packets (i.e. in-case multiple units of an aggregation
 * group are failed and need to be recovered, provided k > 1), then the same
 * set of local data copy packets are transformed into multiple accumulators
 * for a given aggregation group.
 *
 * @pre cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_XFORM
 * @param cp Copy packet that has to be transformed.
 */
M0_INTERNAL int m0_sns_cm_repair_cp_xform(struct m0_cm_cp *cp)
{
	struct m0_sns_cm_ag        *sns_ag;
	struct m0_sns_cm_cp        *scp;
	struct m0_sns_cm_repair_ag *rag;
	struct m0_cm_aggr_group    *ag;
	struct m0_cm_cp            *res_cp;
	struct m0_cm_ag_id          id;
	int                         rc;
	int                         i;

	M0_PRE(cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_XFORM);

	ag = cp->c_ag;
	id = ag->cag_id;
	scp = cp2snscp(cp);
	sns_ag = ag2snsag(ag);
	rag = sag2repairag(sns_ag);
	M0_ASSERT(M0_IN(rag->rag_math.pmi_parity_algo,
			(M0_PARITY_CAL_ALGO_XOR,
			 M0_PARITY_CAL_ALGO_REED_SOLOMON)));
	m0_cm_ag_lock(ag);

        M0_LOG(M0_DEBUG, "xform: id [%lu] [%lu] [%lu] [%lu] local_cp_nr: [%lu]\
	       transformed_cp_nr: [%lu] has_incoming: %d\n",
               id.ai_hi.u_hi, id.ai_hi.u_lo, id.ai_lo.u_hi, id.ai_lo.u_lo,
	       ag->cag_cp_local_nr, ag->cag_transformed_cp_nr,
	       ag->cag_has_incoming);
	/* Increment number of transformed copy packets in the accumulator. */
	M0_CNT_INC(ag->cag_transformed_cp_nr);
	if (!ag->cag_has_incoming)
		M0_ASSERT(ag->cag_transformed_cp_nr <= ag->cag_cp_local_nr);
	else
		M0_ASSERT(ag->cag_transformed_cp_nr <=
			 (ag->cag_cp_global_nr - sns_ag->sag_fnr));

	if (rag->rag_math.pmi_parity_algo == M0_PARITY_CAL_ALGO_REED_SOLOMON) {
		rc = m0_cm_cp_bufvec_merge(cp);
		if (rc != 0) {
			SNS_ADDB_FUNCFAIL(rc, &m0_sns_cp_addb_ctx,
					  CP_XFORM_BUFVEC_MERGE);
			goto out;
		}
		cp_rs_recover(cp, scp->sc_failed_idx);
	}
	for (i = 0; i < sns_ag->sag_fnr; ++i) {
		if (rag->rag_fc[i].fc_is_active)
			continue;
		res_cp = &rag->rag_fc[i].fc_tgt_acc_cp.sc_base;
		if (rag->rag_math.pmi_parity_algo == M0_PARITY_CAL_ALGO_XOR)
			cp_xor_recover(res_cp, cp);

		/*
		 * Merge the bitmaps of incoming copy packet with the
		 * resultant copy packet.
		 */
		if (cp->c_xform_cp_indices.b_nr > 0) {
			if (scp->sc_is_local ||
			    (!scp->sc_is_local && (scp->sc_failed_idx ==
			     rag->rag_fc[i].fc_failed_idx))) {
				res_cp_bitmap_merge(res_cp, cp);
				rc = repair_ag_fc_acc_post(rag, &rag->rag_fc[i]);
				if (rc != 0 && rc != -ENOENT)
					goto out;
			}
		}
	}

out:
	/*
	 * Once transformation is complete, transition copy packet fom to
	 * M0_CCP_FINI since it is not needed anymore. This copy packet will
	 * be freed during M0_CCP_FINI phase execution.
	 * Since the buffers of this copy packet are released back to the
	 * buffer pool, merged bufvec is split back to its original form
	 * to be reused by other copy packets.
	 */
	if (rc == 0 || rc == -ENOENT)
		rc = cp_bufvec_split(cp);
	m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_FINI);
	rc = M0_FSO_WAIT;
	m0_cm_ag_unlock(ag);

	return rc;
}

#undef M0_TRACE_SUBSYSTEM
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
