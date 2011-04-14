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

struct c2_list  c2_net_bulk_mem_domains;

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
		C2_ASSERT(xprt != &c2_net_bulk_mem_xprt);
		dp = dom->nd_xprt_private;
	} else {
		C2_ALLOC_PTR(dp);
		if (dp == NULL) {
			return -ENOMEM;
		}
		dom->nd_xprt_private = dp;
	}
	dp->xd_dom = dom;
	dp->xd_work_fn[C2_NET_XOP_STATE_CHANGE]    = NULL;
	dp->xd_work_fn[C2_NET_XOP_CANCEL_CB]       = NULL;
	dp->xd_work_fn[C2_NET_XOP_MSG_RECV_CB]     = NULL;
	dp->xd_work_fn[C2_NET_XOP_MSG_SEND]        = NULL;
	dp->xd_work_fn[C2_NET_XOP_PASSIVE_BULK_CB] = NULL;
	dp->xd_work_fn[C2_NET_XOP_ACTIVE_BULK]     = NULL;
	dp->xd_sizeof_ep = sizeof(struct c2_net_bulk_mem_end_point);
	dp->xd_sizeof_tm_pvt = sizeof(struct c2_net_bulk_mem_tm_pvt);
	dp->xd_sizeof_buf_pvt = sizeof(struct c2_net_bulk_mem_buffer_pvt);
	dp->xd_num_tm_threads = 1;
	c2_list_link_init(&dp->xd_dom_linkage);

	if (xprt != &c2_net_bulk_mem_xprt) {
		dp->xd_derived = true;
	} else {
		dp->xd_derived = false;
		c2_list_add(&c2_net_bulk_mem_domains, &dp->xd_dom_linkage);
	}
	return 0;
}

/**
   This is a no-op if derived.
   If not derived, it will unlink the domain and free the private data.
 */
static void mem_xo_dom_fini(struct c2_net_domain *dom)
{
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;

	if(dp->xd_derived)
		return;
	c2_list_del(&dp->xd_dom_linkage);
	c2_free(dp);
	dom->nd_xprt_private = NULL;
	return;
}

static int mem_xo_get_max_buffer_size(struct c2_net_domain *dom, 
				      c2_bcount_t *size)
{
	*size = C2_NET_BULK_MEM_MAX_BUFFER_SIZE;
	return 0;
}

static int mem_xo_get_max_buffer_segment_size(struct c2_net_domain *dom,
					      c2_bcount_t *size)
{
	*size = C2_NET_BULK_MEM_MAX_SEGMENT_SIZE;
	return 0;
}

static int mem_xo_get_max_buffer_segments(struct c2_net_domain *dom,
					  int32_t *num_segs)
{
	*num_segs= C2_NET_BULK_MEM_MAX_BUFFER_SEGMENTS;
	return 0;
}

/**
   End point release subroutine invoked when the reference count goes
   to 0.
   Unlinks the end point from the domain, and releases the memory.
   Must be called holding the domain mutex.
*/
static void mem_xo_end_point_release(struct c2_ref *ref)
{
	struct c2_net_end_point *ep;
	C2_ASSERT(c2_mutex_is_locked(&ep->nep_dom->nd_mutex));
	ep = container_of(ref, struct c2_net_end_point, nep_ref);
	c2_list_del(&ep->nep_dom_linkage);
	ep->nep_dom = NULL;

	struct c2_net_bulk_mem_end_point *mep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	c2_free(mep);
}

/**
   This routine will search for an existing end point in the domain, and if not
   found, will allocate and zero out space for a new end point using the
   xd_sizeof_ep field to determine the size. It will fill in the xep_address
   field with the IP and port number, and will link the end point to the domain
   link list.
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
	struct c2_net_end_point *ep;
	struct c2_net_bulk_mem_end_point *mep;
	char *dot_ip;
	int port; /* user: in_port_t, kernel: __be16 */
	struct in_addr addr;

	dot_ip = va_arg(varargs, char *);
	if (dot_ip == NULL)
		return -EINVAL;
	port = htons(va_arg(varargs, int));
	if (port == 0)
		return -EINVAL;
#ifdef __KERNEL__
	addr.s_addr = in_aton(dot_ip);
	if (addr.s_addr == 0)
		return -EINVAL;
#else
	if (inet_aton(dot_ip, &addr) == 0)
		return -EINVAL;
#endif

	/* check if its already on the domain list */
	c2_list_for_each_entry(&dom->nd_end_points, ep,
			       struct c2_net_end_point,
			       nep_dom_linkage) {
		mep = container_of(ep,struct c2_net_bulk_mem_end_point,xep_ep);
		if (mep->xep_sa.sin_addr.s_addr == addr.s_addr &&
		    mep->xep_sa.sin_port == port ){
			c2_ref_get(&ep->nep_ref); /* refcnt++ */
			*epp = ep;
			return 0;
		}
	}

	/** allocate a new end point of appropriate size */
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;
	mep = c2_alloc(dp->xd_sizeof_ep);
	memset(mep, 0, dp->xd_sizeof_ep);
	ep = &mep->xep_ep;
	c2_ref_init(&ep->nep_ref, 1, mem_xo_end_point_release);
	c2_list_link_init(&ep->nep_dom_linkage);
	c2_list_add_tail(&dom->nd_end_points, &ep->nep_dom_linkage);
	mep->xep_sa.sin_addr = addr;
	mep->xep_sa.sin_port = port;
	*epp = ep;
	return 0;
}

static int mem_xo_buf_register(struct c2_net_buffer *nb)
{
	return -ENOSYS;
}

static int mem_xo_buf_deregister(struct c2_net_buffer *nb)
{
	return -ENOSYS;
}

static int mem_xo_buf_add(struct c2_net_buffer *nb)
{
	return -ENOSYS;
}

static int mem_xo_buf_del(struct c2_net_buffer *nb)
{
	return -ENOSYS;
}

static int mem_xo_tm_init(struct c2_net_transfer_mc *tm)
{
	return -ENOSYS;
}

static int mem_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
	return -ENOSYS;
}

static int mem_xo_tm_start(struct c2_net_transfer_mc *tm)
{
	return -ENOSYS;
}

static int mem_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	return -ENOSYS;
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
