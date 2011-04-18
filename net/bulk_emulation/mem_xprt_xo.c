/* -*- C -*- */

#include "lib/memory.h"
#include "lib/misc.h"
#include "net/bulk_emulation/mem_xprt_pvt.h"

#ifdef __KERNEL__
#include <linux/in.h>
#include <linux/inet.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

/**
   @addtogroup bulkmem
   @{
 */
static bool mem_domains_initialized = false;

/**
   List of in-memory network domains.
   Protected by struct c2_net_mutex.
*/
static struct c2_list  mem_domains;

/* To reduce global symbols, yet make the code readable, we
   include other .c files with static symbols into this file.
   Dependency information must be captured in Makefile.am.

   Static functions should be declared in the private header file
   so that the order of their definiton does not matter.
*/
#include "mem_xprt_ep.c"
#include "mem_xprt_tm.c"
#include "mem_xprt_msg.c"
#include "mem_xprt_bulk.c"

static c2_bcount_t mem_buffer_length(struct c2_net_buffer *nb)
{
	return c2_vec_count(&nb->nb_buffer.ov_vec);
}

/**
   Check buffer size limits.
 */
static bool mem_buffer_in_bounds(struct c2_net_buffer *nb)
{
	struct c2_vec *v = &nb->nb_buffer.ov_vec;
	if (v->v_nr > C2_NET_BULK_MEM_MAX_BUFFER_SEGMENTS)
		return false;
	int i;
	c2_bcount_t len=0;
	for (i=0; i < v->v_nr; i++) {
		if (v->v_count[i] > C2_NET_BULK_MEM_MAX_SEGMENT_SIZE)
			return false;
		len += v->v_count[i];
	}
	if (len > C2_NET_BULK_MEM_MAX_BUFFER_SIZE)
		return false;
	return true;
}

/**
   Copy from one buffer to another. Each buffer may have different
   number of segments and segment sizes.
 */
static int mem_copy_buffer(struct c2_net_buffer *dest_nb,
			   struct c2_net_buffer *src_nb,
			   c2_bcount_t num_bytes)
{
	if (mem_buffer_length(dest_nb) < num_bytes) {
		return -EFBIG;
	}
	C2_ASSERT(mem_buffer_length(src_nb) <= num_bytes);

	return -ENOSYS;
}

/**
   Add a work item to the work list
*/
static void mem_wi_add(struct c2_net_bulk_mem_work_item *wi,
		       struct c2_net_bulk_mem_tm_pvt *tp)
{
	c2_list_add_tail(&tp->xtm_work_list, &wi->xwi_link);
	c2_cond_signal(&tp->xtm_work_list_cv, &tp->xtm_tm->ntm_mutex);	
}

static bool mem_dom_invariant(struct c2_net_domain *dom)
{
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;
	return dp != NULL && dp->xd_dom == dom;
}

static bool mem_ep_invariant(struct c2_net_end_point *ep)
{
	struct c2_net_bulk_mem_end_point *mep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	return mep->xep_magic == C2_NET_XEP_MAGIC;
}

static bool mem_buffer_invariant(struct c2_net_buffer *nb)
{
	struct c2_net_bulk_mem_buffer_pvt *bp = nb->nb_xprt_private;
	return  (bp != NULL && bp->xb_buffer == nb &&
		 mem_dom_invariant(nb->nb_dom));
}

static bool mem_tm_invariant(struct c2_net_transfer_mc *tm)
{
	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;
	return (tp != NULL && tp->xtm_tm == tm &&
		mem_dom_invariant(tm->ntm_dom));
}

/**
   This routine will allocate and initialize the private domain data and attach
   it to the domain. It will assume that the domains private pointer is
   allocated if not NULL. This allows for a derived transport to pre-allocate
   this structure before invoking the base method. The method will initialize
   the size and count fields as per the requirements of the in-memory module.
   If the private domain pointer was not allocated, the routine will assume
   that the domain is not derived, and will then link the domain in a private
   list to facilitate in-memory data transfers between transfer machines.
 */
static int mem_xo_dom_init(struct c2_net_xprt *xprt,
			   struct c2_net_domain *dom)
{
	struct c2_net_bulk_mem_domain_pvt *dp;

	if (dom->nd_xprt_private) {
		C2_PRE(xprt != &c2_net_bulk_mem_xprt);
		dp = dom->nd_xprt_private;
	} else {
		C2_ALLOC_PTR(dp);
		if (dp == NULL) {
			return -ENOMEM;
		}
		dom->nd_xprt_private = dp;
	}
	dp->xd_dom = dom;
	dp->xd_work_fn[C2_NET_XOP_STATE_CHANGE]    = mem_wf_state_change;
	dp->xd_work_fn[C2_NET_XOP_CANCEL_CB]       = mem_wf_cancel_cb;
	dp->xd_work_fn[C2_NET_XOP_MSG_RECV_CB]     = mem_wf_msg_recv_cb;
	dp->xd_work_fn[C2_NET_XOP_MSG_SEND]        = mem_wf_msg_send;
	dp->xd_work_fn[C2_NET_XOP_PASSIVE_BULK_CB] = mem_wf_passive_bulk_cb;
	dp->xd_work_fn[C2_NET_XOP_ACTIVE_BULK]     = mem_wf_active_bulk;
	dp->xd_sizeof_ep = sizeof(struct c2_net_bulk_mem_end_point);
	dp->xd_sizeof_tm_pvt = sizeof(struct c2_net_bulk_mem_tm_pvt);
	dp->xd_sizeof_buffer_pvt = sizeof(struct c2_net_bulk_mem_buffer_pvt);
	dp->xd_num_tm_threads = 1;
	c2_list_link_init(&dp->xd_dom_linkage);

	if (xprt != &c2_net_bulk_mem_xprt) {
		dp->xd_derived = true;
	} else {
		dp->xd_derived = false;
		if (!mem_domains_initialized) {
			c2_list_init(&mem_domains);
			mem_domains_initialized = true;
		}
		c2_list_add(&mem_domains, &dp->xd_dom_linkage);
	}
	C2_POST(mem_dom_invariant(dom));
	return 0;
}

/**
   This is a no-op if derived.
   If not derived, it will unlink the domain and free the private data.
 */
static void mem_xo_dom_fini(struct c2_net_domain *dom)
{
	C2_PRE(mem_dom_invariant(dom));

	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;
	if (dp->xd_derived)
		return;
	c2_list_del(&dp->xd_dom_linkage);
	c2_free(dp);
	dom->nd_xprt_private = NULL;
	return;
}

static int mem_xo_get_max_buffer_size(struct c2_net_domain *dom,
				      c2_bcount_t *size)
{
	C2_PRE(mem_dom_invariant(dom));
	*size = C2_NET_BULK_MEM_MAX_BUFFER_SIZE;
	return 0;
}

static int mem_xo_get_max_buffer_segment_size(struct c2_net_domain *dom,
					      c2_bcount_t *size)
{
	C2_PRE(mem_dom_invariant(dom));
	*size = C2_NET_BULK_MEM_MAX_SEGMENT_SIZE;
	return 0;
}

static int mem_xo_get_max_buffer_segments(struct c2_net_domain *dom,
					  int32_t *num_segs)
{
	C2_PRE(mem_dom_invariant(dom));
	*num_segs= C2_NET_BULK_MEM_MAX_BUFFER_SEGMENTS;
	return 0;
}

/**
   This routine will search for an existing end point in the domain, and if not
   found, will allocate and zero out space for a new end point using the
   xd_sizeof_ep field to determine the size. It will fill in the xep_address
   field with the IP and port number, and will link the end point to the domain
   link list.

   Dynamic address assignment is not supported.
   @param epp  Returns the pointer to the end point.
   @param dom  Domain pointer.
   @param varargs Variable length argument list. The following arguments are
   expected:
   - @a ip  Dotted decimal IP address string (char *).
   The string is not referenced after returning from this method.
   - @a port Port number (int)
 */
static int mem_xo_end_point_create(struct c2_net_end_point **epp,
				   struct c2_net_domain *dom,
				   va_list varargs)
{
	char *dot_ip;
	struct sockaddr_in sa;

	dot_ip = va_arg(varargs, char *);
	if (dot_ip == NULL)
		return -EINVAL;
	/* user: in_port_t, kernel: __be16 */
	sa.sin_port = htons(va_arg(varargs, int));
	if (sa.sin_port == 0)
		return -EINVAL;
#ifdef __KERNEL__
	sa.sin_addr.s_addr = in_aton(dot_ip);
	if (sa.sin_addr.s_addr == 0)
		return -EINVAL;
#else
	if (inet_aton(dot_ip, &sa.sin_addr) == 0)
		return -EINVAL;
#endif
	return mem_ep_create(epp, dom, &sa);
}

/**
   This routine allocate the private data associated with the buffer.
   The size of the private data is defined by the xd_sizeof_buffer_pvt
   value in the domain private structure.
 */
static int mem_xo_buf_register(struct c2_net_buffer *nb)
{
	C2_PRE(nb->nb_dom != NULL && mem_dom_invariant(nb->nb_dom));

	if (!mem_buffer_in_bounds(nb))
		return -EFBIG;

	struct c2_net_bulk_mem_domain_pvt *dp = nb->nb_dom->nd_xprt_private;
	struct c2_net_bulk_mem_buffer_pvt *bp;
	bp = c2_alloc(dp->xd_sizeof_buffer_pvt);
	if (bp == NULL)
		return -ENOMEM;

	bp->xb_buffer = nb;
	c2_list_link_init(&bp->xb_wi.xwi_link);
	bp->xb_wi.xwi_op = C2_NET_XOP_NR;
	nb->nb_xprt_private = bp;
	C2_POST(mem_buffer_invariant(nb));
	return 0;
}

/**
   This routine releases the private data associated with the buffer.
 */
static int mem_xo_buf_deregister(struct c2_net_buffer *nb)
{
	C2_PRE(mem_buffer_invariant(nb));

	struct c2_net_bulk_mem_buffer_pvt *bp;
	bp = nb->nb_xprt_private;
	c2_list_link_fini(&bp->xb_wi.xwi_link);
	c2_free(bp);
	nb->nb_xprt_private = NULL;
	return 0;
}

/**
   This routine initiates processing of the buffer operation.
 */
static int mem_xo_buf_add(struct c2_net_buffer *nb)
{
	C2_PRE(mem_buffer_invariant(nb));
	C2_PRE(nb->nb_flags & C2_NET_BUF_QUEUED &&
	       (nb->nb_flags & ~C2_NET_BUF_IN_USE) == 0);

	struct c2_net_transfer_mc *tm = nb->nb_tm;
	C2_PRE(mem_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;

	if (tp->xtm_state > C2_NET_XTM_STARTED)
		return -EPERM;

	struct c2_net_bulk_mem_buffer_pvt *bp = nb->nb_xprt_private;
	struct c2_net_bulk_mem_work_item *wi = &bp->xb_wi;
	wi->xwi_op = C2_NET_XOP_NR;

	int rc;
	switch (nb->nb_qtype) {
	case C2_NET_QT_MSG_RECV:
		break;
	case C2_NET_QT_MSG_SEND:
		wi->xwi_op = C2_NET_XOP_MSG_SEND;
		break;
	case C2_NET_QT_PASSIVE_BULK_RECV:
	case C2_NET_QT_PASSIVE_BULK_SEND:
		break;
	case C2_NET_QT_ACTIVE_BULK_RECV:
	case C2_NET_QT_ACTIVE_BULK_SEND:
		wi->xwi_op = C2_NET_XOP_ACTIVE_BULK;
		C2_ASSERT(nb->nb_ep != NULL);
		rc = mem_ep_create_desc(nb->nb_ep, &nb->nb_desc);
		if (!rc)
			return rc;
		break;
	default:
		C2_IMPOSSIBLE("invalid queue type");
		break;
	}
	nb->nb_flags &= ~C2_NET_BUF_CANCELLED;
	nb->nb_status = -1;

	if (wi->xwi_op != C2_NET_XOP_NR) {
		mem_wi_add(wi, tp);
	}

	return 0;
}

/**
   Cancel ongoing buffer operations.
   @param nb Buffer pointer
   @retval 0 on success
   @retval -EBUSY if operation not cancellable
 */
static int mem_xo_buf_del(struct c2_net_buffer *nb)
{
	C2_PRE(mem_buffer_invariant(nb));
	C2_PRE(nb->nb_flags & C2_NET_BUF_QUEUED);

	struct c2_net_transfer_mc *tm = nb->nb_tm;
	C2_PRE(mem_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;

	if (nb->nb_flags & C2_NET_BUF_IN_USE)
		return -EBUSY;

	struct c2_net_bulk_mem_buffer_pvt *bp = nb->nb_xprt_private;
	struct c2_net_bulk_mem_work_item *wi = &bp->xb_wi;
	wi->xwi_op = C2_NET_XOP_CANCEL_CB;
	nb->nb_flags |= C2_NET_BUF_CANCELLED;

	switch (nb->nb_qtype) {
	case C2_NET_QT_MSG_RECV:
	case C2_NET_QT_PASSIVE_BULK_RECV:
	case C2_NET_QT_PASSIVE_BULK_SEND:
		/* must be added to the work list */
		C2_ASSERT(!c2_list_contains(&tp->xtm_work_list,&wi->xwi_link));
		mem_wi_add(wi, tp);
		break;

	case C2_NET_QT_MSG_SEND:
	case C2_NET_QT_ACTIVE_BULK_RECV:
	case C2_NET_QT_ACTIVE_BULK_SEND:
		/* these are already queued */
		C2_ASSERT(c2_list_contains(&tp->xtm_work_list,&wi->xwi_link));
		break;

	default:
		C2_IMPOSSIBLE("invalid queue type");
		break;
	}

	return 0;
}

/**
   Initialize a transfer machine.
   @param tm Transfer machine pointer
   @retval 0 on success
   @retval -ENOMEM if memory not available
 */
static int mem_xo_tm_init(struct c2_net_transfer_mc *tm)
{
	C2_PRE(mem_dom_invariant(tm->ntm_dom));

	struct c2_net_bulk_mem_domain_pvt *dp = tm->ntm_dom->nd_xprt_private;
	struct c2_net_bulk_mem_tm_pvt *tp;
	tp = c2_alloc(dp->xd_sizeof_tm_pvt);
	if (tp == NULL)
		return -ENOMEM;
	tp->xtm_num_workers = dp->xd_num_tm_threads;
	C2_ALLOC_ARR(tp->xtm_worker_threads, tp->xtm_num_workers);
	if (tp->xtm_worker_threads == NULL) {
		c2_free(tp);
		return -ENOMEM;
	}
	tp->xtm_tm = tm;
	tp->xtm_state = C2_NET_XTM_INITIALIZED;
	c2_list_init(&tp->xtm_work_list);
	c2_cond_init(&tp->xtm_work_list_cv);
	tm->ntm_xprt_private = tp;
	C2_POST(mem_tm_invariant(tm));
	return 0;
}

/**
   Finalize a transfer machine.
   @param tm Transfer machine pointer
   @retval 0 on success
   @retval -EBUSY if cannot be finialized.
 */
static int mem_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
	C2_PRE(mem_tm_invariant(tm));

	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;
	if (tp->xtm_state != C2_NET_XTM_STOPPED)
		return -EBUSY;

	c2_mutex_lock(&tm->ntm_mutex);
	c2_cond_broadcast(&tp->xtm_work_list_cv, &tm->ntm_mutex);
	c2_mutex_unlock(&tm->ntm_mutex);
	int i;
	for (i = 0; i < tp->xtm_num_workers; ++i) {
		if (tp->xtm_worker_threads[i].t_state != TS_PARKED)
			c2_thread_join(&tp->xtm_worker_threads[i]);
	}

	tm->ntm_xprt_private = NULL;
	c2_free(tp->xtm_worker_threads);
	c2_cond_fini(&tp->xtm_work_list_cv);
	c2_list_fini(&tp->xtm_work_list);
	tp->xtm_tm = NULL;
	tm->ntm_xprt_private = NULL;
	c2_free(tp);
	return 0;
}

static int mem_xo_tm_start(struct c2_net_transfer_mc *tm)
{
	C2_PRE(mem_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));

	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;
	if (tp->xtm_state == C2_NET_XTM_STARTED)
		return 0;
	if (tp->xtm_state == C2_NET_XTM_STARTING)
		return 0;
	if (tp->xtm_state != C2_NET_XTM_INITIALIZED)
		return -EPERM;

	/* allocate a state change work item */
	struct c2_net_bulk_mem_work_item *wi_st_chg;
	C2_ALLOC_PTR(wi_st_chg);
	if (wi_st_chg == NULL)
		return -ENOMEM;
	c2_list_link_init(&wi_st_chg->xwi_link);
	wi_st_chg->xwi_op = C2_NET_XOP_STATE_CHANGE;
	wi_st_chg->xwi_next_state = C2_NET_XTM_STARTED;

	/* start worker threads */
	int rc = 0;
	int i;
	for (i = 0; i < tp->xtm_num_workers && rc == 0; ++i)
		rc = C2_THREAD_INIT(&tp->xtm_worker_threads[i],
				    struct c2_net_transfer_mc *, NULL,
				    &mem_xo_tm_worker, tm);

	if (rc == 0) {
		/* set transition state and add the state change work item */
		tp->xtm_state = C2_NET_XTM_STARTING;
		mem_wi_add(wi_st_chg, tp);
	} else {
		tp->xtm_state = C2_NET_XTM_FAILED;
		c2_free(wi_st_chg); /* fini cleans up threads */
	}

	return rc;
}

static int mem_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	C2_PRE(mem_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));

	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;
	if (tp->xtm_state >= C2_NET_XTM_STOPPING)
		return 0;

	/* allocate a state change work item */
	struct c2_net_bulk_mem_work_item *wi_st_chg;
	C2_ALLOC_PTR(wi_st_chg);
	if (wi_st_chg == NULL)
		return -ENOMEM;
	c2_list_link_init(&wi_st_chg->xwi_link);
	wi_st_chg->xwi_op = C2_NET_XOP_STATE_CHANGE;
	wi_st_chg->xwi_next_state = C2_NET_XTM_STOPPED;

	/* walk through the queues and cancel every buffer */
	int rc;
	int qt;
	struct c2_net_buffer *nb;
	for (qt = 0; qt < C2_NET_QT_NR; ++qt) {
		c2_list_for_each_entry(&tm->ntm_q[qt], nb,
				       struct c2_net_buffer,
				       nb_tm_linkage) {
			rc = mem_xo_buf_del(nb);
			if (!rc) {
				/* bump the del stat count */
				tm->ntm_qstats[qt].nqs_num_dels += 1;
			}
		}
	}

	/* set transition state and add the state change work item */
	tp->xtm_state = C2_NET_XTM_STOPPING;
	mem_wi_add(wi_st_chg, tp);

	return 0;
}

static const struct c2_net_xprt_ops mem_xo_xprt_ops = {
	.xo_dom_init                    = mem_xo_dom_init,
	.xo_dom_fini                    = mem_xo_dom_fini,
	.xo_get_max_buffer_size         = mem_xo_get_max_buffer_size,
	.xo_get_max_buffer_segment_size = mem_xo_get_max_buffer_segment_size,
	.xo_get_max_buffer_segments     = mem_xo_get_max_buffer_segments,
	.xo_end_point_create            = mem_xo_end_point_create,
	.xo_buf_register                = mem_xo_buf_register,
	.xo_buf_deregister              = mem_xo_buf_deregister,
	.xo_buf_add                     = mem_xo_buf_add,
	.xo_buf_del                     = mem_xo_buf_del,
	.xo_tm_init                     = mem_xo_tm_init,
	.xo_tm_fini                     = mem_xo_tm_fini,
	.xo_tm_start                    = mem_xo_tm_start,
	.xo_tm_stop                     = mem_xo_tm_stop,
};

struct c2_net_xprt c2_net_bulk_mem_xprt = {
	.nx_name = "bulk-mem",
	.nx_ops  = &mem_xo_xprt_ops
};

/**
   @} bulkmem
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
