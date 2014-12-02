/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED, A SEAGATE COMPANY
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
 * Original authors: James Morse   <james_morse@xyratex.com>,
 *                   Juan Gonzalez <juan_gonzalez@xyratex.com>,
 *                   Sining Wu     <sining_wu@xyratex.com>
 * Original creation date: 07-May-2014
 */

#include <linux/fs.h>	/* struct file, struct inode */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0T1FS
#include "lib/trace.h"

#include "lib/assert.h"
#include "m0t1fs/linux_kernel/ioctl.h"
#include "m0t1fs/linux_kernel/fsync.h"
#include "m0t1fs/linux_kernel/m0t1fs.h"
#include "m0t1fs/linux_kernel/file_internal.h"

M0_INTERNAL long m0t1fs_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	struct inode        *inode;
	struct m0t1fs_inode *m0inode;
	int                  rc;

	M0_ENTRY();

	inode = file_inode(filp);
	M0_ASSERT(inode != NULL);

	m0inode = m0t1fs_inode_to_m0inode(inode);
	M0_ASSERT(m0inode != NULL);

	switch(cmd) {
	case M0_M0T1FS_FWAIT:
		rc = m0t1fs_fsync_core(m0inode, M0_FSYNC_MODE_PASSIVE);
		break;
	default:
		return M0_ERR_INFO(-ENOTTY, "Unknown IOCTL.");
	}

	return M0_RC(rc);
}
