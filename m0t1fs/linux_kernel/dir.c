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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Revision       : Anand Vidwansa <anand_vidwansa@xyratex.com>
 * Metadata       : Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 10/14/2011
 */

#include <linux/version.h> /* LINUX_VERSION_CODE */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include <linux/uidgid.h>  /* from_kuid */
#endif

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0T1FS
#include "lib/trace.h"

#include "lib/misc.h"      /* M0_SET0 */
#include "lib/memory.h"    /* M0_ALLOC_PTR */
#include "lib/bob.h"
#include "fop/fop.h"       /* m0_fop_alloc */
#include "rpc/rpclib.h"    /* m0_rpc_post_sync */
#include "rpc/rpc_opcodes.h"
#include "ioservice/io_device.h"
#include "mero/magic.h"
#include "layout/layout.h"
#include "layout/layout_internal.h" /* LID_NONE */
#include "layout/pdclust.h"
#include "m0t1fs/linux_kernel/m0t1fs.h"
#include "m0t1fs/linux_kernel/fsync.h"
#include "pool/pool.h"
#include "ioservice/fid_convert.h" /* m0_fid_convert_gob2cob, m0_fid_cob_device_id */

extern const struct m0_uint128 m0_rm_m0t1fs_group;
/**
 * Cob create/delete fop send deadline (in ns).
 * Used to utilize rpc formation when too many fops
 * are sending to one ioservice.
 */
enum {COB_REQ_DEADLINE = 2000000};

struct cob_req {
	struct m0_semaphore cr_sem;
	m0_time_t           cr_deadline;
	struct m0t1fs_sb   *cr_csb;
	int32_t             cr_rc;
	struct m0_fid       cr_fid;
};

struct cob_fop {
	struct m0_fop   c_fop;
	struct cob_req *c_req;
};

static void cob_rpc_item_cb(struct m0_rpc_item *item)
{
	int                              rc;
	struct m0_fop                   *fop;
	struct cob_fop                  *cfop;
	struct cob_req                  *creq;
	struct m0_fop_cob_op_reply      *reply;
	struct m0_fop_cob_op_rep_common *r_common;

	M0_ENTRY("rpc_item %p", item);
	M0_PRE(item != NULL);

	fop  = m0_rpc_item_to_fop(item);
	cfop = container_of(fop, struct cob_fop, c_fop);
	creq = cfop->c_req;

	if (item->ri_error != 0) {
		rc = item->ri_error;
		goto out;
	}

	M0_ASSERT(m0_is_cob_create_fop(fop) || m0_is_cob_delete_fop(fop) ||
		  m0_is_cob_truncate_fop(fop) || m0_is_cob_setattr_fop(fop));
	reply = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
	r_common = &reply->cor_common;

	if (reply->cor_rc == M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH) {
		struct m0_fv_event *event;
		struct m0t1fs_sb   *csb = creq->cr_csb;
		uint32_t            i;

		/* Retrieve the latest server version and updates and apply
		 * to the client's copy.
		 */
		rc = 0;
		for (i = 0; i < reply->cor_common.cor_fv_updates.fvu_count; ++i) {
			event = &reply->cor_common.cor_fv_updates.fvu_events[i];
			m0_poolmach_state_transit(&csb->csb_pool_version->pv_mach,
						  (struct m0_poolmach_event*)event,
						  NULL);
		}
	} else
		rc = reply->cor_rc;
out:
	if (creq->cr_rc == 0)
		creq->cr_rc = rc;

	m0_semaphore_up(&creq->cr_sem);

	M0_LOG(M0_DEBUG, "cob_req_fop=%p rc %d, "FID_F"", cfop,
			creq->cr_rc, FID_P(&creq->cr_fid));
	M0_LEAVE();
}

static const struct m0_rpc_item_ops cob_item_ops = {
	.rio_replied = cob_rpc_item_cb,
};

M0_INTERNAL void m0t1fs_inode_bob_init(struct m0t1fs_inode *bob);
M0_INTERNAL bool m0t1fs_inode_bob_check(struct m0t1fs_inode *bob);


static int file_lock_acquire(struct m0_rm_incoming *rm_in,
			     struct m0t1fs_inode *ci);
static void file_lock_release(struct m0_rm_incoming *rm_in);
static int m0t1fs_component_objects_op(struct m0t1fs_inode *ci,
				       struct m0t1fs_mdop *mop,
				       int (*func)(struct cob_req *,
						   const struct m0t1fs_inode *,
						   const struct m0t1fs_mdop *,
						   int idx));

static int m0t1fs_ios_cob_create(struct cob_req *cr,
				 const struct m0t1fs_inode *inode,
				 const struct m0t1fs_mdop *mop,
				 int idx);

static int m0t1fs_ios_cob_delete(struct cob_req *cr,
				 const struct m0t1fs_inode *inode,
				 const struct m0t1fs_mdop *mop,
				 int idx);

static int m0t1fs_ios_cob_setattr(struct cob_req *cr,
				  const struct m0t1fs_inode *inode,
				  const struct m0t1fs_mdop *mop,
				  int idx);
static int m0t1fs_ios_cob_truncate(struct cob_req *cr,
				   const struct m0t1fs_inode *inode,
			           const struct m0t1fs_mdop *mop,
				   int idx);

static int name_mem2wire(struct m0_fop_str *tgt,
			 const struct m0_buf *name)
{
	tgt->s_buf = m0_alloc(name->b_nob);
	if (tgt->s_buf == NULL)
		return M0_ERR(-ENOMEM);
	memcpy(tgt->s_buf, name->b_addr, (int)name->b_nob);
	tgt->s_len = name->b_nob;
	return 0;
}

static void body_mem2wire(struct m0_fop_cob *body,
			  const struct m0_cob_attr *attr,
			  int valid)
{
	body->b_pfid = attr->ca_pfid;
	body->b_tfid = attr->ca_tfid;
	body->b_pver = attr->ca_pver;
	if (valid & M0_COB_ATIME)
		body->b_atime = attr->ca_atime;
	if (valid & M0_COB_CTIME)
		body->b_ctime = attr->ca_ctime;
	if (valid & M0_COB_MTIME)
		body->b_mtime = attr->ca_mtime;
	if (valid & M0_COB_BLOCKS)
		body->b_blocks = attr->ca_blocks;
	if (valid & M0_COB_SIZE)
		body->b_size = attr->ca_size;
	if (valid & M0_COB_MODE)
		body->b_mode = attr->ca_mode;
	if (valid & M0_COB_UID)
		body->b_uid = attr->ca_uid;
	if (valid & M0_COB_GID)
		body->b_gid = attr->ca_gid;
	if (valid & M0_COB_BLOCKS)
		body->b_blocks = attr->ca_blocks;
	if (valid & M0_COB_NLINK)
		body->b_nlink = attr->ca_nlink;
	if (valid & M0_COB_LID)
		body->b_lid = attr->ca_lid;
	body->b_valid = valid;
}

static void body_wire2mem(struct m0_cob_attr *attr,
			  const struct m0_fop_cob *body)
{
	M0_SET0(attr);
	attr->ca_pfid = body->b_pfid;
	attr->ca_tfid = body->b_tfid;
	attr->ca_valid = body->b_valid;
	if (body->b_valid & M0_COB_MODE)
		attr->ca_mode = body->b_mode;
	if (body->b_valid & M0_COB_UID)
		attr->ca_uid = body->b_uid;
	if (body->b_valid & M0_COB_GID)
		attr->ca_gid = body->b_gid;
	if (body->b_valid & M0_COB_ATIME)
		attr->ca_atime = body->b_atime;
	if (body->b_valid & M0_COB_MTIME)
		attr->ca_mtime = body->b_mtime;
	if (body->b_valid & M0_COB_CTIME)
		attr->ca_ctime = body->b_ctime;
	if (body->b_valid & M0_COB_NLINK)
		attr->ca_nlink = body->b_nlink;
	if (body->b_valid & M0_COB_RDEV)
		attr->ca_rdev = body->b_rdev;
	if (body->b_valid & M0_COB_SIZE)
		attr->ca_size = body->b_size;
	if (body->b_valid & M0_COB_BLKSIZE)
		attr->ca_blksize = body->b_blksize;
	if (body->b_valid & M0_COB_BLOCKS)
		attr->ca_blocks = body->b_blocks;
	if (body->b_valid & M0_COB_LID)
		attr->ca_lid = body->b_lid;
	attr->ca_version = body->b_version;
}

/**
   Allocate fid of global file.

   See "Containers and component objects" section in m0t1fs.h for
   more information.

   XXX temporary.
 */
void m0t1fs_fid_alloc(struct m0t1fs_sb *csb, struct m0_fid *out)
{
	M0_PRE(m0t1fs_fs_is_locked(csb));

	m0_fid_set(out, 0, csb->csb_next_key++);

	M0_LOG(M0_DEBUG, "fid "FID_F, FID_P(out));
}

/**
 * Given a fid of an existing file, update "fid allocator" so that this fid is
 * not given out to another file.
 */
void m0t1fs_fid_accept(struct m0t1fs_sb *csb, const struct m0_fid *fid)
{
	M0_PRE(m0t1fs_fs_is_locked(csb));

	csb->csb_next_key = max64(csb->csb_next_key, fid->f_key + 1);
}

int m0t1fs_fid_setxattr(struct dentry *dentry, const char *name,
			const void *value, size_t size, int flags)
{
	return M0_ERR(-EOPNOTSUPP);
}

int m0t1fs_setxattr(struct dentry *dentry, const char *name,
		    const void *value, size_t size, int flags)
{
	struct m0t1fs_inode        *ci = M0T1FS_I(dentry->d_inode);
	struct m0t1fs_sb           *csb = M0T1FS_SB(ci->ci_inode.i_sb);
	struct m0t1fs_mdop          mo;
	int                         rc;
	struct m0_fop              *rep_fop = NULL;
	M0_THREAD_ENTER;

	M0_ENTRY("Setting %.*s's xattr %s=%.*s", dentry->d_name.len,
		 (char*)dentry->d_name.name, name, (int)size, (char *)value);

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);
	m0_buf_init(&mo.mo_attr.ca_name, (char*)dentry->d_name.name,
		    dentry->d_name.len);

	if (m0_streq(name, "lid")) {
		char *endp;
		char  buf[40];

		rc = -EINVAL;
		if (value == NULL || size >= ARRAY_SIZE(buf))
			goto out;
		/*
		 * Layout can be changed only for the freshly
		 * created file which does not contain any data yet.
		 */
		if (ci->ci_inode.i_size != 0) {
			rc = -EEXIST;
			goto out;
		}
		memcpy(buf, value, size);
		buf[size] = '\0';
		ci->ci_layout_id = simple_strtoul(buf, &endp, 0);
		if (endp - buf < size || ci->ci_layout_id == LID_NONE)
			goto out;
		rc = m0t1fs_inode_layout_init(ci);
		if (rc != 0)
			goto out;

		mo.mo_attr.ca_lid = ci->ci_layout_id;
		mo.mo_attr.ca_valid |= M0_COB_LID;

		if (!csb->csb_oostore)
			rc = m0t1fs_mds_cob_setattr(csb, &mo, &rep_fop);
		if (rc == 0) {
			M0_LOG(M0_DEBUG, "changing lid to %lld",
					 mo.mo_attr.ca_lid);
			rc = m0t1fs_component_objects_op(ci, &mo,
						m0t1fs_ios_cob_setattr);
		}
	} else {
		if (csb->csb_oostore)
			return M0_ERR(-EOPNOTSUPP);
		m0_buf_init(&mo.mo_attr.ca_eakey, (void*)name, strlen(name));
		m0_buf_init(&mo.mo_attr.ca_eaval, (void*)value, size);
		rc = m0t1fs_mds_cob_setxattr(csb, &mo, &rep_fop);
	}
out:
	m0_fop_put0_lock(rep_fop);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

ssize_t m0t1fs_fid_getxattr(struct dentry *dentry, const char *name,
			    void *buffer, size_t size)
{
	M0_THREAD_ENTER;
	return M0_ERR(-EOPNOTSUPP);
}

ssize_t m0t1fs_getxattr(struct dentry *dentry, const char *name,
			void *buffer, size_t size)
{
	struct m0t1fs_inode        *ci = M0T1FS_I(dentry->d_inode);
	struct m0t1fs_sb           *csb = M0T1FS_SB(ci->ci_inode.i_sb);
	struct m0_fop_getxattr_rep *rep = NULL;
	struct m0t1fs_mdop          mo;
	int                         rc;
	struct m0_fop              *rep_fop;
	M0_THREAD_ENTER;

	M0_ENTRY("Getting %.*s's xattr %s", dentry->d_name.len,
		 (char*)dentry->d_name.name, name);

	if (csb->csb_oostore)
		return M0_ERR(-EOPNOTSUPP);
	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);
	m0_buf_init(&mo.mo_attr.ca_name, (char*)dentry->d_name.name,
		    dentry->d_name.len);
	m0_buf_init(&mo.mo_attr.ca_eakey, (char*)name, strlen(name));

	rc = m0t1fs_mds_cob_getxattr(csb, &mo, &rep_fop);
	if (rc == 0) {
		rep = m0_fop_data(rep_fop);
		if (buffer != NULL) {
			if ((size_t)rep->g_value.s_len > size) {
				rc = M0_ERR(-ERANGE);
				goto out;
			}
			memcpy(buffer, rep->g_value.s_buf, rep->g_value.s_len);
		}
		rc = rep->g_value.s_len; /* return xattr length */
	} else if (rc == -ENOENT)
		rc = M0_ERR(-ENODATA);
out:
	m0_fop_put0_lock(rep_fop);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

ssize_t m0t1fs_fid_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	M0_THREAD_ENTER;
	return M0_ERR(-EOPNOTSUPP);
}

ssize_t m0t1fs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	M0_THREAD_ENTER;
	return M0_ERR(-EOPNOTSUPP);
}

int m0t1fs_fid_removexattr(struct dentry *dentry, const char *name)
{
	M0_THREAD_ENTER;
	return M0_ERR(-EOPNOTSUPP);
}

int m0t1fs_removexattr(struct dentry *dentry, const char *name)
{
	struct m0t1fs_inode        *ci = M0T1FS_I(dentry->d_inode);
	struct m0t1fs_sb           *csb = M0T1FS_SB(ci->ci_inode.i_sb);
	struct m0t1fs_mdop          mo;
	int                         rc;
	struct m0_fop              *rep_fop;

	M0_THREAD_ENTER;
	M0_ENTRY("Deleting %.*s's xattr %s", dentry->d_name.len,
		 (char*)dentry->d_name.name, name);

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);
	m0_buf_init(&mo.mo_attr.ca_name, (char*)dentry->d_name.name,
		    dentry->d_name.len);
	m0_buf_init(&mo.mo_attr.ca_eakey, (char*)name, strlen(name));

	rc = m0t1fs_mds_cob_delxattr(csb, &mo, &rep_fop);
	if (rc == -ENOENT)
		rc = M0_ERR(-ENODATA);

	m0_fop_put0_lock(rep_fop);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
static int m0t1fs_fid_create(struct inode     *dir,
			     struct dentry    *dentry,
			     umode_t           mode,
			     bool              excl)
#else
static int m0t1fs_fid_create(struct inode     *dir,
			     struct dentry    *dentry,
			     int               mode,
			     struct nameidata *nd)
#endif
{
	return M0_ERR(-EOPNOTSUPP);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
static int m0t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 umode_t           mode,
			 bool              excl)
#else
static int m0t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 int               mode,
			 struct nameidata *nd)
#endif
{
	struct super_block       *sb  = dir->i_sb;
	struct m0t1fs_sb         *csb = M0T1FS_SB(sb);
	struct m0t1fs_inode      *ci;
	struct m0t1fs_mdop        mo;
	struct inode             *inode;
	struct m0_fid             new_fid;
	struct m0_fid             gfid;
	int                       rc;
	struct m0_fop            *rep_fop = NULL;

	M0_THREAD_ENTER;
	M0_ENTRY();

	M0_LOG(M0_DEBUG, "Creating \"%s\" in pdir %lu "FID_F,
	       (char*)dentry->d_name.name, dir->i_ino,
	       FID_P(m0t1fs_inode_fid(M0T1FS_I(dir))));

	inode = new_inode(sb);
	if (inode == NULL)
		return M0_ERR(-ENOMEM);
	m0t1fs_fs_lock(csb);
	if (csb->csb_oostore) {
		rc = m0_fid_sscanf_simple(dentry->d_name.name, &new_fid);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "Cannot parse fid \"%s\" in oostore",
					 (char*)dentry->d_name.name);
			goto out;
		}
	} else {
		m0t1fs_fid_alloc(csb, &new_fid);
	}
	m0_fid_gob_make(&gfid, new_fid.f_container, new_fid.f_key);
	M0_LOG(M0_DEBUG, "New fid = "FID_F, FID_P(&new_fid));
	inode->i_ino = m0_fid_hash(&gfid);
	ci = M0T1FS_I(inode);
	ci->ci_fid = gfid;

	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			inode->i_mode |= S_ISGID;
	} else
		inode->i_gid = current_fsgid();
	inode->i_mtime  = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	inode->i_blocks = 0;
	if (S_ISDIR(mode)) {
		inode->i_op  = &m0t1fs_dir_inode_operations;
		inode->i_fop = &m0t1fs_dir_file_operations;
		inc_nlink(inode);  /* one more link (".") for directories */
	} else {
		inode->i_op             = &m0t1fs_reg_inode_operations;
		inode->i_fop            = &m0t1fs_reg_file_operations;
		inode->i_mapping->a_ops = &m0t1fs_aops;
	}

	ci->ci_layout_id = M0_DEFAULT_LAYOUT_ID; /* layout id for new file */
	m0t1fs_file_lock_init(ci, csb);
	rc = m0t1fs_inode_layout_init(ci);
	if (rc != 0)
		goto out;

	M0_SET0(&mo);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	mo.mo_attr.ca_uid       = from_kuid(current_user_ns(), inode->i_uid);
	mo.mo_attr.ca_gid       = from_kgid(current_user_ns(), inode->i_gid);
#else
	mo.mo_attr.ca_uid       = inode->i_uid;
	mo.mo_attr.ca_gid       = inode->i_gid;
#endif
	mo.mo_attr.ca_atime     = inode->i_atime.tv_sec;
	mo.mo_attr.ca_ctime     = inode->i_ctime.tv_sec;
	mo.mo_attr.ca_mtime     = inode->i_mtime.tv_sec;
	mo.mo_attr.ca_mode      = inode->i_mode;
	mo.mo_attr.ca_blocks    = inode->i_blocks;
	mo.mo_attr.ca_pfid      = *m0t1fs_inode_fid(M0T1FS_I(dir));
	mo.mo_attr.ca_tfid      = *m0t1fs_inode_fid(ci);
	mo.mo_attr.ca_pver      = csb->csb_pool_version->pv_id;
	mo.mo_attr.ca_lid       = ci->ci_layout_id;
	mo.mo_attr.ca_nlink     = inode->i_nlink;
	mo.mo_attr.ca_valid     = (M0_COB_UID    | M0_COB_GID   | M0_COB_ATIME |
				   M0_COB_CTIME  | M0_COB_MTIME | M0_COB_MODE  |
				   M0_COB_BLOCKS | M0_COB_SIZE  | M0_COB_LID   |
				   M0_COB_NLINK);
	m0_buf_init(&mo.mo_attr.ca_name, (char*)dentry->d_name.name,
		    dentry->d_name.len);

	if (!csb->csb_oostore) {
		rc = m0t1fs_mds_cob_create(csb, &mo, &rep_fop);
		if (rc != 0)
			goto out;
	}
	if (S_ISREG(mode)) {
		rc = m0t1fs_component_objects_op(ci, &mo, m0t1fs_ios_cob_create);
		if (rc != 0)
			goto out;
	}

	if (insert_inode_locked(inode) < 0) {
		M0_LOG(M0_ERROR, "Duplicate inode: "FID_F, FID_P(&gfid));
		rc = M0_ERR(-EIO);
		goto out;
	}

	m0_fop_put0_lock(rep_fop);
	m0t1fs_fs_unlock(csb);
	unlock_new_inode(inode);
	M0_ADDB2_ADD(M0_AVI_FS_CREATE,
		     new_fid.f_container, new_fid.f_key, mode, 0);
	mark_inode_dirty(dir);
	d_instantiate(dentry, inode);
	return M0_RC(0);
out:
	m0_fop_put0_lock(rep_fop);
	clear_nlink(inode);
	m0t1fs_fs_unlock(csb);
	make_bad_inode(inode);
	iput(inode);
	M0_ADDB2_ADD(M0_AVI_FS_CREATE,
		     new_fid.f_container, new_fid.f_key, mode, rc);
	return M0_ERR(rc);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
static int m0t1fs_fid_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
#else
static int m0t1fs_fid_mkdir(struct inode *dir, struct dentry *dentry, int mode)
#endif
{
	M0_THREAD_ENTER;
	return m0t1fs_fid_create(dir, dentry, mode | S_IFDIR, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
static int m0t1fs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
#else
static int m0t1fs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
#endif
{
	M0_THREAD_ENTER;
	return m0t1fs_create(dir, dentry, mode | S_IFDIR, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
static struct dentry *m0t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    unsigned int      flags)
#else
static struct dentry *m0t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    struct nameidata *nd)
#endif
{
	struct m0t1fs_sb         *csb;
	struct inode             *inode = NULL;
	struct m0_fop_lookup_rep *rep = NULL;
	struct m0t1fs_mdop        mo;
	int                       rc;
	struct m0_fop            *rep_fop = NULL;

	M0_THREAD_ENTER;
	M0_ENTRY();

	m0_addb2_add(M0_AVI_FS_LOOKUP, M0_ADDB2_OBJ(&M0T1FS_I(dir)->ci_fid));
	csb = M0T1FS_SB(dir->i_sb);

	if (dentry->d_name.len > csb->csb_namelen) {
		M0_LEAVE("ERR_PTR: %p", ERR_PTR(-ENAMETOOLONG));
		return ERR_PTR(-ENAMETOOLONG);
	}

	M0_LOG(M0_INFO, "Name: \"%s\"", (char*)dentry->d_name.name);

	m0t1fs_fs_lock(csb);
	if (csb->csb_oostore) {
		struct m0_fid     new_fid;
		struct m0_fid     gfid;
		struct m0_fop_cob body;

		rc = m0_fid_sscanf_simple(dentry->d_name.name, &new_fid);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "Cannot parse fid \"%s\" in oostore",
					 (char*)dentry->d_name.name);
			m0t1fs_fs_unlock(csb);
			return ERR_PTR(-EINVAL);
		}
		m0_fid_gob_make(&gfid, new_fid.f_container, new_fid.f_key);
		body.b_valid = (M0_COB_MODE | M0_COB_LID);
		body.b_lid = M0_DEFAULT_LAYOUT_ID;
		body.b_mode = S_IFREG;
		inode = m0t1fs_iget(dir->i_sb, &gfid, &body);
		if (IS_ERR(inode)) {
			M0_LEAVE("ERROR: %p", ERR_CAST(inode));
			m0t1fs_fs_unlock(csb);
			return ERR_CAST(inode);
		}
		rc = m0t1fs_cob_getattr(inode);
		if (rc != 0) {
			make_bad_inode(inode);
			iput(inode);
			inode = NULL;
		}
		goto out;
	}

	M0_SET0(&mo);
	mo.mo_attr.ca_pfid = *m0t1fs_inode_fid(M0T1FS_I(dir));
	m0_buf_init(&mo.mo_attr.ca_name, (char*)dentry->d_name.name,
		    dentry->d_name.len);
	rc = m0t1fs_mds_cob_lookup(csb, &mo, &rep_fop);
	if (rc == 0) {
		rep = m0_fop_data(rep_fop);
		mo.mo_attr.ca_tfid = rep->l_body.b_tfid;
		inode = m0t1fs_iget(dir->i_sb, &mo.mo_attr.ca_tfid,
				    &rep->l_body);
		if (IS_ERR(inode)) {
			M0_LEAVE("ERROR: %p", ERR_CAST(inode));
			m0_fop_put0_lock(rep_fop);
			m0t1fs_fs_unlock(csb);
			return ERR_CAST(inode);
		}
	}
	m0_fop_put0_lock(rep_fop);
out:
	m0t1fs_fs_unlock(csb);
	M0_LEAVE();

	return d_splice_alias(inode, dentry);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
static struct dentry *m0t1fs_fid_lookup(struct inode     *dir,
					struct dentry    *dentry,
					unsigned int      flags)
#else
static struct dentry *m0t1fs_fid_lookup(struct inode     *dir,
					struct dentry    *dentry,
					struct nameidata *nd)
#endif
{
	struct m0_fid fid;
	int rc;

	M0_THREAD_ENTER;
	rc = m0_fid_sscanf_simple(dentry->d_name.name, &fid);
	if (rc != 0) {
		M0_LEAVE("Cannot parse fid \"%s\"", (char*)dentry->d_name.name);
		return ERR_PTR(rc);
	}
	m0_fid_tassume(&fid, &m0_file_fid_type);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	return m0t1fs_lookup(dir, dentry, flags);
#else
	return m0t1fs_lookup(dir, dentry, nd);
#endif
}


struct m0_dirent *dirent_next(struct m0_dirent *ent)
{
	return  ent->d_reclen > 0 ? (void *)ent + ent->d_reclen : NULL;
}

struct m0_dirent *dirent_first(struct m0_fop_readdir_rep *rep)
{
	struct m0_dirent *ent = (struct m0_dirent *)rep->r_buf.b_addr;
	return ent->d_namelen > 0 ? ent : NULL;
}

static int m0t1fs_opendir(struct inode *inode, struct file *file)
{
	struct m0t1fs_filedata *fd;
	M0_THREAD_ENTER;
	M0_ENTRY();

	M0_ALLOC_PTR(fd);
	if (fd == NULL)
		return M0_ERR(-ENOMEM);
	file->private_data = fd;

	/** Setup readdir initial pos with "." and max possible namelen. */
	fd->fd_dirpos = m0_alloc(M0T1FS_SB(inode->i_sb)->csb_namelen);
	if (fd->fd_dirpos == NULL) {
		m0_free(fd);
		return M0_ERR(-ENOMEM);
	}
	m0_bitstring_copy(fd->fd_dirpos, ".", 1);
	fd->fd_direof    = 0;
	fd->fd_mds_index = 0;;
	return 0;
}

static int m0t1fs_fid_opendir(struct inode *inode, struct file *file)
{
	M0_THREAD_ENTER;
	return m0t1fs_opendir(inode, file);
}

static int m0t1fs_releasedir(struct inode *inode, struct file *file)
{
	struct m0t1fs_filedata *fd = file->private_data;
	M0_THREAD_ENTER;
	M0_ENTRY();

	m0_free(fd->fd_dirpos);
	m0_free0(&file->private_data);
	return 0;
}

static int m0t1fs_fid_releasedir(struct inode *inode, struct file *file)
{
	M0_THREAD_ENTER;
	return m0t1fs_releasedir(inode, file);
}

static int m0t1fs_fid_readdir(struct file *f,
			      void        *buf,
			      filldir_t    filldir)
{
	M0_THREAD_ENTER;
	return M0_RC(0);
}

static int m0t1fs_readdir(struct file *f,
			  void        *buf,
			  filldir_t    filldir)
{
	struct m0t1fs_inode             *ci;
	struct m0t1fs_mdop               mo;
	struct m0t1fs_sb                *csb;
	struct m0_fop_readdir_rep       *rep;
	struct dentry                   *dentry;
	struct inode                    *dir;
	struct m0_dirent                *ent;
	struct m0t1fs_filedata          *fd;
	int                              type;
	ino_t                            ino;
	int                              i;
	int                              rc;
	int                              over;
	bool                             dot_filled = false;
	bool                             dotdot_filled = false;
	struct m0_fop                   *rep_fop;

	M0_THREAD_ENTER;
	M0_ENTRY();

	dentry = f->f_path.dentry;
	dir    = dentry->d_inode;
	ci     = M0T1FS_I(dir);
	csb    = M0T1FS_SB(dir->i_sb);
	i      = f->f_pos;

	if (csb->csb_oostore)
		return M0_RC(0);
	fd = f->private_data;
	if (fd->fd_direof)
		return M0_RC(0);

	m0t1fs_fs_lock(csb);

switch_mds:
	M0_SET0(&mo);
	/**
	   In case that readdir will be interrupted by enomem situation
	   (filldir fails) on big dir and finishes before eof is reached,
	   it has chance to start from there. This way f->f_pos and string
	   readdir pos are in sync.
	 */
	m0_buf_init(&mo.mo_attr.ca_name, m0_bitstring_buf_get(fd->fd_dirpos),
		    m0_bitstring_len_get(fd->fd_dirpos));
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);
	mo.mo_use_hint  = true;
	mo.mo_hash_hint = fd->fd_mds_index;

	do {
		M0_LOG(M0_DEBUG, "readdir from position \"%*s\"@ mds%d",
		       (int)mo.mo_attr.ca_name.b_nob,
		       (char *)mo.mo_attr.ca_name.b_addr,
			mo.mo_hash_hint);

		rc = m0t1fs_mds_cob_readdir(csb, &mo, &rep_fop);
		rep = m0_fop_data(rep_fop);
		if (rc < 0) {
			M0_LOG(M0_ERROR,
			       "Failed to read dir from pos \"%*s\". Error %d",
			       (int)mo.mo_attr.ca_name.b_nob,
			       (char *)mo.mo_attr.ca_name.b_addr, rc);
			goto out;
		}

		for (ent = dirent_first(rep); ent != NULL;
		     ent = dirent_next(ent)) {
			if (ent->d_namelen == 1 &&
			    memcmp(ent->d_name, ".", 1) == 0) {
				if (dot_filled)
					continue;
				ino = dir->i_ino;
				type = DT_DIR;
				dot_filled = true;
			} else if (ent->d_namelen == 2 &&
				   memcmp(ent->d_name, "..", 2) == 0) {
				if (dotdot_filled)
					continue;
				ino = parent_ino(dentry);
				type = DT_DIR;
				dotdot_filled = true;
			} else {
				/**
				 * TODO: Entry type is unknown and ino is
				 * pretty random, should be fixed later.
				 */
				ino = ++i;
				type = DT_UNKNOWN;
			}

			M0_LOG(M0_DEBUG, "filled off %lu ino %lu name"
			       " \"%.*s\", ino %lu, type %d",
			       (unsigned long)f->f_pos, (unsigned long)ino,
			       ent->d_namelen, (char *)ent->d_name,
			       ino, type);

			over = filldir(buf, ent->d_name, ent->d_namelen,
				       f->f_pos, ino, type);
			if (over) {
				rc = 0;
				M0_LOG(M0_DEBUG, "full");
				goto out;
			}
			f->f_pos++;
		}
		m0_bitstring_copy(fd->fd_dirpos, rep->r_end.s_buf,
				  rep->r_end.s_len);
		m0_buf_init(&mo.mo_attr.ca_name,
			    m0_bitstring_buf_get(fd->fd_dirpos),
			    m0_bitstring_len_get(fd->fd_dirpos));

		M0_LOG(M0_DEBUG, "set position to \"%*s\" rc == %d",
		       (int)mo.mo_attr.ca_name.b_nob,
		       (char *)mo.mo_attr.ca_name.b_addr, rc);
		m0_fop_put0_lock(rep_fop);
		/*
		 * Return codes for m0t1fs_mds_cob_readdir() are the following:
		 * - <0 - some error occured;
		 * - >0 - EOF signaled by mdservice, some number of entries
		 *        available;
		 * -  0 - no error, some number of entries is sent by mdservice.
		 */
	} while (rc == 0);
	/*
	 * EOF from one mdservice is detected. Switch to another mds.
	 */
	if (++fd->fd_mds_index < csb->csb_pools_common.pc_nr_svcs[M0_CST_MDS]) {
		m0_bitstring_copy(fd->fd_dirpos, ".", 1);
		M0_LOG(M0_DEBUG, "switch to mds %d", fd->fd_mds_index);
		goto switch_mds;
	}

	/*
	 * EOF detected from all mds. Set return code to 0 to make vfs happy.
	 */
	fd->fd_direof = 1;
	rc = 0;
out:
	if (rc != 0)
		m0_fop_put0_lock(rep_fop);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

static int m0t1fs_fid_link(struct dentry *old, struct inode *dir,
			   struct dentry *new)
{
	M0_THREAD_ENTER;
	return M0_ERR(-EOPNOTSUPP);
}

static int m0t1fs_link(struct dentry *old, struct inode *dir,
		       struct dentry *new)
{
	struct m0t1fs_sb                *csb;
	struct m0t1fs_mdop               mo;
	struct m0t1fs_inode             *ci;
	struct inode                    *inode;
	struct timespec                  now;
	int                              rc;
	struct m0_fop                   *rep_fop;

	M0_THREAD_ENTER;
	/*
	 * file -> mds is mapped by hash of filename.
	 * Link will create a new file entry in dir, but object may
	 * be on another mds. So link is disabled until this problem is solved.
	 */
	return M0_ERR(-EOPNOTSUPP);

	inode = old->d_inode;
	ci    = M0T1FS_I(inode);
	csb   = M0T1FS_SB(inode->i_sb);

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	now = CURRENT_TIME_SEC;
	mo.mo_attr.ca_pfid  = *m0t1fs_inode_fid(M0T1FS_I(dir));
	mo.mo_attr.ca_tfid  = *m0t1fs_inode_fid(ci);
	mo.mo_attr.ca_nlink = inode->i_nlink + 1;
	mo.mo_attr.ca_ctime = now.tv_sec;
	mo.mo_attr.ca_valid = (M0_COB_CTIME | M0_COB_NLINK);
	m0_buf_init(&mo.mo_attr.ca_name, (char*)new->d_name.name,
		    new->d_name.len);

	rc = m0t1fs_mds_cob_link(csb, &mo, &rep_fop);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "mdservive link fop failed: %d", rc);
		goto out;
	}

	inode_inc_link_count(inode);
	inode->i_ctime = now;
	atomic_inc(&inode->i_count);
	d_instantiate(new, inode);
	mark_inode_dirty(dir);

out:
	m0_fop_put0_lock(rep_fop);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

static int m0t1fs_fid_unlink(struct inode *dir, struct dentry *dentry)
{
	M0_THREAD_ENTER;
	return M0_ERR(-EOPNOTSUPP);
}

static int m0t1fs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct m0t1fs_sb                *csb;
	struct inode                    *inode;
	struct m0t1fs_inode             *ci;
	struct m0t1fs_mdop               mo;
	struct timespec                  now;
	int                              rc;
	struct m0_fop                   *lookup_rep_fop = NULL;
	struct m0_fop                   *unlink_rep_fop = NULL;
	struct m0_fop                   *setattr_rep_fop = NULL;

	M0_THREAD_ENTER;
	M0_ENTRY();

	M0_LOG(M0_INFO, "Name: \"%s\"", (char*)dentry->d_name.name);

	inode = dentry->d_inode;
	csb   = M0T1FS_SB(inode->i_sb);
	ci    = M0T1FS_I(inode);

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	now = CURRENT_TIME_SEC;
	mo.mo_attr.ca_pfid  = *m0t1fs_inode_fid(M0T1FS_I(dir));
	mo.mo_attr.ca_tfid  = *m0t1fs_inode_fid(ci);
	mo.mo_attr.ca_nlink = inode->i_nlink - 1;
	mo.mo_attr.ca_ctime = now.tv_sec;
	mo.mo_attr.ca_valid = (M0_COB_NLINK | M0_COB_CTIME);
	m0_buf_init(&mo.mo_attr.ca_name,
		    (char*)dentry->d_name.name, dentry->d_name.len);

	if (csb->csb_oostore) {
		rc = m0t1fs_component_objects_op(ci, &mo, m0t1fs_ios_cob_delete);
		if (rc != 0)
			M0_LOG(M0_ERROR, "ioservice delete fop failed: %d", rc);
		m0t1fs_fs_unlock(csb);
		return M0_RC(rc);
	}

	rc = m0t1fs_mds_cob_lookup(csb, &mo, &lookup_rep_fop);
	if (rc != 0)
		goto out;

	/* @todo: According to MM, a hash function will be used to choose
	 * a list of extra ioservices, on which "meta component objects" will
	 * be created. For unlink, it is similar to that:
	 *     rc = m0t1fs_component_objects_op(ci, &mo, m0t1fs_ios_cob_delete);
	 * That will be done in separated MM tasks.
	 */
	rc = m0t1fs_mds_cob_unlink(csb, &mo, &unlink_rep_fop);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "mdservive unlink fop failed: %d", rc);
		goto out;
	}

	if (mo.mo_attr.ca_nlink == 0) {
		rc = m0t1fs_component_objects_op(ci, &mo, m0t1fs_ios_cob_delete);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "ioservice delete fop failed: %d", rc);
			goto out;
		}
	}

	/** Update ctime and mtime on parent dir. */
	M0_SET0(&mo);
	mo.mo_attr.ca_tfid  = *m0t1fs_inode_fid(M0T1FS_I(dir));
	mo.mo_attr.ca_ctime = now.tv_sec;
	mo.mo_attr.ca_mtime = now.tv_sec;
	mo.mo_attr.ca_valid = (M0_COB_CTIME | M0_COB_MTIME);
	m0_buf_init(&mo.mo_attr.ca_name,
		    (char*)dentry->d_name.name, dentry->d_name.len);

	rc = m0t1fs_mds_cob_setattr(csb, &mo, &setattr_rep_fop);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "Setattr on parent dir failed with %d", rc);
		goto out;
	}

	inode->i_ctime = dir->i_ctime = dir->i_mtime = now;
	inode_dec_link_count(inode);
	mark_inode_dirty(dir);
out:
	m0_fop_put0_lock(lookup_rep_fop);
	m0_fop_put0_lock(unlink_rep_fop);
	m0_fop_put0_lock(setattr_rep_fop);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

static int m0t1fs_fid_rmdir(struct inode *dir, struct dentry *dentry)
{
	M0_THREAD_ENTER;
	return M0_ERR(-EOPNOTSUPP);
}

static int m0t1fs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int rc;

	M0_THREAD_ENTER;
	M0_ENTRY();
	rc = m0t1fs_unlink(dir, dentry);
	if (rc == 0) {
		inode_dec_link_count(dentry->d_inode);
		drop_nlink(dir);
	}
	return M0_RC(rc);
}

M0_INTERNAL int m0t1fs_fid_getattr(struct vfsmount *mnt, struct dentry *dentry,
				   struct kstat *stat)
{
	struct m0t1fs_sb    *csb;
	struct inode        *inode;
	struct m0_fop_cob   *body;
	struct m0t1fs_inode *ci;
	int                  rc;

	M0_THREAD_ENTER;
	M0_ENTRY();

	M0_LOG(M0_INFO, "Name: \"%s\"", (char*)dentry->d_name.name);

	inode = dentry->d_inode;
	csb   = M0T1FS_SB(inode->i_sb);
	ci    = M0T1FS_I(inode);

	m0t1fs_fs_lock(csb);

	/* Return cached attributes for files in .mero/fid dir */
	body = &csb->csb_virt_body;

	/* Update inode fields with data from @getattr_rep or cached attrs */
	m0t1fs_inode_update(inode, body);
	if (ci->ci_layout_id != body->b_lid) {
		/* layout for this file is changed. */

		M0_ASSERT(ci->ci_layout_instance != NULL);
		m0_layout_instance_fini(ci->ci_layout_instance);

		ci->ci_layout_id = body->b_lid;
		rc = m0t1fs_inode_layout_init(ci);
		M0_ASSERT(rc == 0);
	}

	/** Now its time to return inode stat data to user. */
	stat->dev = inode->i_sb->s_dev;
	stat->ino = inode->i_ino;
	stat->mode = inode->i_mode;
	stat->nlink = inode->i_nlink;
	stat->uid = inode->i_uid;
	stat->gid = inode->i_gid;
	stat->rdev = inode->i_rdev;
	stat->atime = inode->i_atime;
	stat->mtime = inode->i_mtime;
	stat->ctime = inode->i_ctime;
#ifdef HAVE_INODE_BLKSIZE
	stat->blksize = inode->i_blksize;
#else
	stat->blksize = 1 << inode->i_blkbits;
#endif
	stat->size = i_size_read(inode);
	stat->blocks = stat->blksize ? stat->size / stat->blksize : 0;
	m0t1fs_fs_unlock(csb);
	return M0_RC(0);
}

M0_INTERNAL int m0t1fs_getattr(struct vfsmount *mnt, struct dentry *dentry,
			       struct kstat *stat)
{
	struct m0_fop_getattr_rep *getattr_rep;
	struct m0t1fs_sb          *csb;
	struct inode              *inode;
	struct m0_fop_cob         *body;
	struct m0t1fs_inode       *ci;
	struct m0t1fs_mdop         mo;
	int                        rc = 0;
	struct m0_fop             *rep_fop = NULL;

	M0_THREAD_ENTER;
	M0_ENTRY();

	M0_LOG(M0_INFO, "Name: \"%s\"", (char*)dentry->d_name.name);

	inode = dentry->d_inode;
	csb   = M0T1FS_SB(inode->i_sb);
	ci    = M0T1FS_I(inode);

	m0t1fs_fs_lock(csb);

	if (csb->csb_oostore) {
		if (m0t1fs_inode_is_root(&ci->ci_inode))
			goto update;
		rc = m0t1fs_cob_getattr(inode);
		goto update;
	}

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);
	m0_buf_init(&mo.mo_attr.ca_name,
		    (char*)dentry->d_name.name, dentry->d_name.len);

	/**
	   @todo When we have rm locking working, this will be changed to
	   revalidate inode with checking cached lock. If lock is cached
	   (not canceled), which means inode did not change, then we don't
	   have to do getattr and can just use @inode cached data.
	*/
	rc = m0t1fs_mds_cob_getattr(csb, &mo, &rep_fop);
	if (rc != 0)
		goto out;
	getattr_rep = m0_fop_data(rep_fop);
	body = &getattr_rep->g_body;

	/** Update inode fields with data from @getattr_rep or cached attrs. */
	m0t1fs_inode_update(inode, body);
	if (!m0t1fs_inode_is_root(&ci->ci_inode) &&
	    ci->ci_layout_id != body->b_lid) {
		/* layout for this file is changed. */

		M0_ASSERT(ci->ci_layout_instance != NULL);
		m0_layout_instance_fini(ci->ci_layout_instance);

		ci->ci_layout_id = body->b_lid;
		rc = m0t1fs_inode_layout_init(ci);
		M0_ASSERT(rc == 0);
	}
update:
	/** Now its time to return inode stat data to user. */
	stat->dev = inode->i_sb->s_dev;
	stat->ino = inode->i_ino;
	stat->mode = inode->i_mode;
	stat->nlink = inode->i_nlink;
	stat->uid = inode->i_uid;
	stat->gid = inode->i_gid;
	stat->rdev = inode->i_rdev;
	stat->atime = inode->i_atime;
	stat->mtime = inode->i_mtime;
	stat->ctime = inode->i_ctime;
#ifdef HAVE_INODE_BLKSIZE
	stat->blksize = inode->i_blksize;
#else
	stat->blksize = 1 << inode->i_blkbits;
#endif
	stat->size = i_size_read(inode);
	stat->blocks = stat->blksize ? stat->size / stat->blksize : 0;
out:
	m0_fop_put0_lock(rep_fop);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

M0_INTERNAL int m0t1fs_size_update(struct dentry *dentry, uint64_t newsize)
{
	struct m0t1fs_sb                *csb;
	struct inode                    *inode;
	struct m0t1fs_inode             *ci;
	struct m0t1fs_mdop               mo;
	int                              rc = 0;
	struct m0_fop                   *rep_fop = NULL;

	inode = dentry->d_inode;
	csb   = M0T1FS_SB(inode->i_sb);
	ci    = M0T1FS_I(inode);

	m0t1fs_fs_lock(csb);
	if (csb->csb_oostore) {
		inode->i_size = newsize;
		goto out;
	}
	M0_SET0(&mo);
	mo.mo_attr.ca_tfid   = *m0t1fs_inode_fid(ci);
	mo.mo_attr.ca_size   = newsize;
	mo.mo_attr.ca_valid |= M0_COB_SIZE;
	m0_buf_init(&mo.mo_attr.ca_name, (char*)dentry->d_name.name,
		    dentry->d_name.len);

	rc = m0t1fs_mds_cob_setattr(csb, &mo, &rep_fop);
	if (rc != 0)
		goto out;
	inode->i_size = newsize;
out:
	m0_fop_put0_lock(rep_fop);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

M0_INTERNAL int m0t1fs_fid_setattr(struct dentry *dentry, struct iattr *attr)
{
	M0_THREAD_ENTER;
	return M0_ERR(-EOPNOTSUPP);
}

M0_INTERNAL int m0t1fs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct m0t1fs_sb     *csb;
	struct inode         *inode;
	struct m0t1fs_inode  *ci;
	struct m0t1fs_mdop    mo;
	int                   rc;
	struct m0_fop        *rep_fop;
	struct m0_rm_incoming rm_in;

	M0_THREAD_ENTER;
	M0_ENTRY();

	M0_LOG(M0_INFO, "Name: \"%s\"", (char*)dentry->d_name.name);

	inode = dentry->d_inode;
	csb   = M0T1FS_SB(inode->i_sb);
	ci    = M0T1FS_I(inode);

	rc = inode_change_ok(inode, attr);
	if (rc != 0)
		return M0_ERR(rc);

	m0t1fs_fs_lock(csb);
	rc = file_lock_acquire(&rm_in, ci);
	if (rc != 0) {
		m0t1fs_fs_unlock(csb);
		return M0_RC(rc);
	}
	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);
	m0_buf_init(&mo.mo_attr.ca_name, (char*)dentry->d_name.name,
		    dentry->d_name.len);

	if (attr->ia_valid & ATTR_CTIME) {
		mo.mo_attr.ca_ctime = attr->ia_ctime.tv_sec;
		mo.mo_attr.ca_valid |= M0_COB_CTIME;
	}

	if (attr->ia_valid & ATTR_MTIME) {
		mo.mo_attr.ca_mtime = attr->ia_mtime.tv_sec;
		mo.mo_attr.ca_valid |= M0_COB_MTIME;
	}

	if (attr->ia_valid & ATTR_ATIME) {
		mo.mo_attr.ca_atime = attr->ia_atime.tv_sec;
		mo.mo_attr.ca_valid |= M0_COB_ATIME;
	}

	if (attr->ia_valid & ATTR_SIZE) {
		mo.mo_attr.ca_size = attr->ia_size;
		mo.mo_attr.ca_valid |= M0_COB_SIZE;
	}

	if (attr->ia_valid & ATTR_MODE) {
		mo.mo_attr.ca_mode = attr->ia_mode;
		mo.mo_attr.ca_valid |= M0_COB_MODE;
	}

	if (attr->ia_valid & ATTR_UID) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		mo.mo_attr.ca_uid = from_kuid(current_user_ns(), attr->ia_uid);
#else
		mo.mo_attr.ca_uid = attr->ia_uid;
#endif
		mo.mo_attr.ca_valid |= M0_COB_UID;
	}

	if (attr->ia_valid & ATTR_GID) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		mo.mo_attr.ca_gid = from_kgid(current_user_ns(), attr->ia_gid);
#else
		mo.mo_attr.ca_gid = attr->ia_gid;
#endif
		mo.mo_attr.ca_valid |= M0_COB_GID;
	}

	/*
	 * Layout can be changed explicitly in setattr()
	 * to a new layout, e.g. to a composite layout in NBA.
	 * Check for that use case and update layout id for this
	 * file. When that happens, a special setattr() with
	 * valid layout id should be called.
	 */

	if (!csb->csb_oostore) {
		rc = m0t1fs_mds_cob_setattr(csb, &mo, &rep_fop);
		if (rc != 0) {
			m0_fop_put0_lock(rep_fop);
			goto out;
		}
	}
	rc = m0t1fs_component_objects_op(ci, &mo, m0t1fs_ios_cob_setattr);
	if (rc != 0)
		goto out;
	if (attr->ia_valid & ATTR_SIZE && attr->ia_size < inode->i_size) {
		rc = m0t1fs_component_objects_op(ci, &mo,
						 m0t1fs_ios_cob_truncate);
		if (rc != 0)
			goto out;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	setattr_copy(inode, attr);
#else
	rc = inode_setattr(inode, attr);
	if (rc != 0)
		goto out;
#endif
out:
	file_lock_release(&rm_in);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

static int file_lock_acquire(struct m0_rm_incoming *rm_in,
			     struct m0t1fs_inode *ci)
{
	int rc;

	rm_in->rin_want.cr_group_id = m0_rm_m0t1fs_group;
	m0_file_lock(&ci->ci_fowner, rm_in);
	m0_rm_owner_lock(&ci->ci_fowner);
	rc = m0_sm_timedwait(&rm_in->rin_sm, M0_BITS(RI_SUCCESS, RI_FAILURE),
			     M0_TIME_NEVER);
	m0_rm_owner_unlock(&ci->ci_fowner);
	rc = rc ? : rm_in->rin_rc;
	return rc;
}

static void file_lock_release(struct m0_rm_incoming *rm_in)
{
	M0_PRE(rm_in != NULL);
	m0_file_unlock(rm_in);
}
/**
   See "Containers and component objects" section in m0t1fs.h for
   more information.
 */
M0_INTERNAL struct m0_fid m0t1fs_ios_cob_fid(const struct m0t1fs_inode *ci,
					     int index)
{
	struct m0_layout_enum *le;
	struct m0_fid          fid;

	M0_PRE(ci->ci_layout_instance != NULL);
	M0_PRE(index >= 0);

	le = m0_layout_instance_to_enum(ci->ci_layout_instance);

	m0_layout_enum_get(le, index, m0t1fs_inode_fid(ci), &fid);

	M0_LOG(M0_DEBUG, "gob fid "FID_F" @%d = cob fid "FID_F,
	       FID_P(m0t1fs_inode_fid(ci)), index, FID_P(&fid));

	return fid;
}

static uint32_t m0t1fs_ios_cob_idx(const struct m0t1fs_inode *ci,
				   const struct m0_fid *gfid,
				   const struct m0_fid *cfid)
{
	uint32_t cob_idx;

	M0_PRE(ci->ci_layout_instance != NULL);
	M0_PRE(gfid != NULL);
	M0_PRE(cfid != NULL);

	cob_idx = m0_layout_enum_find(m0_layout_instance_to_enum(
				      ci->ci_layout_instance), gfid, cfid);
	M0_LOG(M0_DEBUG, "cob idx: %d for gob fid "FID_F" cob fid "FID_F"",
			 cob_idx, FID_P(gfid), FID_P(cfid));
	return cob_idx;
}

static int m0t1fs_component_objects_op(struct m0t1fs_inode *ci,
				       struct m0t1fs_mdop *mop,
				       int (*func)(struct cob_req *,
						   const struct m0t1fs_inode *,
						   const struct m0t1fs_mdop *,
						   int idx))
{
	struct m0t1fs_sb *csb;
	struct m0_fid     cob_fid;
	struct cob_req    cob_req;
	uint32_t          pool_width;
	uint32_t          N;
	uint32_t          K;
	int               i;
	int               rc = 0;
	uint32_t          cob_idx;
	const char       *op_name;

	M0_PRE(ci != NULL);
	M0_PRE(func != NULL);

	M0_ENTRY();

	op_name = (func == m0t1fs_ios_cob_create) ? "create" :
		(func == m0t1fs_ios_cob_delete) ? "delete" :
		  (func == m0t1fs_ios_cob_truncate) ? "truncate" : "setattr";

	M0_LOG(M0_DEBUG, "Component object %s for "FID_F,
			 op_name, FID_P(m0t1fs_inode_fid(ci)));

	csb = M0T1FS_SB(ci->ci_inode.i_sb);
	pool_width = csb->csb_pool_version->pv_attr.pa_P;
	N = csb->csb_pool_version->pv_attr.pa_N;
	K = csb->csb_pool_version->pv_attr.pa_K;
	M0_ASSERT(pool_width >= (N + (2 * K)));

	m0_semaphore_init(&cob_req.cr_sem, 0);

	cob_req.cr_deadline = m0_time_from_now(0, COB_REQ_DEADLINE);
	cob_req.cr_csb = csb;
	cob_req.cr_rc = 0;
	cob_req.cr_fid = *m0t1fs_inode_fid(ci);

	if (csb->csb_oostore && func != m0t1fs_ios_cob_truncate) {
		M0_LOG(M0_DEBUG,"oostore mode");
		mop->mo_cob_type = M0_COB_MD;
		/** Create, delete and setattr operations on meta data cobs on
		 * ioservices are performed.
		 */
		for (i = 0; i < csb->csb_pools_common.pc_md_redundancy; i++) {
			rc = func(&cob_req, ci, mop, i);
			if (rc != 0) {
				M0_LOG(M0_ERROR, "Cob %s "FID_F" failed with %d",
					op_name, FID_P(&cob_req.cr_fid), rc);
				goto out;
			}
		}
		if (func == m0t1fs_ios_cob_setattr)
			goto out;
		while (--i >= 0)
			m0_semaphore_down(&cob_req.cr_sem);
	}

	for (i = 0; i < pool_width; ++i) {
		cob_fid = m0t1fs_ios_cob_fid(ci, i);
		cob_idx = m0t1fs_ios_cob_idx(ci, m0t1fs_inode_fid(ci),
					     &cob_fid);
		M0_ASSERT(cob_idx != ~0);
		mop->mo_cob_type = M0_COB_IO;
		rc = func(&cob_req, ci, mop, i);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "Cob %s "FID_F" failed with %d",
					 op_name, FID_P(&cob_fid), rc);
			break;
		}
	}
out:
	while (--i >= 0)
		m0_semaphore_down(&cob_req.cr_sem);
	m0_semaphore_fini(&cob_req.cr_sem);
	M0_LOG(M0_DEBUG, "Cob %s "FID_F" with %d", op_name, FID_P(&cob_fid),
			rc ?: cob_req.cr_rc);

	return M0_RC(rc ?: cob_req.cr_rc);
}

static int m0t1fs_mds_cob_fop_populate(struct m0t1fs_sb         *csb,
				       const struct m0t1fs_mdop *mo,
				       struct m0_fop            *fop)
{
	struct m0_fop_create    *create;
	struct m0_fop_unlink    *unlink;
	struct m0_fop_link      *link;
	struct m0_fop_lookup    *lookup;
	struct m0_fop_getattr   *getattr;
	struct m0_fop_statfs    *statfs;
	struct m0_fop_setattr   *setattr;
	struct m0_fop_readdir   *readdir;
	struct m0_fop_setxattr  *setxattr;
	struct m0_fop_getxattr  *getxattr;
	struct m0_fop_listxattr *listxattr;
	struct m0_fop_delxattr  *delxattr;
	struct m0_fop_cob       *req;
	int                      rc = 0;

	switch (m0_fop_opcode(fop)) {
	case M0_MDSERVICE_CREATE_OPCODE:
		create = m0_fop_data(fop);
		req = &create->c_body;

		req->b_pfid = mo->mo_attr.ca_pfid;
		req->b_tfid = mo->mo_attr.ca_tfid;
		req->b_pver = mo->mo_attr.ca_pver;
		body_mem2wire(req, &mo->mo_attr, mo->mo_attr.ca_valid);
		rc = name_mem2wire(&create->c_name, &mo->mo_attr.ca_name);
		break;
	case M0_MDSERVICE_LINK_OPCODE:
		link = m0_fop_data(fop);
		req = &link->l_body;

		req->b_pfid = mo->mo_attr.ca_pfid;
		req->b_tfid = mo->mo_attr.ca_tfid;
		body_mem2wire(req, &mo->mo_attr, mo->mo_attr.ca_valid);
		rc = name_mem2wire(&link->l_name, &mo->mo_attr.ca_name);
		break;
	case M0_MDSERVICE_UNLINK_OPCODE:
		unlink = m0_fop_data(fop);
		req = &unlink->u_body;

		req->b_pfid = mo->mo_attr.ca_pfid;
		req->b_tfid = mo->mo_attr.ca_tfid;
		body_mem2wire(req, &mo->mo_attr, mo->mo_attr.ca_valid);
		rc = name_mem2wire(&unlink->u_name, &mo->mo_attr.ca_name);
		break;
	case M0_MDSERVICE_STATFS_OPCODE:
		statfs = m0_fop_data(fop);
		statfs->f_flags = 0;
		break;
	case M0_MDSERVICE_LOOKUP_OPCODE:
		lookup = m0_fop_data(fop);
		req = &lookup->l_body;

		req->b_pfid = mo->mo_attr.ca_pfid;
		rc = name_mem2wire(&lookup->l_name, &mo->mo_attr.ca_name);
		break;
	case M0_MDSERVICE_GETATTR_OPCODE:
		getattr = m0_fop_data(fop);
		req = &getattr->g_body;

		req->b_tfid = mo->mo_attr.ca_tfid;
		break;
	case M0_MDSERVICE_SETATTR_OPCODE:
		setattr = m0_fop_data(fop);
		req = &setattr->s_body;

		req->b_tfid = mo->mo_attr.ca_tfid;
		body_mem2wire(req, &mo->mo_attr, mo->mo_attr.ca_valid);
		break;
	case M0_MDSERVICE_READDIR_OPCODE:
		readdir = m0_fop_data(fop);
		req = &readdir->r_body;

		req->b_tfid = mo->mo_attr.ca_tfid;
		rc = name_mem2wire(&readdir->r_pos, &mo->mo_attr.ca_name);
		break;
	case M0_MDSERVICE_SETXATTR_OPCODE:
		setxattr = m0_fop_data(fop);
		req = &setxattr->s_body;

		req->b_tfid = mo->mo_attr.ca_tfid;
		rc = name_mem2wire(&setxattr->s_key, &mo->mo_attr.ca_eakey) ?:
			name_mem2wire(&setxattr->s_value,
				      &mo->mo_attr.ca_eaval);
		break;
	case M0_MDSERVICE_GETXATTR_OPCODE:
		getxattr = m0_fop_data(fop);
		req = &getxattr->g_body;

		req->b_tfid = mo->mo_attr.ca_tfid;
		rc = name_mem2wire(&getxattr->g_key, &mo->mo_attr.ca_eakey);
		break;
	case M0_MDSERVICE_LISTXATTR_OPCODE:
		listxattr = m0_fop_data(fop);
		req = &listxattr->l_body;

		req->b_tfid = mo->mo_attr.ca_tfid;
		break;
	case M0_MDSERVICE_DELXATTR_OPCODE:
		delxattr = m0_fop_data(fop);
		req = &delxattr->d_body;

		req->b_tfid = mo->mo_attr.ca_tfid;
		rc = name_mem2wire(&delxattr->d_key, &mo->mo_attr.ca_eakey);
		break;
	default:
		rc = M0_ERR(-ENOSYS);
		break;
	}

	return M0_RC(rc);
}

static int m0t1fs_mds_cob_op(struct m0t1fs_sb            *csb,
			     const struct m0t1fs_mdop    *mo,
			     struct m0_fop_type          *ftype,
			     struct m0_fop              **rep_fop)
{
	int                                rc;
	struct m0_fop                     *fop;
	struct m0_rpc_session             *session;
	union {
		struct m0_fop_create_rep    *create_rep;
		struct m0_fop_unlink_rep    *unlink_rep;
		struct m0_fop_rename_rep    *rename_rep;
		struct m0_fop_link_rep      *link_rep;
		struct m0_fop_setattr_rep   *setattr_rep;
		struct m0_fop_getattr_rep   *getattr_rep;
		struct m0_fop_statfs_rep    *statfs_rep;
		struct m0_fop_lookup_rep    *lookup_rep;
		struct m0_fop_open_rep      *open_rep;
		struct m0_fop_close_rep     *close_rep;
		struct m0_fop_readdir_rep   *readdir_rep;
		struct m0_fop_setxattr_rep  *setxattr_rep;
		struct m0_fop_getxattr_rep  *getxattr_rep;
		struct m0_fop_listxattr_rep *listxattr_rep;
		struct m0_fop_delxattr_rep  *delxattr_rep;
	} u;
	void                              *reply_data;
	struct m0_reqh_service_ctx        *ctx;
	struct m0_be_tx_remid             *remid = NULL;

	M0_PRE(csb != NULL);
	M0_PRE(mo != NULL);
	M0_PRE(rep_fop != NULL);
	M0_PRE(ftype != NULL);

	M0_ENTRY();

	*rep_fop = NULL;
	session = m0t1fs_filename_to_mds_session(csb,
						 mo->mo_attr.ca_name.b_addr,
						 mo->mo_attr.ca_name.b_nob,
						 mo->mo_use_hint,
						 mo->mo_hash_hint);
	M0_ASSERT(session != NULL);

	fop = m0_fop_alloc_at(session, ftype);
	if (fop == NULL) {
		rc = M0_ERR(-ENOMEM);
		M0_LOG(M0_ERROR, "m0_fop_alloc() failed with %d", rc);
		goto out;
	}

	rc = m0t1fs_mds_cob_fop_populate(csb, mo, fop);
	if (rc != 0) {
		M0_LOG(M0_ERROR,
		       "m0t1fs_mds_cob_fop_populate() failed with %d", rc);
		goto out;
	}

	M0_LOG(M0_DEBUG, "Send md operation %u to session %p (sid=%lu)",
			 m0_fop_opcode(fop), session,
			 (unsigned long)session->s_session_id);

	rc = m0_rpc_post_sync(fop, session, NULL, 0 /* deadline */);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "m0_rpc_post_sync(%x) failed with %d",
		       m0_fop_opcode(fop), rc);
		goto out;
	}

	*rep_fop = m0_rpc_item_to_fop(fop->f_item.ri_reply);
	m0_fop_get(*rep_fop);
	reply_data = m0_fop_data(*rep_fop);

	/**
	 * @todo remid can be found generically, outside of this switch through
	 * the use of 'm0_xcode_find()' - this function should be cleaned up
	 * later.
	 */
	switch (m0_fop_opcode(fop)) {
	case M0_MDSERVICE_CREATE_OPCODE:
		u.create_rep = reply_data;
		rc = u.create_rep->c_body.b_rc;
		remid = &u.create_rep->c_mod_rep.fmr_remid;
		break;
	case M0_MDSERVICE_STATFS_OPCODE:
		u.statfs_rep = reply_data;
		rc = u.statfs_rep->f_rc;
		break;
	case M0_MDSERVICE_LOOKUP_OPCODE:
		u.lookup_rep = reply_data;
		rc = u.lookup_rep->l_body.b_rc;
		break;
	case M0_MDSERVICE_LINK_OPCODE:
		u.link_rep = reply_data;
		rc = u.link_rep->l_body.b_rc;
		remid = &u.link_rep->l_mod_rep.fmr_remid;
		break;
	case M0_MDSERVICE_UNLINK_OPCODE:
		u.unlink_rep = reply_data;
		rc = u.unlink_rep->u_body.b_rc;
		remid = &u.unlink_rep->u_mod_rep.fmr_remid;
		break;
	case M0_MDSERVICE_RENAME_OPCODE:
		u.rename_rep = reply_data;
		rc = u.rename_rep->r_body.b_rc;
		remid = &u.rename_rep->r_mod_rep.fmr_remid;
		break;
	case M0_MDSERVICE_SETATTR_OPCODE:
		u.setattr_rep = reply_data;
		rc = u.setattr_rep->s_body.b_rc;
		remid = &u.setattr_rep->s_mod_rep.fmr_remid;
		break;
	case M0_MDSERVICE_GETATTR_OPCODE:
		u.getattr_rep = reply_data;
		rc = u.getattr_rep->g_body.b_rc;
		break;
	case M0_MDSERVICE_OPEN_OPCODE:
		u.open_rep = reply_data;
		rc = u.open_rep->o_body.b_rc;
		remid = &u.open_rep->o_mod_rep.fmr_remid;
		break;
	case M0_MDSERVICE_CLOSE_OPCODE:
		u.close_rep = reply_data;
		rc = u.close_rep->c_body.b_rc;
		break;
	case M0_MDSERVICE_READDIR_OPCODE:
		u.readdir_rep = reply_data;
		rc = u.readdir_rep->r_body.b_rc;
		break;
	case M0_MDSERVICE_SETXATTR_OPCODE:
		u.setxattr_rep = reply_data;
		rc = u.setxattr_rep->s_body.b_rc;
		remid = &u.setxattr_rep->s_mod_rep.fmr_remid;
		break;
	case M0_MDSERVICE_GETXATTR_OPCODE:
		u.getxattr_rep = reply_data;
		rc = u.getxattr_rep->g_body.b_rc;
		break;
	case M0_MDSERVICE_LISTXATTR_OPCODE:
		u.listxattr_rep = reply_data;
		rc = u.listxattr_rep->l_body.b_rc;
		break;
	case M0_MDSERVICE_DELXATTR_OPCODE:
		u.delxattr_rep = reply_data;
		rc = u.delxattr_rep->d_body.b_rc;
		remid = &u.delxattr_rep->d_mod_rep.fmr_remid;
		break;
	default:
		M0_LOG(M0_ERROR, "Unexpected fop opcode %x",
		       m0_fop_opcode(fop));
		rc = M0_ERR(-ENOSYS);
		goto out;
	}

	/* update pending transaction number */
	ctx = m0_reqh_service_ctx_from_session(session);
	if (remid != NULL)
		m0t1fs_fsync_record_update(ctx, csb, NULL, remid);

out:
	m0_fop_put0_lock(fop);
	return M0_RC(rc);
}

int m0t1fs_mds_statfs(struct m0t1fs_sb *csb, struct m0_fop **rep_fop)
{
	struct m0t1fs_mdop mo;
	M0_SET0(&mo);
	return m0t1fs_mds_cob_op(csb, &mo, &m0_fop_statfs_fopt, rep_fop);
}

int m0t1fs_mds_cob_create(struct m0t1fs_sb          *csb,
			  const struct m0t1fs_mdop  *mo,
			  struct m0_fop            **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_create_fopt, rep_fop);
}

int m0t1fs_mds_cob_unlink(struct m0t1fs_sb          *csb,
			  const struct m0t1fs_mdop  *mo,
			  struct m0_fop            **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_unlink_fopt, rep_fop);
}

int m0t1fs_mds_cob_link(struct m0t1fs_sb          *csb,
			const struct m0t1fs_mdop  *mo,
			struct m0_fop            **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_link_fopt, rep_fop);
}

int m0t1fs_mds_cob_lookup(struct m0t1fs_sb          *csb,
			  const struct m0t1fs_mdop  *mo,
			  struct m0_fop            **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_lookup_fopt, rep_fop);
}

int m0t1fs_mds_cob_getattr(struct m0t1fs_sb           *csb,
			   const struct m0t1fs_mdop   *mo,
			   struct m0_fop             **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_getattr_fopt, rep_fop);
}

int m0t1fs_mds_cob_setattr(struct m0t1fs_sb           *csb,
			   const struct m0t1fs_mdop   *mo,
			   struct m0_fop             **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_setattr_fopt, rep_fop);
}

int m0t1fs_mds_cob_readdir(struct m0t1fs_sb           *csb,
			   const struct m0t1fs_mdop   *mo,
			   struct m0_fop             **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_readdir_fopt, rep_fop);
}

int m0t1fs_mds_cob_setxattr(struct m0t1fs_sb            *csb,
			    const struct m0t1fs_mdop    *mo,
			    struct m0_fop              **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_setxattr_fopt, rep_fop);
}

int m0t1fs_mds_cob_getxattr(struct m0t1fs_sb            *csb,
			    const struct m0t1fs_mdop    *mo,
			    struct m0_fop              **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_getxattr_fopt, rep_fop);
}

int m0t1fs_mds_cob_listxattr(struct m0t1fs_sb             *csb,
			     const struct m0t1fs_mdop     *mo,
			     struct m0_fop               **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_listxattr_fopt, rep_fop);
}

int m0t1fs_mds_cob_delxattr(struct m0t1fs_sb            *csb,
			    const struct m0t1fs_mdop    *mo,
			    struct m0_fop              **rep_fop)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_delxattr_fopt, rep_fop);
}

static int m0t1fs_ios_cob_fop_populate(const struct m0t1fs_sb   *csb,
				       const struct m0t1fs_mdop *mop,
				       struct m0_fop            *fop,
				       const struct m0_fid      *cob_fid,
				       const struct m0_fid      *gob_fid,
				       uint32_t                  cob_idx)
{
	struct m0_fop_cob_common    *common;
	struct m0_poolmach_versions *cli;
	struct m0_poolmach_versions  curr;
	struct m0_fop_cob_truncate  *ct;

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type != NULL);
	M0_PRE(cob_fid != NULL);
	M0_PRE(gob_fid != NULL);

	M0_ENTRY();

	common = m0_cobfop_common_get(fop);
	M0_ASSERT(common != NULL);

	/* fill in the current client known version */
	m0_poolmach_current_version_get(&csb->csb_pool_version->pv_mach, &curr);
	cli = (struct m0_poolmach_versions*)&common->c_version;
	*cli = curr;
	body_mem2wire(&common->c_body, &mop->mo_attr, mop->mo_attr.ca_valid);

	common->c_gobfid = *gob_fid;
	common->c_cobfid = *cob_fid;
	common->c_pver   = csb->csb_pool_version->pv_id;
	common->c_cob_idx = cob_idx;
	common->c_cob_type = mop->mo_cob_type;

	if (m0_is_cob_truncate_fop(fop)) {
		ct = m0_fop_data(fop);
		ct->ct_size = mop->mo_attr.ca_size;
	}

	return M0_RC(0);
}

static void cfop_release(struct m0_ref *ref)
{
	struct m0_fop  *fop;
	struct cob_fop *cfop;

	M0_ENTRY();
	M0_PRE(ref != NULL);

	fop  = container_of(ref, struct m0_fop, f_ref);
	cfop = container_of(fop, struct cob_fop, c_fop);
	m0_fop_fini(fop);
	m0_free(cfop);

	M0_LEAVE();
}

static int m0t1fs_ios_cob_op(struct cob_req            *cr,
			     const struct m0t1fs_inode *ci,
			     const struct m0t1fs_mdop  *mop,
			     int                       idx,
			     struct m0_fop_type       *ftype)
{
	int                         rc;
	const struct m0t1fs_sb     *csb;
	struct m0_fid               cob_fid;
	const struct m0_fid        *gob_fid;
	uint32_t                    cob_idx;
	struct m0_fop              *fop;
	struct cob_fop             *cfop;
	struct m0_rpc_session      *session;

	M0_PRE(ci != NULL);
	M0_PRE(ftype != NULL);

	M0_ENTRY();

	csb = M0T1FS_SB(ci->ci_inode.i_sb);
	gob_fid = m0t1fs_inode_fid(ci);

	if (mop->mo_cob_type == M0_COB_MD) {
		session = m0_reqh_mdpool_service_index_to_session(
				&csb->csb_reqh, gob_fid, idx);
		m0_fid_convert_gob2cob(gob_fid, &cob_fid, 0);
		cob_idx = idx;
	} else {
		cob_fid = m0t1fs_ios_cob_fid(ci, idx);
		cob_idx = m0t1fs_ios_cob_idx(ci, gob_fid, &cob_fid);
		session = m0t1fs_container_id_to_session(csb,
				m0_fid_cob_device_id(&cob_fid));
	}
	M0_ASSERT(session != NULL);

	M0_ALLOC_PTR(cfop);
	if (cfop == NULL) {
		rc = M0_ERR(-ENOMEM);
		M0_LOG(M0_ERROR, "cob_fop malloc failed");
		goto out;
	}

	cfop->c_req = cr;
	fop = &cfop->c_fop;

	m0_fop_init(fop, ftype, NULL, cfop_release);
	rc = m0_fop_data_alloc(fop);
	if (rc != 0) {
		m0_fop_fini(fop);
		m0_free(cfop);
		M0_LOG(M0_ERROR, "cob_fop data malloc failed");
		goto out;
	}
	fop->f_item.ri_rmachine = m0_fop_session_machine(session);

	M0_ASSERT(m0_is_cob_create_fop(fop) || m0_is_cob_delete_fop(fop) ||
		  m0_is_cob_truncate_fop(fop) || m0_is_cob_setattr_fop(fop));

	rc = m0t1fs_ios_cob_fop_populate(csb, mop, fop, &cob_fid, gob_fid,
					 cob_idx);
	if (rc != 0)
		goto fop_put;

	M0_LOG(M0_DEBUG, "Send %s "FID_F" to session %p (sid=%lu)",
	       m0_fop_name(fop),
	       FID_P(&cob_fid), session, (unsigned long)session->s_session_id);

	fop->f_item.ri_session  = session;
	fop->f_item.ri_ops      = &cob_item_ops;
	fop->f_item.ri_deadline = cr->cr_deadline; /* pack fops */
	rc = m0_rpc_post(&fop->f_item);
	if (rc != 0)
		goto fop_put;

	return M0_RC(rc);
fop_put:
	m0_fop_put0_lock(fop);
out:
	return M0_RC(rc);
}

static int m0t1fs_ios_cob_create(struct cob_req *cr,
				 const struct m0t1fs_inode *inode,
				 const struct m0t1fs_mdop *mop,
				 int idx)
{
	return m0t1fs_ios_cob_op(cr, inode, mop, idx, &m0_fop_cob_create_fopt);
}

static int m0t1fs_ios_cob_delete(struct cob_req *cr,
				 const struct m0t1fs_inode *inode,
				 const struct m0t1fs_mdop *mop,
				 int idx)
{
	return m0t1fs_ios_cob_op(cr, inode, mop, idx, &m0_fop_cob_delete_fopt);
}

static int m0t1fs_ios_cob_truncate(struct cob_req *cr,
				   const struct m0t1fs_inode *inode,
			           const struct m0t1fs_mdop *mop,
				   int idx)
{
	return m0t1fs_ios_cob_op(cr, inode, mop, idx,
				 &m0_fop_cob_truncate_fopt);
}

static int m0t1fs_ios_cob_setattr(struct cob_req *cr,
				  const struct m0t1fs_inode *inode,
				  const struct m0t1fs_mdop *mop,
				  int idx)
{
	return m0t1fs_ios_cob_op(cr, inode, mop, idx, &m0_fop_cob_setattr_fopt);
}

M0_INTERNAL int m0t1fs_cob_getattr(struct inode *inode)
{
	const struct m0t1fs_sb          *csb;
	struct m0t1fs_inode             *ci;
	const struct m0_fid             *gob_fid;
	struct m0_fop                   *fop = NULL;
	struct m0_fop_cob               *body;
	struct m0_fid                    cob_fid;
	uint32_t                         cob_idx;
	int                              rc = 0;
	struct m0_cob_attr               attr;
	struct m0_rpc_session           *session;
	struct m0t1fs_mdop               mo;
	uint32_t                         i;
	struct m0_fop_cob_getattr_reply *getattr_rep;

	M0_ENTRY();

	csb = M0T1FS_SB(inode->i_sb);
	ci  = M0T1FS_I(inode);

	gob_fid = m0t1fs_inode_fid(ci);

	for (i = 0; i < csb->csb_pools_common.pc_md_redundancy; i++) {
		session = m0_reqh_mdpool_service_index_to_session(
				&csb->csb_reqh, gob_fid, i);
		M0_ASSERT(session != NULL);
		m0_fid_convert_gob2cob(gob_fid, &cob_fid, i);
		cob_idx = i;

		M0_LOG(M0_DEBUG, "Getattr for "FID_F "~" FID_F,
			 FID_P(gob_fid), FID_P(&cob_fid));

		fop = m0_fop_alloc_at(session, &m0_fop_cob_getattr_fopt);
		if (fop == NULL) {
			rc = -ENOMEM;
			M0_LOG(M0_ERROR, "m0_fop_alloc() failed with %d", rc);
			return M0_RC(rc);
		}

		M0_SET0(&mo);
		mo.mo_attr.ca_pfid = *gob_fid;
		mo.mo_attr.ca_tfid = *gob_fid;
		mo.mo_cob_type = M0_COB_MD;
		m0t1fs_ios_cob_fop_populate(csb, &mo, fop, &cob_fid, gob_fid,
					    cob_idx);

		M0_LOG(M0_DEBUG, "Send cob operation %u %s to session %p"
				"(sid=%lu)", m0_fop_opcode(fop),
				m0_fop_name(fop), session,
				(unsigned long)session->s_session_id);

		rc = m0_rpc_post_sync(fop, session, NULL, 0 /* deadline */);
		if (rc != 0) {
			m0_fop_put0_lock(fop);
			return M0_RC(rc);
		}
		getattr_rep = m0_fop_data(m0_rpc_item_to_fop(
					  fop->f_item.ri_reply));
		rc = getattr_rep->cgr_rc;
		M0_LOG(M0_DEBUG, "getattr returned: %d", rc);

		if (rc == M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH) {
			struct m0_fop_cob_op_rep_common *r_common;
			struct m0_fv_event              *event;
			uint32_t                         i;
			/* Retrieve the latest server version and updates
			 * and apply to the client's copy.
			 */
			rc = 0;
			r_common = &getattr_rep->cgr_common;
			for (i = 0; i < r_common->cor_fv_updates.fvu_count; ++i) {
				event = &r_common->cor_fv_updates.fvu_events[i];
				m0_poolmach_state_transit(&csb->csb_pool_version->pv_mach,
					(struct m0_poolmach_event*)event, NULL);
			}
		}

		if (rc == 0) {
			body = &getattr_rep->cgr_body;
			body_wire2mem(&attr, body);
			m0_dump_cob_attr(&attr);
			/* Update inode fields with data from @getattr_rep. */
			m0t1fs_inode_update(inode, body);
			if (ci->ci_layout_id != body->b_lid) {
				/* Change the layout for the file. */
				ci->ci_layout_id = body->b_lid;
				rc = m0t1fs_inode_layout_init(ci);
			}
			m0_fop_put0_lock(fop);
			break;
		}
		m0_fop_put0_lock(fop);
	}
	return M0_RC(rc);
}

M0_INTERNAL int m0t1fs_cob_setattr(struct inode *inode, struct m0t1fs_mdop *mo)
{
	struct m0t1fs_inode *ci = M0T1FS_I(inode);
	struct m0t1fs_sb    *csb = M0T1FS_SB(inode->i_sb);
	int                  rc;
	M0_ENTRY();

	m0t1fs_fs_lock(csb);
	/* Updating size to ios. */
	rc = m0t1fs_component_objects_op(ci, mo, m0t1fs_ios_cob_setattr);
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}


const struct file_operations m0t1fs_dir_file_operations = {
	.read    = generic_read_dir,    /* provided by linux kernel */
	.open    = m0t1fs_opendir,
	.release = m0t1fs_releasedir,
	.readdir = m0t1fs_readdir,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	.fsync   = generic_file_fsync,  /* provided by linux kernel */
#else
	.fsync   = simple_fsync,	/* provided by linux kernel */
#endif
	.llseek  = generic_file_llseek, /* provided by linux kernel */
};

const struct file_operations m0t1fs_fid_dir_file_operations = {
	.read    = generic_read_dir,    /* provided by linux kernel */
	.open    = m0t1fs_fid_opendir,
	.release = m0t1fs_fid_releasedir,
	.readdir = m0t1fs_fid_readdir,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	.fsync   = generic_file_fsync,  /* provided by linux kernel */
#else
	.fsync   = simple_fsync,	/* provided by linux kernel */
#endif
	.llseek  = generic_file_llseek, /* provided by linux kernel */
};

const struct inode_operations m0t1fs_dir_inode_operations = {
	.create         = m0t1fs_create,
	.lookup         = m0t1fs_lookup,
	.unlink         = m0t1fs_unlink,
	.link           = m0t1fs_link,
	.mkdir          = m0t1fs_mkdir,
	.rmdir          = m0t1fs_rmdir,
	.setattr        = m0t1fs_setattr,
	.getattr        = m0t1fs_getattr,
	.setxattr       = m0t1fs_setxattr,
	.getxattr       = m0t1fs_getxattr,
	.listxattr      = m0t1fs_listxattr,
	.removexattr    = m0t1fs_removexattr
};

const struct inode_operations m0t1fs_fid_dir_inode_operations = {
	.create         = m0t1fs_fid_create,
	.lookup         = m0t1fs_fid_lookup,
	.unlink         = m0t1fs_fid_unlink,
	.link           = m0t1fs_fid_link,
	.mkdir          = m0t1fs_fid_mkdir,
	.rmdir          = m0t1fs_fid_rmdir,
	.setattr        = m0t1fs_fid_setattr,
	.getattr        = m0t1fs_fid_getattr,
	.setxattr       = m0t1fs_fid_setxattr,
	.getxattr       = m0t1fs_fid_getxattr,
	.listxattr      = m0t1fs_fid_listxattr,
	.removexattr    = m0t1fs_fid_removexattr
};

#undef M0_TRACE_SUBSYSTEM
