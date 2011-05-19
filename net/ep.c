/* -*- C -*- */

#include "lib/assert.h"
#include "lib/cdefs.h"
#include "net/net_internal.h"

/** @}
 @addtogroup net
 @{
*/

bool c2_net__ep_invariant(struct c2_net_end_point *ep,
			  struct c2_net_domain    *dom,
			  bool                     under_dom_mutex)
{
	if (ep == NULL)
		return false;
	if (c2_atomic64_get(&ep->nep_ref.ref_cnt) <= 0)
		return false;
	if (ep->nep_ref.release == NULL)
		return false;
	if (ep->nep_dom != dom)
		return false;
	if (ep->nep_addr == NULL)
		return false;
	if (under_dom_mutex &&
	    !c2_list_contains(&dom->nd_end_points, &ep->nep_dom_linkage))
		return false;
	return true;
}

int c2_net_end_point_create(struct c2_net_end_point **epp,
			    struct c2_net_domain     *dom,
			    const char               *addr)
{
	int rc;

	C2_PRE(dom != NULL);
	C2_PRE(epp != NULL);

	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	*epp = NULL;

	rc = dom->nd_xprt->nx_ops->xo_end_point_create(epp, dom, addr);

	/* either we failed or we got back a properly initialized end point
	   with reference count of at least 1
	*/
	C2_POST(ergo(rc == 0, c2_net__ep_invariant(*epp, dom, true)));

	c2_mutex_unlock(&dom->nd_mutex);
	return rc;
}
C2_EXPORTED(c2_net_end_point_create);

void c2_net_end_point_get(struct c2_net_end_point *ep)
{
	struct c2_ref *ref = &ep->nep_ref;
	C2_PRE(ep != NULL);
	C2_PRE(c2_atomic64_get(&ref->ref_cnt) >= 1);
	c2_ref_get(ref);
	return;
}
C2_EXPORTED(c2_net_end_point_get);

int c2_net_end_point_put(struct c2_net_end_point *ep)
{
	struct c2_ref *ref = &ep->nep_ref;
	struct c2_net_domain *dom;
	C2_PRE(ep != NULL);
	C2_PRE(c2_atomic64_get(&ref->ref_cnt) >= 1);
	dom = ep->nep_dom;
	C2_PRE(dom != NULL );
	C2_PRE(dom->nd_xprt != NULL);
	/* hold the domain lock to synchronize release(), if called */
	c2_mutex_lock(&dom->nd_mutex);
	c2_ref_put(ref);
	c2_mutex_unlock(&dom->nd_mutex);
	return 0;
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
