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
#include <linux/slab.h>    /* kmalloc(), kfree() */
#include <linux/statfs.h>  /* kstatfs */

#include "m0t1fs/linux_kernel/m0t1fs.h"
#include "mero/magic.h"    /* M0_T1FS_SVC_CTX_MAGIC */
#include "lib/misc.h"      /* M0_SET0() */
#include "lib/memory.h"    /* M0_ALLOC_PTR(), m0_free() */
#include "layout/linear_enum.h"
#include "layout/pdclust.h"
#include "conf/confc.h"    /* m0_confc */
#include "rpc/rpclib.h"    /* m0_rcp_client_connect */
#include "addb/addb.h"
#include "rpc/rpc_internal.h"

static int m0t1fs_layout_build(const uint64_t         layout_id,
			       const uint32_t         N,
			       const uint32_t         K,
			       const uint32_t         pool_width,
			       const uint64_t         unit_size,
			       struct m0_layout_enum *le,
			       struct m0_layout     **layout);

static int m0t1fs_cob_id_enum_build(const uint32_t pool_width,
				    struct m0_layout_enum **lay_enum);

extern struct io_mem_stats iommstats;

M0_INTERNAL void io_bob_tlists_init(void);

static int m0t1fs_statfs(struct dentry *dentry, struct kstatfs *buf);

static const struct super_operations m0t1fs_super_operations = {
	.statfs        = m0t1fs_statfs,
	.alloc_inode   = m0t1fs_alloc_inode,
	.destroy_inode = m0t1fs_destroy_inode,
	.drop_inode    = generic_delete_inode /* provided by linux kernel */
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

/**
 * tlist descriptor for list of m0t1fs_service_context objects placed
 * in m0t1fs_sb::csb_service_contexts list using sc_link.
 */
M0_TL_DESCR_DEFINE(svc_ctx, "Service contexts", static,
		   struct m0t1fs_service_context, sc_link, sc_magic,
		   M0_T1FS_SVC_CTX_MAGIC, M0_T1FS_SVC_CTX_HEAD_MAGIC);

M0_TL_DEFINE(svc_ctx, static, struct m0t1fs_service_context);

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

M0_INTERNAL struct m0_rpc_session *
m0t1fs_container_id_to_session(const struct m0t1fs_sb *csb,
			       uint64_t container_id)
{
	struct m0t1fs_service_context *ctx;

	M0_ENTRY();
	M0_PRE(container_id <= csb->csb_nr_containers);

	ctx = csb->csb_cl_map.clm_map[container_id];
	M0_ASSERT(ctx != NULL);

	M0_LEAVE("id %llu -> session: %p", container_id, &ctx->sc_session);
	return &ctx->sc_session;
}

static int m0t1fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int                       rc;
	struct m0t1fs_sb         *csb = M0T1FS_SB(dentry->d_sb);
	struct m0_fop_statfs_rep *rep = NULL;

	M0_ENTRY();

	m0t1fs_fs_lock(csb);
	rc = m0t1fs_mds_statfs(csb, &rep);
	if (rc == 0) {
		buf->f_type = rep->f_type;
		buf->f_bsize = rep->f_bsize;
		buf->f_blocks = rep->f_blocks;
		buf->f_bfree = buf->f_bavail = rep->f_bfree;
		buf->f_files = rep->f_files;
		buf->f_ffree = rep->f_ffree;
		buf->f_namelen = rep->f_namelen;
	}
	m0t1fs_fs_unlock(csb);

	M0_RETURN(rc);
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
	M0T1FS_MNTOPT_ERR
};

static const match_table_t m0t1fs_mntopt_tokens = {
	{ M0T1FS_MNTOPT_CONFD,      "confd=%s"      },
	{ M0T1FS_MNTOPT_PROFILE,    "profile=%s"    },
	{ M0T1FS_MNTOPT_LOCAL_CONF, "local_conf=%s" },
	{ M0T1FS_MNTOPT_FID_START,  "fid_start=%s"  },
	/* match_token() requires 2nd field of the last element to be NULL */
	{ M0T1FS_MNTOPT_ERR, NULL }
};

static void mount_opts_fini(struct mount_opts *mops)
{
	M0_ENTRY();

	/* Here we use kfree() instead of m0_free() because the memory
	 * was allocated using match_strdup(). */

	if (mops->mo_confd != NULL)
		kfree(mops->mo_confd);
	if (mops->mo_profile != NULL)
		kfree(mops->mo_profile);
	if (mops->mo_local_conf != NULL)
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
		return -ENOMEM;

	rc = strict_strtoul(s, 10, &n);
	if (rc == 0) {
		if (n > UINT32_MAX)
			rc = -EINVAL;
		else
			*dest = (uint32_t)n;
	}

	kfree(s);
	return rc;
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
				return -EPROTO;
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
		M0_RETERR(-EINVAL, "Configuration source is not specified");

	if (is_empty(mops->mo_profile))
		M0_RETERR(-EINVAL, "Mandatory parameter is missing: profile");

	if (!ergo(mops->mo_fid_start != 0, mops->mo_fid_start > 3))
		M0_RETERR(-EINVAL, "fid_start must be greater than 3");

	M0_RETURN(0);
}

static int mount_opts_parse(char *options, struct mount_opts *dest)
{
	substring_t args[MAX_OPT_ARGS];
	char       *op;
	int         rc = 0;

	M0_ENTRY();

	if (options == NULL)
		M0_RETURN(-EINVAL);

	M0_LOG(M0_INFO, "Mount options: `%s'", options);

	M0_SET0(dest);
	dest->mo_fid_start = 4;   /* Default value */
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
				M0_RETERR(-EPROTO, "Unexpected EOF");

			if (rc < 0) {
				M0_ASSERT(rc == -EPROTO);
				M0_RETERR(rc, "Configuration string is "
					  "too nested");
			}

			dest->mo_local_conf = kstrdup(start, GFP_KERNEL);
			if (dest->mo_local_conf == NULL)
				M0_RETURN(-ENOMEM);

			M0_LOG(M0_INFO, "local_conf: `%s'",
			       dest->mo_local_conf);
			break;
		}
		default:
			M0_RETERR(-EINVAL, "Unsupported option: %s", op);
		}
	}
out:
	/*
	 * If there is an error, the allocated memory will be freed by
	 * mount_opts_fini(), called by m0t1fs_fill_super().
	 */
	M0_RETURN(rc ?: mount_opts_validate(dest));
}

/* ----------------------------------------------------------------
 * File-system parameters
 * ---------------------------------------------------------------- */

struct fs_params {
	uint32_t fs_pool_width;      /* P */
	uint32_t fs_nr_data_units;   /* N */
	uint32_t fs_nr_parity_units; /* K */
	uint32_t fs_unit_size;
};

static int fs_params_validate(const struct fs_params *params)
{
	M0_ENTRY();

	M0_PRE(params->fs_pool_width != 0);
	M0_PRE(params->fs_nr_data_units != 0);
	M0_PRE(params->fs_nr_parity_units != 0);
	M0_PRE(params->fs_unit_size != 0);

	/* Need to test with unit size that is not multiple of page size.
	 * Until then --- don't allow. */
	if ((params->fs_unit_size & (PAGE_CACHE_SIZE - 1)) != 0)
		M0_RETERR(-EINVAL,
			  "Unit size must be a multiple of PAGE_CACHE_SIZE");
	M0_RETURN(0);
}

static int fs_params_parse(struct fs_params *dest, const char **src)
{
	int rc;
	substring_t args[MAX_OPT_ARGS];
	enum { POOL_WIDTH, NR_DATA_UNITS, NR_PAR_UNITS, UNIT_SIZE, NONE };
	const match_table_t tbl = {
		{ POOL_WIDTH,    "pool_width=%u" },
		{ NR_DATA_UNITS, "nr_data_units=%u" },
		{ NR_PAR_UNITS,  "nr_parity_units=%u" },
		{ UNIT_SIZE,     "unit_size=%u" },
		{ NONE, NULL }
	};

	M0_ENTRY();

	M0_SET0(dest);

	if (src == NULL)
		goto end;

	for (rc = 0; rc == 0 && *src != NULL; ++src) {
		M0_LOG(M0_DEBUG, "conf fs src=%s", *src);
		/* match_token() doesn't change the string pointed to
		 * by its first argument.  We don't want to remove
		 * `const' from m0_conf_filesystem::cf_params only
		 * because match_token()'s first argument is not
		 * const. We cast to const-less type instead. */
		switch (match_token((char *)*src, tbl, args)) {
		case POOL_WIDTH:
			rc = num_parse(&dest->fs_pool_width, args);
			break;
		case NR_DATA_UNITS:
			rc = num_parse(&dest->fs_nr_data_units, args);
			break;
		case NR_PAR_UNITS:
			rc = num_parse(&dest->fs_nr_parity_units, args);
			break;
		case UNIT_SIZE:
			rc = num_parse(&dest->fs_unit_size, args);
			break;
		default:
			rc = -EINVAL;
		}

		if (rc != 0)
			M0_RETERR(rc, "Invalid filesystem parameter: %s", *src);
	}
end:
	if (dest->fs_nr_data_units == 0)
		dest->fs_nr_data_units = M0T1FS_DEFAULT_NR_DATA_UNITS;

	if (dest->fs_nr_parity_units == 0)
		dest->fs_nr_parity_units = M0T1FS_DEFAULT_NR_PARITY_UNITS;

	if (dest->fs_unit_size == 0)
		dest->fs_unit_size = M0T1FS_DEFAULT_STRIPE_UNIT_SIZE;

	if (dest->fs_pool_width == 0)
		dest->fs_pool_width =
			dest->fs_nr_data_units + 2 * dest->fs_nr_parity_units;

	rc = fs_params_validate(dest);
	if (rc == 0)
		M0_LOG(M0_INFO, "pool_width (P) = %u, nr_data_units (N) = %u,"
		       " nr_parity_units (K) = %u, unit_size = %u",
		       dest->fs_pool_width, dest->fs_nr_data_units,
		       dest->fs_nr_parity_units, dest->fs_unit_size);
	M0_RETURN(rc);
}

/* ----------------------------------------------------------------
 * Services
 * ---------------------------------------------------------------- */

static void m0t1fs_service_context_init(struct m0t1fs_service_context *ctx,
					struct m0t1fs_sb              *csb,
					enum m0_conf_service_type      type)
{
	M0_ENTRY();

	M0_SET0(ctx);
	ctx->sc_csb = csb;
	ctx->sc_type = type;
	ctx->sc_magic = M0_T1FS_SVC_CTX_MAGIC;
	svc_ctx_tlink_init(ctx);

	M0_LEAVE();
}

static void m0t1fs_service_context_fini(struct m0t1fs_service_context *ctx)
{
	M0_ENTRY();

	svc_ctx_tlink_fini(ctx);
	ctx->sc_magic = 0;

	M0_LEAVE();
}

static int connect_to_service(const char *addr, enum m0_conf_service_type type,
			      struct m0t1fs_sb *csb)
{
	struct m0t1fs_service_context *ctx;
	int                            rc;

	M0_ENTRY("addr=`%s' type=%d", addr, type);
	M0_PRE(!is_empty(addr));

	M0_ALLOC_PTR(ctx);
	if (ctx == NULL)
		M0_RETURN(-ENOMEM);

	m0t1fs_service_context_init(ctx, csb, type);
	rc = m0_rpc_client_connect(&ctx->sc_conn, &ctx->sc_session,
				   &m0t1fs_globals.g_rpc_machine, addr,
				   M0T1FS_MAX_NR_RPC_IN_FLIGHT);
	if (rc == 0) {
		svc_ctx_tlist_add_tail(&csb->csb_service_contexts, ctx);
		M0_CNT_INC(csb->csb_nr_active_contexts);
		M0_LOG(M0_INFO, "Connected to service `%s'. %d active contexts",
		       addr, csb->csb_nr_active_contexts);
	} else {
		m0t1fs_service_context_fini(ctx);
		m0_free(ctx);
	}
	M0_RETURN(rc);
}

static void disconnect_from_services(struct m0t1fs_sb *csb)
{
	struct m0t1fs_service_context *ctx;

	M0_ENTRY();

	m0_tl_teardown(svc_ctx, &csb->csb_service_contexts, ctx) {
		if (csb->csb_nr_active_contexts > 0) {
			(void)m0_rpc_session_destroy(&ctx->sc_session,
						     M0_TIME_NEVER);
			(void)m0_rpc_conn_destroy(&ctx->sc_conn,
						  M0_TIME_NEVER);
			M0_CNT_DEC(ctx->sc_csb->csb_nr_active_contexts);
			M0_LOG(M0_INFO, "Disconnected from service."
			       " %d active contexts",
			       csb->csb_nr_active_contexts);
		}
		m0t1fs_service_context_fini(ctx);
		m0_free(ctx);
	}

	M0_POST(csb->csb_nr_active_contexts == 0);
	M0_POST(svc_ctx_tlist_is_empty(&csb->csb_service_contexts));
	M0_LEAVE();
}

static int connect_to_services(struct m0t1fs_sb *csb, struct m0_conf_obj *fs,
			       uint32_t *nr_ios)
{
	struct m0_conf_obj *dir;
	struct m0_conf_obj *entry;
	const char        **pstr;
	int                 rc;
	bool                mds_is_provided       = false;
	bool                dlm_is_provided       = false;

	M0_ENTRY();
	M0_PRE(svc_ctx_tlist_is_empty(&csb->csb_service_contexts));
	M0_PRE(csb->csb_nr_active_contexts == 0);

	rc = m0_confc_open_sync(&dir, fs, M0_CONF_FILESYSTEM_SERVICES_FID);
	if (rc != 0)
		M0_RETURN(rc);

	*nr_ios = 0;

	for (entry = NULL; (rc = m0_confc_readdir_sync(dir, &entry)) > 0; ) {
		const struct m0_conf_service *svc =
			M0_CONF_CAST(entry, m0_conf_service);

		if (svc->cs_type == M0_CST_MDS)
			mds_is_provided = true;
		else if (svc->cs_type == M0_CST_DLM)
			dlm_is_provided = true;
		else if (svc->cs_type == M0_CST_IOS)
			++*nr_ios;

		for (pstr = svc->cs_endpoints; *pstr != NULL; ++pstr) {
			M0_LOG(M0_DEBUG, "svc type=%d, ep=%s",
			       svc->cs_type, *pstr);
			rc = connect_to_service(*pstr, svc->cs_type, csb);
			if (rc != 0)
				goto out;
		}
	}
out:
	m0_confc_close(entry);
	m0_confc_close(dir);

	if (rc == 0 && mds_is_provided && dlm_is_provided && *nr_ios > 0)
		M0_LOG(M0_DEBUG, "Connected to IOS, MDS and RMS");
	else {
		M0_LOG(M0_FATAL, "Error connecting to the services. "
		       "(Please check whether IOS, MDS and RMS are provided)");
		rc = rc ?: -EINVAL;
		disconnect_from_services(csb);
	}
	M0_RETURN(rc);
}

static int configure_addb_rpc_sink(struct m0_addb_mc *addb_mc)
{

	if (!m0_addb_mc_has_rpc_sink(addb_mc)) {
		int rc = m0_addb_mc_configure_rpc_sink(addb_mc,
						&m0t1fs_globals.g_rpc_machine,
						&m0t1fs_globals.g_reqh,
						M0_ADDB_RPCSINK_TS_INIT_PAGES,
						M0_ADDB_RPCSINK_TS_MAX_PAGES,
						M0_ADDB_RPCSINK_TS_PAGE_SIZE);
		if (rc != 0)
			return rc;

		m0_addb_mc_configure_pt_evmgr(addb_mc);
	}

	return 0;
}


/* ----------------------------------------------------------------
 * Superblock
 * ---------------------------------------------------------------- */

static void ast_thread(struct m0t1fs_sb *csb);
static void ast_thread_stop(struct m0t1fs_sb *csb);

static void m0t1fs_sb_init(struct m0t1fs_sb *csb)
{
	int i;
	int j;

	M0_ENTRY("csb = %p", csb);
	M0_PRE(csb != NULL);

	M0_SET0(csb);
	m0_mutex_init(&csb->csb_mutex);
	svc_ctx_tlist_init(&csb->csb_service_contexts);
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

	M0_LEAVE();
}

static void m0t1fs_sb_fini(struct m0t1fs_sb *csb)
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
	svc_ctx_tlist_fini(&csb->csb_service_contexts);
	m0_mutex_fini(&csb->csb_mutex);
	csb->csb_next_key = 0;
	M0_LEAVE();
}

static int m0t1fs_poolmach_create(struct m0_poolmach **out, uint32_t pool_width,
				  uint32_t nr_parity_units)
{
	struct m0_poolmach *m;
	int                 rc;
	enum {
		/* @todo this should be retrieved from confc */
		NR_NODES          = 1,
		MAX_NODE_FAILURES = 1
	};

	M0_ALLOC_PTR(m);
	if (m == NULL)
		return -ENOMEM;

	rc = m0_poolmach_init(m, NULL, NULL, NULL, NR_NODES, pool_width,
			      MAX_NODE_FAILURES, nr_parity_units);
	if (rc == 0)
		*out = m;
	else
		m0_free(m);
	return rc;
}

static void m0t1fs_poolmach_destroy(struct m0_poolmach *mach)
{
	m0_poolmach_fini(mach);
	m0_free(mach);
}

static int cl_map_build(struct m0t1fs_sb *csb, uint32_t nr_ios,
			const struct fs_params *fs_params)
{
	struct m0t1fs_service_context        *ctx;
	int                                   nr_cont_per_svc;
	int                                   cur;
	int                                   i;
	uint32_t                              nr_data_containers;
	struct m0t1fs_container_location_map *map = &csb->csb_cl_map;

	M0_ENTRY();
	M0_PRE(nr_ios > 0);

	/* See "Containers and component objects" section in m0t1fs.h
	 * for more information on following line */
	csb->csb_nr_containers = fs_params->fs_pool_width + 2;
	csb->csb_pool_width    = fs_params->fs_pool_width;

	if (fs_params->fs_pool_width < fs_params->fs_nr_data_units +
	    2 * fs_params->fs_nr_parity_units ||
	    csb->csb_nr_containers > M0T1FS_MAX_NR_CONTAINERS)
		M0_RETURN(-EINVAL);

	/* 1 for MD, 1 for RM, the rest are data containers. */
	nr_data_containers = csb->csb_nr_containers - 2;

	nr_cont_per_svc = nr_data_containers / nr_ios;
	if (nr_data_containers % nr_ios != 0)
		++nr_cont_per_svc;
	M0_LOG(M0_DEBUG, "nr_cont_per_svc = %d", nr_cont_per_svc);

	M0_SET0(map);

	cur = 1;
	m0_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {
		switch (ctx->sc_type) {
		case M0_CST_MDS:
			/* Currently assuming only one MDS, which will serve
			   container 0 */
			map->clm_map[0] = ctx;
			break;

		case M0_CST_IOS:
			for (i = 0;
			     i < nr_cont_per_svc && cur <= nr_data_containers;
			     ++i, ++cur)
				map->clm_map[cur] = ctx;
			break;

		case M0_CST_MGS:
			break;

		case M0_CST_SS:
			break;

		case M0_CST_DLM:
			map->clm_map[csb->csb_nr_containers] = ctx;
			break;

		default:
			M0_IMPOSSIBLE("Invalid service type");
		}
	} m0_tl_endfor;

	M0_RETURN(0);
}

/* ----------------------------------------------------------------
 * Layout
 * ---------------------------------------------------------------- */
static int m0t1fs_layout_build(const uint64_t         layout_id,
			       const uint32_t         N,
			       const uint32_t         K,
			       const uint32_t         pool_width,
			       const uint64_t         unit_size,
			       struct m0_layout_enum *le,
			       struct m0_layout     **layout)
{
	struct m0_pdclust_attr    pl_attr;
	struct m0_pdclust_layout *pdlayout;
	int                       rc;

	M0_ENTRY();
	M0_PRE(pool_width > 0);
	M0_PRE(le != NULL && layout != NULL);

	pl_attr = (struct m0_pdclust_attr){
		.pa_N         = N,
		.pa_K         = K,
		.pa_P         = pool_width,
		.pa_unit_size = unit_size,
	};
	m0_uint128_init(&pl_attr.pa_seed, "upjumpandpumpim,");

	*layout = NULL;
	rc = m0_pdclust_build(&m0t1fs_globals.g_layout_dom,
			      layout_id, &pl_attr, le,
			      &pdlayout);
	if (rc == 0)
		*layout = m0_pdl_to_layout(pdlayout);

	M0_RETURN(rc);
}

static int m0t1fs_cob_id_enum_build(const uint32_t pool_width,
				    struct m0_layout_enum **lay_enum)
{
	struct m0_layout_linear_attr  lin_attr;
	struct m0_layout_linear_enum *lle;
	int                           rc;

	M0_ENTRY();
	M0_PRE(pool_width > 0 && lay_enum != NULL);
	/*
	 * cob_fid = fid { B * idx + A, gob_fid.key }
	 * where idx is in [0, pool_width)
	 */
	lin_attr = (struct m0_layout_linear_attr){
		.lla_nr = pool_width,
		.lla_A  = 1,
		.lla_B  = 1
	};

	*lay_enum = NULL;
	rc = m0_linear_enum_build(&m0t1fs_globals.g_layout_dom,
				  &lin_attr, &lle);
	if (rc == 0)
		*lay_enum = &lle->lle_base;

	M0_RETURN(rc);
}

static int
m0t1fs_sb_layout_init(struct m0t1fs_sb *csb, const struct fs_params *fs_params)
{
	struct m0_layout_enum *layout_enum;
	int                    rc;

	M0_ENTRY();

	M0_PRE(fs_params->fs_pool_width != 0);
	M0_PRE(fs_params->fs_nr_data_units != 0);
	M0_PRE(fs_params->fs_nr_parity_units != 0);
	M0_PRE(fs_params->fs_unit_size != 0);

try_again:
	do {
		uint64_t          random = m0_time_nanoseconds(m0_time_now());
		uint64_t          unique_lid;
		struct m0_layout *unique_layout = NULL;

		/* Generate a random layout id and make sure it doesn't exist */
		do {
			unique_lid = m0_rnd(~0ULL >> 16, &random);
		} while (unique_lid == 0);

		rc = m0t1fs_layout_op(csb, M0_LAYOUT_OP_LOOKUP,
				      unique_lid, &unique_layout);
		if (rc == 0) {
			m0_layout_put(unique_layout);
			M0_LOG(M0_DEBUG, "lid %lld is duplicated, try again.",
					 (unsigned long long)unique_lid);
			continue;
		}
		if (rc != -ENOENT) {
			M0_LOG(M0_ERROR, "lid %lld layout lookup error: %d",
					 (unsigned long long)unique_lid, rc);
			M0_RETURN(rc);
		}
		M0_LOG(M0_DEBUG, "lid %llu not found. It's a unique lid",
				  (unsigned long long)unique_lid);
		csb->csb_layout_id = unique_lid;
		break;
	} while (1);

	rc = m0t1fs_cob_id_enum_build(fs_params->fs_pool_width, &layout_enum);
	if (rc == 0) {
		rc = m0t1fs_layout_build(csb->csb_layout_id,
					 fs_params->fs_nr_data_units,
					 fs_params->fs_nr_parity_units,
					 fs_params->fs_pool_width,
					 fs_params->fs_unit_size, layout_enum,
					 &csb->csb_file_layout);
		if (rc == 0) {
			/* create the new layout on mds: detect -EEXIST.
			 * Other client may already have created this since
			 * last lookup.
			 */
			rc = m0t1fs_layout_op(csb, M0_LAYOUT_OP_ADD,
					      csb->csb_layout_id, NULL);
			if (rc != 0) {
				/* layout_enum will be released along
				 * with this layout */
				m0_layout_put(csb->csb_file_layout);
				csb->csb_file_layout = NULL;
				if (rc == -EEXIST) {
					M0_LOG(M0_DEBUG, "layout duplicated,"
							 " try again.");
					goto try_again;
				}
			}
		} else
			m0_layout_enum_fini(layout_enum);
	}

	M0_RETURN(rc);
}

static void m0t1fs_sb_layout_fini(struct m0t1fs_sb *csb)
{
	M0_ENTRY();

	if (csb->csb_file_layout != NULL)
		m0_layout_put(csb->csb_file_layout);
	csb->csb_file_layout = NULL;

	M0_LEAVE();
}

static int m0t1fs_setup(struct m0t1fs_sb *csb, const struct mount_opts *mops)
{
	struct m0t1fs_service_context *ctx;
	struct m0_confc                confc;
	struct m0_conf_obj            *fs;
	struct m0_reqh                *reqh = &m0t1fs_globals.g_reqh;
	const char                    *ep_addr;
	struct m0_fid                  prof_fid;
	uint32_t                       nr_ios = 0;
	int                            rc;
	struct fs_params               fs_params = {0};
	bool                           stats_svc_is_provided = false;

	M0_ENTRY();
	M0_PRE(csb->csb_astthread.t_state == TS_RUNNING);

	csb->csb_next_key = mops->mo_fid_start;

	rc = m0_fid_sscanf(mops->mo_profile, &prof_fid);
	if (rc != 0) {
		M0_LOG(M0_FATAL, "Cannot parse profile `%s'", mops->mo_profile);
		M0_RETURN(rc);
	}

	rc = m0_confc_init(&confc, &csb->csb_iogroup, &prof_fid,
			   mops->mo_confd, &m0t1fs_globals.g_rpc_machine,
			   mops->mo_local_conf);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_confc_open_sync(&fs, confc.cc_root,
				M0_CONF_PROFILE_FILESYSTEM_FID);
	if (rc != 0)
		goto end;

	rc = fs_params_parse(&fs_params,
			     M0_CONF_CAST(fs, m0_conf_filesystem)->cf_params) ?:
		connect_to_services(csb, fs, &nr_ios);

	m0_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {
		if (ctx->sc_type == M0_CST_SS) {
			stats_svc_is_provided = true;
			break;
		}
	} m0_tlist_endfor;
	if (stats_svc_is_provided) {
		ep_addr = ctx->sc_conn.c_rpcchan->rc_destep->nep_addr;
		m0_addb_monitor_setup(reqh, &ctx->sc_conn, ep_addr);
		M0_LOG(M0_DEBUG, "Stats service connected");
	} else
		M0_LOG(M0_WARN, "Stats service not connected");
	m0_confc_close(fs);
	if (rc != 0)
		goto end;

	rc = configure_addb_rpc_sink(&m0_addb_gmc);
	if (rc != 0)
		goto err_disconnect;

	rc = m0_pool_init(&csb->csb_pool, fs_params.fs_pool_width);
	if (rc != 0)
		goto addb_mc_unconf;

	rc = m0t1fs_poolmach_create(&csb->csb_pool.po_mach,
				    fs_params.fs_pool_width,
				    fs_params.fs_nr_parity_units);
	if (rc != 0)
		goto err_pool_fini;

	rc = cl_map_build(csb, nr_ios, &fs_params);
	if (rc != 0)
		goto err_poolmach_destroy;

	rc = m0t1fs_sb_layout_init(csb, &fs_params);
	if (rc == 0)
		goto end;

	m0t1fs_sb_layout_fini(csb);
err_poolmach_destroy:
	m0t1fs_poolmach_destroy(csb->csb_pool.po_mach);
err_pool_fini:
	m0_pool_fini(&csb->csb_pool);
addb_mc_unconf:
	/* @todo Make a separate unconfigure api and do this in that */
	m0_addb_mc_fini(&m0_addb_gmc);
	m0_addb_mc_init(&m0_addb_gmc);
err_disconnect:
	disconnect_from_services(csb);

end:
	m0_confc_fini(&confc);
	M0_RETURN(rc);
}

static void m0t1fs_teardown(struct m0t1fs_sb *csb)
{
	m0t1fs_sb_layout_fini(csb);
	m0t1fs_poolmach_destroy(csb->csb_pool.po_mach);
	m0_pool_fini(&csb->csb_pool);
	/* @todo Make a separate unconfigure api and do this in that */
	m0_addb_mc_fini(&m0_addb_gmc);
	m0_addb_mc_init(&m0_addb_gmc);
	disconnect_from_services(csb);
}

static int m0t1fs_root_alloc(struct super_block *sb)
{
	struct inode             *root_inode;
	int                       rc;
	struct m0t1fs_sb         *csb = M0T1FS_SB(sb);
	struct m0_fop_statfs_rep *rep = NULL;
	struct m0_addb_ctx *cv[] = { &csb->csb_addb_ctx, NULL };

	M0_ENTRY();

	rc = m0t1fs_mds_statfs(csb, &rep);
	if (rc != 0)
		M0_RETURN(rc);

	sb->s_magic = rep->f_type;
	csb->csb_namelen = rep->f_namelen;

	M0_LOG(M0_DEBUG, "Got mdservice root "FID_F, FID_P(&rep->f_root));

	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_m0t1fs_root_cob, cv,
		     rep->f_root.f_container, rep->f_root.f_key);

	root_inode = m0t1fs_root_iget(sb, &rep->f_root);
	if (IS_ERR(root_inode))
		M0_RETURN((int)PTR_ERR(root_inode));

	sb->s_root = d_alloc_root(root_inode);
	if (sb->s_root == NULL) {
		iput(root_inode);
		M0_RETURN(-ENOMEM);
	}
	M0_RETURN(0);
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

	rc = mount_opts_parse(data, &mops);
	if (rc != 0)
		goto sb_fini;

	rc = M0_THREAD_INIT(&csb->csb_astthread, struct m0t1fs_sb *, NULL,
			    &ast_thread, csb, "ast_thread");
	M0_ASSERT(rc == 0);

	rc = m0t1fs_setup(csb, &mops);
	if (rc != 0)
		goto thread_stop;

	sb->s_fs_info        = csb;
	sb->s_blocksize      = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_maxbytes       = MAX_LFS_FILESIZE;
	sb->s_op             = &m0t1fs_super_operations;

	rc = m0t1fs_root_alloc(sb);
	if (rc != 0)
		goto m0t1fs_teardown;

	io_bob_tlists_init();
	M0_SET0(&iommstats);

	M0_RETURN(0);

m0t1fs_teardown:
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
	M0_RETURN(rc);
}

/** Implementation of file_system_type::get_sb() interface. */
M0_INTERNAL int m0t1fs_get_sb(struct file_system_type *fstype, int flags,
			      const char *devname, void *data,
			      struct vfsmount *mnt)
{
	M0_ENTRY("flags: 0x%x, devname: %s, data: %s", flags, devname,
		 (char *)data);
	M0_RETURN(get_sb_nodev(fstype, flags, data, m0t1fs_fill_super, mnt));
}

/** Implementation of file_system_type::kill_sb() interface. */
M0_INTERNAL void m0t1fs_kill_sb(struct super_block *sb)
{
	struct m0t1fs_sb *csb = M0T1FS_SB(sb);

	M0_ENTRY("csb = %p", csb);

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
	enum { AST_THREAD_TIMEOUT = 10 };

	while (1) {
		m0_chan_timedwait(&csb->csb_iogroup.s_clink,
				  m0_time_from_now(AST_THREAD_TIMEOUT, 0));
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
