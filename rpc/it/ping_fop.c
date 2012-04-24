/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 07/07/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#ifdef __KERNEL__
#include "ping_fop_k.h"
#else
#include "ping_fop_u.h"
#endif
#include "fop/fop_iterator.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fom.h"
#include "rpc/it/ping_fop.ff"
#include "lib/errno.h"
#include "rpc/rpc2.h"
#include "fop/fop_item_type.h"
#include "xcode/bufvec_xcode.h"

/* Ops vector for ping request. */
const struct c2_fop_type_ops c2_fop_ping_ops = {
	.fto_fop_replied = NULL,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = NULL,
};

/* Ops vector for ping reply. */
const struct c2_fop_type_ops c2_fop_ping_rep_ops = {
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_io_coalesce = NULL,
};

/* Ping fop assignment */
C2_FOP_TYPE_DECLARE_OPS(c2_fop_ping, "ping fop", &c2_fop_ping_ops,
			C2_RPC_PING_OPCODE,
			C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO,
			&c2_rpc_fop_default_item_type_ops,
			&c2_fom_ping_type_ops);

C2_FOP_TYPE_DECLARE(c2_fop_ping_rep, "ping fop reply", &c2_fop_ping_rep_ops,
		    C2_RPC_PING_REPLY_OPCODE, C2_RPC_ITEM_TYPE_REPLY);

static struct c2_fop_type_format *fmts[] = {
        &c2_fop_ping_arr_tfmt,
};


static struct c2_fop_type *fops[] = {
        &c2_fop_ping_fopt,
        &c2_fop_ping_rep_fopt,
};

void c2_ping_fop_fini(void)
{
        c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
}

int c2_ping_fop_init(void)
{
        int result;
	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	if (result == 0)
		result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
        return result;
}


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
