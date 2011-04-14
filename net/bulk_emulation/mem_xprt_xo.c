/* -*- C -*- */

#include "net/bulk_emulation/mem_xprt_pvt.h"

/**
   @addtogroup bulkmem
   @{
 */

static int mem_xo_dom_init(struct c2_net_xprt *xprt, 
			   struct c2_net_domain *dom)
{
	return -ENOSYS;
}

static void mem_xo_dom_fini(struct c2_net_domain *dom)
{
}

static int mem_xo_get_max_buffer_size(struct c2_net_domain *dom, 
				      c2_bcount_t *size)
{
	return -ENOSYS;
}

static int mem_xo_get_max_buffer_segment_size(struct c2_net_domain *dom,
					      c2_bcount_t *size)
{
	return -ENOSYS;
}

static int mem_xo_get_max_buffer_segments(struct c2_net_domain *dom,
					  int32_t *num_segs)
{
	return -ENOSYS;
}

static int mem_xo_end_point_create(struct c2_net_end_point **epp,
				   struct c2_net_domain *dom,
				   va_list varargs)
{
	return -ENOSYS;
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
