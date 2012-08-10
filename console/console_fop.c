/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author       : Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/03/2011
 * Revision              : Manish Honap <Manish_Honap@xyratex.com>
 * Revision date         : 07/31/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/memory.h"		/* C2_ALLOC_PTR */
#include "lib/errno.h"		/* ENOMEM */
#include "fop/fop_item_type.h"	/* default fop encode/decode */
#include "fop/fop.h"            /* c2_fop_xcode_length */

#include "console/console_fop.h" /* FOPs defs */
#include "console/console_fom.h" /* FOMs defs */
#include "console/console_xc.h"	 /* FOP memory layout */

/**
   @addtogroup console
   @{
*/

/** Default fop type operations */
static const struct c2_fop_type_ops c2_cons_fop_default_ops = {
	.fto_size_get = c2_fop_xcode_length
};

struct c2_fop_type c2_cons_fop_device_fopt;
struct c2_fop_type c2_cons_fop_reply_fopt;
struct c2_fop_type c2_cons_fop_test_fopt;

void c2_console_fop_fini(void)
{
	c2_fop_type_fini(&c2_cons_fop_device_fopt);
	c2_fop_type_fini(&c2_cons_fop_reply_fopt);
	c2_fop_type_fini(&c2_cons_fop_test_fopt);
	c2_xc_console_xc_fini();
}

int c2_console_fop_init(void)
{
	c2_xc_console_xc_init();

	/* Initialize fom type once */
	c2_cons_fop_device_fopt.ft_fom_type =
		c2_cons_fom_device_type;

	return  C2_FOP_TYPE_INIT(&c2_cons_fop_device_fopt,
			 .name      = "Device Failed",
			 .opcode    = C2_CONS_FOP_DEVICE_OPCODE,
			 .xt        = c2_cons_fop_device_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST,
			 .fop_ops   = &c2_cons_fop_default_ops,
			 .fom_ops   = c2_cons_fom_device_type.ft_ops) ?:
		C2_FOP_TYPE_INIT(&c2_cons_fop_reply_fopt,
			 .name      = "Console Reply",
			 .opcode    = C2_CONS_FOP_REPLY_OPCODE,
			 .xt        = c2_cons_fop_reply_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY,
			 .fop_ops   = &c2_cons_fop_default_ops) ?:
		C2_FOP_TYPE_INIT(&c2_cons_fop_test_fopt,
			 .name      = "Console Test",
			 .opcode    = C2_CONS_TEST,
			 .xt        = c2_cons_fop_test_xc,
			 .rpc_flags = 0,
			 .fop_ops   = &c2_cons_fop_default_ops);
}

/** @} end of console */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

