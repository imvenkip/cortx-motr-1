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
 * Original creation date: 08/06/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#ifdef __KERNEL__
#include "rdwr_fop_k.h"
#else
#include "rdwr_fop_u.h"
#endif
#include "fop/fop_iterator.h"
#include "fop/ut/long_lock/rdwr_fop.h"
#include "fop/ut/long_lock/rdwr_fom.h"
#include "fop/ut/long_lock/rdwr_fop.ff"
#include "lib/errno.h"
#include "rpc/rpc2.h"
#include "fop/fop_item_type.h"
#include "xcode/bufvec_xcode.h"

/* Ops vector for rdwr request. */
const struct c2_fop_type_ops c2_fop_rdwr_ops = {
	.fto_fop_replied = NULL,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = NULL,
};

/* Ops vector for rdwr reply. */
const struct c2_fop_type_ops c2_fop_rdwr_rep_ops = {
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_io_coalesce = NULL,
};

/* Rdwr fop assignment */
C2_FOP_TYPE_DECLARE(c2_fop_rdwr, "RDWR fop", &c2_fop_rdwr_ops,
		    C2_FOP_RDWR_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO);

C2_FOP_TYPE_DECLARE(c2_fop_rdwr_rep, "RDWR fop reply", &c2_fop_rdwr_rep_ops,
		    C2_FOP_RDWR_REPLY_OPCODE, C2_RPC_ITEM_TYPE_REPLY);

/* static struct c2_fop_type_format *fmts[] = { */
/*         &c2_fop_rdwr_arr_tfmt, */
/* }; */


static struct c2_fop_type *fops[] = {
        &c2_fop_rdwr_fopt,
        &c2_fop_rdwr_rep_fopt,
};

void c2_rdwr_fop_fini(void)
{
        c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
}

extern struct c2_fom_type c2_fom_rdwr_mopt;

int c2_rdwr_fop_init(void)
{
        int result;
	/* result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts)); */
        result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
	c2_fop_rdwr_fopt.ft_fom_type = c2_fom_rdwr_mopt;
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
