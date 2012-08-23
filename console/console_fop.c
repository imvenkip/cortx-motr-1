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
#include "fop/fom_generic.h"    /* c2_generic_conf */

#include "console/console_fop.h" /* FOPs defs */
#include "console/console_fom.h" /* FOMs defs */
#include "console/console_xc.h"	 /* FOP memory layout */

/**
   @addtogroup console
   @{
*/

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

extern const struct c2_fom_type_ops c2_cons_fom_device_type_ops;

int c2_console_fop_init(void)
{
	c2_xc_console_xc_init();

	return  C2_FOP_TYPE_INIT(&c2_cons_fop_device_fopt,
			 .name      = "Device Failed",
			 .opcode    = C2_CONS_FOP_DEVICE_OPCODE,
			 .xt        = c2_cons_fop_device_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST,
			 .sm        = &c2_generic_conf,
			 .fom_ops   = &c2_cons_fom_device_type_ops) ?:
		C2_FOP_TYPE_INIT(&c2_cons_fop_reply_fopt,
			 .name      = "Console Reply",
			 .opcode    = C2_CONS_FOP_REPLY_OPCODE,
			 .xt        = c2_cons_fop_reply_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		C2_FOP_TYPE_INIT(&c2_cons_fop_test_fopt,
			 .name      = "Console Test",
			 .opcode    = C2_CONS_TEST,
			 .xt        = c2_cons_fop_test_xc,
			 .sm        = &c2_generic_conf,
			 .rpc_flags = 0);
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

