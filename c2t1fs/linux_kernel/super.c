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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"

#include <linux/mount.h>
#include <linux/parser.h>     /* substring_t                    */
#include <linux/slab.h>       /* kmalloc(), kfree()             */

#include "lib/misc.h"         /* C2_SET0()                      */
#include "lib/memory.h"       /* C2_ALLOC_PTR(), c2_free()      */
#include "c2t1fs/linux_kernel/c2t1fs.h"
#include "layout/linear_enum.h"
#include "layout/pdclust.h"
#include "colibri/magic.h"
#include "conf/conf_fop.h"
#include "rpc/rpclib.h"       /* c2_rpc_client_call */

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

C2_INTERNAL void io_bob_tlists_init(void);
extern const struct c2_addb_ctx_type c2t1fs_addb_type;
extern struct c2_addb_ctx c2t1fs_addb;

/* Mount options */

static void c2t1fs_mnt_opts_init(struct c2t1fs_mnt_opts *mntopts);
static void c2t1fs_mnt_opts_fini(struct c2t1fs_mnt_opts *mntopts);
static int  c2t1fs_mnt_opts_validate(const struct c2t1fs_mnt_opts *mnt_opts);
static int  c2t1fs_mnt_opts_parse(char *options, struct c2t1fs_mnt_opts *mops);

/* service contexts */

static void c2t1fs_service_context_init(struct c2t1fs_service_context *ctx,
					struct c2t1fs_sb              *csb,
					enum c2t1fs_service_type       type,
					char                          *ep_addr);

static void c2t1fs_service_context_fini(struct c2t1fs_service_context *ctx);

static int  c2t1fs_service_contexts_populate(struct c2t1fs_sb *csb);
static void c2t1fs_service_contexts_delete(struct c2t1fs_sb *csb);

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
	.f_container = 1ULL,
	.f_key = 3ULL
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
C2_INTERNAL int c2t1fs_get_sb(struct file_system_type *fstype,
			      int flags,
			      const char *devname,
			      void *data, struct vfsmount *mnt)
{
	C2_ENTRY("flags: 0x%x, devname: %s, data: %s", flags, devname,
		 (char *)data);
	C2_RETURN(get_sb_nodev(fstype, flags, data, c2t1fs_fill_super, mnt));
}

/* Default timeout for waiting on sm_group:c2_clink if ast thread is idle. */
enum { AST_THREAD_TIMEOUT = 10 };

static void ast_thread(struct c2t1fs_sb *csb)
{
	c2_time_t delta = c2_time_set(&delta, AST_THREAD_TIMEOUT, 0);

	while (1) {
		c2_chan_timedwait(&csb->csb_iogroup.s_clink,
				  c2_time_add(c2_time_now(), delta));
		c2_sm_group_lock(&csb->csb_iogroup);
		c2_sm_asts_run(&csb->csb_iogroup);
		c2_sm_group_unlock(&csb->csb_iogroup);
		if (!csb->csb_active &&
		    c2_atomic64_get(&csb->csb_pending_io_nr) == 0) {
			c2_chan_signal(&csb->csb_iowait);
			return;
		}
	}
}

static void ast_thread_stop(struct c2t1fs_sb *csb)
{
        struct c2_clink w;

	c2_clink_init(&w, NULL);
	c2_clink_add(&csb->csb_iowait, &w);

	csb->csb_active = false;
	c2_chan_signal(&csb->csb_iogroup.s_chan);
	c2_chan_wait(&w);
	c2_thread_join(&csb->csb_astthread);

	c2_clink_del(&w);
	c2_clink_fini(&w);
}

extern struct io_mem_stats iommstats;

#if 0 /* XXX conf-ut:conf-net <<<<<<< */
static struct c2_fop *c2t1fs_fop_alloc(void)
{
	struct c2_fop *fop;

	C2_ENTRY();

	fop = c2_fop_alloc(&c2_conf_fetch_fopt, NULL);
	if (unlikely(fop == NULL)) {
		C2_LOG(C2_ERROR, "fop allocation failed");
	} else {
		struct c2_conf_fetch *req = c2_fop_data(fop);

		req->f_origin.oi_type = 999; /* XXX */
		req->f_origin.oi_id = (const struct c2_buf)C2_BUF_INIT0;
		req->f_path.ab_count = 0;
	}

	C2_LEAVE();
	return fop;
}

static void c2t1fs_conf_ping(struct c2_rpc_session *session)
{
	struct c2_fop             *fop;
	struct c2_rpc_item        *item;
	struct c2_conf_fetch_resp *resp;
	int                        rc;

	C2_ENTRY();

	fop = c2t1fs_fop_alloc();
	if (fop == NULL) {
		C2_LEAVE();
		return;
	}

	rc = c2_rpc_client_call(fop, session, &c2_fop_default_item_ops, 0,
				C2T1FS_RPC_TIMEOUT);
	if (rc != 0) {
		C2_LOG(C2_ERROR, "c2_rpc_client_call() error: %d", rc);
		C2_LEAVE();
		return;
	}

	/* XXX TODO Ask Anatoliy -- author of the code -- about the
	 * assertions below.  I'm not sure we can be so confident
	 * here.  --vvv */
	item = &fop->f_item;
	C2_ASSERT(item->ri_error == 0);
	C2_ASSERT(item->ri_reply != NULL);

	resp = c2_fop_data(c2_rpc_item_to_fop(item->ri_reply));
	C2_ASSERT(resp != NULL);

	C2_LOG(C2_DEBUG, "c2t1fs_conf_ping() succeeded");
	C2_LEAVE();
}
#endif /* XXX >>>>>>> */

static int fs_params_parse(struct c2t1fs_mnt_opts *dest, const char **src);

static int root_alloc(struct super_block *sb)
{
	struct inode *r;

	r = c2t1fs_root_iget(sb);
	if (r == NULL)
		return -ENOMEM;

	sb->s_root = d_alloc_root(r);
	if (sb->s_root == NULL) {
		iput(r);
		return -ENOMEM;
	}
	return 0;
}

static int conf_read(struct c2t1fs_mnt_opts *mops, const struct c2t1fs_sb *csb)
{
	int rc;
	struct c2_conf_obj *obj = NULL;

	C2_ENTRY();
	C2_PRE(csb->csb_astthread.t_state == TS_RUNNING);

	rc = c2_confc_open_sync(&obj, csb->csb_confc.cc_root,
				C2_BUF_INITS("filesystem")) ?:
		fs_params_parse(mops, C2_CONF_CAST(obj, c2_conf_filesystem)->
				cf_params);
	c2_confc_close(obj);

	C2_RETURN(rc);
}

static int c2t1fs_fill_super(struct super_block *sb, void *data,
			     int silent __attribute__((unused)))
{
	struct c2t1fs_sb       *csb;
	struct c2t1fs_mnt_opts *mops;
	int                     rc;

	C2_ENTRY();

	C2_ALLOC_PTR(csb);
	if (csb == NULL) {
		rc = -ENOMEM;
		goto end;
	}

	rc = c2t1fs_sb_init(csb);
	if (rc != 0)
		goto sb_free;

	mops = &csb->csb_mnt_opts;
	rc = c2t1fs_mnt_opts_parse(data, mops);
	if (rc != 0)
		goto sb_fini;

	C2_ASSERT(mops->mo_profile != NULL && *mops->mo_profile != '\0');
	rc = c2_confc_init(&csb->csb_confc, mops->mo_conf,
			   &(const struct c2_buf)C2_BUF_INITS(mops->mo_profile),
			   &csb->csb_iogroup);
	if (rc != 0)
		goto sb_fini;

	rc = C2_THREAD_INIT(&csb->csb_astthread, struct c2t1fs_sb *, NULL,
				&ast_thread, csb, "ast_thread");
	C2_ASSERT(rc == 0);
	rc = conf_read(mops, csb);
	if (rc != 0)
		goto thread_fini;

	rc = c2t1fs_connect_to_all_services(csb);
	if (rc != 0)
		goto thread_fini;

	rc = c2_pool_init(&csb->csb_pool, mops->mo_pool_width);
	if (rc != 0)
		goto ioserver_fini;

	csb->csb_pool.po_mach = c2_alloc(sizeof (struct c2_poolmach));
	if (csb->csb_pool.po_mach == NULL) {
		rc = -ENOMEM;
		goto pool_fini;
	}

	rc = c2_poolmach_init(csb->csb_pool.po_mach, NULL,
			      1, mops->mo_pool_width,
			      1, mops->mo_nr_parity_units);
	if (rc != 0) {
		c2_free(csb->csb_pool.po_mach);
		goto pool_fini;
	}

	rc = c2t1fs_sb_layout_init(csb);
	if (rc != 0)
		goto poolmach_fini;

	rc = c2t1fs_container_location_map_init(&csb->csb_cl_map,
						csb->csb_nr_containers);
	if (rc != 0)
		goto layout_fini;

	rc = c2t1fs_container_location_map_build(csb);
	if (rc != 0)
		goto map_fini;

	sb->s_fs_info        = csb;
	sb->s_blocksize      = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic          = C2_T1FS_SUPER_MAGIC;
	sb->s_maxbytes       = MAX_LFS_FILESIZE;
	sb->s_op             = &c2t1fs_super_operations;

	rc = root_alloc(sb);
	if (rc != 0)
		goto map_fini;

        io_bob_tlists_init();

	C2_SET0(&iommstats);
#if 0 /* XXX conf-ut:conf-net */
	c2t1fs_conf_ping(c2t1fs_container_id_to_session(csb, 1));
#endif
	C2_RETURN(0);

map_fini:
	c2t1fs_container_location_map_fini(&csb->csb_cl_map);
layout_fini:
	c2t1fs_sb_layout_fini(csb);
poolmach_fini:
	c2_poolmach_fini(csb->csb_pool.po_mach);
	c2_free(csb->csb_pool.po_mach);
pool_fini:
	c2_pool_fini(&csb->csb_pool);
ioserver_fini:
	c2t1fs_disconnect_from_all_services(csb);
	c2t1fs_service_contexts_delete(csb);
thread_fini:
	ast_thread_stop(csb);
	c2_confc_fini(&csb->csb_confc);
sb_fini:
	c2t1fs_sb_fini(csb);
sb_free:
	c2_free(csb);
end:
	sb->s_fs_info = NULL;
	C2_ASSERT(rc != 0);
	C2_RETURN(rc);
}

static int c2t1fs_sb_layout_init(struct c2t1fs_sb *csb)
{
	struct c2_layout_enum  *layout_enum;
	int                     rc;
	struct c2t1fs_mnt_opts *mops = &csb->csb_mnt_opts;

	C2_ENTRY();

	C2_PRE(mops->mo_pool_width != 0);
	C2_PRE(mops->mo_nr_data_units != 0);
	C2_PRE(mops->mo_nr_parity_units != 0);
	C2_PRE(mops->mo_unit_size != 0);

	/* See "Containers and component objects" section in c2t1fs.h
	 * for more information on following line */
	csb->csb_nr_containers = mops->mo_pool_width + 1;
	csb->csb_pool_width    = mops->mo_pool_width;

	if (mops->mo_pool_width < mops->mo_nr_data_units +
	    2 * mops->mo_nr_parity_units ||
	    csb->csb_nr_containers > C2T1FS_MAX_NR_CONTAINERS)
		C2_RETURN(-EINVAL);

	rc = c2t1fs_build_cob_id_enum(mops->mo_pool_width, &layout_enum);
	if (rc == 0) {
		uint64_t random = c2_time_nanoseconds(c2_time_now());

		csb->csb_layout_id = c2_rnd(~0ULL >> 16, &random);
		rc = c2t1fs_build_layout(csb->csb_layout_id,
					 mops->mo_nr_data_units,
					 mops->mo_nr_parity_units,
					 mops->mo_pool_width,
					 mops->mo_unit_size, layout_enum,
					 &csb->csb_file_layout);
		if (rc != 0)
			c2_layout_enum_fini(layout_enum);
	}

	C2_POST(ergo(rc == 0, csb->csb_file_layout != NULL));
	C2_RETURN(rc);
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

	C2_RETURN(rc);
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

	C2_RETURN(rc);
}

/**
   Implementation of file_system_type::kill_sb() interface.
 */
C2_INTERNAL void c2t1fs_kill_sb(struct super_block *sb)
{
	struct c2t1fs_sb *csb;

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
		c2t1fs_container_location_map_fini(&csb->csb_cl_map);
		c2t1fs_sb_layout_fini(csb);
		c2_poolmach_fini(csb->csb_pool.po_mach);
		c2_pool_fini(&csb->csb_pool);
		c2t1fs_disconnect_from_all_services(csb);
		c2t1fs_service_contexts_delete(csb);
		ast_thread_stop(csb);
		c2_confc_fini(&csb->csb_confc);
		c2t1fs_sb_fini(csb);
		c2_free(csb);
	}
	kill_anon_super(sb);

	C2_LOG(C2_INFO, "mem stats :\n a_ioreq_nr = %llu, d_ioreq_nr = %llu\n"
			"a_pargrp_iomap_nr = %llu, d_pargrp_iomap_nr = %llu\n"
			"a_target_ioreq_nr = %llu, d_target_ioreq_nr = %llu\n",
	       iommstats.a_ioreq_nr, iommstats.d_ioreq_nr,
	       iommstats.a_pargrp_iomap_nr, iommstats.d_pargrp_iomap_nr,
	       iommstats.a_target_ioreq_nr, iommstats.d_target_ioreq_nr);

	C2_LOG(C2_INFO, "a_io_req_fop_nr = %llu, d_io_req_fop_nr = %llu\n"
			"a_data_buf_nr = %llu, d_data_buf_nr = %llu\n"
			"a_page_nr = %llu, d_page_nr = %llu\n",
	       iommstats.a_io_req_fop_nr, iommstats.d_io_req_fop_nr,
	       iommstats.a_data_buf_nr, iommstats.d_data_buf_nr,
	       iommstats.a_page_nr, iommstats.d_page_nr);

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

	C2_RETURN(0);
}

static void c2t1fs_sb_fini(struct c2t1fs_sb *csb)
{
	C2_ENTRY();
	C2_PRE(csb != NULL);

	c2_chan_fini(&csb->csb_iowait);
	c2_sm_group_fini(&csb->csb_iogroup);
	svc_ctx_tlist_fini(&csb->csb_service_contexts);
	c2_mutex_fini(&csb->csb_mutex);
	c2t1fs_mnt_opts_fini(&csb->csb_mnt_opts);
	csb->csb_next_key = 0;
        c2_addb_ctx_fini(&c2t1fs_addb);

	C2_LEAVE();
}

enum c2t1fs_mntopts {
	C2T1FS_MNTOPT_CONF = 1,
	C2T1FS_MNTOPT_PROFILE,
	C2T1FS_MNTOPT_IOS,
	C2T1FS_MNTOPT_ERR,
};

static const match_table_t c2t1fs_mntopt_tokens = {
	{ C2T1FS_MNTOPT_CONF,    "conf=%s"    },
	{ C2T1FS_MNTOPT_PROFILE, "profile=%s" },
	{ C2T1FS_MNTOPT_IOS,     "ios=%s"     },
	/* match_token() requires 2nd field of the last element to be NULL */
	{ C2T1FS_MNTOPT_ERR, NULL }
};

static void c2t1fs_mnt_opts_init(struct c2t1fs_mnt_opts *mntopts)
{
	C2_ENTRY();
	C2_PRE(mntopts != NULL);

	C2_SET0(mntopts);

	C2_LEAVE();
}

static void c2t1fs_mnt_opts_fini(struct c2t1fs_mnt_opts *mntopts)
{
	int i;

	C2_ENTRY();
	C2_ASSERT(mntopts != NULL);

	for (i = 0; i < mntopts->mo_ios_ep_nr; i++) {
		C2_ASSERT(mntopts->mo_ios_ep_addr[i] != NULL);
		/*
		 * using kfree() instead of c2_free() because the memory
		 * was allocated using match_strdup().
		 */
		kfree(mntopts->mo_ios_ep_addr[i]);
	}

	if (mntopts->mo_conf != NULL)
		kfree(mntopts->mo_conf);

	if (mntopts->mo_profile != NULL)
		kfree(mntopts->mo_profile);

	C2_SET0(mntopts);

	C2_LEAVE();
}

static int c2t1fs_mnt_opts_validate(const struct c2t1fs_mnt_opts *mops)
{
	C2_ENTRY();

	if (mops->mo_conf == NULL || *mops->mo_conf == '\0')
		C2_RETERR(-EINVAL, "Mandatory parameter is missing: conf");
	if (mops->mo_profile == NULL || *mops->mo_profile == '\0')
		C2_RETERR(-EINVAL, "Mandatory parameter is missing: profile");

	if (mops->mo_ios_ep_nr == 0)
		C2_RETERR(-EINVAL,
			  "Must specify at least one io-service endpoint");

	/* c2t1fs_mnt_opts_parse() guarantees that this condition holds: */
	C2_ASSERT(mops->mo_ios_ep_nr <= ARRAY_SIZE(mops->mo_ios_ep_addr));

	C2_RETURN(0);
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

static int fs_params_validate(struct c2t1fs_mnt_opts *mops)
{
	C2_ENTRY();

	C2_PRE(mops->mo_pool_width != 0);
	C2_PRE(mops->mo_nr_data_units != 0);
	C2_PRE(mops->mo_nr_parity_units != 0);
	C2_PRE(mops->mo_unit_size != 0);

	/* Need to test with unit size that is not multiple of page size.
	 * Until then --- don't allow. */
	if ((mops->mo_unit_size & (PAGE_CACHE_SIZE - 1)) != 0)
		C2_RETERR(-EINVAL,
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
		C2_RETERR(-EINVAL, "Number of parity units must be 1");

	C2_RETURN(0);
}

static int fs_params_parse(struct c2t1fs_mnt_opts *dest, const char **src)
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

	C2_ENTRY();

	dest->mo_pool_width = dest->mo_nr_data_units =
		dest->mo_nr_parity_units = dest->mo_unit_size = 0;

	if (src == NULL)
		goto end;

	for (rc = 0; rc == 0 && *src != NULL; ++src) {
		/* match_token() doesn't change the string pointed to
		 * by its first argument.  We don't want to remove
		 * `const' from c2_conf_filesystem::cf_params only
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
			C2_RETERR(rc, "Invalid filesystem parameter: %s", *src);
	}
end:
	if (dest->mo_nr_data_units == 0)
		dest->mo_nr_data_units = C2T1FS_DEFAULT_NR_DATA_UNITS;

	if (dest->mo_nr_parity_units == 0)
		dest->mo_nr_parity_units = C2T1FS_DEFAULT_NR_PARITY_UNITS;

	if (dest->mo_unit_size == 0)
		dest->mo_unit_size = C2T1FS_DEFAULT_STRIPE_UNIT_SIZE;

	if (dest->mo_pool_width == 0)
		dest->mo_pool_width =
			dest->mo_nr_data_units + 2 * dest->mo_nr_parity_units;

	rc = fs_params_validate(dest);
	if (rc == 0)
		C2_LOG(C2_INFO, "pool_width (P) = %u, nr_data_units (N) = %u,"
		       " nr_parity_units (K) = %u, unit_size = %u",
		       dest->mo_pool_width, dest->mo_nr_data_units,
		       dest->mo_nr_parity_units, dest->mo_unit_size);
	C2_RETURN(rc);
}

/**
 * Consumes a chunk of `conf=' mount option value.
 *
 * conf_step() knows how to tell the end of `conf=' value by counting
 * '[' and ']' characters.  conf_step() restores comma (',') at the
 * end of given chunk, unless this is the last chunk of `conf=' value.
 *
 * @retval 0        End of configuration string has been reached.
 * @retval 1        A chunk has been consumed. Parsing should be continued.
 * @retval -EPROTO  Too many unclosed brackets. Unable to proceed.
 *
 * @see @ref conf-fspec-preload
 */
static int conf_step(char *s, uint8_t *depth)
{
	C2_PRE(*s != '\0');

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

static int c2t1fs_mnt_opts_parse(char *options, struct c2t1fs_mnt_opts *mops)
{
	substring_t args[MAX_OPT_ARGS];
	char       *op;
	char      **pstr;
	int         rc = 0;

	C2_ENTRY();

	if (options == NULL)
		C2_RETURN(-EINVAL);

	C2_LOG(C2_INFO, "Mount options: `%s'", options);

	while ((op = strsep(&options, ",")) != NULL && *op != '\0') {
		switch (match_token(op, c2t1fs_mntopt_tokens, args)) {
		case C2T1FS_MNTOPT_CONF: {
			const char *start = args->from;
			uint8_t     depth = 0;

			op = args->from;
			do {
				rc = conf_step(op, &depth);
			} while (rc > 0 &&
				 (op = strsep(&options, ",")) != NULL &&
				 *op != '\0');

			if (rc < 0) {
				C2_ASSERT(rc == -EPROTO);
				C2_RETERR(rc, "Configuration string is "
					  "too nested");
			}

			mops->mo_conf = kstrdup(start, GFP_KERNEL);
			if (mops->mo_conf == NULL)
				C2_RETURN(-ENOMEM);

			C2_LOG(C2_INFO, "conf: `%s'", mops->mo_conf);
			break;
		}
		case C2T1FS_MNTOPT_PROFILE:
			rc = str_parse(&mops->mo_profile, args);
			if (rc != 0)
				goto out;
			C2_LOG(C2_INFO, "profile: %s", mops->mo_profile);
			break;

		case C2T1FS_MNTOPT_IOS:
			if (mops->mo_ios_ep_nr ==
			    ARRAY_SIZE(mops->mo_ios_ep_addr))
				C2_RETERR(-EINVAL, "No more than %lu ios"
					  " addresses can be provided",
					  ARRAY_SIZE(mops->mo_ios_ep_addr));

			pstr = &mops->mo_ios_ep_addr[mops->mo_ios_ep_nr];
			rc = str_parse(pstr, args);
			if (rc != 0)
				goto out;
			++mops->mo_ios_ep_nr;
			C2_LOG(C2_INFO, "ioservice: %s", *pstr);
			break;

		default:
			C2_RETERR(-EINVAL, "Unrecognized option: %s\n"
				  "Supported options: conf,profile,ios", op);
		}
	}

	rc = c2t1fs_mnt_opts_validate(mops);
out:
	C2_RETURN(rc);
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

static int c2t1fs_connect_to_all_services(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;
	int                            rc;

	C2_ENTRY();

	C2_PRE(svc_ctx_tlist_is_empty(&csb->csb_service_contexts));

	rc = c2t1fs_service_contexts_populate(csb);
	if (rc == 0) {
		c2_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {
			rc = c2t1fs_connect_to_service(ctx);
			if (rc != 0) {
				c2t1fs_disconnect_from_all_services(csb);
				break;
			}
		} c2_tl_endfor;
	}

	if (rc != 0)
		c2t1fs_service_contexts_delete(csb);
	C2_RETURN(rc);
}

static int c2t1fs_service_contexts_populate(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;
	uint32_t                       i;
	int                            rc = 0;
	struct c2t1fs_mnt_opts        *mops = &csb->csb_mnt_opts;

	C2_ENTRY();

	for (i = 0; i < mops->mo_ios_ep_nr; i++) {
		C2_ALLOC_PTR(ctx);
		if (ctx == NULL) {
			rc = -ENOMEM;
			break;
		}

		/* XXX TODO Use confc data to initiate service contexts. */
		c2t1fs_service_context_init(ctx, csb, C2T1FS_ST_IOS,
					    mops->mo_ios_ep_addr[i]);
		svc_ctx_tlist_add_tail(&csb->csb_service_contexts, ctx);
	}

	if (rc != 0)
		c2t1fs_service_contexts_delete(csb);
	C2_RETURN(rc);
}

static void c2t1fs_service_contexts_delete(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *x;

	C2_ENTRY();

	c2_tl_for(svc_ctx, &csb->csb_service_contexts, x) {
		svc_ctx_tlist_del(x);
		c2t1fs_service_context_fini(x);
		c2_free(x);
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
	rc = c2_rpc_conn_create(conn, ep, rpc_mach, C2T1FS_MAX_NR_RPC_IN_FLIGHT,
				C2_TIME_NEVER);
	c2_net_end_point_put(ep);
	if (rc != 0)
		goto out;

	session = &ctx->sc_session;
	rc = c2_rpc_session_create(session, conn, C2T1FS_NR_SLOTS_PER_SESSION,
				   C2_TIME_NEVER);
	if (rc != 0)
		goto conn_term;

	++ctx->sc_csb->csb_nr_active_contexts;
	C2_LOG(C2_INFO, "Connected to service [%s]. Active contexts: %d",
	       ctx->sc_addr, ctx->sc_csb->csb_nr_active_contexts);
	C2_RETURN(rc);

conn_term:
	(void)c2_rpc_conn_terminate_sync(conn, C2_TIME_NEVER);
	c2_rpc_conn_fini(conn);
out:
	C2_ASSERT(rc != 0);
	C2_RETURN(rc);
}

static void c2t1fs_disconnect_from_service(struct c2t1fs_service_context *ctx)
{
	C2_ENTRY();

	(void)c2_rpc_session_terminate_sync(&ctx->sc_session, C2_TIME_NEVER);
	/* session_fini() before conn_terminate is necessary, to detach
	   session from connection */
	c2_rpc_session_fini(&ctx->sc_session);

	(void)c2_rpc_conn_terminate_sync(&ctx->sc_conn, C2_TIME_NEVER);
	c2_rpc_conn_fini(&ctx->sc_conn);

	--ctx->sc_csb->csb_nr_active_contexts;
	C2_LOG(C2_INFO, "Disconnected from service [%s]. Active contexts: %d",
	       ctx->sc_addr, ctx->sc_csb->csb_nr_active_contexts);
	C2_LEAVE();
}

static void c2t1fs_disconnect_from_all_services(struct c2t1fs_sb *csb)
{
	struct c2t1fs_service_context *ctx;
	C2_ENTRY();

	c2_tl_for(svc_ctx, &csb->csb_service_contexts, ctx) {
		if (csb->csb_nr_active_contexts == 0)
			break;
		c2t1fs_disconnect_from_service(ctx);
	} c2_tl_endfor;

	C2_LEAVE();
}

static int
c2t1fs_container_location_map_init(struct c2t1fs_container_location_map *map,
				   int nr_containers)
{
	C2_ENTRY();
	C2_SET0(map);
	C2_RETURN(0);
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

	nr_ios = csb->csb_mnt_opts.mo_ios_ep_nr;
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
			C2_IMPOSSIBLE("Invalid service type");
		}
	} c2_tl_endfor;

	C2_RETURN(0);
}

C2_INTERNAL struct c2_rpc_session *
c2t1fs_container_id_to_session(const struct c2t1fs_sb *csb,
			       uint64_t container_id)
{
	struct c2t1fs_service_context *ctx;

	C2_ASSERT(container_id < csb->csb_nr_containers);
	ctx = csb->csb_cl_map.clm_map[container_id];
	C2_ASSERT(ctx != NULL);
	C2_LEAVE("session: %p", &ctx->sc_session);
	return &ctx->sc_session;
}

C2_INTERNAL void c2t1fs_fs_lock(struct c2t1fs_sb *csb)
{
	C2_ENTRY();
	c2_mutex_lock(&csb->csb_mutex);
	C2_LEAVE();
}

C2_INTERNAL void c2t1fs_fs_unlock(struct c2t1fs_sb *csb)
{
	C2_ENTRY();
	c2_mutex_unlock(&csb->csb_mutex);
	C2_LEAVE();
}

C2_INTERNAL bool c2t1fs_fs_is_locked(const struct c2t1fs_sb *csb)
{
	return c2_mutex_is_locked(&csb->csb_mutex);
}
