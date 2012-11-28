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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 02/30/2012
 */


#include <linux/module.h>      /* MODULE_XXX */
#include <linux/init.h>        /* module_init */
#include <linux/debugfs.h>     /* debugfs_create_dir */
#include <linux/kernel.h>      /* pr_err */

#include "finject_debugfs.h"   /* fi_dfs_init */

/**
 * @defgroup km0ctl Mero Kernel-space Control
 *
 * @brief km0ctl driver provides a debugfs interface to control kmero in
 * runtime. All control files are placed under "mero/" directory in the root
 * of debugfs file system. For more details about debugfs please @see
 * Documentation/filesystems/debugfs.txt in the linux kernel's source tree.
 *
 * This file is responsible only for creation and cleanup of mero/ directory
 * in debugfs. Please, put all code related to a particular control interface
 * into a separate *.c file. @see finject_debugfs.c for example.
 */


struct dentry  *dfs_root_dir;
const char     dfs_root_name[] = "mero";

int dfs_init(void)
{
	int rc;

	/* create mero's main debugfs directory */
	dfs_root_dir = debugfs_create_dir(dfs_root_name, NULL);
	if (dfs_root_dir == NULL) {
		pr_err("Can't create debugfs dir '%s'\n", dfs_root_name);
		return -EPERM;
	}

	rc = fi_dfs_init();
	if (rc != 0) {
		debugfs_remove_recursive(dfs_root_dir);
		dfs_root_dir = 0;
		return rc;
	}

	return 0;
}

void dfs_cleanup(void)
{
	fi_dfs_cleanup();

	/*
	 * remove all orphaned debugfs files (if any) and mero's debugfs root
	 * directroy itself
	 */
	if (dfs_root_dir != 0) {
		debugfs_remove_recursive(dfs_root_dir);
		dfs_root_dir = 0;
	}
}

int __init m0ctl_init(void)
{
	return dfs_init();
}

void __exit m0ctl_exit(void)
{
	dfs_cleanup();
}

module_init(m0ctl_init);
module_exit(m0ctl_exit);

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Mero control interface");
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
