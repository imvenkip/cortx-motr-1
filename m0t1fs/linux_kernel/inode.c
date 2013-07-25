/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 10/14/2011
 */

#include <linux/slab.h>         /* kmem_cache */

#include "layout/pdclust.h"     /* m0_pdclust_build(), m0_pdl_to_layout(),
				 * m0_pdclust_instance_build()           */
#include "layout/linear_enum.h" /* m0_linear_enum_build()                */
#include "lib/misc.h"           /* M0_SET0()                             */
#include "lib/memory.h"         /* M0_ALLOC_PTR(), m0_free()             */
#include "m0t1fs/linux_kernel/m0t1fs.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0T1FS
#include "lib/trace.h"          /* M0_LOG and M0_ENTRY */
#include "mero/magic.h"
#include "rm/rm_service.h"      /* m0_rm_svc_domain_get */

static struct kmem_cache *m0t1fs_inode_cachep = NULL;

static const struct m0_bob_type m0t1fs_inode_bob = {
	.bt_name         = "m0t1fs_inode",
	.bt_magix_offset = offsetof(struct m0t1fs_inode, ci_magic),
	.bt_magix        = M0_T1FS_INODE_MAGIC,
	.bt_check        = NULL
};

M0_BOB_DEFINE(M0_INTERNAL, &m0t1fs_inode_bob, m0t1fs_inode);

M0_INTERNAL bool m0t1fs_inode_is_root(const struct inode *inode)
{
	struct m0t1fs_inode *ci = M0T1FS_I(inode);
	return m0_fid_eq(&ci->ci_fid, &M0T1FS_SB(inode->i_sb)->csb_root_fid);
}

static void init_once(void *foo)
{
	struct m0t1fs_inode *ci = foo;

	M0_ENTRY();

	m0_fid_set(&ci->ci_fid, 0, 0);
	inode_init_once(&ci->ci_inode);

	M0_LEAVE();
}

M0_INTERNAL int m0t1fs_inode_cache_init(void)
{
	int rc = 0;

	M0_ENTRY();

	m0t1fs_inode_cachep = kmem_cache_create("m0t1fs_inode_cache",
						sizeof(struct m0t1fs_inode),
						0, SLAB_HWCACHE_ALIGN,
						init_once);
	if (m0t1fs_inode_cachep == NULL)
		rc = -ENOMEM;

	M0_LEAVE("rc: %d", rc);
	return rc;
}

M0_INTERNAL void m0t1fs_inode_cache_fini(void)
{
	M0_ENTRY();

	if (m0t1fs_inode_cachep != NULL) {
		kmem_cache_destroy(m0t1fs_inode_cachep);
		m0t1fs_inode_cachep = NULL;
	}

	M0_LEAVE();
}

M0_INTERNAL struct m0_rm_domain *m0t1fs_rmsvc_domain_get(void)
{
	return m0_rm_svc_domain_get(
		m0_reqh_service_find(m0_reqh_service_type_find("rmservice"),
				     &m0t1fs_globals.g_reqh));
}

static inline uint64_t m0t1fs_rm_container(const struct m0t1fs_sb *csb)
{
	/**
	 * @todo
	 * M0_RETURN(csb->csb_nr_containers);
	 * Check why conf profile doesn't parse DLM parameter
	 * Current assumption: Return the container ID for mdservice,
	 * so node running mdservice also runs rmservice.
	 */
	M0_RETURN(0);
}

M0_INTERNAL void m0t1fs_file_lock_init(struct m0t1fs_inode    *ci,
				       const struct m0t1fs_sb *csb)
{
	struct m0_rm_domain *rdom;

	rdom = m0t1fs_rmsvc_domain_get();
	M0_ASSERT(rdom != NULL);
	m0_file_init(&ci->ci_flock, &ci->ci_fid, rdom);
	m0_rm_remote_init(&ci->ci_creditor, &ci->ci_flock.fi_res);
	m0_file_owner_init(&ci->ci_fowner, &ci->ci_flock, NULL);
	ci->ci_fowner.ro_creditor = &ci->ci_creditor;
	ci->ci_creditor.rem_session =
		m0t1fs_container_id_to_session(csb, m0t1fs_rm_container(csb));
}

M0_INTERNAL void m0t1fs_file_lock_fini(struct m0t1fs_inode *ci)
{
	int rc;

	m0_rm_owner_windup(&ci->ci_fowner);
	rc = m0_rm_owner_timedwait(&ci->ci_fowner, M0_BITS(ROS_FINAL),
				   M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	m0_file_owner_fini(&ci->ci_fowner);
	m0_file_fini(&ci->ci_flock);
	m0_rm_remote_fini(&ci->ci_creditor);
}

M0_INTERNAL void m0t1fs_inode_init(struct m0t1fs_inode *ci)
{
	M0_ENTRY("ci: %p", ci);

	M0_SET0(&ci->ci_fid);
	M0_SET0(&ci->ci_flock);
	M0_SET0(&ci->ci_creditor);
	M0_SET0(&ci->ci_fowner);
	ci->ci_layout_instance = NULL;
	m0t1fs_inode_bob_init(ci);

	M0_LEAVE();
}

M0_INTERNAL void m0t1fs_inode_fini(struct m0t1fs_inode *ci)
{
	M0_ENTRY("ci: %p, is_root %s, layout_instance %p",
		 ci, m0_bool_to_str(m0t1fs_inode_is_root(&ci->ci_inode)),
		 ci->ci_layout_instance);

	M0_PRE(m0t1fs_inode_bob_check(ci));

	if (!m0t1fs_inode_is_root(&ci->ci_inode)) {
		m0_layout_instance_fini(ci->ci_layout_instance);
		m0t1fs_file_lock_fini(ci);
	}
	m0t1fs_inode_bob_fini(ci);

	M0_LEAVE();
}

/**
   Implementation of super_operations::alloc_inode() interface.
 */
M0_INTERNAL struct inode *m0t1fs_alloc_inode(struct super_block *sb)
{
	struct m0t1fs_inode *ci;

	M0_ENTRY("sb: %p", sb);

	ci = kmem_cache_alloc(m0t1fs_inode_cachep, GFP_KERNEL);
	if (ci == NULL) {
		M0_LEAVE("inode: %p", NULL);
		return NULL;
	}

	m0t1fs_inode_init(ci);

	M0_LEAVE("inode: %p", &ci->ci_inode);
	return &ci->ci_inode;
}

/**
   Implementation of super_operations::destroy_inode() interface.
 */
M0_INTERNAL void m0t1fs_destroy_inode(struct inode *inode)
{
	struct m0t1fs_inode *ci;

	M0_ENTRY("inode: %p", inode);

	ci = M0T1FS_I(inode);

	M0_LOG(M0_DEBUG, "fid [%lu:%lu]", (unsigned long)ci->ci_fid.f_container,
	       (unsigned long)ci->ci_fid.f_key);

	m0t1fs_inode_fini(ci);
	kmem_cache_free(m0t1fs_inode_cachep, ci);

	M0_LEAVE();
}

M0_INTERNAL struct inode *m0t1fs_root_iget(struct super_block *sb,
					   struct m0_fid      *root_fid)
{
	struct m0_fop_getattr_rep *rep = NULL;
	struct m0t1fs_mdop         mo;
	struct inode              *inode;
	int                        rc;

	M0_ENTRY("sb: %p", sb);

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *root_fid;
	M0T1FS_SB(sb)->csb_root_fid = *root_fid;

	rc = m0t1fs_mds_cob_getattr(M0T1FS_SB(sb), &mo, &rep);
	if (rc != 0) {
		M0_LOG(M0_FATAL, "m0t1fs_mds_cob_getattr() failed with %d", rc);
		return ERR_PTR(rc);
	}

	inode = m0t1fs_iget(sb, root_fid, &rep->g_body);

	M0_LEAVE("root_inode: %p", inode);
	return inode;
}

/**
   In file-systems like m0t1fs or nfs, inode number is not enough to identify
   a file. For such file-systems structure and semantics of file identifier
   are file-system specific e.g. fid in case of m0t1fs, file handle for nfs.

   m0t1fs_inode_test() and m0t1fs_inode_set() are the implementation of
   interfaces that are used by generic vfs code, to compare identities of
   inodes, in a generic manner.
 */
static int m0t1fs_inode_test(struct inode *inode, void *opaque)
{
	struct m0t1fs_inode *ci;
	struct m0_fid       *fid = opaque;
	int                  rc;

	M0_ENTRY();

	ci = M0T1FS_I(inode);

	M0_LOG(M0_DEBUG, "inode(%p) [%lu:%lu] opaque [%lu:%lu]", inode,
				(unsigned long)ci->ci_fid.f_container,
				(unsigned long)ci->ci_fid.f_key,
				(unsigned long)fid->f_container,
				(unsigned long)fid->f_key);

	rc = m0_fid_eq(&ci->ci_fid, fid);

	M0_LEAVE("rc: %d", rc);
	return rc;
}

static int m0t1fs_inode_set(struct inode *inode, void *opaque)
{
	struct m0t1fs_inode *ci;
	struct m0_fid       *fid = opaque;

	M0_ENTRY();
	M0_LOG(M0_DEBUG, "inode(%p) [%lu:%lu]", inode,
			(unsigned long)fid->f_container,
			(unsigned long)fid->f_key);

	ci           = M0T1FS_I(inode);
	ci->ci_fid   = *fid;
	inode->i_ino = fid->f_key;

	M0_LEAVE("rc: 0");
	return 0;
}

M0_INTERNAL int m0t1fs_inode_update(struct inode      *inode,
				    struct m0_fop_cob *body)
{
	int                  rc  = 0;
	struct m0t1fs_inode *ci  = M0T1FS_I(inode);
	struct m0t1fs_sb    *csb = M0T1FS_SB(ci->ci_inode.i_sb);

	M0_ENTRY();

	if (body->b_valid & M0_COB_ATIME)
		inode->i_atime.tv_sec  = body->b_atime;
	if (body->b_valid & M0_COB_MTIME)
		inode->i_mtime.tv_sec  = body->b_mtime;
	if (body->b_valid & M0_COB_CTIME)
		inode->i_ctime.tv_sec  = body->b_ctime;
	if (body->b_valid & M0_COB_UID)
		inode->i_uid    = body->b_uid;
	if (body->b_valid & M0_COB_GID)
		inode->i_gid    = body->b_gid;
	if (body->b_valid & M0_COB_BLOCKS)
		inode->i_blocks = body->b_blocks;
	if (body->b_valid & M0_COB_SIZE)
		inode->i_size = body->b_size;
	if (body->b_valid & M0_COB_NLINK)
		inode->i_nlink = body->b_nlink;
	if (body->b_valid & M0_COB_MODE)
		inode->i_mode = body->b_mode;

	if (!m0t1fs_inode_is_root(inode) &&
	    !m0_file_lock_resource_is_added(&ci->ci_fid))
		m0t1fs_file_lock_init(ci, csb);

	M0_LEAVE("rc: %d", rc);
	return rc;
}

static int m0t1fs_inode_read(struct inode      *inode,
			     struct m0_fop_cob *body)
{
	struct m0t1fs_inode *ci  = M0T1FS_I(inode);
	int                  rc  = 0;

	M0_ENTRY();

	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_uid   = 0;
	inode->i_gid   = 0;
	inode->i_rdev  = 0;

	rc = m0t1fs_inode_update(inode, body);
	if (rc != 0)
		goto out;

	if (S_ISREG(inode->i_mode)) {
		inode->i_op   = &m0t1fs_reg_inode_operations;
		inode->i_fop  = &m0t1fs_reg_file_operations;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op   = &m0t1fs_dir_inode_operations;
		inode->i_fop  = &m0t1fs_dir_file_operations;
	} else {
		rc = -ENOSYS;
	}
	if (!m0t1fs_inode_is_root(inode)) {
		ci->ci_layout_id = body->b_lid;
		rc = m0t1fs_inode_layout_init(ci);
	}
out:
	M0_LEAVE("rc: %d", rc);
	return rc;
}

/**
   XXX Temporary implementation of simple hash on fid
 */
static unsigned long fid_hash(const struct m0_fid *fid)
{
	M0_ENTRY();
	M0_LEAVE("hash: %lu", (unsigned long) fid->f_key);
	return fid->f_key;
}

M0_INTERNAL struct inode *m0t1fs_iget(struct super_block *sb,
				      const struct m0_fid *fid,
				      struct m0_fop_cob *body)
{
	struct inode *inode;
	unsigned long hash;
	int           err = 0;

	M0_ENTRY();

	hash = fid_hash(fid);

	/*
	 * Search inode cache for an inode that has matching @fid.
	 * Use m0t1fs_inode_test() to compare fid_s. If not found, allocate a
	 * new inode. Set its fid to @fid using m0t1fs_inode_set(). Also
	 * set I_NEW flag in inode->i_state for newly allocated inode.
	 */
	inode = iget5_locked(sb, hash, m0t1fs_inode_test, m0t1fs_inode_set,
			     (void *)fid);
	if (IS_ERR(inode)) {
		M0_LEAVE("inode: %p", ERR_CAST(inode));
		return ERR_CAST(inode);
	}
	if ((inode->i_state & I_NEW) != 0) {
		/* New inode, set its fields from @body */
		err = m0t1fs_inode_read(inode, body);
	} else if (!(inode->i_state & (I_FREEING | I_CLEAR))) {
		/* Not a new inode, let's update its attributes from @body */
		err = m0t1fs_inode_update(inode, body);
	}
	if (err != 0)
		goto out_err;
	if ((inode->i_state & I_NEW) != 0)
		unlock_new_inode(inode);
	M0_LEAVE("inode: %p", inode);
	return inode;

out_err:
	iget_failed(inode);
	M0_LEAVE("ERR: %p", ERR_PTR(err));
	return ERR_PTR(err);
}

static int m0t1fs_build_layout_instance(struct m0t1fs_sb           *csb,
					const uint64_t              layout_id,
					const struct m0_fid        *fid,
					struct m0_layout_instance **linst)
{
	struct m0_layout *layout;
	int               rc;

	M0_ENTRY();
	M0_PRE(fid != NULL);
	M0_PRE(linst != NULL);

	layout = m0_layout_find(&m0t1fs_globals.g_layout_dom, layout_id);
	if (layout == NULL) {
		rc = m0t1fs_layout_op(csb, M0_LAYOUT_OP_LOOKUP,
				      layout_id, &layout);
		if (rc != 0)
			goto out;
	}

	*linst = NULL;
	rc = m0_layout_instance_build(layout, fid, linst);
	m0_layout_put(layout);

out:
	M0_LEAVE("rc: %d", rc);
	return rc;
}

M0_INTERNAL int m0t1fs_inode_layout_init(struct m0t1fs_inode *ci)
{
	struct m0t1fs_sb          *csb;
	struct m0_layout_instance *linst;
	int                        rc;

	M0_ENTRY();
	M0_LOG(M0_DEBUG, "fid[%lu:%lu]:",
			(unsigned long)ci->ci_fid.f_container,
			(unsigned long)ci->ci_fid.f_key);

	csb = M0T1FS_SB(ci->ci_inode.i_sb);

	M0_ASSERT(ci->ci_layout_id != 0);
	rc = m0t1fs_build_layout_instance(csb, ci->ci_layout_id,
					  &ci->ci_fid, &linst);
	if (rc == 0)
		ci->ci_layout_instance = linst;

	M0_LEAVE("rc: %d", rc);
	return rc;
}
