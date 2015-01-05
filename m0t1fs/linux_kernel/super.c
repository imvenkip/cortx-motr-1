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
 * Metadata       : Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 11/07/2011
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0T1FS
#include "lib/trace.h"

#include <linux/mount.h>
#include <linux/parser.h>  /* substring_t */
#include <linux/slab.h>    /* kmalloc, kfree */
#include <linux/statfs.h>  /* kstatfs */

#include "m0t1fs/linux_kernel/m0t1fs.h"
#include "m0t1fs/linux_kernel/fsync.h"
#include "mero/magic.h"    /* M0_T1FS_POOLS_MAGIC */
#include "lib/misc.h"      /* M0_SET0 */
#include "lib/memory.h"    /* M0_ALLOC_PTR, m0_free */
#include "layout/linear_enum.h"
#include "layout/pdclust.h"
#include "conf/confc.h"    /* m0_confc */
#include "conf/helpers.h"  /* m0_conf_fs_get */
#include "rpc/rpclib.h"    /* m0_rcp_client_connect */
#include "addb/addb.h"
#include "lib/uuid.h"   /* m0_uuid_generate */
#include "net/lnet/lnet.h"
#include "rpc/rpc_internal.h"
#include "m0t1fs/m0t1fs_addb.h"
#include "net/lnet/lnet_core_types.h"
#include "rm/rm_service.h"                 /* m0_rms_type */
#include "addb/addb_svc.h"                 /* m0_addb_svc_type */
#include "reqh/reqh_service.h" /* m0_reqh_service_ctx */

extern struct io_mem_stats iommstats;
extern struct m0_bitmap    m0t1fs_client_ep_tmid;
extern struct m0_mutex     m0t1fs_mutex;
extern uint32_t            m0t1fs_addb_mon_rw_io_size_key;

static char *local_addr = "0@lo:12345:45:";
module_param(local_addr, charp, S_IRUGO);
MODULE_PARM_DESC(local_addr, "End-point address of m0t1fs "
		 "e.g. 172.18.50.40@o2ib1:12345:34:\n"
		 "the tmid will be generated and filled by every mount");

static uint32_t tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
module_param(tm_recv_queue_min_len , int, S_IRUGO);
MODULE_PARM_DESC(tm_recv_queue_min_len, "TM receive queue minimum length");

static uint32_t max_rpc_msg_size = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
module_param(max_rpc_msg_size, int, S_IRUGO);
MODULE_PARM_DESC(max_rpc_msg_size, "Maximum RPC message size");

static char *ha_addr = "0@lo:12345:66:";
module_param(ha_addr, charp, S_IRUGO);
MODULE_PARM_DESC(ha_addr, "End-point address of running HA instance.");

M0_INTERNAL void io_bob_tlists_init(void);
static int m0t1fs_statfs(struct dentry *dentry, struct kstatfs *buf);

static const struct super_operations m0t1fs_super_operations = {
	.statfs        = m0t1fs_statfs,
	.alloc_inode   = m0t1fs_alloc_inode,
	.destroy_inode = m0t1fs_destroy_inode,
	.drop_inode    = generic_delete_inode, /* provided by linux kernel */
	.sync_fs       = m0t1fs_sync_fs
};


static struct m0_addb_rec_type *m0t1fs_io_cntr_rts[] = {
	/* read[0] */
	&m0_addb_rt_m0t1fs_ior_sizes,
	&m0_addb_rt_m0t1fs_ior_times,
	/* write[1] */
	&m0_addb_rt_m0t1fs_iow_sizes,
	&m0_addb_rt_m0t1fs_iow_times
};

static struct m0_addb_rec_type *m0t1fs_dgio_cntr_rts[] = {
	/* degraded read[0] */
	&m0_addb_rt_m0t1fs_dgior_sizes,
	&m0_addb_rt_m0t1fs_dgior_times,
	/* degraded write[1] */
	&m0_addb_rt_m0t1fs_dgiow_sizes,
	&m0_addb_rt_m0t1fs_dgiow_times
};

M0_INTERNAL void m0t1fs_fs_lock(struct m0t1fs_sb *csb)
{
	M0_ENTRY();
	m0_mutex_lock(&csb->csb_mutex);
	M0_LEAVE();
}

M0_INTERNAL void m0t1fs_fs_unlock(struct m0t1fs_sb *csb)
{
	M0_ENTRY();
	m0_mutex_unlock(&csb->csb_mutex);
	M0_LEAVE();
}

M0_INTERNAL bool m0t1fs_fs_is_locked(const struct m0t1fs_sb *csb)
{
	return m0_mutex_is_locked(&csb->csb_mutex);
}

/**
 * If use_hint is true, use hash_hint as final hash. This is used
 * to get specified mdservice. For example, readdir() wants to
 * get session for specified mds index.
 */
M0_INTERNAL struct m0_rpc_session *
m0t1fs_filename_to_mds_session(const struct m0t1fs_sb *csb,
			       const unsigned char    *filename,
			       unsigned int            nlen,
			       bool                    use_hint,
			       uint32_t                hash_hint)
{
	struct m0_reqh_service_ctx   *ctx;
	const struct m0_pools_common *pc;
	unsigned int                  hash;

	M0_ENTRY();

	if (use_hint)
		hash = hash_hint;
	else {
		/* If operations don't have filename, we map it to mds 0 */
		if (filename != NULL && nlen != 0)
			hash = m0_full_name_hash(filename, nlen);
		else
			hash = 0;
	}
	pc = &csb->csb_pools_common;
	ctx = pc->pc_mds_map[hash % pc->pc_nr_svcs[M0_CST_MDS]];
	M0_ASSERT(ctx != NULL);

	M0_LOG(M0_DEBUG, "%8s->index=%llu ctx=%p session=%p",
	       (const char*)filename,
	       hash % pc->pc_nr_svcs[M0_CST_MDS], ctx, &ctx->sc_session);
	M0_LEAVE();
	return &ctx->sc_session;
}

/**
 * Mapping from container_id to ios session.
 * container_id 0 is not valid.
 */
M0_INTERNAL struct m0_rpc_session *
m0t1fs_container_id_to_session(const struct m0t1fs_sb *csb,
			       uint64_t container_id)
{
	struct m0_reqh_service_ctx *ctx;

	M0_ENTRY();
	M0_PRE(container_id > 0);

	M0_LOG(M0_DEBUG, "container_id=%llu", container_id);

	ctx = csb->csb_pool_version->pv_dev_to_ios_map[container_id - 1];
	M0_ASSERT(ctx != NULL);

	M0_LOG(M0_DEBUG, "id %llu -> ctx=%p session=%p", container_id, ctx,
			 &ctx->sc_session);
	M0_LEAVE();
	return &ctx->sc_session;
}

static int m0t1fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int                       rc;
	struct m0t1fs_sb         *csb = M0T1FS_SB(dentry->d_sb);
	struct m0_fop_statfs_rep *rep = NULL;
	struct m0_fop            *rep_fop;

	M0_ENTRY();

	m0t1fs_fs_lock(csb);
	rc = m0t1fs_mds_statfs(csb, &rep_fop);
	rep = m0_fop_data(rep_fop);
	if (rc == 0) {
		buf->f_type = rep->f_type;
		buf->f_bsize = rep->f_bsize;
		buf->f_blocks = rep->f_blocks;
		buf->f_bfree = buf->f_bavail = rep->f_bfree;
		buf->f_files = rep->f_files;
		buf->f_ffree = rep->f_ffree;
		buf->f_namelen = rep->f_namelen;
	}
	m0_fop_put0_lock(rep_fop);
	m0t1fs_fs_unlock(csb);

	return M0_RC(rc);
}

/* ----------------------------------------------------------------
 * Mount options
 * ---------------------------------------------------------------- */

struct mount_opts {
	char     *mo_confd;
	char     *mo_profile;
	char     *mo_local_conf;
	uint32_t  mo_fid_start;
};

enum m0t1fs_mntopts {
	M0T1FS_MNTOPT_CONFD,
	M0T1FS_MNTOPT_PROFILE,
	M0T1FS_MNTOPT_LOCAL_CONF,
	M0T1FS_MNTOPT_FID_START,
	M0T1FS_MNTOPT_OOSTORE,
	M0T1FS_MNTOPT_ERR
};

static const match_table_t m0t1fs_mntopt_tokens = {
	{ M0T1FS_MNTOPT_CONFD,      "confd=%s"      },
	{ M0T1FS_MNTOPT_PROFILE,    "profile=%s"    },
	{ M0T1FS_MNTOPT_LOCAL_CONF, "local_conf=%s" },
	{ M0T1FS_MNTOPT_FID_START,  "fid_start=%s"  },
	{ M0T1FS_MNTOPT_OOSTORE,    "oostore"       },
	/* match_token() requires 2nd field of the last element to be NULL */
	{ M0T1FS_MNTOPT_ERR, NULL }
};

static void mount_opts_fini(struct mount_opts *mops)
{
	M0_ENTRY();

	/* Here we use kfree() instead of m0_free() because the memory
	 * was allocated using match_strdup(). */
	kfree(mops->mo_confd);
	kfree(mops->mo_profile);
	kfree(mops->mo_local_conf);
	M0_SET0(mops);

	M0_LEAVE();
}

static int str_parse(char **dest, const substring_t *src)
{
	*dest = match_strdup(src);
	return *dest == NULL ? -ENOMEM : 0;
}

static int num_parse(uint32_t *dest, const substring_t *src)
{
	unsigned long n;
	char         *s;
	int           rc;

	s = match_strdup(src);
	if (s == NULL)
		return M0_ERR(-ENOMEM);

	rc = strict_strtoul(s, 10, &n);
	if (rc == 0) {
		if (n > UINT32_MAX)
			rc = -EINVAL;
		else
			*dest = (uint32_t)n;
	}

	kfree(s);
	return M0_RC(rc);
}

/**
 * Consumes a chunk of `local_conf=' mount option value.
 *
 * local_conf_step() knows how to tell the end of `local_conf=' value
 * by counting '[' and ']' characters.  local_conf_step() restores
 * comma at the end of given chunk, unless this is the last chunk of
 * `local_conf=' value.
 *
 * @retval 0        End of configuration string has been reached.
 * @retval 1        A chunk has been consumed. Parsing should be continued.
 * @retval -EPROTO  Too many unclosed brackets. Unable to proceed.
 *
 * @see @ref conf-fspec-preload
 */
static int local_conf_step(char *s, uint8_t *depth)
{
	M0_PRE(*s != '\0');

	for (; *s != '\0'; ++s) {
		if (*s == '[') {
			++*depth;
			if (unlikely(*depth == 0))
				return M0_ERR(-EPROTO);
		} else if (*s == ']') {
			if (*depth > 0)
				--*depth;
			if (*depth == 0)
				break;
		}
	}

	if (*depth == 0)
		return 0;

	if (*s == '\0')
		*s = ',';
	return 1;
}

static bool is_empty(const char *s)
{
	return s == NULL || *s == '\0';
}

static int mount_opts_validate(const struct mount_opts *mops)
{
	if (is_empty(mops->mo_confd) && is_empty(mops->mo_local_conf))
		return M0_ERR_INFO(-EINVAL, "Configuration source is not "
			       "specified");

	if (is_empty(mops->mo_profile))
		return M0_ERR_INFO(-EINVAL, "Mandatory parameter is missing: "
			       "profile");

	if (!ergo(mops->mo_fid_start != 0,
		mops->mo_fid_start > M0_MDSERVICE_START_FID.f_key - 1))
		return M0_ERR_INFO(-EINVAL, "fid_start must be greater than %llu",
					M0_MDSERVICE_START_FID.f_key - 1);

	return M0_RC(0);
}

static int mount_opts_parse(struct m0t1fs_sb *csb, char *options,
			    struct mount_opts *dest)
{
	substring_t args[MAX_OPT_ARGS];
	char       *op;
	int         rc = 0;

	M0_ENTRY();

	if (options == NULL)
		return M0_RC(-EINVAL);

	M0_LOG(M0_INFO, "Mount options: `%s'", options);

	M0_SET0(dest);
	dest->mo_fid_start = M0_MDSERVICE_START_FID.f_key; /* default value */

	while ((op = strsep(&options, ",")) != NULL && *op != '\0') {
		switch (match_token(op, m0t1fs_mntopt_tokens, args)) {
		case M0T1FS_MNTOPT_CONFD:
			rc = str_parse(&dest->mo_confd, args);
			if (rc != 0)
				goto out;
			M0_LOG(M0_INFO, "confd: %s", dest->mo_confd);
			break;

		case M0T1FS_MNTOPT_PROFILE:
			rc = str_parse(&dest->mo_profile, args);
			if (rc != 0)
				goto out;
			M0_LOG(M0_INFO, "profile: %s", dest->mo_profile);
			break;

		case M0T1FS_MNTOPT_FID_START:
			rc = num_parse(&dest->mo_fid_start, args);
			if (rc != 0)
				goto out;
			M0_LOG(M0_INFO, "fid-start: %lu",
				(unsigned long)dest->mo_fid_start);
			break;

		case M0T1FS_MNTOPT_LOCAL_CONF: {
			const char *start = args->from;
			uint8_t     depth = 0;

			op = args->from;
			do {
				rc = local_conf_step(op, &depth);
			} while (rc > 0 &&
				 (op = strsep(&options, ",")) != NULL &&
				 *op != '\0');

			if (depth > 0)
				return M0_ERR_INFO(-EPROTO, "Unexpected EOF");

			if (rc < 0) {
				M0_ASSERT(rc == -EPROTO);
				return M0_ERR_INFO(rc, "Configuration string is "
					       "too nested");
			}

			dest->mo_local_conf = kstrdup(start, GFP_KERNEL);
			if (dest->mo_local_conf == NULL)
				return M0_RC(-ENOMEM);

			M0_LOG(M0_INFO, "local_conf: `%s'",
			       dest->mo_local_conf);
			break;
		}
		case M0T1FS_MNTOPT_OOSTORE:
			csb->csb_oostore = true;
			M0_LOG(M0_DEBUG, "OOSTORE mode!!");
			break;
		default:
			return M0_ERR_INFO(-EINVAL, "Unsupported option: %s", op);
		}
	}
out:
	/*
	 * If there is an error, the allocated memory will be freed by
	 * mount_opts_fini(), called by m0t1fs_fill_super().
	 */
	return M0_RC(rc ?: mount_opts_validate(dest));
}


struct m0_fid m0t1fs_hash_ios(struct m0t1fs_sb *csb,
			      const struct m0_fid *fid,
			      uint32_t i)

{
	uint64_t index;

	M0_PRE(i < csb->csb_pools_common.pc_md_redundancy);

	index = m0_fid_hash(fid, csb->csb_pools_common.pc_nr_svcs[M0_CST_IOS], i);
	return csb->csb_pool_version->pv_dev_to_ios_map[index]->sc_fid;
}

static int configure_addb_rpc_sink(struct m0t1fs_sb *csb,
				   struct m0_addb_mc *addb_mc)
{

	if (!m0_addb_mc_has_rpc_sink(addb_mc)) {
		int rc = m0_addb_mc_configure_rpc_sink(addb_mc,
						&csb->csb_rpc_machine,
						&csb->csb_reqh,
						M0_ADDB_RPCSINK_TS_INIT_PAGES,
						M0_ADDB_RPCSINK_TS_MAX_PAGES,
						M0_ADDB_RPCSINK_TS_PAGE_SIZE);
		if (rc != 0)
			return M0_RC(rc);

		m0_addb_mc_configure_pt_evmgr(addb_mc);
	}

	return 0;
}


/* ----------------------------------------------------------------
 * Superblock
 * ---------------------------------------------------------------- */

static void ast_thread(struct m0t1fs_sb *csb);
static void ast_thread_stop(struct m0t1fs_sb *csb);
static void m0t1fs_obf_dealloc(struct m0t1fs_sb *csb);

M0_INTERNAL void m0t1fs_sb_init(struct m0t1fs_sb *csb)
{
	int i;
	int j;

	M0_ENTRY("csb = %p", csb);
	M0_PRE(csb != NULL);

	M0_SET0(csb);
	m0_mutex_init(&csb->csb_mutex);
	m0_sm_group_init(&csb->csb_iogroup);
	csb->csb_active = true;
	m0_chan_init(&csb->csb_iowait, &csb->csb_iogroup.s_lock);
	m0_atomic64_set(&csb->csb_pending_io_nr, 0);

	M0_ADDB_CTX_INIT(&m0_addb_gmc, &csb->csb_addb_ctx,
	                 &m0_addb_ct_m0t1fs_mountp, &m0t1fs_addb_ctx);

#undef CNTR_INIT
#define CNTR_INIT(_n) m0_addb_counter_init(&csb->csb_io_stats[i]	\
				   .ais_##_n##_cntr, m0t1fs_io_cntr_rts[j++])

	for (i = 0, j = 0; i < ARRAY_SIZE(csb->csb_io_stats); ++i) {
		CNTR_INIT(sizes);
		CNTR_INIT(times);
	}
#undef CNTR_INIT

#define CNTR_INIT(_n) m0_addb_counter_init(&csb->csb_dgio_stats[i]	\
				   .ais_##_n##_cntr, m0t1fs_dgio_cntr_rts[j++])

	for (i = 0, j = 0; i < ARRAY_SIZE(csb->csb_dgio_stats); ++i) {
		CNTR_INIT(sizes);
		CNTR_INIT(times);
	}
#undef CNTR_INIT

	csb->csb_oostore  = false;
	M0_LEAVE();
}

M0_INTERNAL void m0t1fs_sb_fini(struct m0t1fs_sb *csb)
{
	int i;
	int j;

	M0_ENTRY();
	M0_PRE(csb != NULL);

#undef CNTR_FINI
#define CNTR_FINI(_n) m0_addb_counter_fini(&csb->csb_io_stats[i]	\
					   .ais_##_n##_cntr)
	for (i = 0, j = 0; i < ARRAY_SIZE(csb->csb_io_stats); ++i) {
		CNTR_FINI(sizes);
		CNTR_FINI(times);
	}
#undef CNTR_FINI
#define CNTR_FINI(_n) m0_addb_counter_fini(&csb->csb_dgio_stats[i]	\
					   .ais_##_n##_cntr)
	for (i = 0, j = 0; i < ARRAY_SIZE(csb->csb_dgio_stats); ++i) {
		CNTR_FINI(sizes);
		CNTR_FINI(times);
	}
#undef CNTR_FINI

	m0_addb_ctx_fini(&csb->csb_addb_ctx);

	m0_chan_fini_lock(&csb->csb_iowait);
	m0_sm_group_fini(&csb->csb_iogroup);
	m0_mutex_fini(&csb->csb_mutex);
	csb->csb_next_key = 0;
	M0_LEAVE();
}

/*
 * ----------------------------------------------------------------
 * Layout
 * ----------------------------------------------------------------
 */

static int
m0t1fs_sb_layouts_init(struct m0t1fs_sb *csb)
{
	struct m0_layout *l;
	int		  rc;
	int		  i;

	M0_ENTRY();

	for (i = 1; ; ++i) {
		rc = m0t1fs_layout_op(csb, M0_LAYOUT_OP_LOOKUP, i, &l);
		/* all layouts are added to domain, so we don't lose them */
		if (rc != 0) {
			if (rc == -ENOENT) {
				if (i > 1) {
					rc = 0;
					M0_LOG(M0_DEBUG, "found %d layouts",
							i - 1);
				} else {
					M0_LOG(M0_ERROR, "no layouts found");
				}
			} else {
				M0_LOG(M0_ERROR, "error obtaining layout "
				       "lid=%d: %d", i, rc);
			}
			break;
		}
	}

	return M0_RC(rc);
}

static void m0t1fs_sb_layouts_fini(struct m0t1fs_sb *csb)
{
	struct m0_layout *l;
	int		  i;

	M0_ENTRY();

	for (i = 1; ; ++i) {
		l = m0_layout_find(&csb->csb_layout_dom, i);
		if (l == NULL)
			break;
		m0_layout_put(l); /* after find() */
		m0_layout_put(l);
	}

	M0_LEAVE();
}

static int m0t1fs_service_start(struct m0_reqh_service_type *stype,
				struct m0_reqh *reqh)
{
	struct m0_reqh_service *service;
	struct m0_uint128       uuid;

	m0_uuid_generate(&uuid);
	return m0_reqh_service_setup(&service, stype, reqh, NULL, &uuid);
}

int m0t1fs_reqh_services_start(struct m0t1fs_sb *csb)
{
	struct m0_reqh *reqh = &csb->csb_reqh;
	int rc;

	rc = m0t1fs_service_start(&m0_addb_svc_type, reqh);
	if (rc)
		goto err;
	rc = m0t1fs_service_start(&m0_rms_type, reqh);
	if (rc)
		goto err;
	return M0_RC(rc);
err:
	m0_reqh_services_terminate(reqh);
	return M0_RC(rc);
}

int m0t1fs_net_init(struct m0t1fs_sb *csb)
{
	struct m0_net_xprt   *xprt;
	struct m0_net_domain *ndom;
	int		      rc;
	char                 *laddr;

	M0_ENTRY();
	laddr = m0_alloc(M0_NET_LNET_NIDSTR_SIZE * 2);
	if (laddr == NULL)
		return M0_RC(-ENOMEM);

	csb->csb_xprt  = &m0_net_lnet_xprt;;
	m0_mutex_lock(&m0t1fs_mutex);
	csb->csb_tmid = m0_bitmap_ffz(&m0t1fs_client_ep_tmid);
	if (csb->csb_tmid < 0) {
		m0_mutex_unlock(&m0t1fs_mutex);
		m0_free(laddr);
		return M0_RC(-EMFILE);
	}
	m0_bitmap_set(&m0t1fs_client_ep_tmid, csb->csb_tmid, true);
	m0_mutex_unlock(&m0t1fs_mutex);

	snprintf(laddr, M0_NET_LNET_NIDSTR_SIZE * 2,
		 "%s%d", local_addr, (int)csb->csb_tmid);
	M0_LOG(M0_DEBUG, "local ep is %s", laddr);
	csb->csb_laddr = laddr;
	xprt =  csb->csb_xprt;
	ndom = &csb->csb_ndom;

	rc = m0_net_domain_init(ndom, xprt);
	if (rc != 0) {
		csb->csb_laddr = NULL;
		m0_free(laddr);
		m0_mutex_lock(&m0t1fs_mutex);
		m0_bitmap_set(&m0t1fs_client_ep_tmid, csb->csb_tmid, false);
		m0_mutex_unlock(&m0t1fs_mutex);
	}
	M0_LEAVE("rc: %d", rc);
	return M0_RC(rc);
}

void m0t1fs_net_fini(struct m0t1fs_sb *csb)
{
	M0_ENTRY();

	m0_net_domain_fini(&csb->csb_ndom);
	m0_free(csb->csb_laddr);
	m0_mutex_lock(&m0t1fs_mutex);
	m0_bitmap_set(&m0t1fs_client_ep_tmid, csb->csb_tmid, false);
	m0_mutex_unlock(&m0t1fs_mutex);

	M0_LEAVE();
}

int m0t1fs_rpc_init(struct m0t1fs_sb *csb)
{
	struct m0_rpc_machine     *rpc_machine = &csb->csb_rpc_machine;
	struct m0_reqh            *reqh        = &csb->csb_reqh;
	struct m0_net_domain      *ndom        = &csb->csb_ndom;
	const char                *laddr       =  csb->csb_laddr;
	struct m0_net_buffer_pool *buffer_pool = &csb->csb_buffer_pool;
	struct m0_net_transfer_mc *tm;
	int                        rc;
	uint32_t		   bufs_nr;
	uint32_t		   tms_nr;

	M0_ENTRY();

	m0_be_ut_seg_init(&csb->csb_ut_seg,
			  &csb->csb_ut_be, 1ULL << 24);

	tms_nr = 1;
	bufs_nr = m0_rpc_bufs_nr(tm_recv_queue_min_len, tms_nr);

	rc = m0_rpc_net_buffer_pool_setup(ndom, buffer_pool,
					  bufs_nr, tms_nr);
	if (rc != 0)
		goto be_fini;

	rc = M0_REQH_INIT(reqh,
			  .rhia_dtm = (void*)1,
			  .rhia_db = csb->csb_ut_seg.bus_seg,
			  .rhia_mdstore = (void*)1);
	if (rc != 0)
		goto pool_fini;
	rc = m0_rpc_machine_init(rpc_machine, ndom, laddr, reqh,
				 buffer_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size, tm_recv_queue_min_len);
	if (rc != 0)
		goto reqh_fini;
	m0_reqh_start(reqh);
	tm = &rpc_machine->rm_tm;
	M0_ASSERT(tm->ntm_recv_pool == buffer_pool);
	return M0_RC(rc);

reqh_fini:
	m0_reqh_fini(reqh);
pool_fini:
	m0_rpc_net_buffer_pool_cleanup(buffer_pool);
be_fini:
	m0_be_ut_seg_fini(&csb->csb_ut_seg);
	M0_LEAVE("rc: %d", rc);
	M0_ASSERT(rc != 0);
	return M0_RC(rc);
}

struct m0t1fs_sb *reqh2sb(struct m0_reqh *reqh)
{
	return container_of(reqh, struct m0t1fs_sb, csb_reqh);
}

static void m0t1fs_mon_rw_io_watch(struct m0_addb_monitor   *mon,
				   const struct m0_addb_rec *rec,
				   struct m0_reqh           *reqh)
{
	struct m0_addb_sum_rec                  *sum_rec;
	struct m0t1fs_addb_mon_sum_data_io_size *sum_data =
			&reqh2sb(reqh)->csb_addb_mon_sum_data_rw_io_size;

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
m0t1fs_mon_rw_io_sum_rec(struct m0_addb_monitor *mon,
		         struct m0_reqh         *reqh)
{
	struct m0_addb_sum_rec *sum_rec;

	m0_rwlock_read_lock(&reqh->rh_rwlock);
	sum_rec = m0_reqh_lockers_get(reqh, m0t1fs_addb_mon_rw_io_size_key);
	m0_rwlock_read_unlock(&reqh->rh_rwlock);

	return sum_rec;
}

const struct m0_addb_monitor_ops m0t1fs_addb_mon_rw_io_ops = {
	.amo_watch   = m0t1fs_mon_rw_io_watch,
	.amo_sum_rec = m0t1fs_mon_rw_io_sum_rec
};

int m0t1fs_addb_mon_total_io_size_init(struct m0t1fs_sb *csb)
{
	struct m0_addb_sum_rec *sum_rec;
	struct m0_reqh         *reqh = &csb->csb_reqh;
	uint64_t               *sum_data =
		     (uint64_t *)&csb->csb_addb_mon_sum_data_rw_io_size;
	uint32_t                sum_rec_nr =
		     sizeof (csb->csb_addb_mon_sum_data_rw_io_size) /
					     sizeof (uint64_t);
	M0_ALLOC_PTR(sum_rec);
	if (sum_rec == NULL)
		return M0_RC(-ENOMEM);

	m0_addb_monitor_init(&csb->csb_addb_mon_rw_io_size,
			     &m0t1fs_addb_mon_rw_io_ops);

	m0_addb_monitor_sum_rec_init(sum_rec, &m0_addb_rt_m0t1fs_mon_io_size,
				     sum_data, sum_rec_nr);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	m0_reqh_lockers_set(reqh, m0t1fs_addb_mon_rw_io_size_key, sum_rec);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	m0_addb_monitor_add(reqh, &csb->csb_addb_mon_rw_io_size);

	return 0;
}

void m0t1fs_addb_mon_total_io_size_fini(struct m0t1fs_sb *csb)
{
	struct m0_addb_sum_rec *sum_rec;
	struct m0_addb_monitor *mon = &csb->csb_addb_mon_rw_io_size;
	struct m0_reqh         *reqh = &csb->csb_reqh;

	sum_rec = mon->am_ops->amo_sum_rec(mon, &csb->csb_reqh);

	m0_addb_monitor_del(reqh, mon);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	m0_reqh_lockers_clear(reqh, m0t1fs_addb_mon_rw_io_size_key);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
	m0_addb_monitor_sum_rec_fini(sum_rec);
	m0_free(sum_rec);
	m0_addb_monitor_fini(mon);
}

void m0t1fs_rpc_fini(struct m0t1fs_sb *csb)
{
	M0_ENTRY();

	m0_rpc_machine_fini(&csb->csb_rpc_machine);
	if (m0_reqh_state_get(&csb->csb_reqh) != M0_REQH_ST_STOPPED)
		m0_reqh_services_terminate(&csb->csb_reqh);
	m0_reqh_fini(&csb->csb_reqh);
	m0_rpc_net_buffer_pool_cleanup(&csb->csb_buffer_pool);
	m0_be_ut_seg_fini(&csb->csb_ut_seg);

	M0_LEAVE();
}

int m0t1fs_layout_init(struct m0t1fs_sb *csb)
{
	int rc;

	M0_ENTRY();

	/*
	 * XXX TODO: Use csb->csb_pool_version to build layout.
	 */
	rc = m0_layout_domain_init(&csb->csb_layout_dom);
	if (rc == 0) {
		rc = m0_layout_standard_types_register(
						&csb->csb_layout_dom);
		if (rc != 0)
			m0_layout_domain_fini(&csb->csb_layout_dom);
	}

	return M0_RC(rc);
}

void m0t1fs_layout_fini(struct m0t1fs_sb *csb)
{
	M0_ENTRY();

	m0_layout_standard_types_unregister(&csb->csb_layout_dom);
	m0_layout_domain_fini(&csb->csb_layout_dom);

	M0_LEAVE();
}

int m0t1fs_pool_find(struct m0t1fs_sb *csb, struct m0_conf_filesystem *fs)
{
	struct m0_conf_pool *cp;
	struct m0_conf_pver *pver;
	int                  rc;

	rc = m0_conf_poolversion_get(fs, NULL, &pver);
	if (rc != 0)
		return M0_RC(rc);
	cp = M0_CONF_CAST(pver->pv_obj.co_parent->co_parent, m0_conf_pool);
	csb->csb_pool = m0_pool_find(&csb->csb_pools_common, &cp->pl_obj.co_id);
	M0_ASSERT(csb->csb_pool != NULL);
	csb->csb_pool_version = m0_pool_version_find(csb->csb_pool,
						     &pver->pv_obj.co_id);
	M0_ASSERT(csb->csb_pool_version != NULL);

	return M0_RC(rc);
}

static int m0t1fs_setup(struct m0t1fs_sb *csb, const struct mount_opts *mops)
{
	struct m0_reqh_service_ctx *ctx;
	const char                 *ep_addr;
	int                         rc;

	M0_ENTRY();
	M0_PRE(csb->csb_astthread.t_state == TS_RUNNING);

	rc = m0t1fs_net_init(csb);
	if (rc != 0)
		goto err_return;

	rc = m0t1fs_rpc_init(csb);
	if (rc != 0)
		goto err_net_fini;

	rc = m0t1fs_addb_mon_total_io_size_init(csb);
	if (rc != 0)
		goto err_rpc_fini;

	csb->csb_next_key = mops->mo_fid_start;

	rc = m0_conf_fs_get(mops->mo_profile, mops->mo_confd,
			    &csb->csb_rpc_machine, &csb->csb_iogroup,
			    &csb->csb_confc, &csb->csb_fs);
	if (rc != 0)
		goto err_addb_mon_fini;

	rc = m0_pools_common_init(&csb->csb_pools_common,
				  &csb->csb_rpc_machine, csb->csb_fs);
	if (rc != 0)
		goto err_conf_fini;

	ctx = m0_pools_common_service_ctx_find_by_type(&csb->csb_pools_common,
						       M0_CST_HA);
	if (rc != 0 || ctx == NULL) {
		M0_LOG(M0_WARN, "Cannot connect to HA service.");
	} else {
		csb->csb_reqh.rh_ha_rpc_session = ctx->sc_session;
		/* Reset ctx for use later */
		ctx = NULL;
	}

	rc = m0_ha_state_init();
	if (rc != 0)
		goto err_pools_common_fini;

	rc = m0_pools_setup(&csb->csb_pools_common, csb->csb_fs, NULL, NULL, NULL);
	if (rc != 0)
		goto err_ha_fini;

	/* Find pool and pool version to use. */
	rc = m0t1fs_pool_find(csb, csb->csb_fs);
	if (rc != 0)
		goto err_ha_fini;

	rc = m0t1fs_layout_init(csb);
	if (rc != 0)
		goto err_ha_fini;

	ctx = csb->csb_pools_common.pc_ss_ctx;
	if (ctx != NULL) {
		ep_addr = ctx->sc_conn.c_rpcchan->rc_destep->nep_addr;
		m0_addb_monitor_setup(&csb->csb_reqh, &ctx->sc_conn, ep_addr);
		M0_LOG(M0_DEBUG, "Stats service connected");
	} else
		M0_LOG(M0_WARN, "Stats service not connected");

	rc = configure_addb_rpc_sink(csb, &m0_addb_gmc);
	if (rc != 0)
		goto err_layout_fini;

	/* Start resource manager service */
	rc = m0t1fs_reqh_services_start(csb);
	if (rc != 0)
		goto err_addb_mc_unconf;

	rc = m0t1fs_sb_layouts_init(csb);
	if (rc != 0)
		goto err_services_terminate;

	return M0_RC(rc);

err_services_terminate:
	m0_reqh_services_terminate(&csb->csb_reqh);
err_addb_mc_unconf:
	/* @todo Make a separate unconfigure api and do this in that */
	m0_addb_mc_fini(&m0_addb_gmc);
	m0_addb_mc_init(&m0_addb_gmc);
err_layout_fini:
	m0t1fs_layout_fini(csb);
err_ha_fini:
	m0_ha_state_fini();
err_pools_common_fini:
	m0_pools_common_fini(&csb->csb_pools_common);
err_conf_fini:
	/* Close filesystem. */
	m0_confc_close(&csb->csb_fs->cf_obj);
	m0_confc_fini(&csb->csb_confc);
err_addb_mon_fini:
	m0t1fs_addb_mon_total_io_size_fini(csb);
err_rpc_fini:
	m0t1fs_rpc_fini(csb);
err_net_fini:
	m0t1fs_net_fini(csb);
err_return:
	return M0_RC(rc);
}

static void m0t1fs_teardown(struct m0t1fs_sb *csb)
{
	m0t1fs_sb_layouts_fini(csb);
	m0_reqh_services_terminate(&csb->csb_reqh);
	/* @todo Make a separate unconfigure api and do this in that */
	m0_addb_mc_fini(&m0_addb_gmc);
	m0_addb_mc_init(&m0_addb_gmc);
	m0t1fs_layout_fini(csb);
	m0_ha_state_fini();
	m0_pools_common_fini(&csb->csb_pools_common);
	/* Close filesystem. */
	m0_confc_close(&csb->csb_fs->cf_obj);
	m0_confc_fini(&csb->csb_confc);
	m0t1fs_addb_mon_total_io_size_fini(csb);
	m0t1fs_rpc_fini(csb);
	m0t1fs_net_fini(csb);
}

static void m0t1fs_dput(struct dentry *dentry)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
        clear_nlink(dentry->d_inode);
#else
        dentry->d_inode->i_nlink = 0;
#endif
        d_delete(dentry);
        dput(dentry);
}

static void m0t1fs_obf_dealloc(struct m0t1fs_sb *csb) {
	M0_ENTRY();

        M0_PRE(csb != NULL);

	if (csb->csb_fid_dentry != NULL) {
	        m0t1fs_dput(csb->csb_fid_dentry);
                csb->csb_fid_dentry = NULL;
        }
	if (csb->csb_mero_dentry != NULL) {
	        m0t1fs_dput(csb->csb_mero_dentry);
                csb->csb_mero_dentry = NULL;
        }

	M0_LEAVE();
}

static int m0t1fs_obf_alloc(struct super_block *sb)
{
        struct inode             *mero_inode;
        struct dentry            *mero_dentry;
        struct inode             *fid_inode;
        struct dentry            *fid_dentry;
        struct m0t1fs_sb         *csb = M0T1FS_SB(sb);
	struct m0_fop_cob        *body = &csb->csb_virt_body;

	M0_ENTRY();

        body->b_atime = body->b_ctime = body->b_mtime =
					m0_time_seconds(m0_time_now());
        body->b_valid = (M0_COB_MTIME | M0_COB_CTIME | M0_COB_CTIME |
	                 M0_COB_UID | M0_COB_GID | M0_COB_BLOCKS |
	                 M0_COB_SIZE | M0_COB_NLINK | M0_COB_MODE |
	                 M0_COB_LID);
        body->b_blocks = 16;
        body->b_size = 4096;
        body->b_blksize = 4096;
        body->b_nlink = 2;
        body->b_lid = M0_DEFAULT_LAYOUT_ID;
        body->b_mode = (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR |/*rwx for owner*/
                        S_IRGRP | S_IXGRP |                    /*r-x for group*/
                        S_IROTH | S_IXOTH);

        /* Init virtual .mero directory */
        mero_dentry = d_alloc_name(sb->s_root, M0_DOT_MERO_NAME);
        if (mero_dentry == NULL)
                return M0_RC(-ENOMEM);

	m0t1fs_fs_lock(csb);
        mero_inode = m0t1fs_iget(sb, &M0_DOT_MERO_FID, body);
	m0t1fs_fs_unlock(csb);
        if (IS_ERR(mero_inode)) {
                dput(mero_dentry);
                return M0_RC((int)PTR_ERR(mero_inode));
        }

        /* Init virtual .mero/fid directory */
        fid_dentry = d_alloc_name(mero_dentry, M0_DOT_MERO_FID_NAME);
        if (fid_dentry == NULL) {
                iput(mero_inode);
                dput(mero_dentry);
                return M0_RC(-ENOMEM);
        }

	m0t1fs_fs_lock(csb);
        fid_inode = m0t1fs_iget(sb, &M0_DOT_MERO_FID_FID, body);
	m0t1fs_fs_unlock(csb);
        if (IS_ERR(fid_inode)) {
                dput(fid_dentry);
                iput(mero_inode);
                dput(mero_dentry);
                return M0_RC((int)PTR_ERR(fid_inode));
        }

        d_add(fid_dentry, fid_inode);
        csb->csb_fid_dentry = fid_dentry;

        d_add(mero_dentry, mero_inode);
        csb->csb_mero_dentry = mero_dentry;

	return M0_RC(0);
}

static int m0t1fs_root_alloc(struct super_block *sb)
{
	struct inode             *root_inode;
	int                       rc = 0;
	struct m0t1fs_sb         *csb = M0T1FS_SB(sb);
	struct m0_fop_statfs_rep *rep = NULL;
	struct m0_addb_ctx       *cv[] = { &csb->csb_addb_ctx, NULL };
	struct m0_fop            *rep_fop;

	M0_ENTRY();

	rc = m0t1fs_mds_statfs(csb, &rep_fop);
	rep = m0_fop_data(rep_fop);
	if (rc != 0)
		goto out;

	sb->s_magic = rep->f_type;
	csb->csb_namelen = rep->f_namelen;

	M0_LOG(M0_DEBUG, "Got mdservice root "FID_F, FID_P(&rep->f_root));

	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_m0t1fs_root_cob, cv,
		     rep->f_root.f_container, rep->f_root.f_key);

	m0t1fs_fs_lock(csb);
	root_inode = m0t1fs_root_iget(sb, &rep->f_root);
	m0t1fs_fs_unlock(csb);
	if (IS_ERR(root_inode)) {
		rc = (int)PTR_ERR(root_inode);
		goto out;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	sb->s_root = d_make_root(root_inode);
#else
	sb->s_root = d_alloc_root(root_inode);
#endif
	if (sb->s_root == NULL) {
		iput(root_inode);
		rc = -ENOMEM;
		goto out;
	}
out:
	m0_fop_put0_lock(rep_fop);
	return M0_RC(rc);
}

static int m0t1fs_fill_super(struct super_block *sb, void *data,
			     int silent __attribute__((unused)))
{
	struct m0t1fs_sb *csb;
	int               rc;
	struct mount_opts mops = {0};

	M0_ENTRY();

	M0_ALLOC_PTR(csb);
	if (csb == NULL) {
		rc = -ENOMEM;
		goto end;
	}
	m0t1fs_sb_init(csb);

	rc = mount_opts_parse(csb, data, &mops);
	if (rc != 0)
		goto sb_fini;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	rc = bdi_init(&csb->csb_backing_dev_info);
	if (rc != 0)
		goto sb_fini;
#endif

	rc = M0_THREAD_INIT(&csb->csb_astthread, struct m0t1fs_sb *, NULL,
			    &ast_thread, csb, "m0_ast_thread");
	if (rc != 0)
		goto sb_fini;

	rc = m0t1fs_setup(csb, &mops);
	if (rc != 0)
		goto thread_stop;

	sb->s_fs_info        = csb;
	sb->s_blocksize      = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_maxbytes       = MAX_LFS_FILESIZE;
	sb->s_op             = &m0t1fs_super_operations;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	/* for .sync_fs() callback to be called by kernel */
	sb->s_bdi = NULL;
	rc = bdi_register_dev(&csb->csb_backing_dev_info, sb->s_dev);
	if (rc != 0)
		goto m0t1fs_teardown;
	sb->s_bdi = &csb->csb_backing_dev_info;
#endif

	rc = m0t1fs_root_alloc(sb);
	if (rc != 0)
		goto m0t1fs_teardown;

        rc = m0t1fs_obf_alloc(sb);
        if (rc != 0)
                goto m0t1fs_teardown;

	io_bob_tlists_init();
	M0_SET0(&iommstats);

	return M0_RC(0);

m0t1fs_teardown:
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	if (sb->s_bdi != NULL)
		bdi_unregister(sb->s_bdi);
#endif
	m0t1fs_teardown(csb);
thread_stop:
	ast_thread_stop(csb);
sb_fini:
	m0t1fs_sb_fini(csb);
	m0_free(csb);
	mount_opts_fini(&mops);
end:
	sb->s_fs_info = NULL;
	M0_ASSERT(rc != 0);
	return M0_RC(rc);
}

/** Implementation of file_system_type::get_sb() interface. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
M0_INTERNAL struct dentry *m0t1fs_mount(struct file_system_type *fstype,
					int flags, const char *devname,
					void *data)
#else
M0_INTERNAL int m0t1fs_get_sb(struct file_system_type *fstype, int flags,
			      const char *devname, void *data,
			      struct vfsmount *mnt)
#endif
{
	M0_ENTRY("flags: 0x%x, devname: %s, data: %s", flags, devname,
		 (char *)data);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	return mount_nodev(fstype, flags, data, m0t1fs_fill_super);
#else
	return M0_RC(get_sb_nodev(fstype, flags, data, m0t1fs_fill_super, mnt));
#endif
}

/** Implementation of file_system_type::kill_sb() interface. */
M0_INTERNAL void m0t1fs_kill_sb(struct super_block *sb)
{
	struct m0t1fs_sb *csb = M0T1FS_SB(sb);

	M0_ENTRY("csb = %p", csb);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	bdi_unregister(sb->s_bdi);
#endif

        /*
         * Dealloc virtual .mero/fid dirs. This should be done _before_
         * kill_anon_super()
         */
	if (csb != NULL)
                m0t1fs_obf_dealloc(csb);

	kill_anon_super(sb);

	/*
	 * If m0t1fs_fill_super() fails then deactivate_locked_super() calls
	 * m0t1fs_fs_type->kill_sb(). In that case, csb == NULL.
	 * But still not sure, such csb != NULL handling is a good idea.
	 */
	if (csb != NULL) {
		m0t1fs_teardown(csb);
		ast_thread_stop(csb);
		m0t1fs_sb_fini(csb);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		bdi_destroy(&csb->csb_backing_dev_info);
#endif
		m0_free(csb);
	}

	M0_LOG(M0_INFO, "mem stats :\n a_ioreq_nr = %llu, d_ioreq_nr = %llu\n"
			"a_pargrp_iomap_nr = %llu, d_pargrp_iomap_nr = %llu\n"
			"a_target_ioreq_nr = %llu, d_target_ioreq_nr = %llu\n",
	       iommstats.a_ioreq_nr, iommstats.d_ioreq_nr,
	       iommstats.a_pargrp_iomap_nr, iommstats.d_pargrp_iomap_nr,
	       iommstats.a_target_ioreq_nr, iommstats.d_target_ioreq_nr);

	M0_LOG(M0_INFO, "a_io_req_fop_nr = %llu, d_io_req_fop_nr = %llu\n"
			"a_data_buf_nr = %llu, d_data_buf_nr = %llu\n"
			"a_page_nr = %llu, d_page_nr = %llu\n",
	       iommstats.a_io_req_fop_nr, iommstats.d_io_req_fop_nr,
	       iommstats.a_data_buf_nr, iommstats.d_data_buf_nr,
	       iommstats.a_page_nr, iommstats.d_page_nr);

	M0_LEAVE();
}


/* ----------------------------------------------------------------
 * Misc.
 * ---------------------------------------------------------------- */

static void ast_thread(struct m0t1fs_sb *csb)
{
	while (1) {
		m0_chan_wait(&csb->csb_iogroup.s_clink);
		m0_sm_group_lock(&csb->csb_iogroup);
		m0_sm_asts_run(&csb->csb_iogroup);
		m0_sm_group_unlock(&csb->csb_iogroup);
		if (!csb->csb_active &&
		    m0_atomic64_get(&csb->csb_pending_io_nr) == 0) {
			m0_chan_signal_lock(&csb->csb_iowait);
			return;
		}
	}
}

static void ast_thread_stop(struct m0t1fs_sb *csb)
{
	struct m0_clink w;

	m0_clink_init(&w, NULL);
	m0_clink_add_lock(&csb->csb_iowait, &w);

	csb->csb_active = false;
	m0_chan_signal_lock(&csb->csb_iogroup.s_chan);
	m0_chan_wait(&w);
	m0_thread_join(&csb->csb_astthread);

	m0_clink_del_lock(&w);
	m0_clink_fini(&w);
}

#undef M0_TRACE_SUBSYSTEM
