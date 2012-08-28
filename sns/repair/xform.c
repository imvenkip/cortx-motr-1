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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/09/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/bob.h"
#include "cm/cp.h"
#include "sns/repair/ag.h"
#include "sns/parity_math.h"

/**
 * @addtogroup snsrepair
 * @{
 */

/**
 * XORs the source and destination bufvecs and stores the output in
 * destination bufvec.
 * This implementation assumes that both source and destination bufvecs
 * have same size.
 * @param dst - destination bufvec containing the output of src XOR dest.
 * @param src - source bufvec.
 * @param num_bytes - size of bufvec
 */
static void bufvec_xor(struct c2_bufvec *dst, struct c2_bufvec *src,
		       c2_bcount_t num_bytes)
{
        struct c2_bufvec_cursor s_cur;
        struct c2_bufvec_cursor d_cur;
        c2_bcount_t             frag_size = 0;
        struct c2_buf           src_buf;
        struct c2_buf           dst_buf;

	C2_PRE(dst != NULL);
	C2_PRE(src != NULL);

        c2_bufvec_cursor_init(&s_cur, src);
        c2_bufvec_cursor_init(&d_cur, dst);
        /*
	 * bitwise OR used below to ensure both cursors get moved
         * without short-circuit logic, also why cursor move is before
         * simpler num_bytes check.
         */
        while (!(c2_bufvec_cursor_move(&d_cur, frag_size) |
                 c2_bufvec_cursor_move(&s_cur, frag_size)) &&
               num_bytes > 0) {
                frag_size = min3(c2_bufvec_cursor_step(&d_cur),
                                 c2_bufvec_cursor_step(&s_cur),
                                 num_bytes);
                c2_buf_init(&src_buf, c2_bufvec_cursor_addr(&s_cur), frag_size);
                c2_buf_init(&dst_buf, c2_bufvec_cursor_addr(&d_cur), frag_size);
                c2_parity_math_buffer_xor(&dst_buf, &src_buf);
                num_bytes -= frag_size;
        }
}

/**
 * Transformation function for sns repair.
 *
 * @pre cp != NULL && c2_fom_phase(&cp->c_fom) == C2_CCP_XFORM
 * @param cp Copy packet that has to be transformed.
 */
int repair_cp_xform(struct c2_cm_cp *cp)
{
        struct c2_sns_repair_ag *sns_ag;
        struct c2_cm_aggr_group *ag;
	struct c2_cm_cp         *res_cp;
	c2_bcount_t              cp_bufvec_size;

        C2_PRE(cp != NULL && c2_fom_phase(&cp->c_fom) == C2_CCP_XFORM);

        ag = cp->c_ag;
        sns_ag = bob_of(ag, struct c2_sns_repair_ag, sag_base, &aggr_grps_bob);
	res_cp = sns_ag->sag_cp;
        if (res_cp == NULL) {
                ag->cag_cp_nr = ag->cag_ops->cago_local_cp_nr(ag);
                /*
                 * If there is only one copy packet in the aggregation group,
                 * call the next phase of the copy packet fom.
                 */
                if (ag->cag_cp_nr == 1)
                        return cp->c_ops->co_phase(cp);

                /*
                 * If this is the first copy packet for this aggregation group,
                 * (with more copy packets from same aggregation group to be
                 * yet transformed), store it's pointer in
                 * c2_sns_repair_ag::sag_cp. This copy packet will be used as
                 * a resultant copy packet for transformation.
                 */
		sns_ag->sag_cp = cp;
		/*
		 * Value of collected copy packets is zero at this stage, hence
		 * incrementing it will work fine.
		 */
                C2_CNT_INC(ag->cag_transformed_cp_nr);

		/*
		 * Put this copy packet to wait queue of request handler till
		 * transformation of all copy packets belonging to the
		 * aggregation group is complete.
		 */
		return C2_FSO_WAIT;
        } else {
		cp_bufvec_size = c2_cm_cp_data_size(cp);
		/*
		 * Typically, all copy packets will have same buffer vector
		 * size. Hence, there is no need for any complex buffer
		 * manipulation like growing or shrinking the buffers.
		 */
                bufvec_xor(res_cp->c_data, cp->c_data, cp_bufvec_size);
                C2_CNT_INC(ag->cag_transformed_cp_nr);
		C2_ASSERT(ag->cag_cp_nr >= ag->cag_transformed_cp_nr);
                /*
                 * Once transformation is complete, mark the copy
                 * packet's fom's sm state to C2_CCP_FINI since it is not
                 * needed anymore. This copy packet will be freed during
                 * C2_CCP_FINI phase execution.
                 */
		c2_fom_phase_set(&cp->c_fom, C2_CCP_FINI);
                /*
                 * If all copy packets are processed at this stage,
                 * move the resultant copy packet's fom from waiting to ready
                 * queue.
                 */
                if(ag->cag_cp_nr == ag->cag_transformed_cp_nr) {
                        res_cp->c_ops->co_phase(res_cp);
			c2_fom_wakeup(&res_cp->c_fom);
		}
		return C2_FSO_AGAIN;
        }
}

/** @} snsrepair */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
