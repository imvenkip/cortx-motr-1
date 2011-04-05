/* -*- C -*- */

#include "lib/assert.h"
#include "net/net_internal.h"

/** @}
 @addtogroup net
 @{
*/

const struct c2_addb_ctx_type c2_net_dom_addb_ctx = {
	.act_name = "net-dom"
};

const struct c2_addb_loc c2_net_addb_loc = {
	.al_name = "net"
};

int c2_net_domain_init(struct c2_net_domain *dom, struct c2_net_xprt *xprt)
{
	int rc;

	/* begin deprecated */
	c2_list_init(&dom->nd_conn);
	c2_list_init(&dom->nd_service);
	c2_rwlock_init(&dom->nd_lock);
        c2_net_domain_stats_init(dom);
	/* end deprecated */

	c2_mutex_init(&dom->nd_mutex);
	c2_list_init(&dom->nd_end_points);
	c2_list_init(&dom->nd_registered_bufs);
	c2_list_init(&dom->nd_tms);
	
	dom->nd_xprt_private = NULL;
 	dom->nd_xprt = xprt;
	c2_addb_ctx_init(&dom->nd_addb, &c2_net_dom_addb_ctx,
			 &c2_addb_global_ctx);

	/* must hold the mutex when calling xo_ */
	c2_mutex_lock(&dom->nd_mutex);
	rc = xprt->nx_ops->xo_dom_init(xprt, dom);
	if ( rc ) {
		dom->nd_xprt = NULL;
	}
	c2_mutex_unlock(&dom->nd_mutex);
	if ( rc ) {
		c2_net_domain_fini(dom);
	}
	return rc;
}
C2_EXPORTED(c2_net_domain_init);

void c2_net_domain_fini(struct c2_net_domain *dom)
{
	c2_mutex_lock(&dom->nd_mutex);

	C2_ASSERT(c2_list_is_empty(&dom->nd_tms));
	C2_ASSERT(c2_list_is_empty(&dom->nd_registered_bufs));
	C2_ASSERT(c2_list_is_empty(&dom->nd_end_points));

	if ( dom->nd_xprt ) {
		dom->nd_xprt->nx_ops->xo_dom_fini(dom);
	}
	c2_addb_ctx_fini(&dom->nd_addb);
	dom->nd_xprt = NULL;
	dom->nd_xprt_private = NULL;

	c2_list_fini(&dom->nd_tms);
	c2_list_fini(&dom->nd_registered_bufs);
	c2_list_fini(&dom->nd_end_points);

	c2_mutex_unlock(&dom->nd_mutex);
	c2_mutex_fini(&dom->nd_mutex);

	/* begin deprecated */
        c2_net_domain_stats_fini(dom);
	c2_rwlock_fini(&dom->nd_lock);
	c2_list_fini(&dom->nd_service);
	c2_list_fini(&dom->nd_conn);
	/* end deprecated */
}
C2_EXPORTED(c2_net_domain_fini);

int c2_net_domain_get_param(struct c2_net_domain *dom, int param, ...)
{
	int rc;
	va_list varargs;

	c2_mutex_lock(&dom->nd_mutex);
	C2_ASSERT(dom->nd_xprt != NULL);

	va_start(varargs, param);
	rc = dom->nd_xprt->nx_ops->xo_param_get(dom, param, varargs);
	va_end(varargs);

	c2_mutex_unlock(&dom->nd_mutex);
	return rc;
}

int c2_net_domain_get_max_buffer_size(struct c2_net_domain *dom,
				      c2_bcount_t *size)
{
	return c2_net_domain_get_param(dom, 
				       C2_NET_PARAM_MAX_BUFFER_SIZE,
				       size);
}
C2_EXPORTED(c2_net_domain_get_max_buffer_size);

int c2_net_domain_get_max_buffer_segments(struct c2_net_domain *dom,
					  int32_t *num_segs)
{
	return c2_net_domain_get_param(dom, 
				       C2_NET_PARAM_MAX_BUFFER_SEGMENTS,
				       num_segs);
}
C2_EXPORTED(c2_net_domain_get_max_buffer_segments);

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
