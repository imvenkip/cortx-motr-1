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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 *                  Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 05/04/2010
 */

#include <linux/module.h>
#include <linux/init.h>

#include "c2t1fs/linux_kernel/c2t1fs.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"  /* C2_LOG and C2_ENTRY */
#include "lib/memory.h"  /* C2_ALLOC_PTR */
#include "net/bulk_sunrpc.h"
#include "ioservice/io_fops.h"
#include "rpc/rpclib.h"

static char *local_addr = EP_SERVICE(6);

module_param(local_addr, charp, S_IRUGO);
MODULE_PARM_DESC(local_addr, "End-point address of c2t1fs "
		 "e.g. 127.0.0.1:12345:6");

static uint32_t tm_recv_queue_min_len = C2_NET_TM_RECV_QUEUE_DEF_LEN;
module_param(tm_recv_queue_min_len , int, S_IRUGO);
MODULE_PARM_DESC(tm_recv_queue_min_len, "TM receive queue minimum length");

static uint32_t max_rpc_msg_size = 0;
module_param(max_rpc_msg_size, int, S_IRUGO);
MODULE_PARM_DESC(max_rpc_msg_size, "Maximum RPC message size");

static int  c2t1fs_net_init(void);
static void c2t1fs_net_fini(void);

static int  c2t1fs_rpc_init(void);
static void c2t1fs_rpc_fini(void);

extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;

static struct file_system_type c2t1fs_fs_type = {
	.owner        = THIS_MODULE,
	.name         = "c2t1fs",
	.get_sb       = c2t1fs_get_sb,
	.kill_sb      = c2t1fs_kill_sb,
	.fs_flags     = FS_BINARY_MOUNTDATA
};

enum {
	C2T1FS_COB_DOM_ID = 40
};
#define C2T1FS_DB_NAME "c2t1fs.db"

struct c2t1fs_globals c2t1fs_globals = {
	.g_xprt       = &c2_net_bulk_sunrpc_xprt,
	.g_cob_dom_id = { .id = C2T1FS_COB_DOM_ID },
	.g_db_name    = C2T1FS_DB_NAME,
};

int c2t1fs_init(void)
{
	int rc;

	C2_ENTRY();

	c2t1fs_globals.g_laddr = local_addr;

	C2_LOG("local_addr: %s", local_addr);

	rc = c2_ioservice_fop_init();
	if (rc != 0)
		goto out;

	rc = c2t1fs_inode_cache_init();
	if (rc != 0)
		goto out;

	rc = c2t1fs_net_init();
	if (rc != 0)
		goto icache_fini;

	rc = c2t1fs_rpc_init();
	if (rc != 0)
		goto net_fini;

	rc = register_filesystem(&c2t1fs_fs_type);
	if (rc != 0)
		goto rpc_fini;

	C2_LEAVE("rc: 0");
	return 0;

rpc_fini:
	c2t1fs_rpc_fini();

net_fini:
	c2t1fs_net_fini();

icache_fini:
	c2t1fs_inode_cache_fini();

out:
	C2_LEAVE("rc: %d", rc);
	C2_ASSERT(rc != 0);
	return rc;
}

void c2t1fs_fini(void)
{
	C2_ENTRY();

	(void)unregister_filesystem(&c2t1fs_fs_type);

	c2t1fs_rpc_fini();
	c2t1fs_net_fini();
	c2t1fs_inode_cache_fini();
	c2_ioservice_fop_fini();

	C2_LEAVE();
}

static int c2t1fs_net_init(void)
{
	struct c2_net_xprt   *xprt;
	struct c2_net_domain *ndom;
	int		      rc;

	C2_ENTRY();

	xprt =  c2t1fs_globals.g_xprt;
	ndom = &c2t1fs_globals.g_ndom;

	rc = c2_net_xprt_init(xprt);
	if (rc != 0)
		goto out;

	rc = c2_net_domain_init(ndom, xprt);
	if (rc != 0)
		c2_net_xprt_fini(xprt);
out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static void c2t1fs_net_fini(void)
{
	C2_ENTRY();

	c2_net_domain_fini(&c2t1fs_globals.g_ndom);
	c2_net_xprt_fini(c2t1fs_globals.g_xprt);

	C2_LEAVE();
}

static int c2t1fs_rpc_init(void)
{
	struct c2_dbenv           *dbenv;
	struct c2_cob_domain      *cob_dom;
	struct c2_cob_domain_id   *cob_dom_id;
	struct c2_rpc_machine     *rpc_machine;
	struct c2_net_domain      *ndom;
	struct c2_net_transfer_mc *tm;
	char                      *laddr;
	char                      *db_name;
	int                        rc;
	struct c2_net_buffer_pool *buffer_pool;
	uint32_t		   segs_nr;
	uint32_t		   bufs_nr;
	uint32_t		   tms_nr;
	c2_bcount_t		   seg_size;

	C2_ENTRY();

	ndom        = &c2t1fs_globals.g_ndom;
	laddr       =  c2t1fs_globals.g_laddr;
	rpc_machine = &c2t1fs_globals.g_rpc_machine;
	buffer_pool = &c2t1fs_globals.g_buffer_pool;

	seg_size = c2_rpc_seg_size(ndom);
	segs_nr  = c2_rpc_segs_nr(ndom, seg_size);
	tms_nr	 = 1;
	bufs_nr  = c2_rpc_bufs_nr(tm_recv_queue_min_len, tms_nr);

	rc = c2_rpc_net_buffer_pool_setup(ndom, buffer_pool,
					  segs_nr, seg_size,
					  bufs_nr, tms_nr);
	if (rc != 0)
		goto pool_fini;

	dbenv   = &c2t1fs_globals.g_dbenv;
	db_name =  c2t1fs_globals.g_db_name;

	rc = c2_dbenv_init(dbenv, db_name, 0);
	if (rc != 0)
		goto pool_fini;

	cob_dom    = &c2t1fs_globals.g_cob_dom;
	cob_dom_id = &c2t1fs_globals.g_cob_dom_id;

	rc = c2_cob_domain_init(cob_dom, dbenv, cob_dom_id);
	if (rc != 0)
		goto dbenv_fini;

	c2_rpc_machine_params_add(rpc_machine, ndom, C2_BUFFER_ANY_COLOUR,
				  max_rpc_msg_size, tm_recv_queue_min_len);

	rc = c2_rpc_machine_init(rpc_machine, cob_dom, ndom, laddr, NULL,
				 buffer_pool);
	if (rc != 0)
		goto cob_dom_fini;

	tm = &rpc_machine->rm_tm;
	C2_ASSERT(tm->ntm_recv_pool == buffer_pool);

	C2_LEAVE("rc: %d", rc);
	return 0;

cob_dom_fini:
	c2_cob_domain_fini(cob_dom);

dbenv_fini:
	c2_dbenv_fini(dbenv);

pool_fini:
	c2_rpc_net_buffer_pool_cleanup(buffer_pool);
	C2_LEAVE("rc: %d", rc);
	C2_ASSERT(rc != 0);
	return rc;
}

static void c2t1fs_rpc_fini(void)
{
	C2_ENTRY();

	c2_rpc_machine_fini(&c2t1fs_globals.g_rpc_machine);
	c2_cob_domain_fini(&c2t1fs_globals.g_cob_dom);
	c2_dbenv_fini(&c2t1fs_globals.g_dbenv);
	c2_rpc_net_buffer_pool_cleanup(&c2t1fs_globals.g_buffer_pool);

	C2_LEAVE();
}
