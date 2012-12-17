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

static int m0t1fs_sb_layout_init(struct m0t1fs_sb *csb);

static int
m0t1fs_build_cob_id_enum(const uint32_t          pool_width,
			 struct m0_layout_enum **lay_enum);

static int
m0t1fs_build_layout(const uint64_t         layout_id,
		    const uint32_t         N,
		    const uint32_t         K,
		    const uint32_t         pool_width,
		    const uint64_t         unit_size,
		    struct m0_layout_enum *le,
		    struct m0_layout     **layout);

static void m0t1fs_sb_layout_fini(struct m0t1fs_sb *csb);
/* Super block */
static int  m0t1fs_fill_super(struct super_block *sb, void *data, int silent);
static int  m0t1fs_sb_init(struct m0t1fs_sb *csb);
static void m0t1fs_sb_fini(struct m0t1fs_sb *csb);
static int m0t1fs_statfs(struct dentry *dentry, struct kstatfs *buf);

M0_INTERNAL void io_bob_tlists_init(void);
extern const struct m0_addb_ctx_type m0t1fs_addb_type;
extern struct m0_addb_ctx m0t1fs_addb;

/* Mount options */

static void m0t1fs_mnt_opts_init(struct m0t1fs_mnt_opts *mntopts);
static void m0t1fs_mnt_opts_fini(struct m0t1fs_mnt_opts *mntopts);
static int  m0t1fs_mnt_opts_validate(const struct m0t1fs_mnt_opts *mnt_opts);
static int  m0t1fs_mnt_opts_parse(char *options, struct m0t1fs_mnt_opts *mops);

/* service contexts */

static void m0t1fs_service_context_init(struct m0t1fs_service_context *ctx,
					struct m0t1fs_sb              *csb,
					enum m0t1fs_service_type       type,
					char                          *ep_addr);

static void m0t1fs_service_context_fini(struct m0t1fs_service_context *ctx);

static int  m0t1fs_service_contexts_populate(struct m0t1fs_sb *csb);
static void m0t1fs_service_contexts_delete(struct m0t1fs_sb *csb);

static int  m0t1fs_connect_to_services(struct m0t1fs_sb *csb);
static void m0t1fs_disconnect_from_services(struct m0t1fs_sb *csb);

static int  m0t1fs_connect_to_service(struct m0t1fs_service_context *ctx);
static void m0t1fs_disconnect_from_service(struct m0t1fs_service_context *ctx);

/* Container location map */

static int
m0t1fs_container_location_map_init(struct m0t1fs_container_location_map *map,
				   int nr_containers);

static void
m0t1fs_container_location_map_fini(struct m0t1fs_container_location_map *map);

static int m0t1fs_container_location_map_build(struct m0t1fs_sb *csb);

/* global instances */

static const struct super_operations m0t1fs_super_operations = {
	.statfs        = m0t1fs_statfs,
	.alloc_inode   = m0t1fs_alloc_inode,
	.destroy_inode = m0t1fs_destroy_inode,
	.drop_inode    = generic_delete_inode /* provided by linux kernel */
};

/**
   tlist descriptor for list of m0t1fs_service_context objects placed in
   m0t1fs_sb::csb_service_contexts list using sc_link.
 */
M0_TL_DESCR_DEFINE(svc_ctx, "Service contexts", static,
		   struct m0t1fs_service_context, sc_link, sc_magic,
		   M0_T1FS_SVC_CTX_MAGIC, M0_T1FS_SVC_CTX_HEAD_MAGIC);

M0_TL_DEFINE(svc_ctx, static, struct m0t1fs_service_context);

/**
   Implementation of file_system_type::get_sb() interface.
 */
M0_INTERNAL int m0t1fs_get_sb(struct file_system_type *fstype,
			      int flags,
			      const char *devname,
			      void *data, struct vfsmount *mnt)
{
	M0_ENTRY("flags: 0x%x, devname: %s, data: %s", flags, devname,
		 (char *)data);
	M0_RETURN(get_sb_nodev(fstype, flags, data, m0t1fs_fill_super, mnt));
}

/* Default timeout for waiting on sm_group:m0_clink if ast thread is idle. */
enum { AST_THREAD_TIMEOUT = 10 };

static void ast_thread(struct m0t1fs_sb *csb)
{
	while (1) {
		m0_chan_timedwait(&csb->csb_iogroup.s_clink,
				  m0_time_from_now(AST_THREAD_TIMEOUT, 0));
		m0_sm_group_lock(&csb->csb_iogroup);
		m0_sm_asts_run(&csb->csb_iogroup);
		m0_sm_group_unlock(&csb->csb_iogroup);
		if (!csb->csb_active &&
		    m0_atomic64_get(&csb->csb_pending_io_nr) == 0) {
			m0_chan_signal(&csb->csb_iowait);
			return;
		}
	}
}

static void ast_thread_stop(struct m0t1fs_sb *csb)
{
	struct m0_clink w;

	m0_clink_init(&w, NULL);
	m0_clink_add(&csb->csb_iowait, &w);

	csb->csb_active = false;
	m0_chan_signal(&csb->csb_iogroup.s_chan);
	m0_chan_wait(&w);
	m0_thread_join(&csb->csb_astthread);

	m0_clink_del(&w);
	m0_clink_fini(&w);
}

extern struct io_mem_stats iommstats;

static int m0t1fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct m0_fop_statfs_rep *rep = NULL;
	struct super_block *sb = dentry->d_sb;
	struct m0t1fs_sb   *csb = M0T1FS_SB(sb);
	int                 rc;

	m0t1fs_fs_lock(csb);
	rc = m0t1fs_mds_statfs(csb, &rep);
	if (rc != 0) {
		m0t1fs_fs_unlock(csb);
		return rc;
	}
	buf->f_type = rep->f_type;
	buf->f_bsize = rep->f_bsize;
	buf->f_blocks = rep->f_blocks;
	buf->f_bfree = buf->f_bavail = rep->f_bfree;
	buf->f_files = rep->f_files;
	buf->f_ffree = rep->f_ffree;
	buf->f_namelen = rep->f_namelen;
	m0t1fs_fs_unlock(csb);
	return 0;
}

static int fs_params_parse(struct m0t1fs_mnt_opts *dest, const char **src);

static int m0t1fs_root_alloc(struct super_block *sb)
{
	struct m0_fop_statfs_rep *rep = NULL;
	struct m0t1fs_sb         *csb = M0T1FS_SB(sb);
	struct inode             *root_inode;
	int                       rc;

	rc = m0t1fs_mds_statfs(csb, &rep);
	if (rc != 0)
		return rc;

	sb->s_magic = rep->f_type;
	csb->csb_namelen = rep->f_namelen;

	M0_LOG(M0_DEBUG, "Got mdservice root fid [%llx:%llx]",
	       rep->f_root.f_container, rep->f_root.f_key);

	root_inode = m0t1fs_root_iget(sb, &rep->f_root);
	if (IS_ERR(root_inode)) {
		rc = PTR_ERR(root_inode);
		M0_LOG(M0_ERROR, "m0t1fs_root_iget() failed with %d", rc);
		return rc;
	}

	sb->s_root = d_alloc_root(root_inode);
	if (sb->s_root == NULL) {
		iput(root_inode);
		rc = -ENOMEM;
		M0_LOG(M0_ERROR, "d_alloc_root() failed with %d", rc);
		return rc;
	}
	return 0;
}

/** Obtains filesystem parameters using confc API. */
static int fs_params_read(struct m0t1fs_mnt_opts *dest,
			  struct m0_sm_group     *sm_group,
			  const struct m0_buf    *profile,
			  const char             *confd_addr,
			  struct m0_rpc_machine  *rpc_mach,
			  const char             *local_conf)
{
	struct m0_confc     confc;
	struct m0_conf_obj *obj;
	int                 rc;

	M0_ENTRY();

	rc = m0_confc_init(&confc, sm_group, profile, confd_addr, rpc_mach,
			   local_conf);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_confc_open_sync(&obj, confc.cc_root,
				M0_BUF_INITS("filesystem"));
	if (rc == 0) {
		rc = fs_params_parse(
			dest, M0_CONF_CAST(obj, m0_conf_filesystem)->cf_params);
		m0_confc_close(obj);
	}

	m0_confc_fini(&confc);
	M0_RETURN(rc);
}

static int m0t1fs_poolmach_create(struct m0_poolmach **out, uint32_t pool_width,
				  uint32_t nr_parity_units)
{
	struct m0_poolmach *m;
	int                 rc;

	M0_ALLOC_PTR(m);
	if (m == NULL)
		return -ENOMEM;

	rc = m0_poolmach_init(m, NULL, 1, pool_width, 1, nr_parity_units);
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

static int m0t1fs_fill_super(struct super_block *sb, void *data,
			     int silent __attribute__((unused)))
{
	struct m0t1fs_mnt_opts *mops;
	struct m0t1fs_sb       *csb;
	int                     rc;

	M0_ENTRY();

	M0_ALLOC_PTR(csb);
	if (csb == NULL) {
		rc = -ENOMEM;
		goto end;
	}

	rc = m0t1fs_sb_init(csb);
	if (rc != 0)
		goto sb_free;

	mops = &csb->csb_mnt_opts;
	rc = m0t1fs_mnt_opts_parse(data, mops);
	if (rc != 0)
		goto sb_fini;

	rc = M0_THREAD_INIT(&csb->csb_astthread, struct m0t1fs_sb *, NULL,
			    &ast_thread, csb, "ast_thread");
	M0_ASSERT(rc == 0);

	rc = fs_params_read(mops, &csb->csb_iogroup,
			    &(const struct m0_buf)M0_BUF_INITS(
				    mops->mo_profile), mops->mo_confd,
			    &m0t1fs_globals.g_rpc_machine, mops->mo_local_conf);
	if (rc != 0)
		goto thread_fini;

	rc = m0t1fs_connect_to_services(csb);
	if (rc != 0)
		goto thread_fini;

	rc = m0_pool_init(&csb->csb_pool, mops->mo_pool_width);
	if (rc != 0)
		goto ioserver_fini;

	rc = m0t1fs_poolmach_create(&csb->csb_pool.po_mach, mops->mo_pool_width,
				    mops->mo_nr_parity_units);
	if (rc != 0)
		goto pool_fini;

	rc = m0t1fs_sb_layout_init(csb);
	if (rc != 0)
		goto poolmach_destroy;

	rc = m0t1fs_container_location_map_init(&csb->csb_cl_map,
						csb->csb_nr_containers);
	if (rc != 0)
		goto layout_fini;

	rc = m0t1fs_container_location_map_build(csb);
	if (rc != 0)
		goto map_fini;

	sb->s_fs_info        = csb;
	sb->s_blocksize      = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_maxbytes       = MAX_LFS_FILESIZE;
	sb->s_op             = &m0t1fs_super_operations;

	rc = m0t1fs_root_alloc(sb);
	if (rc != 0)
		goto map_fini;

	io_bob_tlists_init();
	M0_SET0(&iommstats);

	M0_RETURN(0);

map_fini:
	m0t1fs_container_location_map_fini(&csb->csb_cl_map);
layout_fini:
	m0t1fs_sb_layout_fini(csb);
poolmach_destroy:
	m0t1fs_poolmach_destroy(csb->csb_pool.po_mach);
pool_fini:
	m0_pool_fini(&csb->csb_pool);
ioserver_fini:
	m0t1fs_disconnect_from_services(csb);
	m0t1fs_service_contexts_delete(csb);
thread_fini:
	ast_thread_stop(csb);
sb_fini:
	m0t1fs_sb_fini(csb);
sb_free:
	m0_free(csb);
end:
	sb->s_fs_info = NULL;
	M0_ASSERT(rc != 0);
	M0_RETURN(rc);
}

static int m0t1fs_sb_layout_init(struct m0t1fs_sb *csb)
{
	struct m0_layout_enum  *layout_enum;
	int                     rc;
	struct m0t1fs_mnt_opts *mops = &csb->csb_mnt_opts;

	M0_ENTRY();

	M0_PRE(mops->mo_pool_width != 0);
	M0_PRE(mops->mo_nr_data_units != 0);
	M0_PRE(mops->mo_nr_parity_units != 0);
	M0_PRE(mops->mo_unit_size != 0);

	/* See "Containers and component objects" section in m0t1fs.h
	 * for more information on following line */
	csb->csb_nr_containers = mops->mo_pool_width + 1;
	csb->csb_pool_width    = mops->mo_pool_width;

	if (mops->mo_pool_width < mops->mo_nr_data_units +
	    2 * mops->mo_nr_parity_units ||
	    csb->csb_nr_containers > M0T1FS_MAX_NR_CONTAINERS)
		M0_RETURN(-EINVAL);

	rc = m0t1fs_build_cob_id_enum(mops->mo_pool_width, &layout_enum);
	if (rc == 0) {
		uint64_t random = m0_time_nanoseconds(m0_time_now());

		csb->csb_layout_id = m0_rnd(~0ULL >> 16, &random);
		rc = m0t1fs_build_layout(csb->csb_layout_id,
					 mops->mo_nr_data_units,
					 mops->mo_nr_parity_units,
					 mops->mo_pool_width,
					 mops->mo_unit_size, layout_enum,
					 &csb->csb_file_layout);
		if (rc != 0)
			m0_layout_enum_fini(layout_enum);
	}

	M0_POST(ergo(rc == 0, csb->csb_file_layout != NULL));
	M0_RETURN(rc);
}

static int
m0t1fs_build_cob_id_enum(const uint32_t          pool_width,
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
	lin_attr = (struct m0_layout_linear_attr) {
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
m0t1fs_build_layout(const uint64_t          layout_id,
		    const uint32_t          N,
		    const uint32_t          K,
		    const uint32_t          pool_width,
		    const uint64_t          unit_size,
		    struct m0_layout_enum  *le,
		    struct m0_layout      **layout)
{
	struct m0_pdclust_attr    pl_attr;
	struct m0_pdclust_layout *pdlayout;
	int                       rc;

	M0_ENTRY();
	M0_PRE(pool_width > 0);
	M0_PRE(le != NULL && layout != NULL);

	pl_attr = (struct m0_pdclust_attr) {
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

/**
   Implementation of file_system_type::kill_sb() interface.
 */
M0_INTERNAL void m0t1fs_kill_sb(struct super_block *sb)
{
	struct m0t1fs_sb *csb;

	M0_ENTRY();

	csb = M0T1FS_SB(sb);
	M0_LOG(M0_DEBUG, "csb = %p", csb);
	/*
	 * If m0t1fs_fill_super() fails then deactivate_locked_super() calls
	 * m0t1fs_fs_type->kill_sb(). In that case, csb == NULL.
	 * But still not sure, such csb != NULL handling is a good idea.
	 */
	if (csb != NULL) {
		m0t1fs_container_location_map_fini(&csb->csb_cl_map);
		m0t1fs_sb_layout_fini(csb);
		m0t1fs_poolmach_destroy(csb->csb_pool.po_mach);
		m0_pool_fini(&csb->csb_pool);
		m0t1fs_disconnect_from_services(csb);
		m0t1fs_service_contexts_delete(csb);
		ast_thread_stop(csb);
		m0t1fs_sb_fini(csb);
		m0_free(csb);
	}
	kill_anon_super(sb);

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

static void m0t1fs_sb_layout_fini(struct m0t1fs_sb *csb)
{
	M0_ENTRY();

	if (csb->csb_file_layout != NULL)
		m0_layout_put(csb->csb_file_layout);
	csb->csb_file_layout = NULL;

	M0_LEAVE();
}

static int m0t1fs_sb_init(struct m0t1fs_sb *csb)
{
	M0_ENTRY();

	M0_ASSERT(csb != NULL);

	M0_SET0(csb);

	m0_mutex_init(&csb->csb_mutex);
	m0t1fs_mnt_opts_init(&csb->csb_mnt_opts);
	svc_ctx_tlist_init(&csb->csb_service_contexts);
	m0_sm_group_init(&csb->csb_iogroup);
	m0_addb_ctx_init(&m0t1fs_addb, &m0t1fs_addb_type, &m0_addb_global_ctx);
	csb->csb_active = true;
	m0_chan_init(&csb->csb_iowait);
	m0_atomic64_set(&csb->csb_pending_io_nr, 0);

	M0_RETURN(0);
}

static void m0t1fs_sb_fini(struct m0t1fs_sb *csb)
{
	M0_ENTRY();
	M0_PRE(csb != NULL);

	m0_chan_fini(&csb->csb_iowait);
	m0_sm_group_fini(&csb->csb_iogroup);
	svc_ctx_tlist_fini(&csb->csb_service_contexts);
	m0_mutex_fini(&csb->csb_mutex);
	m0t1fs_mnt_opts_fini(&csb->csb_mnt_opts);
	csb->csb_next_key = 0;
	m0_addb_ctx_fini(&m0t1fs_addb);

	M0_LEAVE();
}

enum m0t1fs_mntopts {
	M0T1FS_MNTOPT_CONFD = 1,
	M0T1FS_MNTOPT_PROFILE,
	M0T1FS_MNTOPT_LOCAL_CONF,
	M0T1FS_MNTOPT_MDS,
	M0T1FS_MNTOPT_IOS,
	M0T1FS_MNTOPT_ERR,
};

static const match_table_t m0t1fs_mntopt_tokens = {
	{ M0T1FS_MNTOPT_CONFD,      "confd=%s" },
	{ M0T1FS_MNTOPT_PROFILE,    "profile=%s" },
	{ M0T1FS_MNTOPT_LOCAL_CONF, "local_conf=%s" },
	{ M0T1FS_MNTOPT_MDS,        "mds=%s" },
	{ M0T1FS_MNTOPT_IOS,        "ios=%s" },
	/* match_token() requires 2nd field of the last element to be NULL */
	{ M0T1FS_MNTOPT_ERR, NULL }
};

static void m0t1fs_mnt_opts_init(struct m0t1fs_mnt_opts *mntopts)
{
	M0_ENTRY();
	M0_PRE(mntopts != NULL);

	M0_SET0(mntopts);

	M0_LEAVE();
}

static void m0t1fs_mnt_opts_fini(struct m0t1fs_mnt_opts *mntopts)
{
	int i;

	M0_ENTRY();
	M0_PRE(mntopts != NULL);

	/* Here we use kfree() instead of m0_free() because the memory
	 * was allocated using match_strdup(). */

	for (i = 0; i < mntopts->mo_ios_ep_nr; i++) {
		M0_ASSERT(mntopts->mo_ios_ep_addr[i] != NULL);
		kfree(mntopts->mo_ios_ep_addr[i]);
	}
	for (i = 0; i < mntopts->mo_mds_ep_nr; i++) {
		M0_ASSERT(mntopts->mo_mds_ep_addr[i] != NULL);
		kfree(mntopts->mo_mds_ep_addr[i]);
	}
	if (mntopts->mo_confd != NULL)
		kfree(mntopts->mo_confd);
	if (mntopts->mo_profile != NULL)
		kfree(mntopts->mo_profile);
	if (mntopts->mo_local_conf != NULL)
		kfree(mntopts->mo_local_conf);

	M0_SET0(mntopts);
	M0_LEAVE();
}

static bool is_empty(const char *s)
{
	return s == NULL || *s == '\0';
}

static int m0t1fs_mnt_opts_validate(const struct m0t1fs_mnt_opts *mops)
{
	M0_ENTRY();

	if (is_empty(mops->mo_confd) && is_empty(mops->mo_local_conf))
		M0_RETERR(-EINVAL, "Configuration source is not specified");

	if (is_empty(mops->mo_profile))
		M0_RETERR(-EINVAL, "Mandatory parameter is missing: profile");

	if (mops->mo_mds_ep_nr == 0)
		M0_RETERR(-EINVAL, "No mdservice endpoints specified");
	if (mops->mo_ios_ep_nr == 0)
		M0_RETERR(-EINVAL, "No ioservice endpoints specified");

	/* m0t1fs_mnt_opts_parse() guarantees that this condition holds: */
	M0_ASSERT(mops->mo_mds_ep_nr <= ARRAY_SIZE(mops->mo_mds_ep_addr) &&
		  mops->mo_ios_ep_nr <= ARRAY_SIZE(mops->mo_ios_ep_addr));

	M0_RETURN(0);
}
static int str_parse(char **dest, const substring_t *src)
{
	*dest = match_strdup(src);
	return *dest == NULL ? -ENOMEM : 0;
}

static int num_parse(uint32_t *dest, const substring_t *src)
{
	int           rc;
	unsigned long n;
	char         *s = match_strdup(src);

	if (s == NULL)
		return -ENOMEM;

	rc = strict_strtoul(s, 10, &n);
	if (rc == 0)
		*dest = (uint32_t)n; /* XXX FIXME range checking is required */

	kfree(s);
	return rc;
}

static int fs_params_validate(struct m0t1fs_mnt_opts *mops)
{
	M0_ENTRY();

	M0_PRE(mops->mo_pool_width != 0);
	M0_PRE(mops->mo_nr_data_units != 0);
	M0_PRE(mops->mo_nr_parity_units != 0);
	M0_PRE(mops->mo_unit_size != 0);

	/* Need to test with unit size that is not multiple of page size.
	 * Until then --- don't allow. */
	if ((mops->mo_unit_size & (PAGE_CACHE_SIZE - 1)) != 0)
		M0_RETERR(-EINVAL,
			  "Unit size must be a multiple of PAGE_CACHE_SIZE");

	/*
	 * In parity groups, parity is calculated using "read old" and
	 * "read rest" approaches.
	 *
	 * Read old approach uses calculation of differential parity
	 * between old and new version of data along with old version
	 * of parity block.
	 *
	 * Calculation of differential parity needs support from
	 * parity math component. At the moment, only XOR has such
	 * support.
	 *
	 * Parity math component choses the algorithm for parity
	 * calculation, based on number of parity units. If K == 1,
	 * XOR is chosen, otherwise Reed-Solomon is chosen.
	 *
	 * Since Reed-Solomon does not support differential parity
	 * calculation at the moment, we restrict the number of parity
	 * units to 1.
	 */
	if (mops->mo_nr_parity_units > 1)
		M0_RETERR(-EINVAL, "Number of parity units must be 1");

	M0_RETURN(0);
}

static int fs_params_parse(struct m0t1fs_mnt_opts *dest, const char **src)
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

	dest->mo_pool_width = dest->mo_nr_data_units =
		dest->mo_nr_parity_units = dest->mo_unit_size = 0;

	if (src == NULL)
		goto end;

	for (rc = 0; rc == 0 && *src != NULL; ++src) {
		/* match_token() doesn't change the string pointed to
		 * by its first argument.  We don't want to remove
		 * `const' from m0_conf_filesystem::cf_params only
		 * because match_token()'s first argument is not
		 * const. We cast to const-less type instead. */
		switch (match_token((char *)*src, tbl, args)) {
		case POOL_WIDTH:
			rc = num_parse(&dest->mo_pool_width, args);
			break;
		case NR_DATA_UNITS:
			rc = num_parse(&dest->mo_nr_data_units, args);
			break;
		case NR_PAR_UNITS:
			rc = num_parse(&dest->mo_nr_parity_units, args);
			break;
		case UNIT_SIZE:
			rc = num_parse(&dest->mo_unit_size, args);
			break;
		default:
			rc = -EINVAL;
		}

		if (rc != 0)
			M0_RETERR(rc, "Invalid filesystem parameter: %s", *src);
	}
end:
	if (dest->mo_nr_data_units == 0)
		dest->mo_nr_data_units = M0T1FS_DEFAULT_NR_DATA_UNITS;

	if (dest->mo_nr_parity_units == 0)
		dest->mo_nr_parity_units = M0T1FS_DEFAULT_NR_PARITY_UNITS;

	if (dest->mo_unit_size == 0)
		dest->mo_unit_size = M0T1FS_DEFAULT_STRIPE_UNIT_SIZE;

	if (dest->mo_pool_width == 0)
		dest->mo_pool_width =
			dest->mo_nr_data_units + 2 * dest->mo_nr_parity_units;

	rc = fs_params_validate(dest);
	if (rc == 0)
		M0_LOG(M0_INFO, "pool_width (P) = %u, nr_data_units (N) = %u,"
		       " nr_parity_units (K) = %u, unit_size = %u",
		       dest->mo_pool_width, dest->mo_nr_data_units,
		       dest->mo_nr_parity_units, dest->mo_unit_size);
	M0_RETURN(rc);
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

static int m0t1fs_mnt_opts_parse(char *options, struct m0t1fs_mnt_opts *mops)
{
	substring_t args[MAX_OPT_ARGS];
	char       *op;
	char      **pstr;
	int         rc = 0;

	M0_ENTRY();

	if (options == NULL)
		M0_RETURN(-EINVAL);

	M0_LOG(M0_INFO, "Mount options: `%s'", options);

	while ((op = strsep(&options, ",")) != NULL && *op != '\0') {
		switch (match_token(op, m0t1fs_mntopt_tokens, args)) {
		case M0T1FS_MNTOPT_CONFD:
			rc = str_parse(&mops->mo_confd, args);
			if (rc != 0)
				goto out;
			M0_LOG(M0_INFO, "confd: %s", mops->mo_confd);
			break;

		case M0T1FS_MNTOPT_PROFILE:
			rc = str_parse(&mops->mo_profile, args);
			if (rc != 0)
				goto out;
			M0_LOG(M0_INFO, "profile: %s", mops->mo_profile);
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

			if (rc < 0) {
				M0_ASSERT(rc == -EPROTO);
				M0_RETERR(rc, "Configuration string is "
					  "too nested");
			}

			mops->mo_local_conf = kstrdup(start, GFP_KERNEL);
			if (mops->mo_local_conf == NULL)
				M0_RETURN(-ENOMEM);

			M0_LOG(M0_INFO, "local_conf: `%s'",
			       mops->mo_local_conf);
			break;
		}
		case M0T1FS_MNTOPT_MDS:
			if (mops->mo_mds_ep_nr ==
			    ARRAY_SIZE(mops->mo_mds_ep_addr))
				M0_RETERR(-EINVAL, "No more than %lu mds"
					  " addresses can be provided",
					  ARRAY_SIZE(mops->mo_mds_ep_addr));

			pstr = &mops->mo_mds_ep_addr[mops->mo_mds_ep_nr];
			rc = str_parse(pstr, args);
			if (rc != 0)
				goto out;
			++mops->mo_mds_ep_nr;
			M0_LOG(M0_INFO, "mdservice: %s", *pstr);
			break;

		case M0T1FS_MNTOPT_IOS:
			if (mops->mo_ios_ep_nr ==
			    ARRAY_SIZE(mops->mo_ios_ep_addr))
				M0_RETERR(-EINVAL, "No more than %lu ios"
					  " addresses can be provided",
					  ARRAY_SIZE(mops->mo_ios_ep_addr));

			pstr = &mops->mo_ios_ep_addr[mops->mo_ios_ep_nr];
			rc = str_parse(pstr, args);
			if (rc != 0)
				goto out;
			++mops->mo_ios_ep_nr;
			M0_LOG(M0_INFO, "ioservice: %s", *pstr);
			break;

		default:
			M0_RETERR(-EINVAL, "Unrecognized option: %s", op);
		}
	}
out:
	M0_RETURN(rc ?: m0t1fs_mnt_opts_validate(mops));
}

static void m0t1fs_service_context_init(struct m0t1fs_service_context *ctx,
					struct m0t1fs_sb              *csb,
					enum m0t1fs_service_type       type,
					char                          *ep_addr)
{
	M0_ENTRY();

	M0_SET0(ctx);

	ctx->sc_csb   = csb;
	ctx->sc_type  = type;
	ctx->sc_addr  = ep_addr;
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

static int m0t1fs_connect_to_services(struct m0t1fs_sb *csb)
{
	struct m0t1fs_service_context *ctx;
	int                            rc;

	M0_ENTRY();

	M0_PRE(svc_ctx_tlist_is_empty(&csb->csb_service_contexts));

	rc = m0t1fs_service_contexts_populate(csb);
	if (rc != 0)
		M0_RETURN(rc);

	m0_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {
		rc = m0t1fs_connect_to_service(ctx);
		if (rc != 0) {
			m0t1fs_disconnect_from_services(csb);
			break;
		}
	} m0_tl_endfor;

	if (rc != 0)
		m0t1fs_service_contexts_delete(csb);
	M0_RETURN(rc);
}

static int m0t1fs_service_contexts_populate(struct m0t1fs_sb *csb)
{
	struct m0t1fs_service_context *ctx;
	uint32_t                       i;
	int                            rc = 0;
	struct m0t1fs_mnt_opts        *mops = &csb->csb_mnt_opts;

	M0_ENTRY();

	for (i = 0; i < mops->mo_ios_ep_nr; i++) {
		M0_ALLOC_PTR(ctx);
		if (ctx == NULL) {
			rc = -ENOMEM;
			break;
		}

		/* XXX TODO Use confc data to initiate service contexts. */
		m0t1fs_service_context_init(ctx, csb, M0T1FS_ST_IOS,
					    mops->mo_ios_ep_addr[i]);
		svc_ctx_tlist_add_tail(&csb->csb_service_contexts, ctx);
	}

	for (i = 0; rc == 0 && i < mops->mo_mds_ep_nr; i++) {
		M0_ALLOC_PTR(ctx);
		if (ctx == NULL) {
			rc = -ENOMEM;
			break;
		}

		/* XXX TODO Use confc data to initiate service contexts. */
		m0t1fs_service_context_init(ctx, csb, M0T1FS_ST_MDS,
					    mops->mo_mds_ep_addr[i]);
		svc_ctx_tlist_add_tail(&csb->csb_service_contexts, ctx);
	}

	if (rc != 0)
		m0t1fs_service_contexts_delete(csb);
	M0_RETURN(rc);
}

static void m0t1fs_service_contexts_delete(struct m0t1fs_sb *csb)
{
	struct m0t1fs_service_context *x;

	M0_ENTRY();

	m0_tl_for(svc_ctx, &csb->csb_service_contexts, x) {
		svc_ctx_tlist_del(x);
		m0t1fs_service_context_fini(x);
		m0_free(x);
	} m0_tl_endfor;

	M0_LEAVE();
}

static int m0t1fs_connect_to_service(struct m0t1fs_service_context *ctx)
{
	struct m0_rpc_machine   *rpc_mach;
	struct m0_net_end_point *ep;
	struct m0_rpc_conn      *conn;
	int                      rc;

	M0_ENTRY();

	rpc_mach = &m0t1fs_globals.g_rpc_machine;

	/* Create target end-point */
	rc = m0_net_end_point_create(&ep, &rpc_mach->rm_tm, ctx->sc_addr);
	if (rc != 0)
		M0_RETURN(rc);

	conn = &ctx->sc_conn;
	rc = m0_rpc_conn_create(conn, ep, rpc_mach, M0T1FS_MAX_NR_RPC_IN_FLIGHT,
				M0_TIME_NEVER);
	m0_net_end_point_put(ep);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_rpc_session_create(&ctx->sc_session, conn,
				   M0T1FS_NR_SLOTS_PER_SESSION, M0_TIME_NEVER);
	if (rc == 0) {
		++ctx->sc_csb->csb_nr_active_contexts;
		M0_LOG(M0_INFO, "Connected to service [%s]. Active contexts:"
		       " %d", ctx->sc_addr,
		       ctx->sc_csb->csb_nr_active_contexts);
	} else {
		(void)m0_rpc_conn_destroy(conn, M0_TIME_NEVER);
	}
	M0_RETURN(rc);
}

static void m0t1fs_disconnect_from_service(struct m0t1fs_service_context *ctx)
{
	M0_ENTRY();

	(void)m0_rpc_session_destroy(&ctx->sc_session, M0_TIME_NEVER);
	(void)m0_rpc_conn_destroy(&ctx->sc_conn, M0_TIME_NEVER);

	--ctx->sc_csb->csb_nr_active_contexts;
	M0_LOG(M0_INFO, "Disconnected from service [%s]. Active contexts: %d",
	       ctx->sc_addr, ctx->sc_csb->csb_nr_active_contexts);
	M0_LEAVE();
}

static void m0t1fs_disconnect_from_services(struct m0t1fs_sb *csb)
{
	struct m0t1fs_service_context *ctx;
	M0_ENTRY();

	m0_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {
		if (csb->csb_nr_active_contexts == 0)
			break;
		m0t1fs_disconnect_from_service(ctx);
	} m0_tl_endfor;

	M0_LEAVE();
}

static int
m0t1fs_container_location_map_init(struct m0t1fs_container_location_map *map,
				   int nr_containers)
{
	M0_ENTRY();
	M0_SET0(map);
	M0_RETURN(0);
}

static void
m0t1fs_container_location_map_fini(struct m0t1fs_container_location_map *map)
{
	M0_ENTRY();
	M0_LEAVE();
}

static int m0t1fs_container_location_map_build(struct m0t1fs_sb *csb)
{
	struct m0t1fs_service_context        *ctx;
	struct m0t1fs_container_location_map *map;
	int                                   nr_cont_per_svc;
	int                                   nr_data_containers;
	int                                   nr_ios;
	int                                   i;
	int                                   cur;

	M0_ENTRY();

	nr_ios = csb->csb_mnt_opts.mo_ios_ep_nr;
	M0_ASSERT(nr_ios > 0);

	/* Out of csb->csb_nr_containers 1 is md container */
	nr_data_containers = csb->csb_nr_containers - 1;
	M0_ASSERT(nr_data_containers > 0);

	nr_cont_per_svc = nr_data_containers / nr_ios;
	if (nr_data_containers % nr_ios != 0)
		nr_cont_per_svc++;

	M0_LOG(M0_DEBUG, "nr_cont_per_svc = %d", nr_cont_per_svc);

	map = &csb->csb_cl_map;
	cur = 1;

	m0_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {
		switch (ctx->sc_type) {
		case M0T1FS_ST_MDS:
			/* Currently assuming only one MGS, which will serve
			   container 0 */
			map->clm_map[0] = ctx;
			M0_LOG(M0_DEBUG, "container_id [0] at %s", ctx->sc_addr);
			break;

		case M0T1FS_ST_IOS:
			for (i = 0; i < nr_cont_per_svc &&
				    cur <= nr_data_containers; i++, cur++) {
				map->clm_map[cur] = ctx;
				M0_LOG(M0_DEBUG, "container_id [%d] at %s", cur,
						ctx->sc_addr);
			}
			break;

		case M0T1FS_ST_MGS:
			break;

		default:
			M0_IMPOSSIBLE("Invalid service type");
		}
	} m0_tl_endfor;

	M0_RETURN(0);
}

M0_INTERNAL struct m0_rpc_session *
m0t1fs_container_id_to_session(const struct m0t1fs_sb *csb,
			       uint64_t container_id)
{
	struct m0t1fs_service_context *ctx;

	M0_ENTRY();
	M0_PRE(container_id < csb->csb_nr_containers);

	ctx = csb->csb_cl_map.clm_map[container_id];
	M0_ASSERT(ctx != NULL);

	M0_LEAVE("session: %p", &ctx->sc_session);
	return &ctx->sc_session;
}

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
