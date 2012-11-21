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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 10/13/2011
 */

#include <linux/module.h>
#include <linux/init.h>
#include "lib/list.h"
#include "colibri/init.h"

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Colibri Library");
MODULE_LICENSE("GPL");


C2_INTERNAL int init_module(void)
{
        printk("Colibri init\n");
	return c2_init();
}

C2_INTERNAL void cleanup_module(void)
{
        printk("Colibri cleanup\n");
	c2_fini();
}

