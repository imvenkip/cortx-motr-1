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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 05/05/2012
 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "onwire.h"
#include "fop/fop_iterator.h"
#include "conf_fop.h"
#include "conf_fom.h"
#include "onwire.ff"
#include "lib/errno.h"
#include "rpc/rpc.h"
#include "fop/fop_item_type.h"
#include "xcode/bufvec_xcode.h"

/* Ops vector for fetch request. */
const struct c2_fop_type_ops c2_conf_fetch_ops = {
	.fto_fop_replied = NULL,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = NULL,
};

/* Ops vector for fetch reply. */
const struct c2_fop_type_ops c2_conf_fetch_resp_ops = {
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_io_coalesce = NULL,
};

/* Ops vector for update request. */
const struct c2_fop_type_ops c2_conf_update_ops = {
	.fto_fop_replied = NULL,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = NULL,
};

/* Ops vector for update reply. */
const struct c2_fop_type_ops c2_conf_update_resp_ops = {
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_io_coalesce = NULL,
};

/* Declaration of fetch FOPs */
C2_FOP_TYPE_DECLARE(c2_conf_fetch, "fetch fop", &c2_conf_fetch_ops,
		    C2_RPC_FETCH_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO);

C2_FOP_TYPE_DECLARE(c2_conf_fetch_resp, "fetch fop reply", &c2_conf_fetch_resp_ops,
		    C2_RPC_FETCH_REPLY_OPCODE, C2_RPC_ITEM_TYPE_REPLY);

/* Declaration of update FOPs */
C2_FOP_TYPE_DECLARE(c2_conf_update, "update fop", &c2_conf_update_ops,
		    C2_RPC_UPDATE_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO);

C2_FOP_TYPE_DECLARE(c2_conf_update_resp, "update fop reply", &c2_conf_update_resp_ops,
		    C2_RPC_UPDATE_REPLY_OPCODE, C2_RPC_ITEM_TYPE_REPLY);

static struct c2_fop_type_format *fmts[] = {
	&arr_u64_tfmt,
	&arr_buf_tfmt,
	&arr_pathcomp_tfmt,
	&enconf_tfmt,
	&c2_conf_pathcomp_tfmt,
	&objval_tfmt
};


static struct c2_fop_type *fops[] = {
        &c2_conf_fetch_fopt,
        &c2_conf_fetch_resp_fopt,
        &c2_conf_update_fopt,
        &c2_conf_update_resp_fopt,
};

C2_INTERNAL void c2_conf_fop_fini(void)
{
        c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
}

extern struct c2_fom_type c2_fom_fetch_mopt;
extern struct c2_fom_type c2_fom_update_mopt;


C2_INTERNAL int c2_conf_fop_init(void)
{
        int result;
	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
        result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));

	c2_conf_fetch_fopt.ft_fom_type = c2_fom_fetch_mopt;
	c2_conf_update_fopt.ft_fom_type = c2_fom_update_mopt;

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
