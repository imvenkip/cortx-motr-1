/* -*- C -*- */

#include "lib/errno.h"
#include "net/bulk_emulation/sunrpc_xprt.h"

/**
   @addtogroup bulksunrpc
   @{
 */

static int sunrpc_xo_dom_init(struct c2_net_xprt *xprt,
			      struct c2_net_domain *dom)
{
	return -ENOSYS;
}

static void sunrpc_xo_dom_fini(struct c2_net_domain *dom)
{
}

static c2_bcount_t sunrpc_xo_get_max_buffer_size(struct c2_net_domain *dom)
{
	return 0;
}

static c2_bcount_t sunrpc_xo_get_max_buffer_segment_size(struct c2_net_domain
							 *dom)
{
	return 0;
}

static int32_t sunrpc_xo_get_max_buffer_segments(struct c2_net_domain *dom)
{
	return 0;
}

static int sunrpc_xo_end_point_create(struct c2_net_end_point **epp,
				      struct c2_net_domain *dom,
				      va_list varargs)
{
	return -ENOSYS;
}

static int sunrpc_xo_buf_register(struct c2_net_buffer *nb)
{
	return -ENOSYS;
}

static int sunrpc_xo_buf_deregister(struct c2_net_buffer *nb)
{
	return -ENOSYS;
}

static int sunrpc_xo_buf_add(struct c2_net_buffer *nb)
{
	return -ENOSYS;
}

static int sunrpc_xo_buf_del(struct c2_net_buffer *nb)
{
	return -ENOSYS;
}

static int sunrpc_xo_tm_init(struct c2_net_transfer_mc *tm)
{
	return -ENOSYS;
}

static int sunrpc_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
	return -ENOSYS;
}

static int sunrpc_xo_tm_start(struct c2_net_transfer_mc *tm)
{
	return -ENOSYS;
}

static int sunrpc_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	return -ENOSYS;
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
