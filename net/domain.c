/* -*- C -*- */

#include "lib/assert.h"
#include "net/net_internal.h"

/**
 @addtogroup net
 @{
*/

const struct c2_addb_ctx_type c2_net_dom_addb_ctx = {
	.act_name = "net-dom"
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
	c2_addb_ctx_init(&dom->nd_addb, &c2_net_dom_addb_ctx, &c2_net_addb);

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

#define DOM_GET_PARAM(Fn, Type)						\
int c2_net_domain_get_##Fn(struct c2_net_domain *dom, Type *param)	\
{									\
	int rc;								\
	C2_PRE(dom != NULL );						\
	c2_mutex_lock(&dom->nd_mutex);					\
	C2_PRE(dom->nd_xprt != NULL);					\
	rc = dom->nd_xprt->nx_ops->xo_get_##Fn(dom, param);		\
	c2_mutex_unlock(&dom->nd_mutex);				\
	return rc;							\
}									\
C2_EXPORTED(c2_net_domain_get_##Fn)

DOM_GET_PARAM(max_buffer_size, c2_bcount_t);
DOM_GET_PARAM(max_buffer_segment_size, c2_bcount_t);
DOM_GET_PARAM(max_buffer_segments, int32_t);

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
