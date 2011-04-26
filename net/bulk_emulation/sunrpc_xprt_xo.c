/* -*- C -*- */

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "net/bulk_emulation/sunrpc_xprt_pvt.h"
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

/* To reduce global symbols, yet make the code readable, we
   include other .c files with static symbols into this file.
   Dependency information must be captured in Makefile.am.

   Static functions should be declared in the private header file
   so that the order of their definiton does not matter.
*/
#include "sunrpc_xprt_ep.c"
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

static int sunrpc_xo_dom_init(struct c2_net_xprt *xprt,
			      struct c2_net_domain *dom)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp;

	C2_PRE(dom->nd_xprt_private == NULL);
	C2_ALLOC_PTR(dp);
	if (dp == NULL)
		return -ENOMEM;
	dom->nd_xprt_private = dp;
	dp->xd_base_work_fn[C2_NET_XOP_ACTIVE_BULK] = sunrpc_wf_active_bulk;

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
	struct c2_net_transfer_mc *tm = nb->nb_tm;
	struct c2_net_bulk_mem_buffer_pvt *bp = nb->nb_xprt_private;

	int rc;
	switch (nb->nb_qtype) {
	case C2_NET_QT_PASSIVE_BULK_SEND:
		rc = sunrpc_desc_create(&nb->nb_desc, nb->nb_ep, tm,
					nb->nb_qtype, nb->nb_length,
					bp->xb_buf_id);
		if (rc != 0)
			return rc;
	default:
		return -ENOSYS;
	}

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
