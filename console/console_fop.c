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
#include <config.h>
#endif

#include "lib/memory.h"		/* C2_ALLOC_PTR */
#include "lib/errno.h"		/* ENOMEM */
#include "fop/fop_onwire.h"	/* default fop encode/decode */
#include "fop/fop_format_def.h" /* console.ff */


#include "console/console_fop.h" /*FOPs defs */
#include "console/console_fom.h" /*FOMs defs */
#include "console/console_u.h"	 /* FOP memory layout */
#include "console/console.ff"
#include "xcode/bufvec_xcode.h"  /* c2_xcode_fop_size_get() */

/**
   @addtogroup console fops
   @{
*/

static int c2_cons_fop_disk_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
        struct c2_cons_fom_disk *fom_disk;
        struct c2_fom           *fom;

        C2_PRE(fop != NULL);
        C2_PRE(m != NULL);

        C2_ALLOC_PTR(fom_disk);
        if (fom_disk == NULL)
                return -ENOMEM;

        fop->f_type->ft_fom_type = c2_cons_fom_disk_type;

        fom = &fom_disk->disk_gen;
	fom->fo_fop = fop;
        fom->fo_type = &c2_cons_fom_disk_type;
        fom->fo_ops = &c2_cons_fom_disk_ops;

        fom_disk->disk_fop = fop;
        fom_disk->disk_reply_fop = c2_fop_alloc(&c2_cons_fop_reply_fopt,
                                                NULL);
        if (fom_disk->disk_reply_fop == NULL) {
                c2_free(fom_disk);
                return -ENOMEM;
        }

        *m = fom;
        return 0;
}

/** Ops vector for disk failure notification. */
static struct c2_fop_type_ops c2_cons_fop_disk_ops = {
	.fto_fom_init = &c2_cons_fop_disk_fom_init,
	.fto_size_get = c2_xcode_fop_size_get
};

static int c2_cons_fop_device_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
        struct c2_cons_fom_device *fom_dev;
        struct c2_fom             *fom;

        C2_PRE(fop != NULL);
        C2_PRE(m != NULL);

        C2_ALLOC_PTR(fom_dev);
        if (fom_dev == NULL)
                return -ENOMEM;

        fop->f_type->ft_fom_type = c2_cons_fom_device_type;

        fom = &fom_dev->dev_gen;
	fom->fo_fop = fop;
        fom->fo_type = &c2_cons_fom_device_type;
        fom->fo_ops = &c2_cons_fom_device_ops;

        fom_dev->dev_fop = fop;
        fom_dev->dev_reply_fop = c2_fop_alloc(&c2_cons_fop_reply_fopt,
                                                NULL);
        if (fom_dev->dev_reply_fop == NULL) {
                c2_free(fom_dev);
                return -ENOMEM;
        }

        *m = fom;
        return 0;
}

/* Ops vector for device failure notification */
static struct c2_fop_type_ops c2_cons_fop_device_ops = {
	.fto_fom_init = &c2_cons_fop_device_fom_init,
	.fto_size_get = c2_xcode_fop_size_get
};

/* Ops vector for reply of any failure notification */
static struct c2_fop_type_ops c2_cons_fop_reply_ops = {
	.fto_size_get = c2_xcode_fop_size_get
};

/** Fop and RPC Item type definitions for disk and device failures and replies
    and replies
*/
C2_FOP_TYPE_DECLARE(c2_cons_fop_disk, "Disk Failed",
		    &c2_cons_fop_disk_ops,
		    C2_CONS_FOP_DISK_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST);

C2_FOP_TYPE_DECLARE(c2_cons_fop_device, "Device Failed",
		    &c2_cons_fop_device_ops,
		    C2_CONS_FOP_DEVICE_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST);

C2_FOP_TYPE_DECLARE(c2_cons_fop_reply, "Console Reply",
		    &c2_cons_fop_reply_ops,
		    C2_CONS_FOP_REPLY_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY);

C2_FOP_TYPE_DECLARE_OPS(c2_cons_fop_test, "Console Test",
			NULL,
			C2_CONS_TEST,
			0,
			NULL);


static struct c2_fop_type *fops[] = {
        &c2_cons_fop_disk_fopt,
        &c2_cons_fop_device_fopt,
        &c2_cons_fop_reply_fopt,
        &c2_cons_fop_test_fopt
};

static struct c2_fop_type_format *fmts[] = {
	&c2_fop_fid_tfmt,
        &c2_fop_seg_tfmt,
	&c2_fop_vec_tfmt,
	&c2_fop_optfid_tfmt,
	&c2_fop_fid_typedef_tfmt,
};

void c2_console_fop_fini(void)
{
        c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
	c2_fop_type_format_fini_nr(fmts, ARRAY_SIZE(fmts));
}

int c2_console_fop_init(void)
{
        int result;

	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	if (result == 0)
		result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));

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

