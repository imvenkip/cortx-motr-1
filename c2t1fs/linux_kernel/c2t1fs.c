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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 *                  Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 05/04/2010
 */

#include <linux/module.h>
#include <linux/init.h>

#include "c2t1fs/linux_kernel/c2t1fs.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"  /* C2_LOG and C2_ENTRY */
#include "net/bulk_sunrpc.h"
#include "ioservice/io_fops.h"
#include "rpc/rpclib.h"

static char *local_addr = EP_SERVICE(6);

module_param(local_addr, charp, S_IRUGO);
MODULE_PARM_DESC(local_addr, "End-point address of c2t1fs "
		 "e.g. 127.0.0.1:12345:6");

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
	int                   rc;

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
	struct c2_dbenv          *dbenv;
	struct c2_cob_domain     *cob_dom;
	struct c2_cob_domain_id  *cob_dom_id;
	struct c2_rpcmachine     *rpcmachine;
	struct c2_net_domain     *ndom;
	struct c2_net_transfer_mc *tm;
	char                     *laddr;
	char                     *db_name;
	int                       rc;
	static uint32_t		  tm_colours;

	C2_ENTRY();

	dbenv   = &c2t1fs_globals.g_dbenv;
	db_name =  c2t1fs_globals.g_db_name;

	rc = c2_dbenv_init(dbenv, db_name, 0);
	if (rc != 0)
		goto out;

	cob_dom    = &c2t1fs_globals.g_cob_dom;
	cob_dom_id = &c2t1fs_globals.g_cob_dom_id;

	rc = c2_cob_domain_init(cob_dom, dbenv, cob_dom_id);
	if (rc != 0)
		goto dbenv_fini;

	ndom       = &c2t1fs_globals.g_ndom;
	laddr      =  c2t1fs_globals.g_laddr;
	rpcmachine = &c2t1fs_globals.g_rpcmachine;

	rc = c2_net_buffer_pool_setup(ndom);
	if (rc != 0)
		goto pool_fini;
	
	rc = c2_rpcmachine_init(rpcmachine, cob_dom, ndom, laddr, NULL/*reqh*/);
	if (rc != 0)
		goto cob_dom_fini;
	
	tm = &rpcmachine->cr_tm;
	
	c2_net_tm_colour_set(tm, tm_colours++);

	C2_LEAVE("rc: %d", rc);
	return 0;

cob_dom_fini:
	c2_cob_domain_fini(cob_dom);

pool_fini:
	c2_net_buffer_pool_cleanup(ndom);

dbenv_fini:
	c2_dbenv_fini(dbenv);

out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static void c2t1fs_rpc_fini(void)
{
	C2_ENTRY();

	c2_rpcmachine_fini(&c2t1fs_globals.g_rpcmachine);
	c2_net_buffer_pool_cleanup(&c2t1fs_globals.g_ndom);
	c2_cob_domain_fini(&c2t1fs_globals.g_cob_dom);
	c2_dbenv_fini(&c2t1fs_globals.g_dbenv);

	C2_LEAVE();
}
