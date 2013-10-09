/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 10/22/2010
 */

#include "lib/errno.h"              /* EALREADY, EAGAIN */
#include "lib/arith.h"              /* M0_3WAY */
#include "dtm/verno.h"
#include "fol/fol.h"

/**
   @addtogroup dtm
   @{
 */

M0_INTERNAL int m0_verno_cmp(const struct m0_verno *vn0,
			     const struct m0_verno *vn1)
{
	//M0_ASSERT(m0_verno_cmp_invariant(vn0, vn1));

	//return m0_lsn_cmp(vn0->vn_lsn, vn1->vn_lsn);
	return M0_3WAY(vn0->vn_vc, vn1->vn_vc);
}

M0_INTERNAL int m0_verno_is_redoable(const struct m0_verno *unit,
				     const struct m0_verno *before_update,
				     bool total)
{
	int result;
	int cmp;

	// XXX don't need lsn for now
	//M0_ASSERT(m0_verno_cmp_invariant(unit, before_update));

	cmp = m0_verno_cmp(unit, before_update);
	if (cmp < 0)
		result = total ? 0 : -EAGAIN;
	else if (cmp > 0)
		result = -EALREADY;
	else
		result = 0;
	return result;
}

M0_INTERNAL int m0_verno_is_undoable(const struct m0_verno *unit,
				     const struct m0_verno *before_update,
				     bool total)
{
	int result;
	int cmp;

	// XXX don't require lsn for now
	//M0_ASSERT(m0_verno_cmp_invariant(unit, before_update));

	cmp = m0_verno_cmp(unit, before_update);
	if (cmp <= 0)
		result = -EALREADY;
	else if (total)
		result = 0;
	else
		result = before_update->vn_vc + 1 == unit->vn_vc ? 0 : -EAGAIN;
	return result;
}
M0_INTERNAL int m0_verno_cmp_invariant(const struct m0_verno *vn0,
				       const struct m0_verno *vn1)
{
	return m0_lsn_cmp(vn0->vn_lsn, vn1->vn_lsn) ==
		M0_3WAY(vn0->vn_vc, vn1->vn_vc);
}

/** @} end of dtm group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
