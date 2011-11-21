#include <linux/mount.h>
#include <linux/parser.h> /* substring_t */
#include <linux/slab.h> /* kmalloc(), kfree() */

#include "lib/misc.h"  /* C2_SET0() */
#include "lib/memory.h" /* C2_ALLOC_PTR(), c2_free() */
#include "c2t1fs/c2t1fs.h"

static int  c2t1fs_fill_super(struct super_block *sb, void *data, int silent);
static int  c2t1fs_sb_init(struct c2t1fs_sb *csb);
static void c2t1fs_sb_fini(struct c2t1fs_sb *csb);

static void c2t1fs_mnt_opts_init(struct c2t1fs_mnt_opts *mntopts);
static void c2t1fs_mnt_opts_fini(struct c2t1fs_mnt_opts *mntopts);
static int  c2t1fs_mnt_opts_validate(struct c2t1fs_mnt_opts *mnt_opts);
static int  c2t1fs_mnt_opts_parse(char                   *options,
				  struct c2t1fs_mnt_opts *mnt_opts);

static int  c2t1fs_config_fetch(struct c2t1fs_sb *csb);

static int  c2t1fs_populate_service_contexts(struct c2t1fs_sb *csb);
static void c2t1fs_discard_service_contexts(struct c2t1fs_sb *csb);

static int  c2t1fs_connect_to_all_services(struct c2t1fs_sb *csb);
static void c2t1fs_disconnect_from_all_services(struct c2t1fs_sb *csb);

static int  c2t1fs_connect_to_service(struct c2t1fs_service_context *ctx);
static void c2t1fs_disconnect_from_service(struct c2t1fs_service_context *ctx);

static struct super_operations c2t1fs_super_operations = {
	.alloc_inode   = c2t1fs_alloc_inode,
	.destroy_inode = c2t1fs_destroy_inode,
	.drop_inode    = generic_delete_inode
};

const struct c2_fid c2t1fs_root_fid = {
	.f_container = 0,
	.f_key = 2
};

/**
   tlist descriptor for list of c2t1fs_service_context objects placed in
   c2t1fs_sb::csb_service_contexts list using sc_link.
 */
static const struct c2_tl_descr svc_ctx_tl_descr =
			C2_TL_DESCR("service contexts",
				    struct c2t1fs_service_context,
				    sc_link,
				    sc_magic,
				    MAGIC_SVC_CTX,
				    MAGIC_SVCCTXHD);

int c2t1fs_get_sb(struct file_system_type *fstype,
		  int                      flags,
		  const char              *devname,
		  void                    *data,
		  struct vfsmount         *mnt)
{
	int rc;

	TRACE("flags: 0x%x, devname: %s, data: %s\n", flags, devname,
							(char *)data);

	rc = get_sb_nodev(fstype, flags, data, c2t1fs_fill_super, mnt);

	END(rc);
	return rc;
}

static int c2t1fs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct c2t1fs_mnt_opts *mntopts;
	struct c2t1fs_sb       *csb;
	struct inode           *root_inode;
	int                     rc;

	START();

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

	csb->csb_nr_containers   = mntopts->mo_nr_containers ?:
					C2T1FS_DEFAULT_NR_CONTAINERS;
	csb->csb_nr_data_units   = mntopts->mo_nr_data_units ?:
					C2T1FS_DEFAULT_NR_DATA_UNITS;
	csb->csb_nr_parity_units = mntopts->mo_nr_parity_units ?:
					C2T1FS_DEFAULT_NR_PARITY_UNITS;
	csb->csb_unit_size       = mntopts->mo_unit_size ?:
					C2T1FS_DEFAULT_STRIPE_UNIT_SIZE;

	TRACE("P = %d, N = %d, K = %d unit_size %d\n", csb->csb_nr_containers,
			csb->csb_nr_data_units, csb->csb_nr_parity_units,
			csb->csb_unit_size);

	/* P >= N + 2 * K ??*/
	if (csb->csb_nr_containers < csb->csb_nr_data_units +
				2 * csb->csb_nr_parity_units ||
		csb->csb_nr_containers > C2T1FS_MAX_NR_CONTAINERS) {

		rc = -EINVAL;
		goto out_fini;
	}

	rc = c2t1fs_config_fetch(csb);
	if (rc != 0)
		goto out_fini;

	rc = c2t1fs_connect_to_all_services(csb);
	if (rc != 0)
		goto out_fini;

	rc = c2t1fs_container_location_map_init(&csb->csb_cl_map,
						csb->csb_nr_containers);
	if (rc != 0)
		goto disconnect_all;

	rc = c2t1fs_container_location_map_build(csb);
	if (rc != 0)
		goto out_map_fini;

	sb->s_fs_info = csb;

	sb->s_blocksize      = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
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

	END(0);
	return 0;

out_map_fini:
	c2t1fs_container_location_map_fini(&csb->csb_cl_map);

disconnect_all:
	c2t1fs_disconnect_from_all_services(csb);

out_fini:
	c2t1fs_sb_fini(csb);

out_free:
	c2_free(csb);

out:
	sb->s_fs_info = NULL;

	C2_ASSERT(rc != 0);
	END(rc);
	return rc;
}

void c2t1fs_kill_sb(struct super_block *sb)
{
	struct c2t1fs_sb *csb;

	START();

	csb = C2T1FS_SB(sb);
	TRACE("csb = %p\n", csb);
	/* If c2t1fs_fill_super() fails then deactivate_locked_super() calls
	   c2t1fs_fs_type->kill_sb(). In that case, csb == NULL. */
	if (csb != NULL) {
		c2t1fs_container_location_map_fini(&csb->csb_cl_map);
		c2t1fs_disconnect_from_all_services(csb);
		c2t1fs_discard_service_contexts(csb);
		c2t1fs_sb_fini(csb);
		c2_free(csb);
	}
	kill_anon_super(sb);

	END(0);
}

static int c2t1fs_sb_init(struct c2t1fs_sb *csb)
{
	START();

	C2_ASSERT(csb != NULL);

	C2_SET0(csb);

	c2_mutex_init(&csb->csb_mutex);
	c2t1fs_mnt_opts_init(&csb->csb_mnt_opts);
	c2_tlist_init(&svc_ctx_tl_descr, &csb->csb_service_contexts);

	END(0);
	return 0;
}

static void c2t1fs_sb_fini(struct c2t1fs_sb *csb)
{
	START();

	C2_ASSERT(csb != NULL);

	c2_tlist_fini(&svc_ctx_tl_descr, &csb->csb_service_contexts);
	c2_mutex_fini(&csb->csb_mutex);
	c2t1fs_mnt_opts_fini(&csb->csb_mnt_opts);

	END(0);
}

enum {
	C2T1FS_MNTOPT_MGS = 1,
	C2T1FS_MNTOPT_PROFILE,
	C2T1FS_MNTOPT_MDS,
	C2T1FS_MNTOPT_IOS,
	C2T1FS_MNTOPT_NR_CONTAINERS,
	C2T1FS_MNTOPT_NR_DATA_UNITS,
	C2T1FS_MNTOPT_NR_PARITY_UNITS,
	C2T1FS_MNTOPT_UNIT_SIZE,
	C2T1FS_MNTOPT_ERR,
};

static const match_table_t c2t1fs_mntopt_tokens = {
	{ C2T1FS_MNTOPT_MGS,             "mgs=%s" },
	{ C2T1FS_MNTOPT_PROFILE,         "profile=%s" },
	{ C2T1FS_MNTOPT_MDS,             "mds=%s" },
	{ C2T1FS_MNTOPT_IOS,             "ios=%s" },
	{ C2T1FS_MNTOPT_NR_CONTAINERS,   "nr_containers=%s" },
	{ C2T1FS_MNTOPT_NR_DATA_UNITS,   "nr_data_units=%s" },
	{ C2T1FS_MNTOPT_NR_PARITY_UNITS, "nr_parity_units=%s" },
	{ C2T1FS_MNTOPT_UNIT_SIZE,       "unit_size=%s" },
	/* match_token() requires last element must have NULL pattern */
	{ C2T1FS_MNTOPT_ERR,              NULL },
};

static void c2t1fs_mnt_opts_init(struct c2t1fs_mnt_opts *mntopts)
{
	START();
	C2_ASSERT(mntopts != NULL);

	C2_SET0(mntopts);

	END(0);
}

static void c2t1fs_mnt_opts_fini(struct c2t1fs_mnt_opts *mntopts)
{
	int i;

	START();
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
	if (mntopts->mo_options != NULL)
		kfree(mntopts->mo_options);

	END(0);
}

static int c2t1fs_mnt_opts_validate(struct c2t1fs_mnt_opts *mnt_opts)
{
	START();

	if (mnt_opts->mo_nr_ios_ep <= 0) {
		TRACE("ERROR: Must specify at least one io-service endpoint\n");
		goto invalid;
	}

	if ((mnt_opts->mo_unit_size & (PAGE_SIZE - 1)) != 0) {
		TRACE("ERROR: Stripe unit size must be page aligned\n");
		goto invalid;
	}

	if (mnt_opts->mo_nr_ios_ep > MAX_NR_EP_PER_SERVICE_TYPE ||
	    mnt_opts->mo_nr_mds_ep > MAX_NR_EP_PER_SERVICE_TYPE) {
		TRACE("ERROR: number of endpoints must be less than %d\n",
				MAX_NR_EP_PER_SERVICE_TYPE);
		goto invalid;
	}

	END(0);
	return 0;

invalid:
	END(-EINVAL);
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

	START();

	TRACE("options: %p\n", options);

	if (options == NULL) {
		rc = -EINVAL;
		goto out;
	}

	mnt_opts->mo_options = kstrdup(options, GFP_KERNEL);
	if (mnt_opts->mo_options == NULL)
		rc = -ENOMEM;

	while ((op = strsep(&options, ",")) != NULL) {
		TRACE("Processing \"%s\"\n", op);
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
			TRACE("ioservice: %s\n", value);
			mnt_opts->mo_ios_ep_addr[mnt_opts->mo_nr_ios_ep++] =
						value;
			break;

		case C2T1FS_MNTOPT_MDS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			TRACE("mdservice: %s\n", value);
			mnt_opts->mo_mds_ep_addr[mnt_opts->mo_nr_mds_ep++] =
						value;
			break;

		case C2T1FS_MNTOPT_MGS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			TRACE("mgservice: %s\n", value);
			mnt_opts->mo_mgs_ep_addr = value;
			break;

		case C2T1FS_MNTOPT_PROFILE:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			TRACE("profile: %s\n", value);
			mnt_opts->mo_profile = value;
			break;

		case C2T1FS_MNTOPT_NR_CONTAINERS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			rc = strict_strtoul(value, 10, &nr);
			kfree(value);
			if (rc != 0)
				goto out;
			TRACE("nr_containers = %lu\n", nr);
			mnt_opts->mo_nr_containers = nr;
			break;

		case C2T1FS_MNTOPT_NR_DATA_UNITS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			rc = strict_strtoul(value, 10, &nr);
			kfree(value);
			if (rc != 0)
				goto out;
			TRACE("nr_data_units = %lu\n", nr);
			mnt_opts->mo_nr_data_units = nr;
			break;

		case C2T1FS_MNTOPT_NR_PARITY_UNITS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			rc = strict_strtoul(value, 10, &nr);
			kfree(value);
			if (rc != 0)
				goto out;
			TRACE("nr_parity_units = %lu\n", nr);
			mnt_opts->mo_nr_parity_units = nr;
			break;

		case C2T1FS_MNTOPT_UNIT_SIZE:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			rc = strict_strtoul(value, 10, &nr);
			kfree(value);
			if (rc != 0)
				goto out;
			TRACE("unit_size = %lu\n", nr);
			mnt_opts->mo_unit_size = nr;
			break;

		default:
			TRACE("Unrecognized option: %s\n", op);
			TRACE("Supported options: mgs,mds,ios,profile,"
			      "nr_containers,nr_data_units,nr_parity_units,"
			      "unit_size\n");
			rc = -EINVAL;
			goto out;
		}
	}
	rc = c2t1fs_mnt_opts_validate(mnt_opts);

out:
	/* if rc != 0, mnt_opts will be finalised from c2t1fs_sb_fini() */
	END(rc);
	return rc;
}
void c2t1fs_service_context_init(struct c2t1fs_service_context *ctx,
				 struct c2t1fs_sb              *csb,
				 enum c2t1fs_service_type       type,
				 char                          *ep_addr)
{
	START();

	C2_SET0(ctx);
	ctx->sc_csb = csb;
	ctx->sc_type = type;
	ctx->sc_addr = ep_addr;
	ctx->sc_magic = MAGIC_SVC_CTX;
	c2_tlink_init(&svc_ctx_tl_descr, ctx);

	END(0);
}

void c2t1fs_service_context_fini(struct c2t1fs_service_context *ctx)
{
	START();

	c2_tlink_fini(&svc_ctx_tl_descr, ctx);
	ctx->sc_magic = 0;

	END(0);
}

static int c2t1fs_config_fetch(struct c2t1fs_sb *csb)
{
	START();

	/* XXX fetch configuration here */

	END(0);
	return 0;
}

static int c2t1fs_connect_to_all_services(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;
	int                            rc;

	START();

	rc = c2t1fs_populate_service_contexts(csb);
	if (rc != 0)
		goto out;

	c2_tlist_for(&svc_ctx_tl_descr, &csb->csb_service_contexts, ctx) {

		rc = c2t1fs_connect_to_service(ctx);
		if (rc != 0) {
			c2t1fs_disconnect_from_all_services(csb);
			goto out;
		}
	} c2_tlist_endfor;
out:
	END(rc);
	return rc;
}
static int c2t1fs_populate_service_contexts(struct c2t1fs_sb *csb)
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

		TRACE("n = %d type = %d\n", n, type);

		for (i = 0; i < n; i++) {
			ep_addr = ep_arr[i];
			TRACE("i = %d ep_addr = %s\n", i, ep_addr);

			C2_ALLOC_PTR(ctx);
			if (ctx == NULL)
				return -ENOMEM;

			c2t1fs_service_context_init(ctx, csb, type, ep_addr);
			c2_tlist_add_tail(&svc_ctx_tl_descr,
					&csb->csb_service_contexts, ctx);
		}
		return 0;
	}
	START();

	mntopts = &csb->csb_mnt_opts;

	rc = populate(mntopts->mo_mds_ep_addr, mntopts->mo_nr_mds_ep,
				C2T1FS_ST_MDS);
	if (rc != 0)
		goto discard_all;

	rc = populate(mntopts->mo_ios_ep_addr, mntopts->mo_nr_ios_ep,
				C2T1FS_ST_IOS);
	if (rc != 0)
		goto discard_all;

	END(0);
	return 0;

discard_all:
	c2t1fs_discard_service_contexts(csb);
	END(rc);
	return rc;
}

static void c2t1fs_discard_service_contexts(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;

	START();

	c2_tlist_for(&svc_ctx_tl_descr, &csb->csb_service_contexts, ctx) {

		c2_tlist_del(&svc_ctx_tl_descr, ctx);

		TRACE("discard: %s\n", ctx->sc_addr);

		c2t1fs_service_context_fini(ctx);
		c2_free(ctx);

	} c2_tlist_endfor;

	END(0);
}

static int c2t1fs_connect_to_service(struct c2t1fs_service_context *ctx)
{
	struct c2_rpcmachine      *rpc_mach;
	struct c2_net_transfer_mc *tm;
	struct c2_net_end_point   *ep;
	struct c2_rpc_conn        *conn;
	struct c2_rpc_session     *session;
	int                        rc;

	START();

	rpc_mach = &c2t1fs_globals.g_rpcmachine;
	tm       = &rpc_mach->cr_tm;

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
	TRACE("Connected to [%s] active_ctx %d\n", ctx->sc_addr,
				ctx->sc_csb->csb_nr_active_contexts);
	END(rc);
	return rc;

conn_term:
	(void)c2_rpc_conn_terminate_sync(conn, C2T1FS_RPC_TIMEOUT);
	c2_rpc_conn_fini(conn);
out:
	END(rc);
	return rc;
}

static void c2t1fs_disconnect_from_service(struct c2t1fs_service_context *ctx)
{
	struct c2_rpc_conn    *conn;
	struct c2_rpc_session *session;

	START();

	conn    = &ctx->sc_conn;
	session = &ctx->sc_session;

	if (session->s_state == C2_RPC_SESSION_IDLE ||
	    session->s_state == C2_RPC_SESSION_BUSY)
		(void)c2_rpc_session_terminate_sync(session,
						    C2T1FS_RPC_TIMEOUT);

	if (conn->c_state == C2_RPC_CONN_ACTIVE)
		(void)c2_rpc_conn_terminate_sync(conn, C2T1FS_RPC_TIMEOUT);

	c2_rpc_session_fini(session);
	c2_rpc_conn_fini(conn);

	ctx->sc_csb->csb_nr_active_contexts--;
	TRACE("Disconnected from [%s] active_ctx %d\n", ctx->sc_addr,
				ctx->sc_csb->csb_nr_active_contexts);
	END(0);
}

static void c2t1fs_disconnect_from_all_services(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;

	START();

	c2_tlist_for(&svc_ctx_tl_descr, &csb->csb_service_contexts, ctx) {

		c2t1fs_disconnect_from_service(ctx);

		if (csb->csb_nr_active_contexts == 0)
			break;
	} c2_tlist_endfor;

	END(0);
}
int
c2t1fs_container_location_map_init(struct c2t1fs_container_location_map *map,
				   int nr_containers)
{
	START();

	C2_SET0(map);

	END(0);
	return 0;
}
void
c2t1fs_container_location_map_fini(struct c2t1fs_container_location_map *map)
{
	START();
	END(0);
}

int c2t1fs_container_location_map_build(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context        *ctx;
	struct c2t1fs_container_location_map *map;
	int                                   nr_cont_per_svc;
	int                                   nr_containers;
	int                                   nr_ios;
	int                                   rc = 0;
	int                                   i;
	int                                   cur;

	START();

	nr_ios = csb->csb_mnt_opts.mo_nr_ios_ep;
	if (nr_ios == 0) {
		/*
		 * XXX We should return failure at this point. Because there
		 * is no active session with any ioservice.
		 * But for now, for testing purpose it's okay.
		 */
		TRACE("No io-service\n");
		rc = 0;
		goto out;
	}

	nr_containers = csb->csb_nr_containers;
	nr_cont_per_svc = nr_containers / nr_ios;
	if (nr_containers % nr_ios != 0)
		nr_cont_per_svc++;

	TRACE("nr_cont_per_svc = %d\n", nr_cont_per_svc);

	map = &csb->csb_cl_map;
	cur = 1;

	c2_tlist_for(&svc_ctx_tl_descr, &csb->csb_service_contexts, ctx) {

		switch (ctx->sc_type) {

		case C2T1FS_ST_MDS:
			/* Currently assuming only one MGS, which will serve
			   container 0 */
			map->clm_map[0] = ctx;
			TRACE("container_id [0] at %s\n", ctx->sc_addr);
			continue;

		case C2T1FS_ST_IOS:
			for (i = 0; i < nr_cont_per_svc &&
				    cur <= nr_containers; i++, cur++) {
				map->clm_map[cur] = ctx;
				TRACE("container_id [%d] at %s\n", cur,
						ctx->sc_addr);
			}
			break;
		case C2T1FS_ST_MGS:
			break;
		default:
			C2_ASSERT(0);
		}
	} c2_tlist_endfor;
out:
	END(rc);
	return rc;
}

struct c2_rpc_session *
c2t1fs_container_id_to_session(struct c2t1fs_sb *csb,
			       uint64_t          container_id)
{
	struct c2t1fs_container_location_map *map;
	struct c2t1fs_service_context        *ctx;

	START();

	C2_ASSERT(container_id <= csb->csb_nr_containers);

	map = &csb->csb_cl_map;
	ctx = map->clm_map[container_id];
	C2_ASSERT(ctx != NULL);

	END(&ctx->sc_session);
	return &ctx->sc_session;
}
void c2t1fs_fs_lock(struct c2t1fs_sb *csb)
{
	START();
	c2_mutex_lock(&csb->csb_mutex);
	END(0);
}

void c2t1fs_fs_unlock(struct c2t1fs_sb *csb)
{
	START();
	c2_mutex_unlock(&csb->csb_mutex);
	END(0);
}

