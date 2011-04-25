/* -*- C -*- */

#include "lib/assert.h"
#include "net/net_internal.h"

/** @}
 @addtogroup net
 @{
*/

int c2_net_end_point_create(struct c2_net_end_point   **epp,
			    struct c2_net_domain       *dom,
			    ...)
{
	int rc;
	va_list varargs;

	C2_PRE(dom != NULL );
	C2_PRE(epp != NULL );

	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	*epp = NULL;

	va_start(varargs, dom);
	rc = dom->nd_xprt->nx_ops->xo_end_point_create(epp, dom, varargs);
	va_end(varargs);

	/* either we failed or we got back a properly initialized end point
	   with reference count of at least 1
	*/
	C2_POST(rc ||
		(*epp != NULL &&
		 c2_atomic64_get(&((*epp)->nep_ref.ref_cnt)) >= 1 &&
		 (*epp)->nep_ref.release != NULL &&
		 (*epp)->nep_dom == dom));
	/* transport must have added the ep to the list */
	C2_POST(rc ||
		c2_list_contains(&dom->nd_end_points,&(*epp)->nep_dom_linkage));

	c2_mutex_unlock(&dom->nd_mutex);
	return rc;
}
C2_EXPORTED(c2_net_end_point_create);

int c2_net_end_point_get(struct c2_net_end_point *ep)
{
	struct c2_ref *ref = &ep->nep_ref;
	C2_PRE(ep != NULL);
	C2_PRE(c2_atomic64_get(&(ref->ref_cnt)) >= 1);
	c2_ref_get(ref);
	return 0;
}
C2_EXPORTED(c2_net_end_point_get);

int c2_net_end_point_put(struct c2_net_end_point *ep)
{
	struct c2_ref *ref = &ep->nep_ref;
	struct c2_net_domain *dom;
	C2_PRE(ep != NULL);
	C2_PRE(c2_atomic64_get(&(ref->ref_cnt)) >= 1);
	/* hold the domain lock to synchronize release(), if called */
	dom = ep->nep_dom;
	C2_PRE(dom != NULL );
	C2_PRE(dom->nd_xprt != NULL);
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
