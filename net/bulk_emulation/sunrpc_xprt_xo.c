/* -*- C -*- */

#include "lib/arith.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/rwlock.h"
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
	&sunrpc_ep_tfmt,
	&sunrpc_buf_desc_tfmt,
	&sunrpc_buffer_tfmt,
};

static struct c2_rwlock      sunrpc_server_lock;
static struct c2_mutex       sunrpc_server_mutex;
static struct c2_list        sunrpc_server_tms;
static struct c2_net_domain  sunrpc_server_domain;
static struct c2_service_id  sunrpc_server_id;
static struct c2_service     sunrpc_server_service;
static uint32_t              sunrpc_server_active_tms = 0;
static struct c2_mutex       sunrpc_tm_start_mutex;

/* forward reference */
static const struct c2_net_bulk_mem_ops sunrpc_xprt_methods;

static bool sunrpc_dom_invariant(const struct c2_net_domain *dom)
{
	const struct c2_net_bulk_sunrpc_domain_pvt *dp = sunrpc_dom_to_pvt(dom);
	return (dp != NULL && dp->xd_magic == C2_NET_BULK_SUNRPC_XDP_MAGIC &&
		mem_dom_to_pvt(dom) == &dp->xd_base);
}

static bool sunrpc_ep_invariant(const struct c2_net_end_point *ep)
{
	const struct c2_net_bulk_sunrpc_end_point *sep = sunrpc_ep_to_pvt(ep);
	return (sunrpc_dom_invariant(ep->nep_dom) &&
		sep->xep_magic == C2_NET_BULK_SUNRPC_XEP_MAGIC &&
		mem_ep_to_pvt(ep) == &sep->xep_base &&
		sep->xep_sid_valid);
}

static bool sunrpc_tm_invariant(const struct c2_net_transfer_mc *tm)
{
	const struct c2_net_bulk_sunrpc_tm_pvt *tp = sunrpc_tm_to_pvt(tm);
	return (tp != NULL && tp->xtm_magic == C2_NET_BULK_SUNRPC_XTM_MAGIC &&
		mem_tm_to_pvt(tm) == &tp->xtm_base &&
		sunrpc_dom_invariant(tm->ntm_dom));
}

static bool sunrpc_buffer_invariant(const struct c2_net_buffer *nb)
{
	const struct c2_net_bulk_sunrpc_buffer_pvt *sbp =
		sunrpc_buffer_to_pvt(nb);
	return (sbp != NULL &&
		sbp->xsb_magic == C2_NET_BULK_SUNRPC_XBP_MAGIC &&
		mem_buffer_to_pvt(nb) == &sbp->xsb_base &&
		sunrpc_dom_invariant(nb->nb_dom));
}

/* To reduce global symbols, yet make the code readable, we
   include other .c files with static symbols into this file.
   Dependency information must be captured in Makefile.am.

   Static functions should be declared in the private header file
   so that the order of their definition does not matter.
*/
#include "sunrpc_xprt_ep.c"
#include "sunrpc_xprt_tm.c"
#include "sunrpc_xprt_bulk.c"
#include "sunrpc_xprt_msg.c"

/**
   Transport finalization subroutine called from c2_fini().
 */
void c2_sunrpc_fop_fini(void)
{
	c2_mutex_fini(&sunrpc_tm_start_mutex);
	c2_list_fini(&sunrpc_server_tms);
	c2_mutex_fini(&sunrpc_server_mutex);
	c2_rwlock_fini(&sunrpc_server_lock);
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
	c2_fop_type_format_fini_nr(fmts, ARRAY_SIZE(fmts));
}
C2_EXPORTED(c2_sunrpc_fop_fini);

/**
   Transport initialization subroutine called from c2_init().
 */
int c2_sunrpc_fop_init(void)
{
	int result;

	c2_rwlock_init(&sunrpc_server_lock);
	c2_mutex_init(&sunrpc_server_mutex);
	c2_list_init(&sunrpc_server_tms);
	c2_mutex_init(&sunrpc_tm_start_mutex);

	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	if (result == 0)
		result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
	if (result != 0)
		c2_sunrpc_fop_fini();
	return result;
}
C2_EXPORTED(c2_sunrpc_fop_init);

/**
   Search the list of existing transfer machines for the one whose end point
   has the given service ID and return it.
   @param sid service ID of the desired transfer machine
   @retval NULL transfer machine not found
   @retval !NULL transfer machine pointer, the transfer machine mutex
   is locked.
 */
static struct c2_net_transfer_mc *sunrpc_find_tm(uint32_t sid)
{
	struct c2_net_bulk_sunrpc_tm_pvt *tp;
	struct c2_net_transfer_mc *ret = NULL;

	c2_rwlock_read_lock(&sunrpc_server_lock);
	c2_list_for_each_entry(&sunrpc_server_tms, tp,
			       struct c2_net_bulk_sunrpc_tm_pvt,
			       xtm_tm_linkage) {
		struct c2_net_transfer_mc *tm = tp->xtm_base.xtm_tm;
		struct c2_net_end_point *ep;
		struct c2_net_bulk_mem_end_point *mep;

		c2_mutex_lock(&tm->ntm_mutex);
		C2_ASSERT(c2_net__tm_invariant(tm));

		ep = tm->ntm_ep;
		if (ep == NULL || tm->ntm_state != C2_NET_TM_STARTED) {
			c2_mutex_unlock(&tm->ntm_mutex);
			continue;
		}

		mep = mem_ep_to_pvt(ep);
		if (mep->xep_service_id == sid) {
			/* leave mutex locked */
			ret = tm;
			break;
		}
		c2_mutex_unlock(&tm->ntm_mutex);
	}
	c2_rwlock_read_unlock(&sunrpc_server_lock);

	return ret;
}

/**
   Inherit the wi add method.
*/
static void sunrpc_wi_add(struct c2_net_bulk_mem_work_item *wi,
			  struct c2_net_bulk_mem_tm_pvt *tp)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp;
	C2_PRE(sunrpc_dom_invariant(tp->xtm_tm->ntm_dom));
	dp = sunrpc_dom_to_pvt(tp->xtm_tm->ntm_dom);
	(*dp->xd_base_ops->bmo_wi_add)(wi, tp);
}

/**
   Inherit the post buffer event method.
 */
static void sunrpc_wi_post_buffer_event(struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_buffer *nb = mem_wi_to_buffer(wi);
	struct c2_net_bulk_sunrpc_domain_pvt *dp;
	C2_PRE(sunrpc_buffer_invariant(nb));
	dp = sunrpc_dom_to_pvt(nb->nb_dom);
	(*dp->xd_base_ops->bmo_wi_post_buffer_event)(wi);
}

static int sunrpc_xo_dom_init(struct c2_net_xprt *xprt,
			      struct c2_net_domain *dom)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp;
	struct c2_net_bulk_mem_domain_pvt *bdp;
	int rc;

	C2_PRE(dom->nd_xprt_private == NULL);
	C2_ALLOC_PTR(dp);
	if (dp == NULL)
		return -ENOMEM;
	bdp = &dp->xd_base;
	dom->nd_xprt_private = bdp; /* base pointer required */
	rc = c2_net_bulk_mem_xprt.nx_ops->xo_dom_init(xprt, dom);
	if (rc != 0)
		goto err_exit;
	C2_ASSERT(mem_dom_to_pvt(dom) == bdp);
	C2_ASSERT(sunrpc_dom_to_pvt(dom) == dp);

	/* save the base internal subs */
	dp->xd_base_ops = bdp->xd_ops;

	/* Replace the base ops pointer to override some of the methods. */
	bdp->xd_ops = &sunrpc_xprt_methods;

	/* override tunable parameters */
	bdp->xd_addr_tuples       = 3;
	bdp->xd_num_tm_threads    = C2_NET_BULK_SUNRPC_TM_THREADS;

	/* create the rpc domain (use in-mutex version of domain init) */
#ifdef __KERNEL__
	rc = c2_net__domain_init(&dp->xd_rpc_dom, &c2_net_ksunrpc_xprt);
#else
	rc = c2_net__domain_init(&dp->xd_rpc_dom, &c2_net_usunrpc_minimal_xprt);
#endif
	if (rc == 0) {
		dp->xd_magic = C2_NET_BULK_SUNRPC_XDP_MAGIC;
		C2_POST(sunrpc_dom_invariant(dom));
	} else {
		/* got to fini the base */
		c2_net_bulk_mem_xprt.nx_ops->xo_dom_fini(dom);
		C2_POST(mem_dom_to_pvt(dom) == bdp);
	}
 err_exit:
	if (rc != 0 && dp != NULL) {
		c2_free(dp);
		dom->nd_xprt_private = NULL;
	}
	return rc;
}

static void sunrpc_xo_dom_fini(struct c2_net_domain *dom)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp = sunrpc_dom_to_pvt(dom);
	C2_PRE(sunrpc_dom_invariant(dom));

	/* fini the RPC domain (use in-mutex version of domain fini) */
	c2_net__domain_fini(&dp->xd_rpc_dom);

	/* fini the base */
	c2_net_bulk_mem_xprt.nx_ops->xo_dom_fini(dom);

	/* free the pvt structure */
	C2_ASSERT(mem_dom_to_pvt(dom) == &dp->xd_base);
	dp->xd_magic = 0;
	c2_free(dp);
	dom->nd_xprt_private = NULL;
}

static c2_bcount_t sunrpc_xo_get_max_buffer_size(
					      const struct c2_net_domain *dom)
{
	return C2_NET_BULK_SUNRPC_MAX_BUFFER_SIZE;
}

static c2_bcount_t sunrpc_xo_get_max_buffer_segment_size(
					      const struct c2_net_domain *dom)
{
	return C2_NET_BULK_SUNRPC_MAX_SEGMENT_SIZE;
}

static int32_t sunrpc_xo_get_max_buffer_segments(
					      const struct c2_net_domain *dom)
{
	return C2_NET_BULK_SUNRPC_MAX_BUFFER_SEGMENTS;
}

static int sunrpc_xo_end_point_create(struct c2_net_end_point **epp,
				      struct c2_net_domain *dom,
				      const char *addr)
{
	/* calls sunrpc_ep_create */
	return c2_net_bulk_mem_xprt.nx_ops->xo_end_point_create(epp, dom, addr);
}

/**
   Check buffer size limits.
 */
static bool sunrpc_buffer_in_bounds(const struct c2_net_buffer *nb)
{
	const struct c2_vec *v = &nb->nb_buffer.ov_vec;
	int i;
	c2_bcount_t len = 0;

	if (v->v_nr > C2_NET_BULK_SUNRPC_MAX_BUFFER_SEGMENTS)
		return false;
	for (i = 0; i < v->v_nr; ++i) {
		if (v->v_count[i] > C2_NET_BULK_SUNRPC_MAX_SEGMENT_SIZE)
			return false;
		len += v->v_count[i];
	}
	if (len > C2_NET_BULK_SUNRPC_MAX_BUFFER_SIZE)
		return false;
	return true;
}

static int sunrpc_xo_buf_register(struct c2_net_buffer *nb)
{
	int rc;
	struct c2_net_bulk_sunrpc_buffer_pvt *sbp;
	C2_PRE(sunrpc_dom_invariant(nb->nb_dom));
	/* allocate the buffer private data */
	C2_PRE(nb->nb_xprt_private == NULL);
	C2_ALLOC_PTR(sbp);
	if (sbp == NULL)
		return -ENOMEM;
	nb->nb_xprt_private = &sbp->xsb_base; /* base pointer required */
	/* register with the base - calls sunrpc_buffer_in_bounds */
	rc = c2_net_bulk_mem_xprt.nx_ops->xo_buf_register(nb);
	if (rc == 0) {
		C2_ASSERT(mem_buffer_to_pvt(nb) == &sbp->xsb_base);
		C2_ASSERT(sunrpc_buffer_to_pvt(nb) == sbp);
		sbp->xsb_magic = C2_NET_BULK_SUNRPC_XBP_MAGIC;
		C2_POST(sunrpc_buffer_invariant(nb));
	} else {
		c2_free(sbp);
	}
	return rc;
}

static int sunrpc_xo_buf_deregister(struct c2_net_buffer *nb)
{
	int rc;
	struct c2_net_bulk_sunrpc_buffer_pvt *sbp = sunrpc_buffer_to_pvt(nb);
	C2_PRE(sunrpc_buffer_invariant(nb));
	rc = c2_net_bulk_mem_xprt.nx_ops->xo_buf_deregister(nb);
	if (rc == 0) {
		/* free the private data */
		C2_ASSERT(mem_buffer_to_pvt(nb) == &sbp->xsb_base);
		sbp->xsb_magic = 0;
		c2_free(sbp);
		nb->nb_xprt_private = NULL;
	}
	return rc;
}

static int sunrpc_xo_buf_add(struct c2_net_buffer *nb)
{
	C2_PRE(sunrpc_buffer_invariant(nb));
	C2_PRE(sunrpc_tm_invariant(nb->nb_tm));
	/* may call sunrpc_desc_create */
	return c2_net_bulk_mem_xprt.nx_ops->xo_buf_add(nb);
}

static void sunrpc_xo_buf_del(struct c2_net_buffer *nb)
{
	C2_PRE(sunrpc_buffer_invariant(nb));
	C2_PRE(sunrpc_tm_invariant(nb->nb_tm));
	c2_net_bulk_mem_xprt.nx_ops->xo_buf_del(nb);
	return;
}

static int sunrpc_xo_tm_init(struct c2_net_transfer_mc *tm)
{
	struct c2_net_bulk_sunrpc_tm_pvt *tp;
	int rc;
	/* allocate the TM private data */
	C2_ALLOC_PTR(tp);
	if (tp == NULL)
		return -ENOMEM;
	tm->ntm_xprt_private = &tp->xtm_base; /* base pointer required */
	c2_rwlock_write_lock(&sunrpc_server_lock);
	rc = c2_net_bulk_mem_xprt.nx_ops->xo_tm_init(tm);
	if (rc == 0) {
		C2_ASSERT(mem_tm_to_pvt(tm) == &tp->xtm_base);
		C2_ASSERT(sunrpc_tm_to_pvt(tm) == tp);
		tp->xtm_magic = C2_NET_BULK_SUNRPC_XTM_MAGIC;
		c2_list_add_tail(&sunrpc_server_tms, &tp->xtm_tm_linkage);
		C2_POST(sunrpc_tm_invariant(tm));
	} else {
		c2_free(tp);
	}
	c2_rwlock_write_unlock(&sunrpc_server_lock);
	return rc;
}

static void sunrpc_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
	struct c2_net_bulk_sunrpc_tm_pvt *tp = sunrpc_tm_to_pvt(tm);
	C2_PRE(sunrpc_tm_invariant(tm));

	c2_rwlock_write_lock(&sunrpc_server_lock);
	c2_list_del(&tp->xtm_tm_linkage);
	c2_rwlock_write_unlock(&sunrpc_server_lock);
	c2_net_bulk_mem_xprt.nx_ops->xo_tm_fini(tm);
	/* free the private data */
	C2_ASSERT(mem_tm_to_pvt(tm) == &tp->xtm_base);
	tp->xtm_magic = 0;
	c2_free(tp);
	tm->ntm_xprt_private = NULL;
	return;
}

void c2_net_bulk_sunrpc_tm_set_num_threads(struct c2_net_transfer_mc *tm,
					  size_t num)
{
	C2_PRE(sunrpc_tm_invariant(tm));
	c2_net_bulk_mem_tm_set_num_threads(tm, num);
	return;
}

size_t c2_net_bulk_sunrpc_tm_get_num_threads(const struct c2_net_transfer_mc
					     *tm)
{
	C2_PRE(sunrpc_tm_invariant(tm));
	return c2_net_bulk_mem_tm_get_num_threads(tm);
}

static int sunrpc_xo_tm_start(struct c2_net_transfer_mc *tm)
{
	int rc = 0;
	struct c2_net_end_point *ep = tm->ntm_ep;
	const char *tm_addr = ep->nep_addr;
	struct c2_net_bulk_sunrpc_tm_pvt *tp;

	C2_PRE(sunrpc_tm_invariant(tm));
	c2_mutex_lock(&sunrpc_tm_start_mutex); /* serialize invocation */
	c2_rwlock_read_lock(&sunrpc_server_lock); /* protect list */
	c2_list_for_each_entry(&sunrpc_server_tms, tp,
			       struct c2_net_bulk_sunrpc_tm_pvt,
			       xtm_tm_linkage) {
		struct c2_net_transfer_mc *ltm = tp->xtm_base.xtm_tm;
		if (ltm == tm)
			continue; /* skip self */
		ep = ltm->ntm_ep;
		if (ep == NULL)
			continue; /* not yet started */
		if (strcmp(tm_addr, ep->nep_addr) == 0) {
			rc = -EADDRINUSE;
			break;
		}
	}
	c2_rwlock_read_unlock(&sunrpc_server_lock);
	c2_mutex_unlock(&sunrpc_tm_start_mutex);
	if (rc != 0)
		return rc;
	return c2_net_bulk_mem_xprt.nx_ops->xo_tm_start(tm);
}

static int sunrpc_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	C2_PRE(sunrpc_tm_invariant(tm));
	return c2_net_bulk_mem_xprt.nx_ops->xo_tm_stop(tm, cancel);
}

/* Internal methods of this transport. */
static const struct c2_net_bulk_mem_ops sunrpc_xprt_methods = {
	.bmo_work_fn = {
		[C2_NET_XOP_STATE_CHANGE]    = sunrpc_wf_state_change,
		[C2_NET_XOP_CANCEL_CB]       = sunrpc_wf_cancel_cb,
		[C2_NET_XOP_MSG_RECV_CB]     = sunrpc_wf_msg_recv_cb,
		[C2_NET_XOP_MSG_SEND]        = sunrpc_wf_msg_send,
		[C2_NET_XOP_PASSIVE_BULK_CB] = sunrpc_wf_passive_bulk_cb,
		[C2_NET_XOP_ACTIVE_BULK]     = sunrpc_wf_active_bulk,
		[C2_NET_XOP_ERROR_CB]        = sunrpc_wf_error_cb,
	},
	.bmo_ep_create                       = sunrpc_ep_create,
	.bmo_ep_alloc                        = sunrpc_ep_alloc,
	.bmo_ep_free                         = sunrpc_ep_free,
	.bmo_ep_release                      = sunrpc_xo_end_point_release,
	.bmo_wi_add                          = sunrpc_wi_add,
	.bmo_buffer_in_bounds                = sunrpc_buffer_in_bounds,
	.bmo_desc_create                     = sunrpc_desc_create,
	.bmo_post_error                      = sunrpc_post_error,
	.bmo_wi_post_buffer_event            = sunrpc_wi_post_buffer_event,
};

/* External interface */
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
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
