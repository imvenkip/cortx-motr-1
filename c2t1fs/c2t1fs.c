#include <linux/module.h>
#include <linux/init.h>

#include "c2t1fs/c2t1fs.h"

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
	.fs_flags     = FS_BINARY_MOUNTDATA | FS_REQUIRES_DEV
};

enum {
	C2T1FS_COB_DOM_ID = 40
};
#define C2T1FS_DB_NAME "c2t1fs.db"

struct c2t1fs_globals c2t1fs_globals = {
	.g_xprt       = &c2_net_bulk_sunrpc_xprt,
	.g_laddr      = "127.0.0.1:12345:1",
	.g_cob_dom_id = { .id = C2T1FS_COB_DOM_ID },
	.g_db_name    = C2T1FS_DB_NAME,
};

int c2t1fs_init(void)
{
	int rc;

	START();

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

	END(0);
	return 0;

rpc_fini:
	c2t1fs_rpc_fini();

net_fini:
	c2t1fs_net_fini();

icache_fini:
	c2t1fs_inode_cache_fini();

out:
	END(rc);
	C2_ASSERT(rc != 0);
	return rc;
}

void c2t1fs_fini(void)
{
	START();

	(void)unregister_filesystem(&c2t1fs_fs_type);

	c2t1fs_rpc_fini();
	c2t1fs_net_fini();
	c2t1fs_inode_cache_fini();

	END(0);
}

static int c2t1fs_net_init(void)
{
	struct c2_net_xprt   *xprt;
	struct c2_net_domain *ndom;
	int                   rc;

	START();

	xprt =  c2t1fs_globals.g_xprt;
	ndom = &c2t1fs_globals.g_ndom;

	rc = c2_net_xprt_init(xprt);
	if (rc != 0)
		goto out;

	rc = c2_net_domain_init(ndom, xprt);
	if (rc != 0)
		c2_net_xprt_fini(xprt);
out:
	END(rc);
	return rc;
}

static void c2t1fs_net_fini(void)
{
	START();

	c2_net_domain_fini(&c2t1fs_globals.g_ndom);
	c2_net_xprt_fini(c2t1fs_globals.g_xprt);

	END(0);
}

static int c2t1fs_rpc_init(void)
{
	struct c2_dbenv          *dbenv;
	struct c2_cob_domain     *cob_dom;
	struct c2_cob_domain_id  *cob_dom_id;
	struct c2_rpcmachine     *rpcmachine;
	struct c2_net_domain     *ndom;
	char                     *laddr;
	char                     *db_name;
	int                       rc;

	START();

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

	rc = c2_rpcmachine_init(rpcmachine, cob_dom, ndom, laddr, NULL/*reqh*/);
	if (rc != 0)
		goto cob_dom_fini;

	END(rc);
	return 0;

cob_dom_fini:
	c2_cob_domain_fini(cob_dom);

dbenv_fini:
	c2_dbenv_fini(dbenv);

out:
	END(rc);
	return rc;
}

static void c2t1fs_rpc_fini(void)
{
	START();

	c2_rpcmachine_fini(&c2t1fs_globals.g_rpcmachine);
	c2_cob_domain_fini(&c2t1fs_globals.g_cob_dom);
	c2_dbenv_fini(&c2t1fs_globals.g_dbenv);

	END(0);
}
