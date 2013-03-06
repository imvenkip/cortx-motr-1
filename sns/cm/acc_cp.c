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
  Implements accumulator copy packet for an aggregation group.
  Accumulator copy packet is initialised when an aggregation group is created.
  Data buffers for accumulator copy packets are pre-acquired in context of the
  sns copy machine iterator. The number of accumulator copy packets in an
  aggregation group is equivalent to the total number of failed units in an
  aggregation group. In case of multiple accumulator copy packets, an
  accumulator copy packet is chosen based on the index of failed unit in an
  aggregation group to be recovered.

  @addtogroup SNSCMCP
  @{
*/

M0_INTERNAL bool m0_sns_cm_cp_invariant(const struct m0_cm_cp *cp);
M0_INTERNAL void m0_sns_cm_cp_complete(struct m0_cm_cp *cp);
M0_INTERNAL void m0_sns_cm_cp_free(struct m0_cm_cp *cp);
M0_INTERNAL int m0_sns_cm_cp_fini(struct m0_cm_cp *cp);

static int acc_cp_next[] = {
	[M0_CCP_INIT]       = M0_CCP_WRITE,
	[M0_CCP_WRITE]      = M0_CCP_IO_WAIT,
	[M0_CCP_IO_WAIT]    = M0_CCP_FINI
};

static int acc_cp_phase_next(struct m0_cm_cp *cp)
{
	int phase = m0_fom_phase(&cp->c_fom);

	M0_PRE(M0_IN(phase, (M0_CCP_INIT, M0_CCP_WRITE, M0_CCP_IO_WAIT)));

	/**
	 * @todo If the target unit is not local then do M0_CCP_INIT ->
	 * M0_CCP_SEND.
	 */
	m0_fom_phase_set(&cp->c_fom, acc_cp_next[phase]);

        return M0_IN(m0_fom_phase(&cp->c_fom), (M0_CCP_IO_WAIT, M0_CCP_FINI)) ?
               M0_FSO_WAIT : M0_FSO_AGAIN;

}

const struct m0_cm_cp_ops m0_sns_cm_acc_cp_ops = {
	.co_action = {
		[M0_CCP_INIT]         = &m0_sns_cm_cp_init,
		[M0_CCP_READ]         = &m0_sns_cm_cp_read,
		[M0_CCP_WRITE]        = &m0_sns_cm_cp_write,
		[M0_CCP_IO_WAIT]      = &m0_sns_cm_cp_io_wait,
		[M0_CCP_XFORM]        = &m0_sns_cm_cp_xform,
		[M0_CCP_SEND]         = &m0_sns_cm_cp_send,
		[M0_CCP_RECV]         = &m0_sns_cm_cp_recv,
		/* To satisfy the m0_cm_cp_invariant() */
		[M0_CCP_FINI]         = &m0_sns_cm_cp_fini,
	},
	.co_action_nr            = M0_CCP_NR,
	.co_phase_next           = &acc_cp_phase_next,
	.co_invariant            = &m0_sns_cm_cp_invariant,
	.co_home_loc_helper      = &cp_home_loc_helper,
	.co_complete             = &m0_sns_cm_cp_complete,
	.co_free                 = &m0_sns_cm_cp_free,
};

/**
 * Initialises accumulator copy packet and its corresponding FOM.
 */
M0_INTERNAL void m0_sns_cm_acc_cp_init(struct m0_sns_cm_cp *scp,
				       struct m0_sns_cm_ag *sag)
{
	struct m0_cm_cp *cp = &scp->sc_base;

        M0_PRE(scp != NULL);

	cp->c_ag = &sag->sag_base;
	cp->c_ops = &m0_sns_cm_acc_cp_ops;
	/*
	 * Initialise the bitmap representing the copy packets
	 * which will be transformed into the resultant copy
	 * packet.
	 */
	m0_bitmap_init(&scp->sc_base.c_xform_cp_indices,
		       sag->sag_base.cag_cp_global_nr);
        m0_cm_cp_init(sag->sag_base.cag_cm, cp);
}

/**
 * Configures accumulator copy packet and acquires data buffers.
 */
M0_INTERNAL int m0_sns_cm_acc_cp_setup(struct m0_sns_cm_cp *scp,
				       struct m0_fid *tgt_cobfid,
				       uint64_t tgt_cob_index)
{
	struct m0_sns_cm_ag *sag = ag2snsag(scp->sc_base.c_ag);
	struct m0_cm *cm = sag->sag_base.cag_cm;

        M0_PRE(scp != NULL && sag != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	return m0_sns_cm_cp_setup(scp, tgt_cobfid, tgt_cob_index, ~0);
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
