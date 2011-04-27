/* -*- C -*- */

#include "lib/arith.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "net/bulk_emulation/sunrpc_xprt_pvt.h"
#include "net/net_internal.h"
#include "fop/fop_format_def.h"

/**
   @addtogroup bulksunrpc
   @{
 */

#include "net/bulk_emulation/sunrpc_io.ff"

static struct c2_fop_type_ops sunrpc_msg_ops = {
	.fto_execute = sunrpc_msg_handler,
};

static struct c2_fop_type_ops sunrpc_get_ops = {
	.fto_execute = sunrpc_get_handler,
};

static struct c2_fop_type_ops sunrpc_put_ops = {
	.fto_execute = sunrpc_put_handler,
};

C2_FOP_TYPE_DECLARE(sunrpc_msg,      "sunrpc_msg", 30, &sunrpc_msg_ops);
C2_FOP_TYPE_DECLARE(sunrpc_get,      "sunrpc_get", 31, &sunrpc_get_ops);
C2_FOP_TYPE_DECLARE(sunrpc_put,      "sunrpc_put", 32, &sunrpc_put_ops);

C2_FOP_TYPE_DECLARE(sunrpc_msg_resp, "sunrpc_msg reply", 35, NULL);
C2_FOP_TYPE_DECLARE(sunrpc_get_resp, "sunrpc_get reply", 36, NULL);
C2_FOP_TYPE_DECLARE(sunrpc_put_resp, "sunrpc_put reply", 37, NULL);

static struct c2_fop_type *fops[] = {
	&sunrpc_msg_fopt,
	&sunrpc_get_fopt,
	&sunrpc_put_fopt,

	&sunrpc_msg_resp_fopt,
	&sunrpc_get_resp_fopt,
	&sunrpc_put_resp_fopt,
};

static struct c2_fop_type_format *fmts[] = {
	&sunrpc_buf_desc_tfmt,
	&sunrpc_buffer_tfmt,
};

static bool sunrpc_dom_invariant(struct c2_net_domain *dom)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp = dom->nd_xprt_private;
	return (dp != NULL && dp->xd_magic == C2_NET_BULK_SUNRPC_XDP_MAGIC);
}

static bool sunrpc_ep_invariant(struct c2_net_end_point *ep)
{
	struct c2_net_bulk_mem_end_point *mep;
	struct c2_net_bulk_sunrpc_end_point *sep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	sep = container_of(mep, struct c2_net_bulk_sunrpc_end_point, xep_base);
	return (sunrpc_dom_invariant(ep->nep_dom) &&
		sep->xep_magic == C2_NET_BULK_SUNRPC_XEP_MAGIC);
}

static bool sunrpc_tm_invariant(struct c2_net_transfer_mc *tm)
{
	struct c2_net_bulk_sunrpc_tm_pvt *tp = tm->ntm_xprt_private;
	return (tp != NULL && tp->xtm_magic == C2_NET_BULK_SUNRPC_XTM_MAGIC &&
		sunrpc_dom_invariant(tm->ntm_dom));
}

static bool sunrpc_buffer_invariant(struct c2_net_buffer *nb)
{
	struct c2_net_bulk_sunrpc_buffer_pvt *sbp = nb->nb_xprt_private;
	return (sbp != NULL &&
		sbp->xsb_magic == C2_NET_BULK_SUNRPC_XBP_MAGIC &&
		sunrpc_dom_invariant(nb->nb_dom));
}

/* To reduce global symbols, yet make the code readable, we
   include other .c files with static symbols into this file.
   Dependency information must be captured in Makefile.am.

   Static functions should be declared in the private header file
   so that the order of their definiton does not matter.
*/
#include "sunrpc_xprt_ep.c"
#include "sunrpc_xprt_tm.c"
#include "sunrpc_xprt_bulk.c"
#include "sunrpc_xprt_msg.c"

void c2_sunrpc_fop_fini(void)
{
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
	c2_fop_type_format_fini_nr(fmts, ARRAY_SIZE(fmts));
}

int c2_sunrpc_fop_init(void)
{
	int result;

	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	if (result == 0) {
		result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
		/* TODO: need to call c2_fop_object_init? */
	}
	if (result != 0)
		c2_sunrpc_fop_fini();
	return result;
}

/**
   Add a work item to the work list
*/
static void sunrpc_wi_add(struct c2_net_bulk_mem_work_item *wi,
			  struct c2_net_bulk_sunrpc_tm_pvt *tp)
{
	c2_list_add_tail(&tp->xtm_base.xtm_work_list, &wi->xwi_link);
	c2_cond_signal(&tp->xtm_base.xtm_work_list_cv,
		       &tp->xtm_base.xtm_tm->ntm_mutex);
}

static int sunrpc_xo_dom_init(struct c2_net_xprt *xprt,
			      struct c2_net_domain *dom)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp;
	struct c2_net_bulk_mem_domain_pvt *bdp;
	int i;
	int rc;

	C2_PRE(dom->nd_xprt_private == NULL);
	C2_ALLOC_PTR(dp);
	if (dp == NULL)
		return -ENOMEM;
	dom->nd_xprt_private = dp;
	rc = c2_net_bulk_mem_xprt.nx_ops->xo_dom_init(xprt, dom);
	if (rc != 0)
		goto err_exit;
	bdp = &dp->xd_base;

	/* save the work functions of the base */
	for (i=0; i < C2_NET_XOP_NR; i++) {
		dp->xd_base_work_fn[i] = bdp->xd_work_fn[i];
	}

	/* override base work functions */
	bdp->xd_work_fn[C2_NET_XOP_STATE_CHANGE]    = sunrpc_wf_state_change;
	bdp->xd_work_fn[C2_NET_XOP_MSG_SEND]        = sunrpc_wf_msg_send;
	bdp->xd_work_fn[C2_NET_XOP_ACTIVE_BULK]     = sunrpc_wf_active_bulk;

	/* save the base internal subs */
	dp->xd_base_ops = bdp->xd_ops;

	/* override the base internal subs */
	bdp->xd_ops.bmo_ep_create  = &sunrpc_ep_create;
	bdp->xd_ops.bmo_ep_release = &sunrpc_xo_end_point_release;

	/* override tunable parameters */
	bdp->xd_sizeof_ep = sizeof(struct c2_net_bulk_sunrpc_end_point);
	bdp->xd_sizeof_tm_pvt = sizeof(struct c2_net_bulk_sunrpc_tm_pvt);
	bdp->xd_sizeof_buffer_pvt =
	    sizeof(struct c2_net_bulk_sunrpc_buffer_pvt);
	bdp->xd_num_tm_threads = 2;

	/* create the rpc domain (use in-mutex version of domain init) */
#ifdef __KERNEL__
	rc = c2_net__domain_init(&dp->xd_rpc_dom, &c2_net_ksunrpc_xprt);
#else
	rc = c2_net__domain_init(&dp->xd_rpc_dom, &c2_net_usunrpc_xprt);
#endif
	if (rc != 0)
		goto err_exit;

	/* initialize the ep mutex if needed */
	if (sunrpc_ep_mutex_initialized == 0) {
		c2_mutex_init(&sunrpc_ep_mutex);
	}
	sunrpc_ep_mutex_initialized++;

	C2_POST(sunrpc_dom_invariant(dom));
	rc = 0;
 err_exit:
	if (rc != 0) {
		if (dp != NULL)
			c2_free(dp);
	}
	return rc;
}

static void sunrpc_xo_dom_fini(struct c2_net_domain *dom)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp = dom->nd_xprt_private;
	c2_net__domain_fini(&dp->xd_rpc_dom);
	c2_net_bulk_mem_xprt.nx_ops->xo_dom_fini(dom);
	/* remove the ep mutex when done */
	C2_ASSERT(sunrpc_ep_mutex_initialized >= 1);
	--sunrpc_ep_mutex_initialized;
	if (sunrpc_ep_mutex_initialized == 0) {
		c2_mutex_fini(&sunrpc_ep_mutex);
	}
}

static c2_bcount_t sunrpc_xo_get_max_buffer_size(struct c2_net_domain *dom)
{
	return c2_net_bulk_mem_xprt.nx_ops->xo_get_max_buffer_size(dom);
}

static c2_bcount_t sunrpc_xo_get_max_buffer_segment_size(struct c2_net_domain
							 *dom)
{
	return c2_net_bulk_mem_xprt.nx_ops->xo_get_max_buffer_segment_size(dom);
}

static int32_t sunrpc_xo_get_max_buffer_segments(struct c2_net_domain *dom)
{
	return c2_net_bulk_mem_xprt.nx_ops->xo_get_max_buffer_segments(dom);
}

static int sunrpc_xo_end_point_create(struct c2_net_end_point **epp,
				      struct c2_net_domain *dom,
				      va_list varargs)
{
	int rc = c2_net_bulk_mem_xprt.nx_ops->xo_end_point_create(epp, dom,
								  varargs);
	if (rc == 0)
		C2_POST(sunrpc_ep_invariant(*epp));
	return rc;
}

/**
   Check buffer size limits.
 */
static bool sunrpc_buffer_in_bounds(struct c2_net_buffer *nb)
{
	struct c2_vec *v = &nb->nb_buffer.ov_vec;
	if (v->v_nr > sunrpc_xo_get_max_buffer_segments(nb->nb_dom))
		return false;
	int i;
	c2_bcount_t len=0;
	for (i=0; i < v->v_nr; i++) {
		if (v->v_count[i] >
		    sunrpc_xo_get_max_buffer_segment_size(nb->nb_dom))
			return false;
		len += v->v_count[i];
	}
	if (len > sunrpc_xo_get_max_buffer_size(nb->nb_dom))
		return false;
	return true;
}

static int sunrpc_xo_buf_register(struct c2_net_buffer *nb)
{
	int rc;
	C2_PRE(sunrpc_dom_invariant(nb->nb_dom));
	if (!sunrpc_buffer_in_bounds(nb))
		return -EFBIG;
	rc = c2_net_bulk_mem_xprt.nx_ops->xo_buf_register(nb);
	if (rc == 0) {
		struct c2_net_bulk_sunrpc_buffer_pvt *sbp =
			nb->nb_xprt_private;
		C2_POST(sbp != NULL);
		sbp->xsb_magic = C2_NET_BULK_SUNRPC_XBP_MAGIC;
		C2_POST(sunrpc_buffer_invariant(nb));
	}
	return rc;
}

static int sunrpc_xo_buf_deregister(struct c2_net_buffer *nb)
{
	C2_PRE(sunrpc_buffer_invariant(nb));
	return c2_net_bulk_mem_xprt.nx_ops->xo_buf_deregister(nb);
}

static int sunrpc_xo_buf_add(struct c2_net_buffer *nb)
{
	struct c2_net_transfer_mc *tm = nb->nb_tm;
	struct c2_net_bulk_sunrpc_buffer_pvt *bp = nb->nb_xprt_private;
	int rc;

	C2_PRE(sunrpc_buffer_invariant(nb));
	C2_PRE(sunrpc_tm_invariant(tm));
	switch (nb->nb_qtype) {
	case C2_NET_QT_PASSIVE_BULK_SEND:
		rc = sunrpc_desc_create(&nb->nb_desc, nb->nb_ep, tm,
					nb->nb_qtype, nb->nb_length,
					bp->xsb_base.xb_buf_id);
		if (rc != 0)
			return rc;
	default:
		return -ENOSYS;
	}

	return -ENOSYS;
}

static int sunrpc_xo_buf_del(struct c2_net_buffer *nb)
{
	C2_PRE(sunrpc_buffer_invariant(nb));
	C2_PRE(sunrpc_tm_invariant(nb->nb_tm));
	return c2_net_bulk_mem_xprt.nx_ops->xo_buf_del(nb);
}

static int sunrpc_xo_tm_init(struct c2_net_transfer_mc *tm)
{
	int rc = c2_net_bulk_mem_xprt.nx_ops->xo_tm_init(tm);
	if (rc == 0) {
		struct c2_net_bulk_sunrpc_tm_pvt *tp = tm->ntm_xprt_private;
		tp->xtm_magic = C2_NET_BULK_SUNRPC_XTM_MAGIC;
		C2_POST(sunrpc_tm_invariant(tm));
	}
	return rc;
}

static int sunrpc_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
	C2_PRE(sunrpc_tm_invariant(tm));
	return c2_net_bulk_mem_xprt.nx_ops->xo_tm_fini(tm);
}

static int sunrpc_xo_tm_start(struct c2_net_transfer_mc *tm)
{
	C2_PRE(sunrpc_tm_invariant(tm));
	return c2_net_bulk_mem_xprt.nx_ops->xo_tm_start(tm);
}

static int sunrpc_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	C2_PRE(sunrpc_tm_invariant(tm));
	return c2_net_bulk_mem_xprt.nx_ops->xo_tm_stop(tm, cancel);
}

static const struct c2_net_xprt_ops sunrpc_xo_xprt_ops = {
	.xo_dom_init                    = sunrpc_xo_dom_init,
	.xo_dom_fini                    = sunrpc_xo_dom_fini,
	.xo_get_max_buffer_size         = sunrpc_xo_get_max_buffer_size,
	.xo_get_max_buffer_segment_size = sunrpc_xo_get_max_buffer_segment_size,
	.xo_get_max_buffer_segments     = sunrpc_xo_get_max_buffer_segments,
	.xo_end_point_create            = sunrpc_xo_end_point_create,
	.xo_buf_register                = sunrpc_xo_buf_register,
	.xo_buf_deregister              = sunrpc_xo_buf_deregister,
	.xo_buf_add                     = sunrpc_xo_buf_add,
	.xo_buf_del                     = sunrpc_xo_buf_del,
	.xo_tm_init                     = sunrpc_xo_tm_init,
	.xo_tm_fini                     = sunrpc_xo_tm_fini,
	.xo_tm_start                    = sunrpc_xo_tm_start,
	.xo_tm_stop                     = sunrpc_xo_tm_stop,
};

struct c2_net_xprt c2_net_bulk_sunrpc_xprt = {
	.nx_name = "bulk-sunrpc",
	.nx_ops  = &sunrpc_xo_xprt_ops
};

/**
   @} bulksunrpc
*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
