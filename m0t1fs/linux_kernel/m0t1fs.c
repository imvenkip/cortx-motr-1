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
 * Metadata       : Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 05/04/2010
 */

#include <linux/module.h>
#include <linux/init.h>

#include "m0t1fs/linux_kernel/m0t1fs.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0T1FS
#include "lib/trace.h"  /* M0_LOG and M0_ENTRY */
#include "lib/memory.h"
#include "lib/uuid.h"   /* m0_uuid_generate */
#include "net/lnet/lnet.h"
#include "fid/fid.h"
#include "ioservice/io_fops.h"
#include "mdservice/md_fops.h"
#include "rpc/rpclib.h"
#include "rm/rm.h"

static char *node_uuid = "00000000-0000-0000-0000-000000000000"; /* nil UUID */
module_param(node_uuid, charp, S_IRUGO);
MODULE_PARM_DESC(node_uuid, "UUID of Mero node");

static char *local_addr = "0@lo:12345:45:6";

module_param(local_addr, charp, S_IRUGO);
MODULE_PARM_DESC(local_addr, "End-point address of m0t1fs "
		 "e.g. 172.18.50.40@o2ib1:12345:34:1");

static uint32_t tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
module_param(tm_recv_queue_min_len , int, S_IRUGO);
MODULE_PARM_DESC(tm_recv_queue_min_len, "TM receive queue minimum length");

static uint32_t max_rpc_msg_size = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
module_param(max_rpc_msg_size, int, S_IRUGO);
MODULE_PARM_DESC(max_rpc_msg_size, "Maximum RPC message size");

static int  m0t1fs_net_init(void);
static void m0t1fs_net_fini(void);

static int  m0t1fs_rpc_init(void);
static void m0t1fs_rpc_fini(void);

static int  m0t1fs_addb_mon_total_io_size_init(void);
static void m0t1fs_addb_mon_total_io_size_fini(void);

static int  m0t1fs_layout_init(void);
static void m0t1fs_layout_fini(void);

static int m0t1fs_reqh_services_start(void);
static void m0t1fs_reqh_services_stop(void);

struct m0_addb_ctx m0t1fs_addb_ctx;

static struct file_system_type m0t1fs_fs_type = {
	.owner        = THIS_MODULE,
	.name         = "m0t1fs",
	.get_sb       = m0t1fs_get_sb,
	.kill_sb      = m0t1fs_kill_sb,
	.fs_flags     = FS_BINARY_MOUNTDATA
};

#define M0T1FS_DB_NAME "m0t1fs.db"

struct m0t1fs_globals m0t1fs_globals = {
	.g_xprt       = &m0_net_lnet_xprt,
	.g_db_name    = M0T1FS_DB_NAME,
};

M0_INTERNAL const char *m0t1fs_param_node_uuid_get(void)
{
	return node_uuid;
}

M0_INTERNAL int m0t1fs_init(void)
{
	int rc;

	M0_ENTRY();

	m0_addb_ctx_type_register(&m0_addb_ct_m0t1fs_mod);
	m0_addb_ctx_type_register(&m0_addb_ct_m0t1fs_mountp);
	m0_addb_ctx_type_register(&m0_addb_ct_m0t1fs_op_read);
	m0_addb_ctx_type_register(&m0_addb_ct_m0t1fs_op_write);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0t1fs_addb_ctx, &m0_addb_ct_m0t1fs_mod,
	                 &m0_addb_proc_ctx);

	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_io_finish);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_cob_io_finish);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_root_cob);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_ior_sizes);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_iow_sizes);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_ior_times);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_iow_times);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_dgior_sizes);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_dgiow_sizes);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_dgior_times);
	m0_addb_rec_type_register(&m0_addb_rt_m0t1fs_dgiow_times);

	M0_ADDB_MONITOR_STATS_TYPE_REGISTER(&m0_addb_rt_m0t1fs_mon_io_size,
					    "io_size");

	m0t1fs_globals.g_laddr = local_addr;

        rc = m0_fid_init();
        if (rc != 0)
                goto out;

	rc = m0_ioservice_fop_init();
	if (rc != 0)
		goto fid_fini;

	rc = m0_mdservice_fop_init();
	if (rc != 0)
		goto ioservice_fini;

	rc = m0t1fs_inode_cache_init();
	if (rc != 0)
		goto mdservice_fini;

	rc = m0t1fs_net_init();
	if (rc != 0)
		goto icache_fini;

	rc = m0t1fs_rpc_init();
	if (rc != 0)
		goto net_fini;

	rc = m0t1fs_addb_mon_total_io_size_init();
	if (rc != 0)
		goto rpc_fini;

	rc = m0t1fs_layout_init();
	if (rc != 0)
		goto addb_mon_fini;

	rc = register_filesystem(&m0t1fs_fs_type);
	if (rc != 0)
		goto layout_fini;

	M0_LEAVE("rc: 0");
	return 0;

layout_fini:
	m0t1fs_layout_fini();
addb_mon_fini:
	m0t1fs_addb_mon_total_io_size_fini();
rpc_fini:
	m0t1fs_rpc_fini();
net_fini:
	m0t1fs_net_fini();
icache_fini:
	m0t1fs_inode_cache_fini();
mdservice_fini:
        m0_mdservice_fop_fini();
ioservice_fini:
        m0_ioservice_fop_fini();
fid_fini:
        m0_fid_fini();
out:
	m0_addb_ctx_fini(&m0t1fs_addb_ctx);

	M0_LEAVE("rc: %d", rc);
	M0_ASSERT(rc != 0);
	return rc;
}

M0_INTERNAL void m0t1fs_fini(void)
{
	M0_ENTRY();

	(void)unregister_filesystem(&m0t1fs_fs_type);

	m0t1fs_layout_fini();
	m0t1fs_rpc_fini();
	m0t1fs_net_fini();
	m0t1fs_inode_cache_fini();
	m0_mdservice_fop_fini();
	m0_ioservice_fop_fini();
	m0_fid_fini();

	m0_addb_ctx_fini(&m0t1fs_addb_ctx);

	M0_LEAVE();
}

static int m0t1fs_net_init(void)
{
	struct m0_net_xprt   *xprt;
	struct m0_net_domain *ndom;
	int		      rc;

	M0_ENTRY();

	xprt =  m0t1fs_globals.g_xprt;
	ndom = &m0t1fs_globals.g_ndom;

	rc = m0_net_xprt_init(xprt);
	if (rc != 0)
		goto out;

	/** @todo replace &m0_addb_proc_ctx */
	rc = m0_net_domain_init(ndom, xprt, &m0_addb_proc_ctx);
	if (rc != 0)
		m0_net_xprt_fini(xprt);
out:
	M0_LEAVE("rc: %d", rc);
	return rc;
}

static void m0t1fs_net_fini(void)
{
	M0_ENTRY();

	m0_net_domain_fini(&m0t1fs_globals.g_ndom);
	m0_net_xprt_fini(m0t1fs_globals.g_xprt);

	M0_LEAVE();
}

static int m0t1fs_rpc_init(void)
{
	struct m0_dbenv           *dbenv       = &m0t1fs_globals.g_dbenv;
	char                      *db_name     =  m0t1fs_globals.g_db_name;
	struct m0_rpc_machine     *rpc_machine = &m0t1fs_globals.g_rpc_machine;
	struct m0_reqh            *reqh        = &m0t1fs_globals.g_reqh;
	struct m0_net_domain      *ndom        = &m0t1fs_globals.g_ndom;
	const char                *laddr       =  m0t1fs_globals.g_laddr;
	struct m0_net_buffer_pool *buffer_pool = &m0t1fs_globals.g_buffer_pool;
	struct m0_fol             *fol         = &m0t1fs_globals.g_fol;
	struct m0_net_transfer_mc *tm;
	int                        rc;
	uint32_t		   bufs_nr;
	uint32_t		   tms_nr;

	M0_ENTRY();

	tms_nr	 = 1;
	bufs_nr  = m0_rpc_bufs_nr(tm_recv_queue_min_len, tms_nr);

	rc = m0_rpc_net_buffer_pool_setup(ndom, buffer_pool,
					  bufs_nr, tms_nr);
	if (rc != 0)
		goto pool_fini;

	rc = m0_dbenv_init(dbenv, db_name, 0);
	if (rc != 0)
		goto pool_fini;

	rc = M0_REQH_INIT(reqh,
			  .rhia_dtm          = (void*)1,
			  .rhia_db           = NULL,
			  .rhia_mdstore      = (void*)1,
			  .rhia_fol          = fol,
			  .rhia_svc          = (void*)1);
	if (rc != 0)
		goto dbenv_fini;
	rc = m0_rpc_machine_init(rpc_machine, ndom, laddr, reqh,
				 buffer_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size, tm_recv_queue_min_len);
	if (rc != 0)
		goto reqh_fini;

	m0_reqh_start(reqh);
	tm = &rpc_machine->rm_tm;
	M0_ASSERT(tm->ntm_recv_pool == buffer_pool);

	m0_reqh_rpc_mach_tlink_init_at_tail(rpc_machine,
					    &reqh->rh_rpc_machines);

	/* Start resource manager service */
	rc = m0t1fs_reqh_services_start();
	if (rc != 0)
		goto reqh_fini;
	M0_RETURN(0);

reqh_fini:
	m0_reqh_fini(reqh);
dbenv_fini:
	m0_dbenv_fini(dbenv);
pool_fini:
	m0_rpc_net_buffer_pool_cleanup(buffer_pool);
	M0_LEAVE("rc: %d", rc);
	M0_ASSERT(rc != 0);
	return rc;
}

static void m0t1fs_mon_rw_io_watch(const struct m0_addb_monitor *mon,
				   const struct m0_addb_rec     *rec,
				   struct m0_reqh               *reqh)
{
	struct m0_addb_sum_rec                  *sum_rec;
	struct m0t1fs_addb_mon_sum_data_io_size *sum_data =
				&m0t1fs_globals.g_addb_mon_sum_data_rw_io_size;

	if (m0_addb_rec_rid_make(M0_ADDB_BRT_DP, M0T1FS_ADDB_RECID_IO_FINISH)
	    == rec->ar_rid) {
		sum_rec = mon->am_ops->amo_sum_rec(mon, reqh);
		M0_ASSERT(sum_rec != NULL);

		m0_mutex_lock(&sum_rec->asr_mutex);
		if (rec->ar_data.au64s_data[0] == IRT_READ) {
			sum_data->sd_rio += rec->ar_data.au64s_data[1];
			sum_rec->asr_dirty = true;
		} else if (rec->ar_data.au64s_data[0] == IRT_WRITE) {
			sum_data->sd_wio += rec->ar_data.au64s_data[1];
			sum_rec->asr_dirty = true;
		}
		else
			M0_IMPOSSIBLE("Invalid IO state");
		m0_mutex_unlock(&sum_rec->asr_mutex);

	}
}

static struct m0_addb_sum_rec *
m0t1fs_mon_rw_io_sum_rec(const struct m0_addb_monitor *mon,
		         struct m0_reqh               *reqh)
{
	struct m0_addb_sum_rec *sum_rec;

	m0_rwlock_read_lock(&reqh->rh_rwlock);
	sum_rec = m0_reqh_lockers_get(reqh,
			m0t1fs_globals.g_addb_mon_rw_io_size_key);
	m0_rwlock_read_unlock(&reqh->rh_rwlock);

	return sum_rec;
}

const struct m0_addb_monitor_ops m0t1fs_addb_mon_rw_io_ops = {
	.amo_watch   = m0t1fs_mon_rw_io_watch,
	.amo_sum_rec = m0t1fs_mon_rw_io_sum_rec
};

static int m0t1fs_addb_mon_total_io_size_init(void)
{
	struct m0_addb_sum_rec *sum_rec;
	struct m0_reqh         *reqh = &m0t1fs_globals.g_reqh;
	uint32_t               *key = &m0t1fs_globals.g_addb_mon_rw_io_size_key;
	uint64_t               *sum_data =
		     (uint64_t *)&m0t1fs_globals.g_addb_mon_sum_data_rw_io_size;
	uint32_t                sum_rec_nr =
		     sizeof (m0t1fs_globals.g_addb_mon_sum_data_rw_io_size) /
					     sizeof (uint64_t);
	M0_ALLOC_PTR(sum_rec);
	if (sum_rec == NULL)
		M0_RETURN(-ENOMEM);

	m0_addb_monitor_init(&m0t1fs_globals.g_addb_mon_rw_io_size,
			     &m0t1fs_addb_mon_rw_io_ops);

	m0_addb_monitor_sum_rec_init(sum_rec, &m0_addb_rt_m0t1fs_mon_io_size,
				     sum_data, sum_rec_nr);

	*key = m0_reqh_lockers_allot();

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	m0_reqh_lockers_set(reqh, *key, sum_rec);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	m0_addb_monitor_add(reqh, &m0t1fs_globals.g_addb_mon_rw_io_size);

	return 0;
}

static void m0t1fs_addb_mon_total_io_size_fini(void)
{
	struct m0_addb_sum_rec *sum_rec;
	struct m0_addb_monitor *mon = &m0t1fs_globals.g_addb_mon_rw_io_size;
	struct m0_reqh         *reqh = &m0t1fs_globals.g_reqh;

	sum_rec = mon->am_ops->amo_sum_rec(mon, &m0t1fs_globals.g_reqh);

	m0_addb_monitor_del(reqh, mon);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	m0_reqh_lockers_clear(reqh, m0t1fs_globals.g_addb_mon_rw_io_size_key);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
	m0_addb_monitor_sum_rec_fini(sum_rec);
	m0_free(sum_rec);
	m0_addb_monitor_fini(mon);
}

static void m0t1fs_rpc_fini(void)
{
	M0_ENTRY();

	m0t1fs_reqh_services_stop();
	m0_reqh_rpc_mach_tlink_del_fini(&m0t1fs_globals.g_rpc_machine);
	m0_rpc_machine_fini(&m0t1fs_globals.g_rpc_machine);
	m0_reqh_fini(&m0t1fs_globals.g_reqh);
	m0_dbenv_fini(&m0t1fs_globals.g_dbenv);
	m0_rpc_net_buffer_pool_cleanup(&m0t1fs_globals.g_buffer_pool);

	M0_LEAVE();
}

static int m0t1fs_layout_init(void)
{
	int rc;

	M0_ENTRY();

	rc = m0_layout_domain_init(&m0t1fs_globals.g_layout_dom,
				   &m0t1fs_globals.g_dbenv);
	if (rc == 0) {
		rc = m0_layout_standard_types_register(
						&m0t1fs_globals.g_layout_dom);
		if (rc != 0)
			m0_layout_domain_fini(&m0t1fs_globals.g_layout_dom);
	}

	M0_RETURN(rc);
}

static void m0t1fs_layout_fini(void)
{
	M0_ENTRY();

	m0_layout_standard_types_unregister(&m0t1fs_globals.g_layout_dom);
	m0_layout_domain_fini(&m0t1fs_globals.g_layout_dom);

	M0_LEAVE();
}

static int m0t1fs_service_start(const char *sname)
{
	int                          rc;
	struct m0_reqh              *reqh = &m0t1fs_globals.g_reqh;
	struct m0_reqh_service_type *stype;
	struct m0_reqh_service      *service;
	struct m0_uint128            uuid;

	stype = m0_reqh_service_type_find(sname);
	if (stype == NULL)
		M0_RETURN(-EINVAL);
	rc = m0_reqh_service_allocate(&service, stype, NULL);
	if (rc != 0)
		M0_RETURN(rc);
	m0_uuid_generate(&uuid);
	m0_reqh_service_init(service, reqh, &uuid);
	rc = m0_reqh_service_start(service);

	M0_RETURN(rc);
}

static int m0t1fs_reqh_services_start(void)
{
	int rc;

	rc = m0t1fs_service_start(M0_ADDB_SVC_NAME);
	if (rc)
		goto err;
	rc = m0t1fs_service_start("rmservice");
	if (rc)
		goto err;
	M0_RETURN(rc);
err:
	m0t1fs_reqh_services_stop();
	M0_RETURN(rc);
}

static void m0t1fs_reqh_services_stop(void)
{
	m0_reqh_services_terminate(&m0t1fs_globals.g_reqh);
}
