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
 * Modified by : Dima Chumak <dmitriy_chumak@xyratex.com>
 * Modification date: 7-Aug-2013
 */

#include <linux/kernel.h>  /* printk */
#include <linux/module.h>  /* THIS_MODULE */
#include <linux/init.h>    /* module_init */

#include "lib/list.h"
#include "mero/init.h"
#include "mero/version.h"
#include "mero/linux_kernel/module.h"


M0_INTERNAL int __init mero_init(void)
{
	pr_info("mero: init\n");
	m0_build_info_print();
	return m0_init();
}

M0_INTERNAL void __exit mero_exit(void)
{
	pr_info("mero: cleanup\n");
	m0_fini();
}

module_init(mero_init);
module_exit(mero_exit);

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Mero Library");
MODULE_LICENSE("GPL");

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
