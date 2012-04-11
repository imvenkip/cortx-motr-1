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
 * Original creation date: 11/07/2011
 */

#include <linux/mount.h>
#include <linux/parser.h>     /* substring_t               */
#include <linux/slab.h>       /* kmalloc(), kfree()        */

#include "lib/misc.h"         /* C2_SET0()                 */
#include "lib/memory.h"       /* C2_ALLOC_PTR(), c2_free() */
#include "c2t1fs/linux_kernel/c2t1fs.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"        /* C2_LOG and C2_ENTRY */
#include "pool/pool.h"        /* c2_pool_init(), c2_pool_fini() */

/* Super block */

static int  c2t1fs_fill_super(struct super_block *sb, void *data, int silent);
static int  c2t1fs_sb_init(struct c2t1fs_sb *csb);
static void c2t1fs_sb_fini(struct c2t1fs_sb *csb);

static int  c2t1fs_config_fetch(struct c2t1fs_sb *csb);

/* Mount options */

static void c2t1fs_mnt_opts_init(struct c2t1fs_mnt_opts *mntopts);
static void c2t1fs_mnt_opts_fini(struct c2t1fs_mnt_opts *mntopts);
static int  c2t1fs_mnt_opts_validate(const struct c2t1fs_mnt_opts *mnt_opts);
static int  c2t1fs_mnt_opts_parse(char                   *options,
				  struct c2t1fs_mnt_opts *mnt_opts);

/* service contexts */

static void c2t1fs_service_context_init(struct c2t1fs_service_context *ctx,
					struct c2t1fs_sb              *csb,
					enum c2t1fs_service_type       type,
					char                          *ep_addr);

static void c2t1fs_service_context_fini(struct c2t1fs_service_context *ctx);

static int  c2t1fs_service_contexts_populate(struct c2t1fs_sb *csb);
static void c2t1fs_service_contexts_discard(struct c2t1fs_sb *csb);

static int  c2t1fs_connect_to_all_services(struct c2t1fs_sb *csb);
static void c2t1fs_disconnect_from_all_services(struct c2t1fs_sb *csb);

static int  c2t1fs_connect_to_service(struct c2t1fs_service_context *ctx);
static void c2t1fs_disconnect_from_service(struct c2t1fs_service_context *ctx);

/* Container location map */

static int
c2t1fs_container_location_map_init(struct c2t1fs_container_location_map *map,
				   int nr_containers);

static void
c2t1fs_container_location_map_fini(struct c2t1fs_container_location_map *map);

static int c2t1fs_container_location_map_build(struct c2t1fs_sb *csb);

/* global instances */

static const struct super_operations c2t1fs_super_operations = {
	.alloc_inode   = c2t1fs_alloc_inode,
	.destroy_inode = c2t1fs_destroy_inode,
	.drop_inode    = generic_delete_inode /* provided by linux kernel */
};

const struct c2_fid c2t1fs_root_fid = {
	.f_container = 0,
	.f_key       = 2
};

/**
   tlist descriptor for list of c2t1fs_service_context objects placed in
   c2t1fs_sb::csb_service_contexts list using sc_link.
 */
C2_TL_DESCR_DEFINE(svc_ctx, "Service contexts", static,
		   struct c2t1fs_service_context, sc_link, sc_magic,
		   MAGIC_SVC_CTX, MAGIC_SVCCTXHD);

C2_TL_DEFINE(svc_ctx, static, struct c2t1fs_service_context);

/**
   Implementation of file_system_type::get_sb() interface.
 */
int c2t1fs_get_sb(struct file_system_type *fstype,
		  int                      flags,
		  const char              *devname,
		  void                    *data,
		  struct vfsmount         *mnt)
{
	int rc;

	C2_ENTRY("flags: 0x%x, devname: %s, data: %s", flags, devname,
					       (char *)data);

	rc = get_sb_nodev(fstype, flags, data, c2t1fs_fill_super, mnt);

	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int c2t1fs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct c2t1fs_mnt_opts *mntopts;
	struct c2t1fs_sb       *csb;
	struct inode           *root_inode;
	uint32_t                pool_width;
	int                     rc;

	C2_ENTRY();

	C2_ALLOC_PTR(csb);
	if (csb == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2t1fs_sb_init(csb);
	if (rc != 0)
		goto out_free;

	mntopts = &csb->csb_mnt_opts;
	rc = c2t1fs_mnt_opts_parse(data, mntopts);
	if (rc != 0)
		goto out_fini;

	csb->csb_nr_data_units   = mntopts->mo_nr_data_units ?:
					C2T1FS_DEFAULT_NR_DATA_UNITS;
	csb->csb_nr_parity_units = mntopts->mo_nr_parity_units ?:
					C2T1FS_DEFAULT_NR_PARITY_UNITS;
	csb->csb_unit_size       = mntopts->mo_unit_size ?:
					C2T1FS_DEFAULT_STRIPE_UNIT_SIZE;
	pool_width               = mntopts->mo_pool_width ?:
					C2T1FS_DEFAULT_POOL_WIDTH;
	/* See "Containers and component objects" section in c2t1fs.h for more
	   information on following line */
	csb->csb_nr_containers   = pool_width + 1;

	C2_LOG("P = %d, N = %d, K = %d unit_size %d",
			pool_width, csb->csb_nr_data_units,
			csb->csb_nr_parity_units, csb->csb_unit_size);

	/* P >= N + 2 * K ??*/
	if (pool_width <
	    csb->csb_nr_data_units + 2 * csb->csb_nr_parity_units ||
		csb->csb_nr_containers > C2T1FS_MAX_NR_CONTAINERS) {

		rc = -EINVAL;
		goto out_fini;
	}

	rc = c2t1fs_config_fetch(csb);
	if (rc != 0)
		goto out_fini;

	rc = c2_pool_init(&csb->csb_pool, pool_width);
	if (rc != 0)
		goto out_fini;

	rc = c2t1fs_connect_to_all_services(csb);
	if (rc != 0)
		goto pool_fini;

	rc = c2t1fs_container_location_map_init(&csb->csb_cl_map,
						csb->csb_nr_containers);
	if (rc != 0)
		goto disconnect_all;

	rc = c2t1fs_container_location_map_build(csb);
	if (rc != 0)
		goto out_map_fini;

	sb->s_fs_info = csb;

	sb->s_blocksize      = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic          = C2T1FS_SUPER_MAGIC;
	sb->s_maxbytes       = MAX_LFS_FILESIZE;
	sb->s_op             = &c2t1fs_super_operations;

	root_inode = c2t1fs_root_iget(sb);
	if (root_inode == NULL) {
		rc = -ENOMEM;
		goto out_map_fini;
	}

	sb->s_root = d_alloc_root(root_inode);
	if (sb->s_root == NULL) {
		iput(root_inode);
		rc = -ENOMEM;
		goto out_map_fini;
	}

	C2_LEAVE("rc: %d", rc);
	return 0;

out_map_fini:
	c2t1fs_container_location_map_fini(&csb->csb_cl_map);

disconnect_all:
	c2t1fs_disconnect_from_all_services(csb);

pool_fini:
	c2_pool_fini(&csb->csb_pool);

out_fini:
	c2t1fs_sb_fini(csb);

out_free:
	c2_free(csb);

out:
	sb->s_fs_info = NULL;

	C2_ASSERT(rc != 0);
	C2_LEAVE("rc: %d", rc);
	return rc;
}

/**
   Implementation of file_system_type::kill_sb() interface.
 */
void c2t1fs_kill_sb(struct super_block *sb)
{
	struct c2t1fs_sb *csb;

	C2_ENTRY();

	csb = C2T1FS_SB(sb);
	C2_LOG("csb = %p", csb);
	/*
	 * If c2t1fs_fill_super() fails then deactivate_locked_super() calls
	 * c2t1fs_fs_type->kill_sb(). In that case, csb == NULL.
	 * But still not sure, such csb != NULL handling is a good idea.
	 */
	if (csb != NULL) {
		c2t1fs_container_location_map_fini(&csb->csb_cl_map);
		c2t1fs_disconnect_from_all_services(csb);
		c2t1fs_service_contexts_discard(csb);
		c2_pool_fini(&csb->csb_pool);
		c2t1fs_sb_fini(csb);
		c2_free(csb);
	}
	kill_anon_super(sb);

	C2_LEAVE();
}

static int c2t1fs_sb_init(struct c2t1fs_sb *csb)
{
	C2_ENTRY();

	C2_ASSERT(csb != NULL);

	C2_SET0(csb);

	c2_mutex_init(&csb->csb_mutex);
	c2t1fs_mnt_opts_init(&csb->csb_mnt_opts);
	svc_ctx_tlist_init(&csb->csb_service_contexts);
	csb->csb_next_key = c2t1fs_root_fid.f_key + 1;

	C2_LEAVE("rc: 0");
	return 0;
}

static void c2t1fs_sb_fini(struct c2t1fs_sb *csb)
{
	C2_ENTRY();

	C2_ASSERT(csb != NULL);

	svc_ctx_tlist_fini(&csb->csb_service_contexts);
	c2_mutex_fini(&csb->csb_mutex);
	c2t1fs_mnt_opts_fini(&csb->csb_mnt_opts);
	csb->csb_next_key = 0;

	C2_LEAVE();
}

enum c2t1fs_mntopts {
	C2T1FS_MNTOPT_MGS = 1,
	C2T1FS_MNTOPT_PROFILE,
	C2T1FS_MNTOPT_MDS,
	C2T1FS_MNTOPT_IOS,
	C2T1FS_MNTOPT_POOL_WIDTH,
	C2T1FS_MNTOPT_NR_DATA_UNITS,
	C2T1FS_MNTOPT_NR_PARITY_UNITS,
	C2T1FS_MNTOPT_UNIT_SIZE,
	C2T1FS_MNTOPT_ERR,
};

static const match_table_t c2t1fs_mntopt_tokens = {
	{ C2T1FS_MNTOPT_MGS,             "mgs=%s"             },
	{ C2T1FS_MNTOPT_PROFILE,         "profile=%s"         },
	{ C2T1FS_MNTOPT_MDS,             "mds=%s"             },
	{ C2T1FS_MNTOPT_IOS,             "ios=%s"             },
	{ C2T1FS_MNTOPT_POOL_WIDTH,      "pool_width=%s"      },
	{ C2T1FS_MNTOPT_NR_DATA_UNITS,   "nr_data_units=%s"   },
	{ C2T1FS_MNTOPT_NR_PARITY_UNITS, "nr_parity_units=%s" },
	{ C2T1FS_MNTOPT_UNIT_SIZE,       "unit_size=%s"       },
	/* match_token() requires last element must have NULL pattern */
	{ C2T1FS_MNTOPT_ERR,              NULL                },
};

static void c2t1fs_mnt_opts_init(struct c2t1fs_mnt_opts *mntopts)
{
	C2_ENTRY();
	C2_ASSERT(mntopts != NULL);

	C2_SET0(mntopts);

	C2_LEAVE();
}

static void c2t1fs_mnt_opts_fini(struct c2t1fs_mnt_opts *mntopts)
{
	int i;

	C2_ENTRY();
	C2_ASSERT(mntopts != NULL);

	for (i = 0; i < mntopts->mo_nr_ios_ep; i++) {
		C2_ASSERT(mntopts->mo_ios_ep_addr[i] != NULL);
		/*
		 * using kfree() instead of c2_free() because the memory
		 * was allocated using match_strdup().
		 */
		kfree(mntopts->mo_ios_ep_addr[i]);
	}
	for (i = 0; i < mntopts->mo_nr_mds_ep; i++) {
		C2_ASSERT(mntopts->mo_mds_ep_addr[i] != NULL);
		kfree(mntopts->mo_mds_ep_addr[i]);
	}

	if (mntopts->mo_profile != NULL)
		kfree(mntopts->mo_profile);

	if (mntopts->mo_mgs_ep_addr != NULL)
		kfree(mntopts->mo_mgs_ep_addr);

	if (mntopts->mo_options != NULL)
		kfree(mntopts->mo_options);

	C2_SET0(mntopts);

	C2_LEAVE();
}

static int c2t1fs_mnt_opts_validate(const struct c2t1fs_mnt_opts *mnt_opts)
{
	C2_ENTRY();

	if (mnt_opts->mo_nr_ios_ep == 0) {
		C2_LOG("ERROR:"
			 "Must specify at least one io-service endpoint");
		goto invalid;
	}

	/*
	 * Need to test, with unit size that is not multiple of page size.
	 * Until then don't allow.
	 */
	if ((mnt_opts->mo_unit_size & (PAGE_CACHE_SIZE - 1)) != 0) {
		C2_LOG("ERROR:"
			 "Unit size must be multiple of PAGE_CACHE_SIZE");
		goto invalid;
	}

	/*
	 * For simplicity, end point addresses are kept in statically allocated
	 * array. Hence size of the array is limit on number of end-point
	 * addresses.
	 */
	if (mnt_opts->mo_nr_ios_ep > MAX_NR_EP_PER_SERVICE_TYPE ||
	    mnt_opts->mo_nr_mds_ep > MAX_NR_EP_PER_SERVICE_TYPE) {
		C2_LOG("ERROR: number of endpoints must be less than %d",
				MAX_NR_EP_PER_SERVICE_TYPE);
		goto invalid;
	}

	C2_LEAVE("rc: 0");
	return 0;

invalid:
	C2_LEAVE("rc: %d", -EINVAL);
	return -EINVAL;
}

static int c2t1fs_mnt_opts_parse(char                   *options,
				 struct c2t1fs_mnt_opts *mnt_opts)
{
	unsigned long nr;
	substring_t   args[MAX_OPT_ARGS];
	char         *value;
	char         *op;
	int           token;
	int           rc = 0;

	int process_numeric_option(substring_t *substr, unsigned long *nump)
	{
		value = match_strdup(substr);
		if (value == NULL)
			return -ENOMEM;

		rc = strict_strtoul(value, 10, nump);
		kfree(value);
		value = NULL;
		return rc;
	}

	C2_ENTRY();

	C2_LOG("options: %p", options);

	if (options == NULL) {
		rc = -EINVAL;
		goto out;
	}

	mnt_opts->mo_options = kstrdup(options, GFP_KERNEL);
	if (mnt_opts->mo_options == NULL)
		rc = -ENOMEM;

	while ((op = strsep(&options, ",")) != NULL) {
		C2_LOG("Processing \"%s\"", op);

		if (*op == '\0')
			continue;

		token = match_token(op, c2t1fs_mntopt_tokens, args);
		switch (token) {

		case C2T1FS_MNTOPT_IOS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			C2_LOG("ioservice: %s", value);
			mnt_opts->mo_ios_ep_addr[mnt_opts->mo_nr_ios_ep++] =
						value;
			break;

		case C2T1FS_MNTOPT_MDS:
			/*
			 * following 6 lines are duplicated for each "string"
			 * mount option. It is possible to bring them at one
			 * place, but preferred current implementation for
			 * simplicity.
			 */
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			C2_LOG("mdservice: %s", value);
			mnt_opts->mo_mds_ep_addr[mnt_opts->mo_nr_mds_ep++] =
						value;
			break;

		case C2T1FS_MNTOPT_MGS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			C2_LOG("mgservice: %s", value);
			mnt_opts->mo_mgs_ep_addr = value;
			break;

		case C2T1FS_MNTOPT_PROFILE:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			C2_LOG("profile: %s", value);
			mnt_opts->mo_profile = value;
			break;

		case C2T1FS_MNTOPT_POOL_WIDTH:
			rc = process_numeric_option(args, &nr);
			if (rc != 0)
				goto out;
			C2_LOG("pool_width = %lu", nr);
			mnt_opts->mo_pool_width = nr;
			break;

		case C2T1FS_MNTOPT_NR_DATA_UNITS:
			rc = process_numeric_option(args, &nr);
			if (rc != 0)
				goto out;
			C2_LOG("nr_data_units = %lu", nr);
			mnt_opts->mo_nr_data_units = nr;
			break;

		case C2T1FS_MNTOPT_NR_PARITY_UNITS:
			rc = process_numeric_option(args, &nr);
			if (rc != 0)
				goto out;
			C2_LOG("nr_parity_units = %lu", nr);
			mnt_opts->mo_nr_parity_units = nr;
			break;

		case C2T1FS_MNTOPT_UNIT_SIZE:
			rc = process_numeric_option(args, &nr);
			if (rc != 0)
				goto out;
			C2_LOG("unit_size = %lu", nr);
			mnt_opts->mo_unit_size = nr;
			break;

		default:
			C2_LOG("Unrecognized option: %s", op);
			C2_LOG("Supported options: mgs,mds,ios,profile,"
			      "pool_width,nr_data_units,nr_parity_units,"
			      "unit_size");
			rc = -EINVAL;
			goto out;
		}
	}
	rc = c2t1fs_mnt_opts_validate(mnt_opts);

out:
	/* if rc != 0, mnt_opts will be finalised from c2t1fs_sb_fini() */
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static void c2t1fs_service_context_init(struct c2t1fs_service_context *ctx,
					struct c2t1fs_sb              *csb,
					enum c2t1fs_service_type       type,
					char                          *ep_addr)
{
	C2_ENTRY();

	C2_SET0(ctx);

	ctx->sc_csb   = csb;
	ctx->sc_type  = type;
	ctx->sc_addr  = ep_addr;
	ctx->sc_magic = MAGIC_SVC_CTX;

	svc_ctx_tlink_init(ctx);

	C2_LEAVE();
}

static void c2t1fs_service_context_fini(struct c2t1fs_service_context *ctx)
{
	C2_ENTRY();

	svc_ctx_tlink_fini(ctx);
	ctx->sc_magic = 0;

	C2_LEAVE();
}

static int c2t1fs_config_fetch(struct c2t1fs_sb *csb)
{
	C2_ENTRY();

	/* XXX fetch configuration here */

	C2_LEAVE("rc: 0");
	return 0;
}

static int c2t1fs_connect_to_all_services(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;
	int                            rc;

	C2_ENTRY();

	rc = c2t1fs_service_contexts_populate(csb);
	if (rc != 0)
		goto out;

	c2_tlist_for(&svc_ctx_tl, &csb->csb_service_contexts, ctx) {

		rc = c2t1fs_connect_to_service(ctx);
		if (rc != 0) {
			c2t1fs_disconnect_from_all_services(csb);
			goto out;
		}
	} c2_tlist_endfor;
out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int c2t1fs_service_contexts_populate(struct c2t1fs_sb *csb)
{
	struct c2t1fs_mnt_opts        *mntopts;
	int                            rc = 0;

	/* XXX For now, service contexts are populated using mount options.
	   When configuration will be available it should be used. */
	int populate(char *ep_arr[], int n, enum c2t1fs_service_type type)
	{
		struct c2t1fs_service_context *ctx;
		char                          *ep_addr;
		int                            i;

		C2_LOG("n = %d type = %d", n, type);

		for (i = 0; i < n; i++) {
			ep_addr = ep_arr[i];
			C2_LOG("i = %d ep_addr = %s", i, ep_addr);

			C2_ALLOC_PTR(ctx);
			if (ctx == NULL)
				return -ENOMEM;

			c2t1fs_service_context_init(ctx, csb, type, ep_addr);
			svc_ctx_tlist_add_tail(&csb->csb_service_contexts,
						ctx);
		}
		return 0;
	}

	C2_ENTRY();

	mntopts = &csb->csb_mnt_opts;

	rc = populate(mntopts->mo_mds_ep_addr, mntopts->mo_nr_mds_ep,
				C2T1FS_ST_MDS);
	if (rc != 0)
		goto discard_all;

	rc = populate(mntopts->mo_ios_ep_addr, mntopts->mo_nr_ios_ep,
				C2T1FS_ST_IOS);
	if (rc != 0)
		goto discard_all;

	C2_LEAVE("rc: 0");
	return 0;

discard_all:
	c2t1fs_service_contexts_discard(csb);
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static void c2t1fs_service_contexts_discard(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;

	C2_ENTRY();

	c2_tlist_for(&svc_ctx_tl, &csb->csb_service_contexts, ctx) {

		svc_ctx_tlist_del(ctx);
		C2_LOG("discard: %s", ctx->sc_addr);

		c2t1fs_service_context_fini(ctx);
		c2_free(ctx);

	} c2_tlist_endfor;

	C2_LEAVE();
}

static int c2t1fs_connect_to_service(struct c2t1fs_service_context *ctx)
{
	struct c2_rpc_machine     *rpc_mach;
	struct c2_net_transfer_mc *tm;
	struct c2_net_end_point   *ep;
	struct c2_rpc_conn        *conn;
	struct c2_rpc_session     *session;
	int                        rc;

	C2_ENTRY();

	rpc_mach = &c2t1fs_globals.g_rpc_machine;
	tm       = &rpc_mach->cr_tm;

	/* Create target end-point */
	rc = c2_net_end_point_create(&ep, tm, ctx->sc_addr);
	if (rc != 0)
		goto out;

	conn = &ctx->sc_conn;
	rc = c2_rpc_conn_create(conn, ep, rpc_mach,
			C2T1FS_MAX_NR_RPC_IN_FLIGHT, C2T1FS_RPC_TIMEOUT);
	c2_net_end_point_put(ep);
	if (rc != 0)
		goto out;

	session = &ctx->sc_session;
	rc = c2_rpc_session_create(session, conn, C2T1FS_NR_SLOTS_PER_SESSION,
					C2T1FS_RPC_TIMEOUT);
	if (rc != 0)
		goto conn_term;

	ctx->sc_csb->csb_nr_active_contexts++;
	C2_LOG("Connected to [%s] active_ctx %d", ctx->sc_addr,
				ctx->sc_csb->csb_nr_active_contexts);
	C2_LEAVE("rc: %d", rc);
	return rc;

conn_term:
	(void)c2_rpc_conn_terminate_sync(conn, C2T1FS_RPC_TIMEOUT);
	c2_rpc_conn_fini(conn);
out:
	C2_ASSERT(rc != 0);
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static void c2t1fs_disconnect_from_service(struct c2t1fs_service_context *ctx)
{
	C2_ENTRY();

	(void)c2_rpc_session_terminate_sync(&ctx->sc_session,
						C2T1FS_RPC_TIMEOUT);

	(void)c2_rpc_conn_terminate_sync(&ctx->sc_conn, C2T1FS_RPC_TIMEOUT);

	c2_rpc_session_fini(&ctx->sc_session);
	c2_rpc_conn_fini(&ctx->sc_conn);

	ctx->sc_csb->csb_nr_active_contexts--;
	C2_LOG("Disconnected from [%s] active_ctx %d", ctx->sc_addr,
				ctx->sc_csb->csb_nr_active_contexts);
	C2_LEAVE();
}

static void c2t1fs_disconnect_from_all_services(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;

	C2_ENTRY();

	c2_tlist_for(&svc_ctx_tl, &csb->csb_service_contexts, ctx) {

		c2t1fs_disconnect_from_service(ctx);

		if (csb->csb_nr_active_contexts == 0)
			break;

	} c2_tlist_endfor;

	C2_LEAVE();
}

static int
c2t1fs_container_location_map_init(struct c2t1fs_container_location_map *map,
				   int nr_containers)
{
	C2_ENTRY();

	C2_SET0(map);

	C2_LEAVE("rc: 0");
	return 0;
}

static void
c2t1fs_container_location_map_fini(struct c2t1fs_container_location_map *map)
{
	C2_ENTRY();
	C2_LEAVE();
}

static int c2t1fs_container_location_map_build(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context        *ctx;
	struct c2t1fs_container_location_map *map;
	int                                   nr_cont_per_svc;
	int                                   nr_data_containers;
	int                                   nr_ios;
	int                                   i;
	int                                   cur;

	C2_ENTRY();

	nr_ios = csb->csb_mnt_opts.mo_nr_ios_ep;
	C2_ASSERT(nr_ios > 0);

	/* Out of csb->csb_nr_containers 1 is md container */
	nr_data_containers = csb->csb_nr_containers - 1;
	C2_ASSERT(nr_data_containers > 0);

	nr_cont_per_svc = nr_data_containers / nr_ios;
	if (nr_data_containers % nr_ios != 0)
		nr_cont_per_svc++;

	C2_LOG("nr_cont_per_svc = %d", nr_cont_per_svc);

	map = &csb->csb_cl_map;
	cur = 1;

	c2_tlist_for(&svc_ctx_tl, &csb->csb_service_contexts, ctx) {

		switch (ctx->sc_type) {

		case C2T1FS_ST_MDS:
			/* Currently assuming only one MGS, which will serve
			   container 0 */
			map->clm_map[0] = ctx;
			C2_LOG("container_id [0] at %s", ctx->sc_addr);
			break;

		case C2T1FS_ST_IOS:
			for (i = 0; i < nr_cont_per_svc &&
				    cur <= nr_data_containers; i++, cur++) {
				map->clm_map[cur] = ctx;
				C2_LOG("container_id [%d] at %s", cur,
						ctx->sc_addr);
			}
			break;

		case C2T1FS_ST_MGS:
			break;

		default:
			C2_ASSERT(0);
		}

	} c2_tlist_endfor;

	C2_LEAVE("rc: 0");
	return 0;
}

struct c2_rpc_session *
c2t1fs_container_id_to_session(const struct c2t1fs_sb *csb,
			       uint64_t                container_id)
{
	struct c2t1fs_service_context        *ctx;

	C2_ENTRY();

	C2_ASSERT(container_id < csb->csb_nr_containers);

	ctx = csb->csb_cl_map.clm_map[container_id];
	C2_ASSERT(ctx != NULL);

	C2_LEAVE("session: %p", &ctx->sc_session);
	return &ctx->sc_session;
}

void c2t1fs_fs_lock(struct c2t1fs_sb *csb)
{
	C2_ENTRY();

	c2_mutex_lock(&csb->csb_mutex);

	C2_LEAVE();
}

void c2t1fs_fs_unlock(struct c2t1fs_sb *csb)
{
	C2_ENTRY();

	c2_mutex_unlock(&csb->csb_mutex);

	C2_LEAVE();
}

bool c2t1fs_fs_is_locked(const struct c2t1fs_sb *csb)
{
	return c2_mutex_is_locked(&csb->csb_mutex);
}
