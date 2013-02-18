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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/06/2012
 */

#ifndef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#endif
#include "lib/memory.h" /* m0_free() */
#include "lib/misc.h"

#include "fop/fom.h"
#include "cm/ag.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm.h"
#include "sns/cm/ag.h"

/**
  @addtogroup SNSCMCP

  @{
*/

M0_INTERNAL struct m0_sns_cm_cp *cp2snscp(const struct m0_cm_cp *cp)
{
	return container_of(cp, struct m0_sns_cm_cp, sc_base);
}

M0_INTERNAL bool m0_sns_cm_cp_invariant(const struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp *sns_cp = cp2snscp(cp);

	return m0_fom_phase(&cp->c_fom) < M0_CCP_NR &&
	       ergo(m0_fom_phase(&cp->c_fom) > M0_CCP_INIT,
		    m0_stob_id_is_set(&sns_cp->sc_sid));
}

/*
 * Uses GOB fid key and parity group number to generate a scalar to
 * help select a request handler locality for copy packet FOM.
 */
M0_INTERNAL uint64_t cp_home_loc_helper(const struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp *sns_cp = cp2snscp(cp);

	/*
         * Serialize read on a particular stob by returning target
         * container id to assign a reqh locality to the cp fom.
         */
	return sns_cp->sc_sid.si_bits.u_hi;
}

static int cp_init(struct m0_cm_cp *cp)
{
	M0_PRE(m0_fom_phase(&cp->c_fom) == M0_CCP_INIT);
	return cp->c_ops->co_phase_next(cp);
}

M0_INTERNAL int m0_sns_cm_cp_send(struct m0_cm_cp *cp)
{
	return M0_FSO_AGAIN;
}

M0_INTERNAL int m0_sns_cm_cp_recv(struct m0_cm_cp *cp)
{
	return M0_FSO_AGAIN;
}

static int next[] = {
	[M0_CCP_INIT]       = M0_CCP_READ,
	[M0_CCP_READ]       = M0_CCP_IO_WAIT,
	[M0_CCP_XFORM]      = M0_CCP_XFORM_WAIT,
	[M0_CCP_XFORM_WAIT] = M0_CCP_WRITE,
	[M0_CCP_WRITE]      = M0_CCP_IO_WAIT,
	[M0_CCP_IO_WAIT]    = M0_CCP_XFORM
};

M0_INTERNAL int m0_sns_cm_cp_phase_next(struct m0_cm_cp *cp)
{
	int phase = m0_sns_cm_cp_next_phase_get(m0_fom_phase(&cp->c_fom), cp);

	m0_fom_phase_set(&cp->c_fom, phase);

        return M0_IN(phase, (M0_CCP_IO_WAIT, M0_CCP_XFORM_WAIT,
				   M0_CCP_FINI)) ?
	       M0_FSO_WAIT : M0_FSO_AGAIN;
}

M0_INTERNAL int m0_sns_cm_cp_next_phase_get(int phase, struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp *sns_cp;

	/*
	 * cp is used as context to make decisions. It could be NULL, when no
	 * such context is required.
	 */
	if (cp != NULL) {
		sns_cp = cp2snscp(cp);

		if (phase == M0_CCP_IO_WAIT) {
			if (cp->c_io_op == M0_CM_CP_READ)
				return  M0_CCP_XFORM;
			else
				return M0_CCP_FINI;
		}

		if (phase == M0_CCP_XFORM && cp->c_ag->cag_cp_local_nr == 1)
			return M0_CCP_WRITE;
	}

	return next[phase];
}

M0_INTERNAL void m0_sns_cm_cp_complete(struct m0_cm_cp *cp)
{
}

M0_INTERNAL void m0_cm_cp_buf_release(struct m0_cm_cp *cp)
{
	struct m0_net_buffer      *nbuf;
	struct m0_net_buffer_pool *nbp;
	uint64_t                   colour;

	m0_tl_for(cp_data_buf, &cp->c_buffers, nbuf) {
		nbp = nbuf->nb_pool;
		M0_ASSERT(nbp != NULL);
		colour = cp_home_loc_helper(cp) % nbp->nbp_colours_nr;
		m0_sns_cm_buffer_put(nbp, nbuf, colour);
		cp_data_buf_tlink_del_fini(nbuf);
	} m0_tl_endfor;
}

M0_INTERNAL void m0_sns_cm_cp_free(struct m0_cm_cp *cp)
{
	M0_PRE(cp != NULL);

	m0_cm_cp_buf_release(cp);
	m0_free(cp2snscp(cp));
}

/*
 * Dummy dud destructor function for struct m0_cm_cp_ops::co_action array
 * in-order to statisfy the m0_cm_cp_invariant.
 */
M0_INTERNAL int m0_sns_cm_cp_fini(struct m0_cm_cp *cp)
{
	return 0;
}

const struct m0_cm_cp_ops m0_sns_cm_cp_ops = {
	.co_action = {
		[M0_CCP_INIT]         = &cp_init,
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
	.co_phase_next	         = &m0_sns_cm_cp_phase_next,
	.co_invariant	         = &m0_sns_cm_cp_invariant,
	.co_home_loc_helper      = &cp_home_loc_helper,
	.co_complete	         = &m0_sns_cm_cp_complete,
	.co_free                 = &m0_sns_cm_cp_free,
};

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
