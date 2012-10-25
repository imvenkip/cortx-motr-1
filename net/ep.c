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

C2_INTERNAL bool c2_net__ep_invariant(struct c2_net_end_point *ep,
				      struct c2_net_transfer_mc *tm,
				      bool under_tm_mutex)
{
	if (ep == NULL)
		return false;
	if (c2_atomic64_get(&ep->nep_ref.ref_cnt) <= 0)
		return false;
	if (ep->nep_ref.release == NULL)
		return false;
	if (ep->nep_tm != tm)
		return false;
	if (ep->nep_addr == NULL)
		return false;
	if (under_tm_mutex &&
	    !c2_list_contains(&tm->ntm_end_points, &ep->nep_tm_linkage))
		return false;
	return true;
}

C2_INTERNAL int c2_net_end_point_create(struct c2_net_end_point **epp,
					struct c2_net_transfer_mc *tm,
					const char *addr)
{
	int rc;
	struct c2_net_domain *dom;

	C2_PRE(tm != NULL && tm->ntm_state == C2_NET_TM_STARTED);
	C2_PRE(epp != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

	dom = tm->ntm_dom;
	C2_PRE(dom->nd_xprt != NULL);

	c2_mutex_lock(&tm->ntm_mutex);

	*epp = NULL;

	rc = dom->nd_xprt->nx_ops->xo_end_point_create(epp, tm, addr);

	/* either we failed or we got back a properly initialized end point
	   with reference count of at least 1
	*/
	C2_POST(ergo(rc == 0, c2_net__ep_invariant(*epp, tm, true)));

	if (rc != 0)
		NET_ADDB_FUNCFAIL_ADD(tm->ntm_addb, rc);
	c2_mutex_unlock(&tm->ntm_mutex);
	return rc;
}
C2_EXPORTED(c2_net_end_point_create);

C2_INTERNAL void c2_net_end_point_get(struct c2_net_end_point *ep)
{
	struct c2_ref *ref = &ep->nep_ref;
	C2_PRE(ep != NULL);
	C2_PRE(c2_atomic64_get(&ref->ref_cnt) >= 1);
	c2_ref_get(ref);
	return;
}
C2_EXPORTED(c2_net_end_point_get);

C2_INTERNAL void c2_net_end_point_put(struct c2_net_end_point *ep)
{
	struct c2_ref *ref = &ep->nep_ref;
	struct c2_net_transfer_mc *tm;
	C2_PRE(ep != NULL);
	C2_PRE(c2_atomic64_get(&ref->ref_cnt) >= 1);
	tm = ep->nep_tm;
	C2_PRE(tm != NULL);
	/* hold the transfer machine lock to synchronize release(), if called */
	c2_mutex_lock(&tm->ntm_mutex);
	c2_ref_put(ref);
	c2_mutex_unlock(&tm->ntm_mutex);
	return;
}
C2_EXPORTED(c2_net_end_point_put);

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
