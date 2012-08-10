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
 * Original creation date: 09/08/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/bob.h"
#include "lib/errno.h"
#include "cm/ag.h"
#include "sns/repair/ag.h"
#include "cm/cm_internal.h"
#include "sns/parity_math.h"

/**
 * XORs the source and destination bufvecs and stores the output in
 * destination bufvec.
 * This implementation assumes that both source and destination bufvecs
 * have same size which is equal to C2_CP_SIZE.
 * @param dst - destination bufvec containing the output of src XOR dest.
 * @param src - source bufvec.
 */
static void bufvec_xor(struct c2_bufvec *dst, struct c2_bufvec *src)
{
        struct c2_bufvec_cursor s_cur;
        struct c2_bufvec_cursor d_cur;
        c2_bcount_t             frag_size = 0;
        struct c2_buf           src_buf;
        struct c2_buf           dst_buf;
        c2_bcount_t             num_bytes = C2_CP_SIZE;

	C2_PRE(dst != NULL);
	C2_PRE(src != NULL);

        c2_bufvec_cursor_init(&s_cur, src);
        c2_bufvec_cursor_init(&d_cur, dst);
        /* bitwise OR used below to ensure both cursors get moved
           without short-circuit logic, also why cursor move is before
           simpler num_bytes check */
        while (!(c2_bufvec_cursor_move(&d_cur, frag_size) |
                 c2_bufvec_cursor_move(&s_cur, frag_size)) &&
               num_bytes > 0) {
                frag_size = min3(c2_bufvec_cursor_step(&d_cur),
                                 c2_bufvec_cursor_step(&s_cur),
                                 num_bytes);
                c2_buf_init(&src_buf, c2_bufvec_cursor_addr(&s_cur), frag_size);
                c2_buf_init(&dst_buf, c2_bufvec_cursor_addr(&d_cur), frag_size);
                c2_parity_math_buffer_xor(&src_buf, &dst_buf);
                num_bytes -= frag_size;
        }
}

/**
 * Creates resultant copy packet and saves the context of the first incoming
 * copy packet.
 * @param sns_ag SNS specific aggregation group in which the context is to be
 * stored.
 * @param cp First incoming copy packet for transformation.
 */
static int sns_res_cp_create(struct c2_sns_ag *sns_ag, struct c2_cm_cp *cp)
{
        struct c2_cm_cp *res_cp;

        C2_PRE(sns_ag != NULL);
        C2_PRE(cp != NULL);

	res_cp = sns_ag->sag_base.cag_ops->cago_cp_alloc(&sns_ag->sag_base,
							 cp->c_data);
        if (res_cp == NULL)
                return -ENOMEM;

        sns_ag->sag_collected_cp_nr = 1;
        res_cp->c_prio = cp->c_prio;
	res_cp->c_fom.fo_phase = cp->c_fom.fo_phase;
        res_cp->c_data = cp->c_data;
        res_cp->c_ag = &sns_ag->sag_base;

	sns_ag->sag_ccp = res_cp;

        cp->c_data = NULL;

        return 0;
}

/**
 * Transformation function for sns repair.
 *
 * Finds aggregation group c2_sns_ag corresponding to the incoming
 * copy packet. Calculates the total number of copy packets
 * c2_sns_ag::sag_local_cp_nr belonging
 * to c2_sns_ag and checks it with the number of copy packets which are
 * transformed (c2_sns_ag::sag_collected_cp_nr, which is incremented after
 * every transformation).
 * If all the copy packets belonging to the aggregation group are transformed,
 * then creates a new copy packet and its corresponding fom.
 *
 * Transformation involves XORing the c2_buf_vec's from copy packet
 * c2_cm_cp::c_data with c2_sns_ag::sag_ccp::c_data.
 * XORing is done using parity math operation like c2_parity_math_buffer_xor().
 *
 * When first copy packet of the aggregation group is transformed, its
 * corresponding c2_sns_ag::sag_ccp::c_data is set to c2_cm_cp::c_data.
 * Typically, all copy packets will have same buffer vector size.
 * Hence, there is no need for any complex buffer manipulation like growing or
 * shrinking the c2_sns_ag::sag_ccp::c_data.
 *
 * Every copy packet once transformed is freed. This is done by setting it's fom
 * state to CCP_FINI.
 *
 * @pre cp != NULL && cp->cp_state == CCP_XFORM
 * @param cp Copy packet that has to be transformed.
 */
int repair_cp_xform(struct c2_cm_cp *cp)
{
        struct c2_sns_ag        *sns_ag;
        struct c2_cm_aggr_group *ag;
	struct c2_cm_cp         *res_cp;

        C2_PRE(cp != NULL && cp->c_fom.fo_phase == CCP_XFORM);

        ag = cp->c_ag;
        sns_ag = bob_of(ag, struct c2_sns_ag, sag_base, &aggr_grps_bob);
	res_cp = sns_ag->sag_ccp;
        if (res_cp == NULL) {
                sns_ag->sag_local_cp_nr = ag->cag_ops->cago_local_cp_nr(ag);
                /*
                 *  If there is only one copy packet in the aggregation group,
                 *  call the next phase of the copy packet fom.
                 */
                if (sns_ag->sag_local_cp_nr == 1) {
                        cp->c_fom.fo_rc = cp->c_ops->co_phase(cp);
                        goto out;
                }
                /*
                 * If this is the first copy packet for this aggregation group,
                 * create the resultant copy packet and save context of the
                 * current copy packet.
                 */
                cp->c_fom.fo_rc = sns_res_cp_create(sns_ag, cp);
        } else {
                bufvec_xor(res_cp->c_data, cp->c_data);
                C2_CNT_INC(sns_ag->sag_collected_cp_nr);
                /*
                 * Once transformation is complete, mark the copy
                 * packet's fom to CCP_FINI since it is not needed anymore,
                 * provided that it is not the first copy packet.
                 */
                cp->c_fom.fo_phase = CCP_FINI;
                /*
                 * If all copy packets are processed at this stage, post the
                 * resultant copy packet to the request handler's queue for
                 * processing.
                 */
                if(sns_ag->sag_local_cp_nr == sns_ag->sag_collected_cp_nr) {
			c2_cm_cp_enqueue(res_cp->c_ag->cag_cm, res_cp);
                        res_cp->c_fom.fo_rc = res_cp->c_ops->co_phase(res_cp);
		}
        }

out:
        return C2_FSO_AGAIN;
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
