/* -*- C -*- */
/*
 * COPYRIGHT 2018 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 26-Apr-2018
 */

/**
 * @addtogroup be
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/reg.h"

#include "be/op.h"              /* M0_BE_OP_SYNC */
#include "be/seg.h"             /* m0_be_reg */
#include "be/paged.h"           /* m0_be_pd_reg_get */
#include "be/tx.h"              /* m0_be_tx */
#include "be/domain.h"          /* m0_be_domain */

static struct m0_be_pd *be_reg_pd_get(const struct m0_be_reg *reg,
                                      struct m0_be_tx        *tx)
{
	return reg->br_seg != NULL ? reg->br_seg->bs_pd : &tx->t_dom->bd_pd;
}

M0_INTERNAL void m0_be_reg_get(const struct m0_be_reg *reg, struct m0_be_tx *tx)
{
	M0_BE_OP_SYNC(op, m0_be_pd_reg_get(be_reg_pd_get(reg, tx), reg, &op));
}

M0_INTERNAL void m0_be_reg_put(const struct m0_be_reg *reg, struct m0_be_tx *tx)
{
	m0_be_pd_reg_put(be_reg_pd_get(reg, tx), reg);
}


#undef M0_TRACE_SUBSYSTEM

/** @} end of be group */

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
