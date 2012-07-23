/* -*- C -*- */
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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/03/2011
 */
/*
 * Failure fops should be defined by not yet existing "failure" module. For the
 * time being, it makes sense to put them in cm/ or console/. ioservice is not
 * directly responsible for handling failures, it is intersected by copy-machine
 * (cm).
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/memory.h"		/* C2_ALLOC_PTR */
#include "lib/errno.h"		/* ENOMEM */
#include "fop/fop_item_type.h"	/* default fop encode/decode */

#include "console/console_fop.h" /*FOPs defs */
#include "console/console_fom.h" /*FOMs defs */
#include "console/console_xc.h"	 /* FOP memory layout */
#include "xcode/bufvec_xcode.h"  /* c2_xcode_fop_size_get() */

/**
   @addtogroup console fops
   @{
*/

/* Ops vector for device failure notification */
static const struct c2_fop_type_ops c2_cons_fop_device_ops = {
	.fto_size_get = c2_xcode_fop_size_get
};

/* Ops vector for reply of any failure notification */
static const struct c2_fop_type_ops c2_cons_fop_reply_ops = {
	.fto_size_get = c2_xcode_fop_size_get
};

/* Fop and RPC Item type definitions for device failures and replies
   and replies */
C2_FOP_TYPE_DECLARE_XC(c2_cons_fop_device, "Device Failed",
		       &c2_cons_fop_device_ops,
		       C2_CONS_FOP_DEVICE_OPCODE,
		       C2_RPC_ITEM_TYPE_REQUEST);

C2_FOP_TYPE_DECLARE_XC(c2_cons_fop_reply, "Console Reply",
		       &c2_cons_fop_reply_ops,
		       C2_CONS_FOP_REPLY_OPCODE,
		       C2_RPC_ITEM_TYPE_REPLY);

C2_FOP_TYPE_DECLARE_XC(c2_cons_fop_test, "Console Test", NULL, C2_CONS_TEST, 0);

static struct c2_fop_type *fops[] = {
        &c2_cons_fop_device_fopt,
        &c2_cons_fop_reply_fopt,
        &c2_cons_fop_test_fopt
};

void c2_console_fop_fini(void)
{
        c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
	c2_xc_console_xc_fini();
}

int c2_console_fop_init(void)
{
        int result;

	c2_xc_console_xc_init();
	result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));

	/* Initialize fom type once */
	c2_cons_fop_device_fopt.ft_fom_type = c2_cons_fom_device_type;

	if (result != 0)
		c2_console_fop_fini();

        return result;
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

