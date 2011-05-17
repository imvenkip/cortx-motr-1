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

static bool sunrpc_dom_invariant(const struct c2_net_domain *dom)
{
	const struct c2_net_bulk_sunrpc_domain_pvt *dp = dom->nd_xprt_private;
	return (dp != NULL && dp->xd_magic == C2_NET_BULK_SUNRPC_XDP_MAGIC);
}

static bool sunrpc_ep_invariant(const struct c2_net_end_point *ep)
{
	const struct c2_net_bulk_mem_end_point *mep;
	const struct c2_net_bulk_sunrpc_end_point *sep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	sep = container_of(mep, struct c2_net_bulk_sunrpc_end_point, xep_base);
	return (sunrpc_dom_invariant(ep->nep_dom) &&
		sep->xep_magic == C2_NET_BULK_SUNRPC_XEP_MAGIC &&
		sep->xep_sid_valid);
}

static bool sunrpc_tm_invariant(const struct c2_net_transfer_mc *tm)
{
	const struct c2_net_bulk_sunrpc_tm_pvt *tp = tm->ntm_xprt_private;
	return (tp != NULL && tp->xtm_magic == C2_NET_BULK_SUNRPC_XTM_MAGIC &&
		sunrpc_dom_invariant(tm->ntm_dom));
}

static bool sunrpc_buffer_invariant(const struct c2_net_buffer *nb)
{
	const struct c2_net_bulk_sunrpc_buffer_pvt *sbp = nb->nb_xprt_private;
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

/**
   Transport finalization subroutine called from c2_fini().
 */
void c2_sunrpc_fop_fini(void)
{
	c2_mutex_fini(&sunrpc_tm_start_mutex);
	c2_list_init(&sunrpc_server_tms);
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

	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	if (result == 0) {
		result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
	}
	if (result != 0)
		c2_sunrpc_fop_fini();
	else {
		c2_rwlock_init(&sunrpc_server_lock);
		c2_mutex_init(&sunrpc_server_mutex);
		c2_list_init(&sunrpc_server_tms);
		c2_mutex_init(&sunrpc_tm_start_mutex);
	}
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

		c2_mutex_lock(&tm->ntm_mutex);
		C2_ASSERT(c2_net__tm_invariant(tm));

		struct c2_net_end_point *ep = tm->ntm_ep;
		if (ep == NULL || tm->ntm_state != C2_NET_TM_STARTED) {
			c2_mutex_unlock(&tm->ntm_mutex);
			continue;
		}

		struct c2_net_bulk_mem_end_point *mep =
		    container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);

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
   Add a work item to the work list
*/
static void sunrpc_wi_add(struct c2_net_bulk_mem_work_item *wi,
			  struct c2_net_bulk_sunrpc_tm_pvt *tp)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp =
		tp->xtm_base.xtm_tm->ntm_dom->nd_xprt_private;
	(*dp->xd_base_ops.bmo_wi_add)(wi, &tp->xtm_base);
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
	bdp->xd_ops.bmo_ep_create        = &sunrpc_ep_create;
	bdp->xd_ops.bmo_ep_release       = &sunrpc_xo_end_point_release;
	bdp->xd_ops.bmo_buffer_in_bounds = &sunrpc_buffer_in_bounds;
	bdp->xd_ops.bmo_desc_create      = &sunrpc_desc_create;

	/* override tunable parameters */
	bdp->xd_sizeof_ep         = sizeof(struct c2_net_bulk_sunrpc_end_point);
	bdp->xd_sizeof_tm_pvt     = sizeof(struct c2_net_bulk_sunrpc_tm_pvt);
	bdp->xd_sizeof_buffer_pvt =
	    sizeof(struct c2_net_bulk_sunrpc_buffer_pvt);
	bdp->xd_addr_tuples       = 3;
	bdp->xd_num_tm_threads    = C2_NET_BULK_SUNRPC_TM_THREADS;

	/* create the rpc domain (use in-mutex version of domain init) */
#ifdef __KERNEL__
	rc = c2_net__domain_init(&dp->xd_rpc_dom, &c2_net_ksunrpc_xprt);
#else
	rc = c2_net__domain_init(&dp->xd_rpc_dom, &c2_net_usunrpc_minimal_xprt);
#endif
	if (rc != 0)
		goto err_exit;

	dp->xd_magic = C2_NET_BULK_SUNRPC_XDP_MAGIC;
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

	/* fini the RPC domain (use in-mutex version of domain fini) */
	c2_net__domain_fini(&dp->xd_rpc_dom);

	/* fini the base */
	c2_net_bulk_mem_xprt.nx_ops->xo_dom_fini(dom);

	/* free the pvt structure */
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
				      va_list varargs)
{
	/* calls sunrpc_ep_create */
	return c2_net_bulk_mem_xprt.nx_ops->xo_end_point_create(epp, dom,
								varargs);
}

/**
   Check buffer size limits.
 */
static bool sunrpc_buffer_in_bounds(const struct c2_net_buffer *nb)
{
	const struct c2_vec *v = &nb->nb_buffer.ov_vec;
	if (v->v_nr > C2_NET_BULK_SUNRPC_MAX_BUFFER_SEGMENTS)
		return false;
	int i;
	c2_bcount_t len=0;
	for (i=0; i < v->v_nr; i++) {
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
	C2_PRE(sunrpc_dom_invariant(nb->nb_dom));
	/* calls sunrpc_buffer_in_bounds */
	rc = c2_net_bulk_mem_xprt.nx_ops->xo_buf_register(nb);
	if (rc == 0) {
		struct c2_net_bulk_sunrpc_buffer_pvt *sbp =
			nb->nb_xprt_private;
		C2_ASSERT(sbp != NULL);
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
	int rc;
	c2_rwlock_write_lock(&sunrpc_server_lock);
	rc = c2_net_bulk_mem_xprt.nx_ops->xo_tm_init(tm);
	if (rc == 0) {
		struct c2_net_bulk_sunrpc_tm_pvt *tp = tm->ntm_xprt_private;
		tp->xtm_magic = C2_NET_BULK_SUNRPC_XTM_MAGIC;
		c2_list_link_init(&tp->xtm_tm_linkage);
		c2_list_add_tail(&sunrpc_server_tms, &tp->xtm_tm_linkage);
		C2_POST(sunrpc_tm_invariant(tm));
	}
	c2_rwlock_write_unlock(&sunrpc_server_lock);
	return rc;
}

static int sunrpc_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
	struct c2_net_bulk_sunrpc_tm_pvt *tp = tm->ntm_xprt_private;
	C2_PRE(sunrpc_tm_invariant(tm));
	c2_rwlock_write_lock(&sunrpc_server_lock);
	c2_list_del(&tp->xtm_tm_linkage);
	c2_rwlock_write_unlock(&sunrpc_server_lock);
	return c2_net_bulk_mem_xprt.nx_ops->xo_tm_fini(tm);
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
