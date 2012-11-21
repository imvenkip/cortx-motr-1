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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/17/2010
 */

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
	c2_mutex_lock(&c2_net_mutex);
	rc = c2_net__domain_init(dom, xprt);
	c2_mutex_unlock(&c2_net_mutex);
	return rc;
}
C2_EXPORTED(c2_net_domain_init);

void c2_net_domain_fini(struct c2_net_domain *dom)
{
	c2_mutex_lock(&c2_net_mutex);
	c2_net__domain_fini(dom);
	c2_mutex_unlock(&c2_net_mutex);
}
C2_EXPORTED(c2_net_domain_fini);

C2_INTERNAL int c2_net__domain_init(struct c2_net_domain *dom,
				    struct c2_net_xprt *xprt)
{
	int rc;

	C2_PRE(c2_mutex_is_locked(&c2_net_mutex));
	C2_PRE(dom->nd_xprt == NULL);

	c2_mutex_init(&dom->nd_mutex);
	c2_list_init(&dom->nd_registered_bufs);
	c2_list_init(&dom->nd_tms);

	dom->nd_xprt_private = NULL;
	dom->nd_xprt = xprt;
	c2_addb_ctx_init(&dom->nd_addb, &c2_net_dom_addb_ctx, &c2_net_addb);

	rc = xprt->nx_ops->xo_dom_init(xprt, dom);
	if (rc != 0) {
		dom->nd_xprt = NULL; /* prevent call to xo_dom_fini */
		c2_net__domain_fini(dom);
		NET_ADDB_FUNCFAIL_ADD(c2_net_addb, rc);
	}
	return rc;
}

C2_INTERNAL void c2_net__domain_fini(struct c2_net_domain *dom)
{
	C2_PRE(c2_mutex_is_locked(&c2_net_mutex));
	C2_ASSERT(c2_list_is_empty(&dom->nd_tms));
	C2_ASSERT(c2_list_is_empty(&dom->nd_registered_bufs));

	if (dom->nd_xprt != NULL) {
		dom->nd_xprt->nx_ops->xo_dom_fini(dom);
		dom->nd_xprt = NULL;
	}
	c2_addb_ctx_fini(&dom->nd_addb);
	dom->nd_xprt_private = NULL;

	c2_list_fini(&dom->nd_tms);
	c2_list_fini(&dom->nd_registered_bufs);

	c2_mutex_fini(&dom->nd_mutex);
}

#define DOM_GET_PARAM(Fn, Type)				\
Type c2_net_domain_get_##Fn(struct c2_net_domain *dom)	\
{							\
	Type rc;					\
	C2_PRE(dom != NULL);				\
	c2_mutex_lock(&dom->nd_mutex);			\
	C2_PRE(dom->nd_xprt != NULL);			\
	rc = dom->nd_xprt->nx_ops->xo_get_##Fn(dom);	\
	c2_mutex_unlock(&dom->nd_mutex);		\
	return rc;					\
}

DOM_GET_PARAM(max_buffer_size, c2_bcount_t);
DOM_GET_PARAM(max_buffer_segment_size, c2_bcount_t);
DOM_GET_PARAM(max_buffer_segments, int32_t);
DOM_GET_PARAM(max_buffer_desc_size, c2_bcount_t);

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
