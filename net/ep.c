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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 04/04/2011
 */

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/cdefs.h"
#include "lib/finject.h"
#include "net/net_internal.h"

/** @}
 @addtogroup net
 @{
*/

M0_INTERNAL bool m0_net__ep_invariant(struct m0_net_end_point *ep,
				      struct m0_net_transfer_mc *tm,
				      bool under_tm_mutex)
{
	if (ep == NULL)
		return false;
	if (m0_atomic64_get(&ep->nep_ref.ref_cnt) <= 0)
		return false;
	if (ep->nep_ref.release == NULL)
		return false;
	if (ep->nep_tm != tm)
		return false;
	if (ep->nep_addr == NULL)
		return false;
	if (under_tm_mutex &&
	    !m0_list_contains(&tm->ntm_end_points, &ep->nep_tm_linkage))
		return false;
	return true;
}

M0_INTERNAL int m0_net_end_point_create(struct m0_net_end_point **epp,
					struct m0_net_transfer_mc *tm,
					const char *addr)
{
	int rc;
	struct m0_net_domain *dom;

	M0_PRE(tm != NULL && tm->ntm_state == M0_NET_TM_STARTED);
	M0_PRE(epp != NULL);

	if (M0_FI_ENABLED("fake_error"))
		return -EINVAL;

	dom = tm->ntm_dom;
	M0_PRE(dom->nd_xprt != NULL);

	m0_mutex_lock(&tm->ntm_mutex);

	*epp = NULL;

	rc = dom->nd_xprt->nx_ops->xo_end_point_create(epp, tm, addr);

	/* either we failed or we got back a properly initialized end point
	   with reference count of at least 1
	*/
	M0_POST(ergo(rc == 0, m0_net__ep_invariant(*epp, tm, true)));

	if (rc != 0)
		NET_ADDB_FUNCFAIL_ADD(tm->ntm_addb, rc);
	m0_mutex_unlock(&tm->ntm_mutex);
	return rc;
}
M0_EXPORTED(m0_net_end_point_create);

M0_INTERNAL void m0_net_end_point_get(struct m0_net_end_point *ep)
{
	struct m0_ref *ref = &ep->nep_ref;
	M0_PRE(ep != NULL);
	M0_PRE(m0_atomic64_get(&ref->ref_cnt) >= 1);
	m0_ref_get(ref);
	return;
}
M0_EXPORTED(m0_net_end_point_get);

void m0_net_end_point_put(struct m0_net_end_point *ep)
{
	struct m0_ref *ref = &ep->nep_ref;
	struct m0_net_transfer_mc *tm;
	M0_PRE(ep != NULL);
	M0_PRE(m0_atomic64_get(&ref->ref_cnt) >= 1);
	tm = ep->nep_tm;
	M0_PRE(tm != NULL);
	/* hold the transfer machine lock to synchronize release(), if called */
	m0_mutex_lock(&tm->ntm_mutex);
	m0_ref_put(ref);
	m0_mutex_unlock(&tm->ntm_mutex);
	return;
}
M0_EXPORTED(m0_net_end_point_put);

/** @} end of net group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
