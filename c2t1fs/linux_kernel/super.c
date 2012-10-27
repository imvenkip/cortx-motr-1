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
#include <linux/parser.h>     /* substring_t                    */
#include <linux/slab.h>       /* kmalloc(), kfree()             */

#include "lib/misc.h"         /* C2_SET0()                      */
#include "lib/memory.h"       /* C2_ALLOC_PTR(), c2_free()      */
#include "c2t1fs/linux_kernel/c2t1fs.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"        /* C2_LOG and C2_ENTRY            */
#include "layout/linear_enum.h"
#include "layout/pdclust.h"
#include "colibri/magic.h"

static int c2t1fs_sb_layout_init(struct c2t1fs_sb *csb);

static int
c2t1fs_build_cob_id_enum(const uint32_t          pool_width,
			 struct c2_layout_enum **lay_enum);

static int
c2t1fs_build_layout(const uint64_t         layout_id,
		    const uint32_t         N,
		    const uint32_t         K,
		    const uint32_t         pool_width,
		    const uint64_t         unit_size,
		    struct c2_layout_enum *le,
		    struct c2_layout     **layout);

static void c2t1fs_sb_layout_fini(struct c2t1fs_sb *csb);
/* Super block */
static int  c2t1fs_fill_super(struct super_block *sb, void *data, int silent);
static int  c2t1fs_sb_init(struct c2t1fs_sb *csb);
static void c2t1fs_sb_fini(struct c2t1fs_sb *csb);

extern void io_bob_tlists_init(void);
extern const struct c2_addb_ctx_type c2t1fs_addb_type;
extern struct c2_addb_ctx c2t1fs_addb;

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

/* Others */

static void c2t1fs_destroy_all_dir_ents(struct super_block *sb);

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
		   C2_T1FS_SVC_CTX_MAGIC, C2_T1FS_SVC_CTX_HEAD_MAGIC);

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

void ast_thread(struct c2t1fs_sb *csb)
{
	while (1) {
		c2_chan_wait(&csb->csb_iogroup.s_clink);
		c2_sm_group_lock(&csb->csb_iogroup);
		c2_sm_asts_run(&csb->csb_iogroup);
		c2_sm_group_unlock(&csb->csb_iogroup);
		if (!csb->csb_active && c2_atomic64_get(&csb->csb_pending_io_nr)
				== 0) {
			c2_chan_signal(&csb->csb_iowait);
			break;
		}
	}
}

static int c2t1fs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct c2t1fs_mnt_opts *mntopts;
	struct c2t1fs_sb       *csb;
	struct inode           *root_inode;
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

	rc = c2t1fs_config_fetch(csb);
	if (rc != 0)
		goto out_fini;

	rc = c2t1fs_connect_to_all_services(csb);
	if (rc != 0)
		goto out_fini;

	rc = c2t1fs_sb_layout_init(csb);
	if (rc != 0)
		goto disconnect_all;

	rc = c2t1fs_container_location_map_init(&csb->csb_cl_map,
						csb->csb_nr_containers);
	if (rc != 0)
		goto layout_fini;

	rc = c2t1fs_container_location_map_build(csb);
	if (rc != 0)
		goto out_map_fini;

	sb->s_fs_info        = csb;
	sb->s_blocksize      = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic          = C2_T1FS_SUPER_MAGIC;
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

        io_bob_tlists_init();

	rc = C2_THREAD_INIT(&csb->csb_astthread, struct c2t1fs_sb *, NULL,
			    &ast_thread, csb, "ast_thread");
	C2_ASSERT(rc == 0);

	C2_LEAVE("rc: %d", rc);
	return 0;

out_map_fini:
	c2t1fs_container_location_map_fini(&csb->csb_cl_map);

layout_fini:
	c2t1fs_sb_layout_fini(csb);

disconnect_all:
	c2t1fs_disconnect_from_all_services(csb);

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

static int c2t1fs_sb_layout_init(struct c2t1fs_sb *csb)
{
	struct c2t1fs_mnt_opts *mntopts = &csb->csb_mnt_opts;
	struct c2_layout_enum  *layout_enum;
	uint32_t                pool_width;
	uint32_t                nr_data_units;
	uint32_t                nr_parity_units;
	uint32_t                unit_size;
	int                     rc;

	C2_ENTRY();
	nr_data_units   = mntopts->mo_nr_data_units ?:
				C2T1FS_DEFAULT_NR_DATA_UNITS;
	nr_parity_units = mntopts->mo_nr_parity_units ?:
				C2T1FS_DEFAULT_NR_PARITY_UNITS;
	unit_size       = mntopts->mo_unit_size ?:
				C2T1FS_DEFAULT_STRIPE_UNIT_SIZE;
	pool_width = mntopts->mo_pool_width ?:
			nr_data_units + 2 * nr_parity_units;

	/* See "Containers and component objects" section in c2t1fs.h for more
	   information on following line */
	csb->csb_nr_containers = pool_width + 1;
	csb->csb_pool_width    = pool_width;

	C2_LOG(C2_INFO, "P = %d, N = %d, K = %d unit_size %d",
			pool_width, nr_data_units, nr_parity_units, unit_size);

	/* P >= N + 2 * K ??*/
	if (pool_width < nr_data_units + 2 * nr_parity_units ||
	    csb->csb_nr_containers > C2T1FS_MAX_NR_CONTAINERS) {
		C2_LEAVE("rc: -EINVAL");
		return -EINVAL;
	}

	rc = c2t1fs_build_cob_id_enum(pool_width, &layout_enum);
	if (rc == 0) {
		uint64_t random;

		random = c2_time_nanoseconds(c2_time_now());
		csb->csb_layout_id = c2_rnd(~0ULL >> 16, &random);

		rc = c2t1fs_build_layout(csb->csb_layout_id, nr_data_units,
					 nr_parity_units, pool_width, unit_size,
					 layout_enum, &csb->csb_file_layout);
		if (rc != 0)
			c2_layout_enum_fini(layout_enum);
	}

	C2_POST(equi(rc == 0, csb->csb_file_layout != NULL &&
		     csb->csb_file_layout->l_ref > 0));
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int
c2t1fs_build_cob_id_enum(const uint32_t          pool_width,
			 struct c2_layout_enum **lay_enum)
{
	struct c2_layout_linear_attr  lin_attr;
	struct c2_layout_linear_enum *lle;
	int                           rc;

	C2_ENTRY();
	C2_PRE(pool_width > 0 && lay_enum != NULL);
	/*
	 * cob_fid = fid { B * idx + A, gob_fid.key }
	 * where idx is in [0, pool_width)
	 */
	lin_attr = (struct c2_layout_linear_attr) {
		.lla_nr = pool_width,
		.lla_A  = 1,
		.lla_B  = 1
	};

	*lay_enum = NULL;
	rc = c2_linear_enum_build(&c2t1fs_globals.g_layout_dom,
				  &lin_attr, &lle);
	if (rc == 0)
		*lay_enum = &lle->lle_base;

	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int
c2t1fs_build_layout(const uint64_t          layout_id,
		    const uint32_t          N,
		    const uint32_t          K,
		    const uint32_t          pool_width,
		    const uint64_t          unit_size,
		    struct c2_layout_enum  *le,
		    struct c2_layout      **layout)
{
	struct c2_pdclust_attr    pl_attr;
	struct c2_pdclust_layout *pdlayout;
	int                       rc;

	C2_ENTRY();
	C2_PRE(pool_width > 0);
	C2_PRE(le != NULL && layout != NULL);

	pl_attr = (struct c2_pdclust_attr) {
		.pa_N         = N,
		.pa_K         = K,
		.pa_P         = pool_width,
		.pa_unit_size = unit_size,
	};
	c2_uint128_init(&pl_attr.pa_seed, "upjumpandpumpim,");

	*layout = NULL;
	rc = c2_pdclust_build(&c2t1fs_globals.g_layout_dom,
			      layout_id, &pl_attr, le,
			      &pdlayout);
	if (rc == 0)
		*layout = c2_pdl_to_layout(pdlayout);

	C2_LEAVE("rc: %d", rc);
	return rc;
}

/**
   Implementation of file_system_type::kill_sb() interface.
 */
void c2t1fs_kill_sb(struct super_block *sb)
{
	struct c2t1fs_sb *csb;
        struct c2_clink   iowait;

	C2_ENTRY();

	csb = C2T1FS_SB(sb);
	C2_LOG(C2_DEBUG, "csb = %p", csb);
	/*
	 * If c2t1fs_fill_super() fails then deactivate_locked_super() calls
	 * c2t1fs_fs_type->kill_sb(). In that case, csb == NULL.
	 * But still not sure, such csb != NULL handling is a good idea.
	 */
	if (csb != NULL) {
		c2t1fs_destroy_all_dir_ents(sb);
		c2t1fs_sb_layout_fini(csb);
		csb->csb_active = false;
		c2_clink_init(&iowait, NULL);
		c2_clink_add(&csb->csb_iowait, &iowait);
		c2_chan_signal(&csb->csb_iogroup.s_chan);
                c2_chan_wait(&iowait);
		c2_thread_join(&csb->csb_astthread);
		c2_clink_del(&iowait);
		c2_clink_fini(&iowait);
		c2_chan_fini(&csb->csb_iowait);
		c2t1fs_container_location_map_fini(&csb->csb_cl_map);
		c2t1fs_disconnect_from_all_services(csb);
		c2t1fs_service_contexts_discard(csb);
		c2t1fs_sb_fini(csb);
		c2_free(csb);
	}
	kill_anon_super(sb);

	C2_LEAVE();
}

static void c2t1fs_destroy_all_dir_ents(struct super_block *sb)
{
	struct c2t1fs_dir_ent *de;
	struct c2t1fs_inode   *root_inode;
	struct inode          *inode;
	C2_ENTRY();

	if (sb->s_root == NULL) {
		C2_LEAVE();
		return;
	}

	inode = sb->s_root->d_inode;
	C2_ASSERT(inode != NULL && c2t1fs_inode_is_root(inode));

	root_inode = C2T1FS_I(inode);
	c2_tl_for(dir_ents, &root_inode->ci_dir_ents, de) {
		c2t1fs_dir_ent_remove(de);
		/* c2t1fs_dir_ent_remove has freed de */
	} c2_tl_endfor;

	C2_LEAVE();
}

static void c2t1fs_sb_layout_fini(struct c2t1fs_sb *csb)
{
	C2_ENTRY();

	if (csb->csb_file_layout != NULL)
		c2_layout_put(csb->csb_file_layout);
	csb->csb_file_layout = NULL;

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
        c2_sm_group_init(&csb->csb_iogroup);
        c2_addb_ctx_init(&c2t1fs_addb, &c2t1fs_addb_type, &c2_addb_global_ctx);
	csb->csb_active = true;
	c2_chan_init(&csb->csb_iowait);
	c2_atomic64_set(&csb->csb_pending_io_nr, 0);

	C2_LEAVE("rc: 0");
	return 0;
}

static void c2t1fs_sb_fini(struct c2t1fs_sb *csb)
{
	C2_ENTRY();

	C2_ASSERT(csb != NULL);

	c2_sm_group_fini(&csb->csb_iogroup);
	svc_ctx_tlist_fini(&csb->csb_service_contexts);
	c2_mutex_fini(&csb->csb_mutex);
	c2t1fs_mnt_opts_fini(&csb->csb_mnt_opts);
	csb->csb_next_key = 0;
        c2_addb_ctx_fini(&c2t1fs_addb);

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
		C2_LOG(C2_ERROR, "ERROR:"
			 "Must specify at least one io-service endpoint");
		goto invalid;
	}

	/*
	 * Need to test, with unit size that is not multiple of page size.
	 * Until then don't allow.
	 */
	if ((mnt_opts->mo_unit_size & (PAGE_CACHE_SIZE - 1)) != 0) {
		C2_LOG(C2_ERROR, "ERROR:"
			 " Unit size must be multiple of PAGE_CACHE_SIZE");
		goto invalid;
	}

	/*
	 * For simplicity, end point addresses are kept in statically allocated
	 * array. Hence size of the array is limit on number of end-point
	 * addresses.
	 */
	if (mnt_opts->mo_nr_ios_ep > MAX_NR_EP_PER_SERVICE_TYPE ||
	    mnt_opts->mo_nr_mds_ep > MAX_NR_EP_PER_SERVICE_TYPE) {
		C2_LOG(C2_ERROR, "ERROR:"
				" number of endpoints must be less than %d",
				MAX_NR_EP_PER_SERVICE_TYPE);
		goto invalid;
	}

	/*
	 * In parity groups, parity is calculated using 2 approaches.
	 * - read old
	 * - read rest.
	 * In read old approach, parity is calculated using differential parity
	 * between old and new version of data along with old version of
	 * parity block. This needs support from parity math component to
	 * calculate differential parity.
	 * At the moment, only XOR has such support.
	 * Parity math component choses the algorithm for parity calculation
	 * based on number of parity units. If K == 1, XOR is chosen, otherwise
	 * Reed-Solomon is chosen.
	 * Since Reed-Solomon does not support differential parity calculation
	 * at the moment, number of parity units are restricted to 1 for now.
	 */
	if (mnt_opts->mo_nr_parity_units > 1) {
		C2_LOG(C2_ERROR, "ERROR:"
				"Number of parity units must be 1");
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

	C2_LOG(C2_INFO, "options: %p", options);

	if (options == NULL) {
		rc = -EINVAL;
		goto out;
	}

	mnt_opts->mo_options = kstrdup(options, GFP_KERNEL);
	if (mnt_opts->mo_options == NULL)
		rc = -ENOMEM;

	while ((op = strsep(&options, ",")) != NULL) {
		C2_LOG(C2_INFO, "Processing \"%s\"", op);

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
			C2_LOG(C2_INFO, "ioservice: %s", value);
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
			C2_LOG(C2_INFO, "mdservice: %s", value);
			mnt_opts->mo_mds_ep_addr[mnt_opts->mo_nr_mds_ep++] =
						value;
			break;

		case C2T1FS_MNTOPT_MGS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			C2_LOG(C2_INFO, "mgservice: %s", value);
			mnt_opts->mo_mgs_ep_addr = value;
			break;

		case C2T1FS_MNTOPT_PROFILE:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			C2_LOG(C2_INFO, "profile: %s", value);
			mnt_opts->mo_profile = value;
			break;

		case C2T1FS_MNTOPT_POOL_WIDTH:
			rc = process_numeric_option(args, &nr);
			if (rc != 0)
				goto out;
			C2_LOG(C2_INFO, "pool_width = %lu", nr);
			mnt_opts->mo_pool_width = nr;
			break;

		case C2T1FS_MNTOPT_NR_DATA_UNITS:
			rc = process_numeric_option(args, &nr);
			if (rc != 0)
				goto out;
			C2_LOG(C2_INFO, "nr_data_units = %lu", nr);
			mnt_opts->mo_nr_data_units = nr;
			break;

		case C2T1FS_MNTOPT_NR_PARITY_UNITS:
			rc = process_numeric_option(args, &nr);
			if (rc != 0)
				goto out;
			C2_LOG(C2_INFO, "nr_parity_units = %lu", nr);
			mnt_opts->mo_nr_parity_units = nr;
			break;

		case C2T1FS_MNTOPT_UNIT_SIZE:
			rc = process_numeric_option(args, &nr);
			if (rc != 0)
				goto out;
			C2_LOG(C2_INFO, "unit_size = %lu", nr);
			mnt_opts->mo_unit_size = nr;
			break;

		default:
			C2_LOG(C2_ERROR, "Unrecognized option: %s", op);
			C2_LOG(C2_ERROR, "Supported options: mgs,mds,ios,"
			      "profile,pool_width,nr_data_units,"
			      "nr_parity_units,unit_size");
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
	ctx->sc_magic = C2_T1FS_SVC_CTX_MAGIC;

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

	c2_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {

		rc = c2t1fs_connect_to_service(ctx);
		if (rc != 0) {
			c2t1fs_disconnect_from_all_services(csb);
			goto out;
		}
	} c2_tl_endfor;
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

		C2_LOG(C2_DEBUG, "n = %d type = %d", n, type);

		for (i = 0; i < n; i++) {
			ep_addr = ep_arr[i];
			C2_LOG(C2_DEBUG, "i = %d ep_addr = %s", i, ep_addr);

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

	c2_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {

		svc_ctx_tlist_del(ctx);
		C2_LOG(C2_DEBUG, "discard: %s", ctx->sc_addr);

		c2t1fs_service_context_fini(ctx);
		c2_free(ctx);

	} c2_tl_endfor;

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
	tm       = &rpc_mach->rm_tm;

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
	C2_LOG(C2_INFO, "Connected to [%s] active_ctx %d", ctx->sc_addr,
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

	/* session_fini() before conn_terminate is necessary, to detach
	   session from connection */
	c2_rpc_session_fini(&ctx->sc_session);

	(void)c2_rpc_conn_terminate_sync(&ctx->sc_conn, C2T1FS_RPC_TIMEOUT);

	c2_rpc_conn_fini(&ctx->sc_conn);

	ctx->sc_csb->csb_nr_active_contexts--;
	C2_LOG(C2_INFO, "Disconnected from [%s] active_ctx %d", ctx->sc_addr,
				ctx->sc_csb->csb_nr_active_contexts);
	C2_LEAVE();
}

static void c2t1fs_disconnect_from_all_services(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;

	C2_ENTRY();

	c2_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {

		c2t1fs_disconnect_from_service(ctx);

		if (csb->csb_nr_active_contexts == 0)
			break;

	} c2_tl_endfor;

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

	C2_LOG(C2_DEBUG, "nr_cont_per_svc = %d", nr_cont_per_svc);

	map = &csb->csb_cl_map;
	cur = 1;

	c2_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {

		switch (ctx->sc_type) {

		case C2T1FS_ST_MDS:
			/* Currently assuming only one MGS, which will serve
			   container 0 */
			map->clm_map[0] = ctx;
			C2_LOG(C2_DEBUG, "container_id [0] at %s", ctx->sc_addr);
			break;

		case C2T1FS_ST_IOS:
			for (i = 0; i < nr_cont_per_svc &&
				    cur <= nr_data_containers; i++, cur++) {
				map->clm_map[cur] = ctx;
				C2_LOG(C2_DEBUG, "container_id [%d] at %s", cur,
						ctx->sc_addr);
			}
			break;

		case C2T1FS_ST_MGS:
			break;

		default:
			C2_ASSERT(0);
		}

	} c2_tl_endfor;

	C2_LEAVE("rc: 0");
	return 0;
}

struct c2_rpc_session *
c2t1fs_container_id_to_session(const struct c2t1fs_sb *csb,
			       uint64_t                container_id)
{
	struct c2t1fs_service_context        *ctx;

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
