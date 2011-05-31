/* -*- C -*- */

#include "lib/memory.h"
#include "lib/misc.h"
#include "net/bulk_emulation/mem_xprt_pvt.h"

/**
   @addtogroup bulkmem
   @{
 */

/**
   List of in-memory network domains.
   Protected by struct c2_net_mutex.
*/
static struct c2_list  mem_domains;

/**
   Transport initialization subroutine called from c2_init().
 */
int c2_mem_xprt_init(void)
{
	c2_list_init(&mem_domains);
	return 0;
}

/**
   Transport finalization subroutine called from c2_fini().
 */
void c2_mem_xprt_fini(void)
{
	c2_list_fini(&mem_domains);
}

/* To reduce global symbols, yet make the code readable, we
   include other .c files with static symbols into this file.
   Dependency information must be captured in Makefile.am.

   Static functions should be declared in the private header file
   so that the order of their definition does not matter.
*/
#include "net/bulk_emulation/mem_xprt_ep.c"
#include "net/bulk_emulation/mem_xprt_tm.c"
#include "net/bulk_emulation/mem_xprt_msg.c"
#include "net/bulk_emulation/mem_xprt_bulk.c"

static c2_bcount_t mem_buffer_length(const struct c2_net_buffer *nb)
{
	return c2_vec_count(&nb->nb_buffer.ov_vec);
}

/**
   Check buffer size limits.
 */
static bool mem_buffer_in_bounds(const struct c2_net_buffer *nb)
{
	const struct c2_vec *v = &nb->nb_buffer.ov_vec;
	uint32_t i;
	c2_bcount_t len = 0;

	if (v->v_nr > C2_NET_BULK_MEM_MAX_BUFFER_SEGMENTS)
		return false;
	for (i = 0; i < v->v_nr; ++i) {
		if (v->v_count[i] > C2_NET_BULK_MEM_MAX_SEGMENT_SIZE)
			return false;
		C2_ASSERT(len + v->v_count[i] >= len);
		len += v->v_count[i];
	}
	return len <= C2_NET_BULK_MEM_MAX_BUFFER_SIZE;
}

/**
   Copy from one buffer to another. Each buffer may have different
   number of segments and segment sizes.
   The subroutine does not set the nb_length field of the d_nb buffer.
   @param d_nb  The destination buffer pointer.
   @param s_nb  The source buffer pointer.
   @param num_bytes The number of bytes to copy.
   @pre
mem_buffer_length(d_nb) >= num_bytes &&
mem_buffer_length(s_nb) >= num_bytes
 */
static int mem_copy_buffer(struct c2_net_buffer *d_nb,
			   struct c2_net_buffer *s_nb,
			   c2_bcount_t num_bytes)
{
	struct c2_bufvec_cursor s_cur;
	struct c2_bufvec_cursor d_cur;
	c2_bcount_t bytes_copied;

	if (mem_buffer_length(d_nb) < num_bytes) {
		return -EFBIG;
	}
	C2_PRE(mem_buffer_length(s_nb) >= num_bytes);

	c2_bufvec_cursor_init(&s_cur, &s_nb->nb_buffer);
	c2_bufvec_cursor_init(&d_cur, &d_nb->nb_buffer);
	bytes_copied = c2_bufvec_cursor_copy(&d_cur, &s_cur, num_bytes);
	C2_ASSERT(bytes_copied == num_bytes);

	return 0;
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

/**
   Post a buffer event.
 */
static void mem_wi_post_buffer_event(struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_buffer *nb = mem_wi_to_buffer(wi);
	struct c2_net_buffer_event ev = {
		.nbe_buffer = nb,
		.nbe_status = wi->xwi_status,
		.nbe_offset = 0,
		.nbe_length = wi->xwi_nbe_length,
		.nbe_ep     = wi->xwi_nbe_ep
	};
	C2_PRE(wi->xwi_status <= 0);
	c2_time_now(&ev.nbe_time);
	c2_net_buffer_event_post(&ev);
	return;
}

static bool mem_dom_invariant(const struct c2_net_domain *dom)
{
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;
	return dp != NULL && dp->xd_dom == dom;
}

static bool mem_ep_invariant(const struct c2_net_end_point *ep)
{
	const struct c2_net_bulk_mem_end_point *mep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	return mep->xep_magic == C2_NET_BULK_MEM_XEP_MAGIC &&
		mep->xep_ep.nep_addr == &mep->xep_addr[0];
}

static bool mem_buffer_invariant(const struct c2_net_buffer *nb)
{
	const struct c2_net_bulk_mem_buffer_pvt *bp = nb->nb_xprt_private;
	return bp != NULL && bp->xb_buffer == nb &&
		mem_dom_invariant(nb->nb_dom);
}

static bool mem_tm_invariant(const struct c2_net_transfer_mc *tm)
{
	const struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;
	return tp != NULL && tp->xtm_tm == tm && mem_dom_invariant(tm->ntm_dom);
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

	if (dom->nd_xprt_private != NULL) {
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

	/* set function pointers for indirectly invoked subroutines */
	dp->xd_work_fn[C2_NET_XOP_STATE_CHANGE]    = mem_wf_state_change;
	dp->xd_work_fn[C2_NET_XOP_CANCEL_CB]       = mem_wf_cancel_cb;
	dp->xd_work_fn[C2_NET_XOP_MSG_RECV_CB]     = mem_wf_msg_recv_cb;
	dp->xd_work_fn[C2_NET_XOP_MSG_SEND]        = mem_wf_msg_send;
	dp->xd_work_fn[C2_NET_XOP_PASSIVE_BULK_CB] = mem_wf_passive_bulk_cb;
	dp->xd_work_fn[C2_NET_XOP_ACTIVE_BULK]     = mem_wf_active_bulk;
	dp->xd_work_fn[C2_NET_XOP_ERROR_CB]        = mem_wf_error_cb;
	dp->xd_ops.bmo_ep_create            = &mem_ep_create;
	dp->xd_ops.bmo_ep_release           = &mem_xo_end_point_release;
	dp->xd_ops.bmo_wi_add               = &mem_wi_add;
	dp->xd_ops.bmo_buffer_in_bounds     = &mem_buffer_in_bounds;
	dp->xd_ops.bmo_desc_create          = &mem_desc_create;
	dp->xd_ops.bmo_post_error           = &mem_post_error;
	dp->xd_ops.bmo_wi_post_buffer_event = &mem_wi_post_buffer_event;

	/* tunable parameters */
	dp->xd_sizeof_ep         = sizeof(struct c2_net_bulk_mem_end_point);
	dp->xd_sizeof_tm_pvt     = sizeof(struct c2_net_bulk_mem_tm_pvt);
	dp->xd_sizeof_buffer_pvt = sizeof(struct c2_net_bulk_mem_buffer_pvt);
	dp->xd_addr_tuples       = 2;
	dp->xd_num_tm_threads    = 1;

	dp->xd_buf_id_counter = 0;
	c2_list_link_init(&dp->xd_dom_linkage);

	if (xprt != &c2_net_bulk_mem_xprt) {
		dp->xd_derived = true;
	} else {
		dp->xd_derived = false;
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
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;
	C2_PRE(mem_dom_invariant(dom));

	if (dp->xd_derived)
		return;
	c2_list_del(&dp->xd_dom_linkage);
	c2_free(dp);
	dom->nd_xprt_private = NULL;
}

static c2_bcount_t mem_xo_get_max_buffer_size(const struct c2_net_domain *dom)
{
	C2_PRE(mem_dom_invariant(dom));
	return C2_NET_BULK_MEM_MAX_BUFFER_SIZE;
}

static c2_bcount_t mem_xo_get_max_buffer_segment_size(
					      const struct c2_net_domain *dom)
{
	C2_PRE(mem_dom_invariant(dom));
	return C2_NET_BULK_MEM_MAX_SEGMENT_SIZE;
}

static int32_t mem_xo_get_max_buffer_segments(const struct c2_net_domain *dom)
{
	C2_PRE(mem_dom_invariant(dom));
	return C2_NET_BULK_MEM_MAX_BUFFER_SEGMENTS;
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
   @param addr Address string in one of the following two formats:
   - "dottedIP:portNumber" if 2-tuple addressing used.
   - "dottedIP:portNumber:serviceId" if 3-tuple addressing used.
 */
static int mem_xo_end_point_create(struct c2_net_end_point **epp,
				   struct c2_net_domain *dom,
				   const char *addr)
{
	char buf[C2_NET_BULK_MEM_XEP_ADDR_LEN];
	const char *dot_ip;
	char *p;
	char *pp;
	int pnum;
	struct sockaddr_in sa;
	uint32_t id = 0;
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;

	C2_PRE(dp->xd_addr_tuples == 2 || dp->xd_addr_tuples == 3);

	if (addr == NULL)
		return -ENOSYS; /* no dynamic addressing */

	strncpy(buf, addr, sizeof(buf)-1); /* copy to modify */
	buf[sizeof(buf)-1] = '\0';
	for (p=buf; *p && *p != ':'; p++);
	if (*p == '\0')
		return -EINVAL;
	*p++ = '\0'; /* terminate the ip address : */
	pp = p;
	for (p=pp; *p && *p != ':'; p++);
	if (dp->xd_addr_tuples == 3) {
		*p++ = '\0'; /* terminate the port number */
		sscanf(p, "%u", &id);
		if (id == 0)
			return -EINVAL;
	}
	else if (*p == ':')
		return -EINVAL; /* 3-tuple where expecting 2 */
	sscanf(pp, "%d", &pnum);
	sa.sin_port = htons(pnum);
	dot_ip = buf;
#ifdef __KERNEL__
	sa.sin_addr.s_addr = in_aton(dot_ip);
	if (sa.sin_addr.s_addr == 0)
		return -EINVAL;
#else
	if (inet_aton(dot_ip, &sa.sin_addr) == 0)
		return -EINVAL;
#endif
	return MEM_EP_CREATE(epp, dom, &sa, id);
}

/**
   This routine allocate the private data associated with the buffer.
   The size of the private data is defined by the xd_sizeof_buffer_pvt
   value in the domain private structure.
 */
static int mem_xo_buf_register(struct c2_net_buffer *nb)
{
	struct c2_net_bulk_mem_domain_pvt *dp;
	struct c2_net_bulk_mem_buffer_pvt *bp;

	C2_PRE(nb->nb_dom != NULL && mem_dom_invariant(nb->nb_dom));

	if (!MEM_BUFFER_IN_BOUNDS(nb))
		return -EFBIG;

	dp = nb->nb_dom->nd_xprt_private;
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
	struct c2_net_bulk_mem_buffer_pvt *bp;

	C2_PRE(mem_buffer_invariant(nb));
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
	struct c2_net_transfer_mc *tm;
	struct c2_net_bulk_mem_tm_pvt *tp;
	struct c2_net_bulk_mem_domain_pvt *dp;
	struct c2_net_bulk_mem_buffer_pvt *bp;
	struct c2_net_bulk_mem_work_item *wi;
	int rc;

	C2_PRE(mem_buffer_invariant(nb));
	C2_PRE(nb->nb_flags & C2_NET_BUF_QUEUED &&
	       (nb->nb_flags & C2_NET_BUF_IN_USE) == 0);

	C2_PRE(nb->nb_offset == 0); /* don't support any other value */

	tm = nb->nb_tm;
	C2_PRE(mem_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	tp = tm->ntm_xprt_private;

	if (tp->xtm_state > C2_NET_XTM_STARTED)
		return -EPERM;

	dp = tm->ntm_dom->nd_xprt_private;
	bp = nb->nb_xprt_private;
	wi = &bp->xb_wi;
	wi->xwi_op = C2_NET_XOP_NR;

	switch (nb->nb_qtype) {
	case C2_NET_QT_MSG_RECV:
		break;
	case C2_NET_QT_MSG_SEND:
		C2_ASSERT(nb->nb_ep != NULL);
		wi->xwi_op = C2_NET_XOP_MSG_SEND;
		break;
	case C2_NET_QT_PASSIVE_BULK_RECV:
		nb->nb_length = 0;
	case C2_NET_QT_PASSIVE_BULK_SEND:
		bp->xb_buf_id = ++dp->xd_buf_id_counter;
		rc = MEM_DESC_CREATE(&nb->nb_desc, nb->nb_ep, tm,
				     nb->nb_qtype, nb->nb_length,
				     bp->xb_buf_id);
		if (rc != 0)
			return rc;
		break;
	case C2_NET_QT_ACTIVE_BULK_RECV:
	case C2_NET_QT_ACTIVE_BULK_SEND:
		wi->xwi_op = C2_NET_XOP_ACTIVE_BULK;
		break;
	default:
		C2_IMPOSSIBLE("invalid queue type");
		break;
	}
	nb->nb_flags &= ~C2_NET_BUF_CANCELLED;
	wi->xwi_status = -1;

	if (wi->xwi_op != C2_NET_XOP_NR) {
		mem_wi_add(wi, tp);
	}

	return 0;
}

/**
   Cancel ongoing buffer operations.
   @param nb Buffer pointer
 */
static void mem_xo_buf_del(struct c2_net_buffer *nb)
{
	struct c2_net_bulk_mem_buffer_pvt *bp = nb->nb_xprt_private;
	struct c2_net_transfer_mc *tm;
	struct c2_net_bulk_mem_tm_pvt *tp;
	struct c2_net_bulk_mem_work_item *wi;

	C2_PRE(mem_buffer_invariant(nb));
	C2_PRE(nb->nb_flags & C2_NET_BUF_QUEUED);

	tm = nb->nb_tm;
	C2_PRE(mem_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	tp = tm->ntm_xprt_private;

	if (nb->nb_flags & C2_NET_BUF_IN_USE)
		return;

	wi = &bp->xb_wi;
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
}

/**
   Initialize a transfer machine.
   @param tm Transfer machine pointer
   @retval 0 on success
   @retval -ENOMEM if memory not available
 */
static int mem_xo_tm_init(struct c2_net_transfer_mc *tm)
{
	struct c2_net_bulk_mem_domain_pvt *dp;
	struct c2_net_bulk_mem_tm_pvt *tp;

	C2_PRE(mem_dom_invariant(tm->ntm_dom));

	dp = tm->ntm_dom->nd_xprt_private;
	tp = c2_alloc(dp->xd_sizeof_tm_pvt);
	if (tp == NULL)
		return -ENOMEM;
	tp->xtm_num_workers = dp->xd_num_tm_threads;
	/* defer allocation of thread array to start time */
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
 */
static void mem_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;

	C2_PRE(mem_tm_invariant(tm));
	C2_PRE(tp->xtm_state == C2_NET_XTM_STOPPED ||
	       tp->xtm_state == C2_NET_XTM_FAILED  ||
	       tp->xtm_state == C2_NET_XTM_INITIALIZED);

	c2_mutex_lock(&tm->ntm_mutex);
	tp->xtm_state = C2_NET_XTM_STOPPED; /* to stop the workers */
	c2_cond_broadcast(&tp->xtm_work_list_cv, &tm->ntm_mutex);
	c2_mutex_unlock(&tm->ntm_mutex);
	if (tp->xtm_worker_threads != NULL) {
		int i;
		for (i = 0; i < tp->xtm_num_workers; ++i) {
			if (tp->xtm_worker_threads[i].t_state != TS_PARKED)
				c2_thread_join(&tp->xtm_worker_threads[i]);
		}
		c2_free(tp->xtm_worker_threads);
	}
	tm->ntm_xprt_private = NULL;
	c2_cond_fini(&tp->xtm_work_list_cv);
	c2_list_fini(&tp->xtm_work_list);
	tp->xtm_tm = NULL;
	tm->ntm_xprt_private = NULL;
	c2_free(tp);
}

void c2_net_bulk_mem_tm_set_num_threads(struct c2_net_transfer_mc *tm,
				       size_t num)
{
	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;
	C2_PRE(mem_tm_invariant(tm));
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_PRE(tp->xtm_state == C2_NET_XTM_INITIALIZED);
	tp->xtm_num_workers = num;
	c2_mutex_unlock(&tm->ntm_mutex);
}

size_t c2_net_bulk_mem_tm_get_num_threads(const struct c2_net_transfer_mc *tm) {
	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;
	C2_PRE(mem_tm_invariant(tm));
	return tp->xtm_num_workers;
}

static int mem_xo_tm_start(struct c2_net_transfer_mc *tm)
{
	struct c2_net_bulk_mem_tm_pvt *tp;
	struct c2_net_bulk_mem_work_item *wi_st_chg;
	int rc = 0;
	int i;

	C2_PRE(mem_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));

	tp = tm->ntm_xprt_private;
	if (tp->xtm_state == C2_NET_XTM_STARTED)
		return 0;
	if (tp->xtm_state == C2_NET_XTM_STARTING)
		return 0;
	if (tp->xtm_state != C2_NET_XTM_INITIALIZED)
		return -EPERM;

	/* allocate worker thread array */
	if (tp->xtm_worker_threads == NULL) {
		/* allocation creates parked threads in case of failure */
		C2_CASSERT(TS_PARKED == 0);
		C2_ALLOC_ARR(tp->xtm_worker_threads, tp->xtm_num_workers);
		if (tp->xtm_worker_threads == NULL)
			return -ENOMEM;
	}

	/* allocate a state change work item */
	C2_ALLOC_PTR(wi_st_chg);
	if (wi_st_chg == NULL)
		return -ENOMEM;
	c2_list_link_init(&wi_st_chg->xwi_link);
	wi_st_chg->xwi_op = C2_NET_XOP_STATE_CHANGE;
	wi_st_chg->xwi_next_state = C2_NET_XTM_STARTED;
	wi_st_chg->xwi_status = 0;

	/* start worker threads */
	for (i = 0; i < tp->xtm_num_workers && rc == 0; ++i)
		rc = C2_THREAD_INIT(&tp->xtm_worker_threads[i],
				    struct c2_net_transfer_mc *, NULL,
				    &mem_xo_tm_worker, tm,
				    "mem_tm_worker%d", i);

	if (rc == 0) {
		/* set transition state and add the state change work item */
		tp->xtm_state = C2_NET_XTM_STARTING;
		mem_wi_add(wi_st_chg, tp);
	} else {
		tp->xtm_state = C2_NET_XTM_FAILED;
		c2_list_link_fini(&wi_st_chg->xwi_link);
		c2_free(wi_st_chg); /* fini cleans up threads */
	}

	return rc;
}

static int mem_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	struct c2_net_bulk_mem_tm_pvt *tp;
	struct c2_net_bulk_mem_work_item *wi_st_chg;
	struct c2_net_buffer *nb;
	int qt;

	C2_PRE(mem_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));

	tp = tm->ntm_xprt_private;
	if (tp->xtm_state >= C2_NET_XTM_STOPPING)
		return 0;

	/* allocate a state change work item */
	C2_ALLOC_PTR(wi_st_chg);
	if (wi_st_chg == NULL)
		return -ENOMEM;
	c2_list_link_init(&wi_st_chg->xwi_link);
	wi_st_chg->xwi_op = C2_NET_XOP_STATE_CHANGE;
	wi_st_chg->xwi_next_state = C2_NET_XTM_STOPPED;

	/* walk through the queues and cancel every buffer */
	for (qt = 0; qt < C2_NET_QT_NR; ++qt) {
		c2_list_for_each_entry(&tm->ntm_q[qt], nb,
				       struct c2_net_buffer,
				       nb_tm_linkage) {
			mem_xo_buf_del(nb);
			/* bump the del stat count */
			tm->ntm_qstats[qt].nqs_num_dels += 1;
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
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
