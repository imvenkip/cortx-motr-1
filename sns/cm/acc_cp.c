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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 13/02/2013
 */

#ifndef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#endif
#include "lib/memory.h" /* m0_free() */
#include "lib/misc.h"

#include "fop/fom.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm.h"
#include "sns/cm/ag.h"

/**
  @addtogroup SNSCMCP

  @{
*/

M0_INTERNAL bool m0_sns_cm_cp_invariant(const struct m0_cm_cp *cp);
M0_INTERNAL void m0_sns_cm_cp_complete(struct m0_cm_cp *cp);
M0_INTERNAL void m0_sns_cm_cp_free(struct m0_cm_cp *cp);
M0_INTERNAL int m0_sns_cm_cp_fini(struct m0_cm_cp *cp);

static int acc_cp_init(struct m0_cm_cp *cp)
{
        M0_PRE(m0_fom_phase(&cp->c_fom) == M0_CCP_INIT);
	m0_fom_phase_set(&cp->c_fom, M0_CCP_WRITE);
	return M0_FSO_AGAIN;
}

M0_INTERNAL uint64_t acc_cp_home_loc_helper(const struct m0_cm_cp *cp)
{
        struct m0_sns_cm_ag *sns_ag = ag2snsag(cp->c_ag);

        /*
         * Serialize writes on a particular stob by returning target
         * container id to assign a reqh locality to the cp fom.
         */
	return sns_ag->sag_tgt_cobfid.f_container;
}

const struct m0_cm_cp_ops m0_sns_cm_acc_cp_ops = {
        .co_action = {
                [M0_CCP_INIT]         = &acc_cp_init,
                [M0_CCP_READ]         = &m0_sns_cm_cp_read,
                [M0_CCP_WRITE]        = &m0_sns_cm_cp_write,
                [M0_CCP_IO_WAIT]      = &m0_sns_cm_cp_io_wait,
                [M0_CCP_XFORM]        = &m0_sns_cm_cp_xform,
                [M0_CCP_XFORM_WAIT]   = &m0_sns_cm_cp_xform_wait,
                [M0_CCP_SEND]         = &m0_sns_cm_cp_send,
                [M0_CCP_RECV]         = &m0_sns_cm_cp_recv,
                /* To satisfy the m0_cm_cp_invariant() */
                [M0_CCP_FINI]         = &m0_sns_cm_cp_fini,
        },
        .co_action_nr            = M0_CCP_NR,
        .co_phase_next           = &m0_sns_cm_cp_phase_next,
        .co_invariant            = &m0_sns_cm_cp_invariant,
        .co_home_loc_helper      = &acc_cp_home_loc_helper,
        .co_complete             = &m0_sns_cm_cp_complete,
        .co_free                 = &m0_sns_cm_cp_free,
};

static void cp_buffers_move(struct m0_cm_cp *src_cp, struct m0_cm_cp *dst_cp)
{
	struct m0_net_buffer *nbuf;

	M0_PRE(!cp_data_buf_tlist_is_empty(&src_cp->c_buffers));

	m0_tl_for(cp_data_buf, &src_cp->c_buffers, nbuf) {
		cp_data_buf_tlist_del(nbuf);
		cp_data_buf_tlist_add(&dst_cp->c_buffers, nbuf);
	} m0_tl_endfor;

	M0_POST(!cp_data_buf_tlist_is_empty(&dst_cp->c_buffers));
}

static void cp_copy(struct m0_sns_cm_cp *src_cp, struct m0_sns_cm_cp *dst_cp)
{
	dst_cp->sc_base.c_prio = src_cp->sc_base.c_prio;
	dst_cp->sc_base.c_ops  = src_cp->sc_base.c_ops;
	dst_cp->sc_base.c_ag = src_cp->sc_base.c_ag;
	dst_cp->sc_base.c_ag_cp_idx = src_cp->sc_base.c_ag_cp_idx;
	dst_cp->sc_base.c_xform_cp_indices = src_cp->sc_base.c_xform_cp_indices;
	dst_cp->sc_base.c_buf_nr = src_cp->sc_base.c_buf_nr;
	dst_cp->sc_base.c_data_seg_nr = src_cp->sc_base.c_data_seg_nr;
	dst_cp->sc_base.c_bulk = src_cp->sc_base.c_bulk;
	dst_cp->sc_base.c_io_op = src_cp->sc_base.c_io_op;
	dst_cp->sc_base.c_magix = src_cp->sc_base.c_magix;
	dst_cp->sc_sid = src_cp->sc_sid;
	dst_cp->sc_index = src_cp->sc_index;
	dst_cp->sc_stio = src_cp->sc_stio;
	dst_cp->sc_stob = src_cp->sc_stob;

	/*
	 * We are using the original resultant cp data buffers.
	 * Thus move data buffers from original cp to the accumulator.
	 */
        cp_buffers_move(&src_cp->sc_base, &dst_cp->sc_base);
	
}

/**
 * Creates an accumulator copy packet from the previous resultant copy packet
 * after transformation. The newly created accumulator copy packet is submitted
 * as a new separate copy packet FOM to reqh for processing.
 * Currently, for K = 1, the accumulator copy packet uses the original resultant
 * copy packet's bufvec after transformation. Later for k > 1 the accumulator
 * copy packet may use its own separate bufvec in which the tranformation of
 * other data/parity copy packets for the corresponding group will be performed.
 */
M0_INTERNAL void m0_sns_cm_acc_cp_init_and_post(struct m0_cm_cp *cp)
{
        struct m0_sns_cm_cp *sns_cp = cp2snscp(cp);
        struct m0_sns_cm_cp *acc_sns_cp;
        struct m0_sns_cm_ag *sns_ag = ag2snsag(cp->c_ag);
        struct m0_cm_cp *acc_cp;
        struct m0_cm *cm = sns_ag->sag_base.cag_cm;

        M0_PRE(sns_cp != NULL);

        acc_cp = cm->cm_ops->cmo_cp_alloc(cm);
        M0_ASSERT(acc_cp != NULL);
        m0_cm_cp_init(acc_cp);

	/* Override original cp ops with accumulator cp ops. */
	sns_cp->sc_base.c_ops = &m0_sns_cm_acc_cp_ops;

        sns_ag->sag_acc = cp2snscp(acc_cp);
        acc_sns_cp = sns_ag->sag_acc;
	cp_copy(sns_cp, acc_sns_cp);

        m0_cm_lock(cm);
	/*
	 * Increment local number of copy packets for newly created accumulator
	 * copy packet.
	 */
        ++sns_ag->sag_base.cag_cp_local_nr;
        m0_cm_unlock(cm);
	m0_cm_cp_fom_init(acc_cp);
        M0_ASSERT(m0_cm_cp_invariant(acc_cp));
        m0_cm_cp_enqueue(cm, acc_cp);
}

/** @} SNSCMCP */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
