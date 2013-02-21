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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/bob.h"
#include "lib/misc.h"
#include "sns/cm/ag.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm.h"
#include "sns/parity_math.h"

/**
 * @addtogroup SNSCMCP
 * @{
 */

M0_INTERNAL void m0_sns_cm_acc_cp_init_and_post(struct m0_cm_cp *cp);

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

static void bufvecs_xor(struct m0_cm_cp *src_cp, struct m0_cm_cp *dst_cp)
{
	struct m0_net_buffer      *src_nbuf;
	struct m0_net_buffer      *dst_nbuf;
	struct m0_net_buffer_pool *nbp;
	uint64_t                   rem_data_size;
	uint64_t                   buf_size = 0;

	M0_PRE(!cp_data_buf_tlist_is_empty(&src_cp->c_buffers));
	M0_PRE(!cp_data_buf_tlist_is_empty(&dst_cp->c_buffers));
	M0_PRE(src_cp->c_buf_nr == dst_cp->c_buf_nr);

	for (src_nbuf = cp_data_buf_tlist_head(&src_cp->c_buffers),
	     dst_nbuf = cp_data_buf_tlist_head(&dst_cp->c_buffers);
	     src_nbuf != NULL && dst_nbuf != NULL;
	     src_nbuf = cp_data_buf_tlist_next(&src_cp->c_buffers, src_nbuf),
	     dst_nbuf = cp_data_buf_tlist_next(&dst_cp->c_buffers, dst_nbuf))
	{
		nbp = src_nbuf->nb_pool;
		rem_data_size = (src_cp->c_data_seg_nr * nbp->nbp_seg_size) -
				buf_size;
		buf_size = nbp->nbp_seg_nr * nbp->nbp_seg_size;
		bufvec_xor(&dst_nbuf->nb_buffer, &src_nbuf->nb_buffer,
			   min64u(buf_size, rem_data_size));
	}
}

/**
 * Checks if the bitmap of resultant copy packet is full, i.e. bits
 * corresponding to all copy packets in an aggregation group are set.
 */
static bool res_cp_bitmap_is_full(struct m0_cm_cp *cp)
{
	int      i;
	uint64_t xform_cnt = 0;

	M0_PRE(cp != NULL);

	for (i = 0; i < cp->c_ag->cag_cp_global_nr; ++i) {
		if (m0_bitmap_get(&cp->c_xform_cp_indices, i))
			M0_CNT_INC(xform_cnt);
	}
	return xform_cnt == cp->c_ag->cag_cp_global_nr - FAILURES_NR ?
			    true : false;
}

/** Wakes up the copy packet fom by assigning the next phase. */
static void res_cp_fom_wakeup(struct m0_cm_cp *cp)
{
	M0_PRE(cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_XFORM_WAIT);

	m0_fom_wakeup(&cp->c_fom);
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

M0_INTERNAL int m0_sns_cm_cp_xform_wait(struct m0_cm_cp *cp)
{
	cp->c_ops->co_phase_next(cp);
	m0_sns_cm_acc_cp_init_and_post(cp);
	m0_fom_phase_set(&cp->c_fom, M0_CCP_FINI);
	return M0_FSO_WAIT;
}

/**
 * Transformation function for sns repair.
 *
 * @pre cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_XFORM
 * @param cp Copy packet that has to be transformed.
 */
M0_INTERNAL int m0_sns_cm_cp_xform(struct m0_cm_cp *cp)
{
	struct m0_sns_cm_ag     *sns_ag;
	struct m0_cm_aggr_group *ag;
	struct m0_cm_cp         *res_cp;
	int                      rc;

	M0_PRE(cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_XFORM);

	ag = cp->c_ag;
	sns_ag = ag2snsag(ag);
	m0_cm_ag_lock(ag);
	res_cp = sns_ag->sag_cp;
	if (res_cp == NULL) {
		/*
		 * If there is only one copy packet in the aggregation group,
		 * call the next phase of the copy packet fom.
		 */
		if (ag->cag_cp_local_nr > 1) {
			/*
			 * If this is the first copy packet for this aggregation
			 * group, (with more copy packets from same aggregation
			 * group yet to be transformed), store it's pointer in
			 * m0_sns_cm_ag::sag_cp. This copy packet will be used
			 * as a resultant copy packet for transformation.
			 */
			sns_ag->sag_cp = cp;
			/*
			 * Value of collected copy packets is zero at this
			 * stage, hence incrementing it will work fine.
			 */
			M0_CNT_INC(ag->cag_transformed_cp_nr);

			/*
			 * Initialise the bitmap representing the copy packets
			 * which will be transformed into the resultant copy
			 * packet.
			 */
			m0_bitmap_init(&cp->c_xform_cp_indices,
				       ag->cag_cp_global_nr);

			/*
			 * The resultant copy packet also includes partial
			 * parity of itself. Hence set the bit value of its own
			 * index in the transformation bitmap.
			 */
			m0_bitmap_set(&cp->c_xform_cp_indices, cp->c_ag_cp_idx,
				      true);

		}
		/*
		 * Put this copy packet to wait queue of request handler till
		 * transformation of all copy packets belonging to the
		 * aggregation group is complete.
		 */
		rc = cp->c_ops->co_phase_next(cp);
	} else {
		bufvecs_xor(cp, res_cp);
		M0_CNT_INC(ag->cag_transformed_cp_nr);
		m0_bitmap_set(&res_cp->c_xform_cp_indices, cp->c_ag_cp_idx,
			      true);
		/*
		 * Merge the bitmaps of incoming copy packet with the
		 * resultant copy packet. This is needed in the incoming path,
		 * where the incoming copy packet might be transformed copy
		 * packet which has arrived from some remote node.
		 */
		if (cp->c_xform_cp_indices.b_nr > 0)
			res_cp_bitmap_merge(res_cp, cp);

		M0_ASSERT(ag->cag_cp_local_nr >= ag->cag_transformed_cp_nr);
		/*
		 * Once transformation is complete, mark the copy
		 * packet's fom's sm state to M0_CCP_FINI since it is not
		 * needed anymore. This copy packet will be freed during
		 * M0_CCP_FINI phase execution.
		 */
		m0_fom_phase_set(&cp->c_fom, M0_CCP_FINI);
		/*
		 * If all copy packets are processed at this stage,
		 * move the resultant copy packet's fom from waiting to ready
		 * queue.
		 * For incoming path i.e. when the next-to-next phase of the
		 * resultant copy packet is STORAGE-OUT, transformation can be
		 * marked as complete if bitmap of transformed copy packets
		 * "global" to aggregation group is full.
		 * For outgoing path i.e. when the next-to-next phase of the
		 * resultant copy packet is NETWORK-OUT, if all "local" copy
		 * packets in aggregation group are transformed, then
		 * transformation can be marked complete.
		 */
		if (m0_sns_cm_cp_next_phase_get(
					m0_sns_cm_cp_next_phase_get(m0_fom_phase(
							&res_cp->c_fom),
						NULL), NULL) == M0_CCP_WRITE) {
			if (res_cp_bitmap_is_full(res_cp))
				res_cp_fom_wakeup(res_cp);
		} else if (ag->cag_cp_local_nr == ag->cag_transformed_cp_nr)
			res_cp_fom_wakeup(res_cp);
		rc = M0_FSO_WAIT;
	}
	m0_cm_ag_unlock(ag);

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
