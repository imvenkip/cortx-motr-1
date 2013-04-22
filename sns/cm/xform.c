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
#include "lib/bob.h"
#include "lib/misc.h"
#include "lib/trace.h"

#include "reqh/reqh.h"

#include "sns/cm/ag.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm.h"
#include "sns/cm/cm_utils.h"
#include "sns/parity_math.h"

/**
 * @addtogroup SNSCMCP
 * @{
 */

/**
 * Number of failed disks.
 * @todo Replace this with information received from trigger fop, once it is
 * implemented. This information will be fetched from each aggregation group.
 * Also, currently repair supports only single failure.
 */
enum {
	FAILURES_NR = 1
};

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

static void bufvecs_xor(struct m0_cm_cp *dst_cp, struct m0_cm_cp *src_cp)
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

/**
 * Checks if the bitmap of resultant copy packet is full, i.e. bits
 * corresponding to all copy packets in an aggregation group are set.
 */
static bool res_cp_bitmap_is_full(struct m0_cm_cp *cp, uint64_t fnr)
{
	int      i;
	uint64_t xform_cnt = 0;

	M0_PRE(cp != NULL);

	for (i = 0; i < cp->c_ag->cag_cp_global_nr; ++i) {
		if (m0_bitmap_get(&cp->c_xform_cp_indices, i))
			M0_CNT_INC(xform_cnt);
	}
	return xform_cnt == cp->c_ag->cag_cp_global_nr - fnr ?
			    true : false;
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
M0_INTERNAL int m0_sns_cm_cp_xform(struct m0_cm_cp *cp)
{
	struct m0_sns_cm_ag     *sns_ag;
	struct m0_cm_aggr_group *ag;
	struct m0_cm_cp         *res_cp;
	struct m0_sns_cm        *scm;
        struct m0_dbenv         *dbenv;
        struct m0_cob_domain    *cdom;
	struct m0_cm_ag_id       id;
	int                      rc;
	int                      i;

	M0_PRE(cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_XFORM);

	ag = cp->c_ag;
	id = ag->cag_id;
	scm = cm2sns(ag->cag_cm);
	sns_ag = ag2snsag(ag);
	m0_cm_ag_lock(ag);

        M0_LOG(M0_DEBUG, "xform: id [%lu] [%lu] [%lu] [%lu] local_cp_nr: [%lu]\
	       transformed_cp_nr: [%lu] has_incoming: %d\n",
               id.ai_hi.u_hi, id.ai_hi.u_lo, id.ai_lo.u_hi, id.ai_lo.u_lo,
	       ag->cag_cp_local_nr, ag->cag_transformed_cp_nr, ag->cag_has_incoming);

        dbenv = scm->sc_base.cm_service.rs_reqh->rh_dbenv;
        cdom  = scm->sc_it.si_cob_dom;

	/* Increment number of transformed copy packets in the accumulator. */
	M0_CNT_INC(ag->cag_transformed_cp_nr);
	if (!ag->cag_has_incoming)
		M0_ASSERT(ag->cag_transformed_cp_nr <= ag->cag_cp_local_nr);

	for (i = 0; i < sns_ag->sag_fnr; ++i) {
		res_cp = &sns_ag->sag_fc[i].fc_tgt_acc_cp.sc_base;
		bufvecs_xor(res_cp, cp);
		/*
		 * The resultant copy packet also includes partial
		 * parity of itself. Hence set the bit value of its own
		 * index in the transformation bitmap.
		 */
		if (cp->c_ag_cp_idx != ~0 && cp->c_ag_cp_idx <=
							ag->cag_cp_global_nr)
			m0_bitmap_set(&res_cp->c_xform_cp_indices,
				      cp->c_ag_cp_idx, true);
		/*
		 * Merge the bitmaps of incoming copy packet with the
		 * resultant copy packet. This is needed in the incoming path,
		 * where the incoming copy packet might be transformed copy
		 * packet which has arrived from some remote node.
		 */
		if (cp->c_xform_cp_indices.b_nr > 0)
			res_cp_bitmap_merge(res_cp, cp);
		/*
		 *
		 * If all copy packets are processed at this stage,
		 * For incoming path transformation can be marked as complete
		 * iff bitmap of transformed copy packets "global" to aggregation
		 * group is full.
		 * For outgoing path, iff all "local" copy packets in aggregation
		 * group are transformed, then transformation can be marked
		 * complete.
		 */
		if ((rc = m0_sns_cm_cob_is_local(&sns_ag->sag_fc[i].fc_tgt_cobfid,
					   dbenv, cdom)) == 0) {
			if (res_cp_bitmap_is_full(res_cp, sns_ag->sag_fnr))
				m0_cm_cp_enqueue(res_cp->c_ag->cag_cm, res_cp);
		} else if (rc == -ENOENT && ag->cag_cp_local_nr ==
						ag->cag_transformed_cp_nr)
			m0_cm_cp_enqueue(res_cp->c_ag->cag_cm, res_cp);
	}

	/*
	 * Once transformation is complete, transition copy packet fom to
	 * M0_CCP_FINI since it is not needed anymore. This copy packet will
	 * be freed during M0_CCP_FINI phase execution.
	 */
	m0_fom_phase_set(&cp->c_fom, M0_CCP_FINI);
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
