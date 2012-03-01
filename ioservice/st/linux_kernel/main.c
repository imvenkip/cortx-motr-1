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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original creation date: 02/23/2012
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/inet.h>

#include "lib/memory.h"
#include "ioservice/io_fops.h"
#include "ioservice/ut/bulkio_common.h"

/*
 * By default, this kernel module acts as a client side for bulk IO.
 * A separate user-space server is started before this module is loaded.
 * This kernel modules communicates with server using the
 * server_address:server_port:service_id combination.
 * As of now, only IPv4 addressing is supported since we are using
 * bulk_sunrpc inherently.
 * Please see bulkio.sh for more details.
 */

/* Module parameters supported by bulkio-st. */
/*
 * server address.
 * server port.
 * client address.
 * client port.
 */
//char saddr[INET_ADDRSTRLEN];
char *saddr;
module_param(saddr, charp, S_IRUGO);
MODULE_PARM_DESC(saddr, "Server address.");

int sport;
module_param(sport, int, S_IRUGO);
MODULE_PARM_DESC(sport, "Server port number.");

//char caddr[INET_ADDRSTRLEN];
char *caddr;
module_param(caddr, charp, S_IRUGO);
MODULE_PARM_DESC(caddr, "Client address.");

int cport;
module_param(cport, int, S_IRUGO);
MODULE_PARM_DESC(cport, "Port number.");

static int __init c2_bulkio_kern_st_init(void)
{
	int		      rc;
	struct bulkio_params *bp;

	/* Only IPv4 style addresses are supported at the moment. */
	if (in_aton(saddr) == 0 || in_aton(caddr) == 0) {
		printk(KERN_ERR "Only IPv4 style dotted quartet addresses \
				  are supported.\n");
		return -EINVAL;
	}

	printk(KERN_INFO "Bulk IO System Test started.\n");
	printk(KERN_INFO "Server addr = %s, Server port_no = %d, \
			  Client addr = %s, Client port_no = %d\n",
	       saddr, sport, caddr, cport);

	C2_ALLOC_PTR(bp);
	C2_ASSERT(bp != NULL);
	bulkio_params_init(bp);

	rc = bulkio_client_start(bp, caddr, cport, saddr, sport);
	if (rc != 0) {
		printk(KERN_ERR "BulkI IO client failed to start up.\
			         rc = %d.\n", rc);
		bulkio_params_fini(bp);
		c2_free(bp);
		return rc;
	}

	/*
	 * Multiple threads send one IO fop each, to the bulk server.
	 * This call waits until replies for all IO fops are received.
	 */
	bulkio_test(bp, IO_FIDS_NR, IO_FOPS_NR, IO_SEGS_NR);

	bulkio_client_stop(bp->bp_cctx);

	bulkio_params_fini(bp);
	c2_free(bp);

	printk(KERN_INFO "Bulk IO System Test completed successfully.\n");
	return 0;
}

static void __exit c2_bulkio_kern_st_fini(void)
{
	printk(KERN_INFO "Bulk IO System Test module removed.\n");
}

module_init(c2_bulkio_kern_st_init)
module_exit(c2_bulkio_kern_st_fini)

MODULE_AUTHOR("Xyratex");
MODULE_DESCRIPTION("Colibri Bulk IO System Test.");
MODULE_VERSION("1:1.0");
MODULE_LICENSE("proprietary");
