/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "net/net_internal.h"
#include "fop/fop_format_def.h"

/**
   @addtogroup net Networking.
 */

const struct c2_addb_loc c2_net_addb_loc = {
	.al_name = "net"
};

const struct c2_addb_ctx_type c2_net_addb_ctx = {
	.act_name = "net"
};

struct c2_addb_ctx c2_net_addb;

#include "net/net_otw_types.ff"

static struct c2_fop_type_format *fmts[] = {
	&c2_net_buf_desc_tfmt,
};

int c2_net_init()
{
	int rc;
	c2_addb_ctx_init(&c2_net_addb, &c2_net_addb_ctx, &c2_addb_global_ctx);
	rc = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	return rc;
}

void c2_net_fini()
{
	c2_fop_type_format_fini_nr(fmts, ARRAY_SIZE(fmts));
}

int c2_net_xprt_init(struct c2_net_xprt *xprt)
{
	return 0;
}
C2_EXPORTED(c2_net_xprt_init);

void c2_net_xprt_fini(struct c2_net_xprt *xprt)
{
}
C2_EXPORTED(c2_net_xprt_fini);

int c2_net_desc_copy(struct c2_net_buf_desc *from_desc,
		     struct c2_net_buf_desc *to_desc)
{
	C2_PRE(from_desc->nbd_len > 0);
	to_desc->nbd_data = c2_alloc(from_desc->nbd_len);
	if ( to_desc->nbd_data == NULL ) {
		C2_ADDB_ADD(&c2_net_addb, &c2_net_addb_loc, c2_addb_oom);
		return -ENOMEM;
	}
	memcpy(to_desc->nbd_data, from_desc->nbd_data, from_desc->nbd_len);
	to_desc->nbd_len = from_desc->nbd_len;
	return 0;
}
C2_EXPORTED(c2_net_desc_copy);

void c2_net_desc_free(struct c2_net_buf_desc *desc)
{
	if ( desc->nbd_len > 0 ) {
		C2_PRE(desc->nbd_data != NULL);
		c2_free(desc->nbd_data);
		desc->nbd_len = 0;
	}
	desc->nbd_data = NULL;
}
C2_EXPORTED(c2_net_desc_free);

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
