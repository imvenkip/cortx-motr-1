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

#include "lib/misc.h"      /* M0_SET0() */
#include "lib/memory.h"    /* M0_ALLOC_PTR() */
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0T1FS
#include "lib/trace.h"           /* M0_LOG and M0_ENTRY */
#include "lib/bob.h"
#include "fop/fop.h"             /* m0_fop_alloc() */
#include "rpc/rpclib.h"          /* m0_rpc_client_call */
#include "rpc/rpc_opcodes.h"
#include "ioservice/io_device.h"
#include "mero/magic.h"
#include "layout/layout.h"
#include "layout/pdclust.h"
#include "m0t1fs/linux_kernel/m0t1fs.h"

M0_INTERNAL void m0t1fs_inode_bob_init(struct m0t1fs_inode *bob);
M0_INTERNAL bool m0t1fs_inode_bob_check(struct m0t1fs_inode *bob);

static int m0t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 int               mode,
			 struct nameidata *nd);

static int m0t1fs_fid_create(struct inode     *dir,
			     struct dentry    *dentry,
			     int               mode,
			     struct nameidata *nd);

static struct dentry *m0t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    struct nameidata *nd);

static struct dentry *m0t1fs_fid_lookup(struct inode     *dir,
				        struct dentry    *dentry,
				        struct nameidata *nd);

static int m0t1fs_readdir(struct file *f,
			  void        *dirent,
			  filldir_t    filldir);

static int m0t1fs_fid_readdir(struct file *f,
			      void        *dirent,
			      filldir_t    filldir);

static int m0t1fs_opendir(struct inode *inode, struct file *file);
static int m0t1fs_releasedir(struct inode *inode, struct file *file);
static int m0t1fs_fid_opendir(struct inode *inode, struct file *file);
static int m0t1fs_fid_releasedir(struct inode *inode, struct file *file);

static int m0t1fs_unlink(struct inode *dir, struct dentry *dentry);
static int m0t1fs_fid_unlink(struct inode *dir, struct dentry *dentry);
static int m0t1fs_link(struct dentry *old, struct inode *dir,
		       struct dentry *new);
static int m0t1fs_fid_link(struct dentry *old, struct inode *dir,
		           struct dentry *new);

static int m0t1fs_mkdir(struct inode *dir, struct dentry *dentry, int mode);
static int m0t1fs_fid_mkdir(struct inode *dir, struct dentry *dentry, int mode);
static int m0t1fs_rmdir(struct inode *dir, struct dentry *dentry);
static int m0t1fs_fid_rmdir(struct inode *dir, struct dentry *dentry);

static int m0t1fs_component_objects_op(struct m0t1fs_inode *ci,
				       int (*func)(struct m0t1fs_sb *csb,
						   const struct m0_fid *cfid,
						   const struct m0_fid *gfid,
						   uint32_t cob_idx));

static int m0t1fs_ios_cob_op(struct m0t1fs_sb    *csb,
			     const struct m0_fid *cob_fid,
			     const struct m0_fid *gob_fid,
			     uint32_t cob_idx,
			     struct m0_fop_type  *ftype);

static int m0t1fs_ios_cob_create(struct m0t1fs_sb    *csb,
				 const struct m0_fid *cob_fid,
				 const struct m0_fid *gob_fid,
				 uint32_t cob_idx);

static int m0t1fs_ios_cob_delete(struct m0t1fs_sb    *csb,
				 const struct m0_fid *cob_fid,
				 const struct m0_fid *gob_fid,
				 uint32_t cob_idx);

static int m0t1fs_ios_cob_fop_populate(struct m0t1fs_sb    *csb,
				       struct m0_fop       *fop,
				       const struct m0_fid *cob_fid,
				       const struct m0_fid *gob_fid,
				       uint32_t cob_idx);

const struct file_operations m0t1fs_dir_file_operations = {
	.read    = generic_read_dir,    /* provided by linux kernel */
	.open    = m0t1fs_opendir,
	.release = m0t1fs_releasedir,
	.readdir = m0t1fs_readdir,
	.fsync   = simple_fsync,	/* provided by linux kernel */
	.llseek  = generic_file_llseek, /* provided by linux kernel */
};

const struct file_operations m0t1fs_fid_dir_file_operations = {
	.read    = generic_read_dir,    /* provided by linux kernel */
	.open    = m0t1fs_fid_opendir,
	.release = m0t1fs_fid_releasedir,
	.readdir = m0t1fs_fid_readdir,
	.fsync   = simple_fsync,	/* provided by linux kernel */
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

static int name_mem2wire(struct m0_fop_str *tgt,
			 const struct m0_buf *name)
{
	tgt->s_buf = m0_alloc(name->b_nob);
	if (tgt->s_buf == NULL)
		return -ENOMEM;
	memcpy(tgt->s_buf, name->b_addr, (int)name->b_nob);
	tgt->s_len = name->b_nob;
	return 0;
}

static void body_mem2wire(struct m0_fop_cob *body,
			  const struct m0_cob_attr *attr,
			  int valid)
{
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
        return -EOPNOTSUPP;
}

int m0t1fs_setxattr(struct dentry *dentry, const char *name,
                    const void *value, size_t size, int flags)
{
	struct m0t1fs_inode        *ci = M0T1FS_I(dentry->d_inode);
	struct m0t1fs_sb           *csb = M0T1FS_SB(ci->ci_inode.i_sb);
	struct m0_fop_setxattr_rep *rep = NULL;
	struct m0t1fs_mdop          mo;
	int                         rc;

	M0_ENTRY("Setting %.*s's xattr %s=%.*s", dentry->d_name.len,
		 dentry->d_name.name, name, (int)size, (char *)value);

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);
	m0_buf_init(&mo.mo_attr.ca_name, (void *)dentry->d_name.name,
		    dentry->d_name.len);
	m0_buf_init(&mo.mo_attr.ca_eakey, (void *)name, strlen(name));
	m0_buf_init(&mo.mo_attr.ca_eaval, (void *)value, size);

	rc = m0t1fs_mds_cob_setxattr(csb, &mo, &rep);

	m0t1fs_fs_unlock(csb);
	M0_LEAVE("rc: %d", rc);
	return rc;
}

ssize_t m0t1fs_fid_getxattr(struct dentry *dentry, const char *name,
                            void *buffer, size_t size)
{
        return -EOPNOTSUPP;
}

ssize_t m0t1fs_getxattr(struct dentry *dentry, const char *name,
                        void *buffer, size_t size)
{
	struct m0t1fs_inode        *ci = M0T1FS_I(dentry->d_inode);
	struct m0t1fs_sb           *csb = M0T1FS_SB(ci->ci_inode.i_sb);
	struct m0_fop_getxattr_rep *rep = NULL;
	struct m0t1fs_mdop          mo;
	int                         rc;

	M0_ENTRY("Getting %.*s's xattr %s", dentry->d_name.len,
		 dentry->d_name.name, name);

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);
	m0_buf_init(&mo.mo_attr.ca_name, (void *)dentry->d_name.name,
		    dentry->d_name.len);
	m0_buf_init(&mo.mo_attr.ca_eakey, (void *)name, strlen(name));

	rc = m0t1fs_mds_cob_getxattr(csb, &mo, &rep);
	if (rc == 0) {
		if (buffer != NULL) {
			if ((size_t)rep->g_value.s_len > size) {
				rc = -ERANGE;
				goto out;
			}
			memcpy(buffer, rep->g_value.s_buf, rep->g_value.s_len);
		}
		rc = rep->g_value.s_len; /* return xattr length */
	} else if (rc == -ENOENT)
		rc = -ENODATA;
out:
	m0t1fs_fs_unlock(csb);
	M0_LEAVE("rc: %d", rc);
	return rc;
}

ssize_t m0t1fs_fid_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
        return -EOPNOTSUPP;
}

ssize_t m0t1fs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
        return -EOPNOTSUPP;
}

int m0t1fs_fid_removexattr(struct dentry *dentry, const char *name)
{
        return -EOPNOTSUPP;
}

int m0t1fs_removexattr(struct dentry *dentry, const char *name)
{
	struct m0t1fs_inode        *ci = M0T1FS_I(dentry->d_inode);
	struct m0t1fs_sb           *csb = M0T1FS_SB(ci->ci_inode.i_sb);
	struct m0_fop_delxattr_rep *rep = NULL;
	struct m0t1fs_mdop          mo;
	int                         rc;

	M0_ENTRY("Deleting %.*s's xattr %s", dentry->d_name.len,
		 dentry->d_name.name, name);

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);
	m0_buf_init(&mo.mo_attr.ca_name, (void *)dentry->d_name.name,
		    dentry->d_name.len);
	m0_buf_init(&mo.mo_attr.ca_eakey, (void *)name, strlen(name));

	rc = m0t1fs_mds_cob_delxattr(csb, &mo, &rep);
	if (rc == -ENOENT)
		rc = -ENODATA;

	m0t1fs_fs_unlock(csb);
	M0_LEAVE("rc: %d", rc);
	return rc;
}

static int m0t1fs_fid_create(struct inode     *dir,
			     struct dentry    *dentry,
			     int               mode,
			     struct nameidata *nd)
{
        return -EOPNOTSUPP;
}

static int m0t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 int               mode,
			 struct nameidata *nd)
{
	struct super_block       *sb  = dir->i_sb;
	struct m0t1fs_sb         *csb = M0T1FS_SB(sb);
	struct m0_fop_create_rep *rep = NULL;
	struct m0t1fs_inode      *ci;
	struct m0t1fs_mdop        mo;
	struct inode             *inode;
	struct m0_fid             new_fid;
	int                       rc;

	M0_ENTRY();

	M0_LOG(M0_INFO, "Creating \"%s\" in pdir %lu "FID_F,
	       dentry->d_name.name, dir->i_ino,
	       FID_P(m0t1fs_inode_fid(M0T1FS_I(dir))));

	inode = new_inode(sb);
	if (inode == NULL)
		return M0_RC(-ENOMEM);
	m0t1fs_fs_lock(csb);
	m0t1fs_fid_alloc(csb, &new_fid);
	inode->i_ino = fid_hash(&new_fid);
	ci = M0T1FS_I(inode);
	ci->ci_fid = new_fid;

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
		inode->i_op  = &m0t1fs_reg_inode_operations;
		inode->i_fop = &m0t1fs_reg_file_operations;
	}

	ci->ci_layout_id = csb->csb_layout_id; /* layout id for new file */
	m0t1fs_file_lock_init(ci, csb);
	rc = m0t1fs_inode_layout_init(ci);
	if (rc != 0)
		goto out;

	M0_SET0(&mo);
	mo.mo_attr.ca_uid       = inode->i_uid;
	mo.mo_attr.ca_gid       = inode->i_gid;
	mo.mo_attr.ca_atime     = inode->i_atime.tv_sec;
	mo.mo_attr.ca_ctime     = inode->i_ctime.tv_sec;
	mo.mo_attr.ca_mtime     = inode->i_mtime.tv_sec;
	mo.mo_attr.ca_mode      = inode->i_mode;
	mo.mo_attr.ca_blocks    = inode->i_blocks;
	mo.mo_attr.ca_pfid      = *m0t1fs_inode_fid(M0T1FS_I(dir));
	mo.mo_attr.ca_tfid      = *m0t1fs_inode_fid(ci);
	mo.mo_attr.ca_lid       = ci->ci_layout_id;
	mo.mo_attr.ca_nlink     = inode->i_nlink;
	mo.mo_attr.ca_valid     = (M0_COB_UID    | M0_COB_GID   | M0_COB_ATIME |
				   M0_COB_CTIME  | M0_COB_MTIME | M0_COB_MODE  |
				   M0_COB_BLOCKS | M0_COB_SIZE  | M0_COB_LID   |
				   M0_COB_NLINK);
	m0_buf_init(&mo.mo_attr.ca_name, (char *)dentry->d_name.name,
		    dentry->d_name.len);

	rc = m0t1fs_mds_cob_create(csb, &mo, &rep);
	if (rc != 0)
		goto out;

	if (S_ISREG(mode)) {
		rc = m0t1fs_component_objects_op(ci, m0t1fs_ios_cob_create);
		if (rc != 0)
			goto out;
	}

	if (insert_inode_locked(inode) < 0) {
		M0_LOG(M0_ERROR, "Duplicate inode: "FID_F, FID_P(&new_fid));
		rc = -EIO;
		goto out;
	}

	m0t1fs_fs_unlock(csb);
	unlock_new_inode(inode);

	mark_inode_dirty(dir);
	d_instantiate(dentry, inode);
	return M0_RC(0);
out:
	clear_nlink(inode);
	m0t1fs_fs_unlock(csb);
	make_bad_inode(inode);
	iput(inode);
	return M0_RC(rc);
}

static int m0t1fs_fid_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	return m0t1fs_fid_create(dir, dentry, mode | S_IFDIR, NULL);
}

static int m0t1fs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	return m0t1fs_create(dir, dentry, mode | S_IFDIR, NULL);
}

static struct dentry *m0t1fs_fid_lookup(struct inode     *dir,
				        struct dentry    *dentry,
				        struct nameidata *nd)
{
	struct m0_fid             fid;
        int rc;

        rc = m0_fid_sscanf_simple((char *)dentry->d_name.name, &fid);
        if (rc != 0) {
		M0_LEAVE("Cannot parse fid \"%s\"", (char *)dentry->d_name.name);
		return ERR_PTR(rc);
        }
        /**
           @todo: When multi-mdservice support is added,
           @fid will be used to figure out what mdservice
           lookup operation should be sent to.
        */
        return m0t1fs_lookup(dir, dentry, nd);
}

static struct dentry *m0t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    struct nameidata *nd)
{
	struct m0t1fs_sb         *csb;
	struct m0t1fs_inode      *ci;
	struct inode             *inode = NULL;
	struct m0_fop_lookup_rep *rep = NULL;
	struct m0t1fs_mdop        mo;
	int rc;


	M0_ENTRY();

	ci = M0T1FS_I(dir);
	csb = M0T1FS_SB(dir->i_sb);

	if (dentry->d_name.len > csb->csb_namelen) {
		M0_LEAVE("ERR_PTR: %p", ERR_PTR(-ENAMETOOLONG));
		return ERR_PTR(-ENAMETOOLONG);
	}

	M0_LOG(M0_DEBUG, "Name: \"%s\"", dentry->d_name.name);

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	mo.mo_attr.ca_pfid = *m0t1fs_inode_fid(M0T1FS_I(dir));
	m0_buf_init(&mo.mo_attr.ca_name, (char *)dentry->d_name.name,
		    dentry->d_name.len);
	rc = m0t1fs_mds_cob_lookup(csb, &mo, &rep);
	if (rc == 0) {
		mo.mo_attr.ca_tfid = rep->l_body.b_tfid;
		inode = m0t1fs_iget(dir->i_sb, &mo.mo_attr.ca_tfid,
				    &rep->l_body);
		if (IS_ERR(inode)) {
			M0_LEAVE("ERROR: %p", ERR_CAST(inode));
			m0t1fs_fs_unlock(csb);
			return ERR_CAST(inode);
		}
	}

	m0t1fs_fs_unlock(csb);
	return d_splice_alias(inode, dentry);
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

static int m0t1fs_fid_opendir(struct inode *inode, struct file *file)
{
        return m0t1fs_opendir(inode, file);
}

static int m0t1fs_opendir(struct inode *inode, struct file *file)
{
	struct m0t1fs_filedata *fd;
	M0_ENTRY();

	M0_ALLOC_PTR(fd);
	if (fd == NULL)
		return -ENOMEM;
	file->private_data = fd;

	/** Setup readdir initial pos with "." and max possible namelen. */
	fd->fd_dirpos = m0_alloc(M0T1FS_SB(inode->i_sb)->csb_namelen);
	if (fd->fd_dirpos == NULL) {
		m0_free(fd);
		return -ENOMEM;
	}
	m0_bitstring_copy(fd->fd_dirpos, ".", 1);
	fd->fd_direof = 0;
	return 0;
}

static int m0t1fs_fid_releasedir(struct inode *inode, struct file *file)
{
        return m0t1fs_releasedir(inode, file);
}

static int m0t1fs_releasedir(struct inode *inode, struct file *file)
{
	struct m0t1fs_filedata *fd = file->private_data;
	M0_ENTRY();

	m0_free(fd->fd_dirpos);
	m0_free0(&file->private_data);
	return 0;
}

static int m0t1fs_fid_readdir(struct file *f,
			      void        *buf,
			      filldir_t    filldir)
{
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

	M0_ENTRY();

	dentry = f->f_path.dentry;
	dir    = dentry->d_inode;
	ci     = M0T1FS_I(dir);
	csb    = M0T1FS_SB(dir->i_sb);
	i      = f->f_pos;

	fd = f->private_data;
	if (fd->fd_direof)
		return M0_RC(0);

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

	m0t1fs_fs_lock(csb);

	do {
		M0_LOG(M0_DEBUG, "readdir from position \"%*s\"",
		       (int)mo.mo_attr.ca_name.b_nob,
		       (char *)mo.mo_attr.ca_name.b_addr);

		rc = m0t1fs_mds_cob_readdir(csb, &mo, &rep);
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
				ino = dir->i_ino;
				type = DT_DIR;
			} else if (ent->d_namelen == 2 &&
				   memcmp(ent->d_name, "..", 2) == 0) {
				ino = parent_ino(dentry);
				type = DT_DIR;
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
		/**
		   Return codes for m0t1fs_mds_cob_readdir() are the following:
		   - <0 - some error occured;
		   - >0 - EOF signaled by mdservice, some number of entries
		          available;
		   -  0 - no error, some number of entries is sent by mdservice.
		 */
	} while (rc == 0);

	/**
	   EOF detected, let's set return code to 0 to make vfs happy.
	 */
	fd->fd_direof = 1;
	rc = 0;
out:
	m0t1fs_fs_unlock(csb);
	M0_LEAVE("rc: %d", rc);
	return rc;
}

static int m0t1fs_fid_link(struct dentry *old, struct inode *dir,
		           struct dentry *new)
{
        return -EOPNOTSUPP;
}

static int m0t1fs_link(struct dentry *old, struct inode *dir,
		       struct dentry *new)
{
	struct m0t1fs_sb                *csb;
	struct m0_fop_link_rep          *link_rep;
	struct m0t1fs_mdop               mo;
	struct m0t1fs_inode             *ci;
	struct inode                    *inode;
	struct timespec                  now;
	int                              rc;

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
	m0_buf_init(&mo.mo_attr.ca_name, (char *)new->d_name.name,
		    new->d_name.len);

	rc = m0t1fs_mds_cob_link(csb, &mo, &link_rep);
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
	m0t1fs_fs_unlock(csb);
	M0_LEAVE("rc: %d", rc);
	return rc;
}

static int m0t1fs_fid_unlink(struct inode *dir, struct dentry *dentry)
{
        return -EOPNOTSUPP;
}

static int m0t1fs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct m0_fop_lookup_rep        *lookup_rep;
	struct m0_fop_unlink_rep        *unlink_rep;
	struct m0_fop_setattr_rep       *setattr_rep;
	struct m0t1fs_sb                *csb;
	struct inode                    *inode;
	struct m0t1fs_inode             *ci;
	struct m0t1fs_mdop               mo;
	struct timespec                  now;
	int                              rc;

	M0_ENTRY();

	M0_LOG(M0_INFO, "Name: \"%s\"", dentry->d_name.name);

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
		    (char *)dentry->d_name.name, dentry->d_name.len);

	rc = m0t1fs_mds_cob_lookup(csb, &mo, &lookup_rep);
	if (rc != 0)
		goto out;

	rc = m0t1fs_mds_cob_unlink(csb, &mo, &unlink_rep);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "mdservive unlink fop failed: %d", rc);
		goto out;
	}

	if (mo.mo_attr.ca_nlink == 0) {
		rc = m0t1fs_component_objects_op(ci, m0t1fs_ios_cob_delete);
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

	rc = m0t1fs_mds_cob_setattr(csb, &mo, &setattr_rep);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "Setattr on parent dir failed with %d", rc);
		goto out;
	}

	inode->i_ctime = dir->i_ctime = dir->i_mtime = now;
	inode_dec_link_count(inode);
	mark_inode_dirty(dir);
out:
	m0t1fs_fs_unlock(csb);
	M0_LEAVE("rc: %d", rc);
	return rc;
}

static int m0t1fs_fid_rmdir(struct inode *dir, struct dentry *dentry)
{
        return -EOPNOTSUPP;
}

static int m0t1fs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int rc;

	M0_ENTRY();
	rc = m0t1fs_unlink(dir, dentry);
	if (rc == 0) {
		inode_dec_link_count(dentry->d_inode);
		drop_nlink(dir);
	}
	M0_LEAVE("rc: %d", rc);

	return rc;
}

M0_INTERNAL int m0t1fs_fid_getattr(struct vfsmount *mnt, struct dentry *dentry,
			           struct kstat *stat)
{
	struct m0t1fs_sb    *csb;
	struct inode        *inode;
	struct m0_fop_cob   *body;
	struct m0t1fs_inode *ci;
	int                  rc;

	M0_ENTRY();

	M0_LOG(M0_INFO, "Name: \"%s\"", dentry->d_name.name);

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
	stat->blocks = inode->i_blocks;
	m0t1fs_fs_unlock(csb);
	return M0_RC(0);
}

M0_INTERNAL int m0t1fs_getattr(struct vfsmount *mnt, struct dentry *dentry,
			       struct kstat *stat)
{
	struct m0_fop_getattr_rep       *getattr_rep;
	struct m0t1fs_sb                *csb;
	struct inode                    *inode;
	struct m0_fop_cob               *body;
	struct m0t1fs_inode             *ci;
	struct m0t1fs_mdop               mo;
	int                              rc;

	M0_ENTRY();

	M0_LOG(M0_INFO, "Name: \"%s\"", dentry->d_name.name);

	inode = dentry->d_inode;
	csb   = M0T1FS_SB(inode->i_sb);
	ci    = M0T1FS_I(inode);

	m0t1fs_fs_lock(csb);

        M0_SET0(&mo);
        mo.mo_attr.ca_tfid  = *m0t1fs_inode_fid(ci);

	/**
	   @todo When we have rm locking working, this will be changed to
	   revalidate inode with checking cached lock. If lock is cached
	   (not canceled), which means inode did not change, then we don't
	   have to do getattr and can just use @inode cached data.
	*/
        rc = m0t1fs_mds_cob_getattr(csb, &mo, &getattr_rep);
        if (rc != 0)
	        goto out;
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
	stat->blocks = inode->i_blocks;
out:
	m0t1fs_fs_unlock(csb);
	return M0_RC(rc);
}

M0_INTERNAL int m0t1fs_size_update(struct inode *inode, uint64_t newsize)
{
	struct m0_fop_setattr_rep       *setattr_rep;
	struct m0t1fs_sb                *csb;
	struct m0t1fs_inode             *ci;
	struct m0t1fs_mdop               mo;
	int                              rc;

	csb   = M0T1FS_SB(inode->i_sb);
	ci    = M0T1FS_I(inode);

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid   = *m0t1fs_inode_fid(ci);
	mo.mo_attr.ca_size   = newsize;
	mo.mo_attr.ca_valid |= M0_COB_SIZE;

	rc = m0t1fs_mds_cob_setattr(csb, &mo, &setattr_rep);
	if (rc != 0)
		goto out;
	inode->i_size = newsize;
out:
	m0t1fs_fs_unlock(csb);
	return rc;
}

M0_INTERNAL int m0t1fs_fid_setattr(struct dentry *dentry, struct iattr *attr)
{
        return -EOPNOTSUPP;
}

M0_INTERNAL int m0t1fs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct m0_fop_setattr_rep       *setattr_rep;
	struct m0t1fs_sb                *csb;
	struct inode                    *inode;
	struct m0t1fs_inode             *ci;
	struct m0t1fs_mdop               mo;
	int                              rc;

	M0_ENTRY();

	M0_LOG(M0_INFO, "Name: \"%s\"", dentry->d_name.name);

	inode = dentry->d_inode;
	csb   = M0T1FS_SB(inode->i_sb);
	ci    = M0T1FS_I(inode);

	rc = inode_change_ok(inode, attr);
	if (rc)
		return rc;

	m0t1fs_fs_lock(csb);

	M0_SET0(&mo);
	mo.mo_attr.ca_tfid = *m0t1fs_inode_fid(ci);

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
		mo.mo_attr.ca_uid = attr->ia_uid;
		mo.mo_attr.ca_valid |= M0_COB_UID;
	}

	if (attr->ia_valid & ATTR_GID) {
		mo.mo_attr.ca_gid = attr->ia_gid;
		mo.mo_attr.ca_valid |= M0_COB_GID;
	}

	/*
	 * Layout can be changed explicitly in setattr()
	 * to a new layout, e.g. to a composite layout in NBA.
	 * Check for that use case and update layout id for this
	 * file. When that happens, a special setattr() with
	 * valid layout id should be called.
	 */

	rc = m0t1fs_mds_cob_setattr(csb, &mo, &setattr_rep);
	if (rc != 0)
		goto out;

	rc = inode_setattr(inode, attr);
	if (rc != 0)
		goto out;
out:
	m0t1fs_fs_unlock(csb);
	M0_LEAVE("rc: %d", rc);
	return rc;
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

	M0_PRE(m0t1fs_inode_fid(ci)->f_container == 0);
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
	M0_PRE(ci->ci_layout_instance != NULL);
	M0_PRE(gfid != NULL);
	M0_PRE(cfid != NULL);

	return m0_layout_enum_find(m0_layout_instance_to_enum(
				   ci->ci_layout_instance), gfid, cfid);

}

static int m0t1fs_component_objects_op(struct m0t1fs_inode *ci,
				       int (*func)(struct m0t1fs_sb *csb,
						   const struct m0_fid *cfid,
						   const struct m0_fid *gfid,
						   uint32_t cob_idx))
{
	struct m0t1fs_sb *csb;
	struct m0_fid     cob_fid;
	int               pool_width;
	int               i;
	int               rc = 0;
	uint32_t          cob_idx;

	M0_PRE(ci != NULL);
	M0_PRE(func != NULL);

	M0_ENTRY();

	M0_LOG(M0_DEBUG, "Component object %s for "FID_F,
	       func == m0t1fs_ios_cob_create? "create" : "delete",
	       FID_P(m0t1fs_inode_fid(ci)));

	csb = M0T1FS_SB(ci->ci_inode.i_sb);
	pool_width = csb->csb_pool_width;
	M0_ASSERT(pool_width >= 1);

	for (i = 0; i < pool_width; ++i) {
		cob_fid = m0t1fs_ios_cob_fid(ci, i);
		cob_idx = m0t1fs_ios_cob_idx(ci, m0t1fs_inode_fid(ci),
					     &cob_fid);
		M0_ASSERT(cob_idx != ~0);
again:
		rc = func(csb, &cob_fid, m0t1fs_inode_fid(ci), cob_idx);
		if (rc == -EAGAIN) {
			M0_LOG(M0_NOTICE, "Failure vector updated. Do this"
					  " operation again");
			goto again;
		}
		if (rc != 0) {
			M0_LOG(M0_ERROR, "Cob %s "FID_F" failed with %d",
			       func == m0t1fs_ios_cob_create ? "create" : "delete",
			       FID_P(&cob_fid), rc);
			goto out;
		}
	}
out:
	M0_LEAVE("rc: %d", rc);
	return rc;
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
	struct m0_fop_layout    *layout;
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
	 case M0_LAYOUT_OPCODE: {
		struct m0_bufvec          bv;
		struct m0_bufvec_cursor   cur;
		struct m0_layout         *l = mo->mo_layout;

		layout = m0_fop_data(fop);
		layout->l_op  = mo->mo_layout_op;
		layout->l_lid = mo->mo_attr.ca_lid;

		if (layout->l_op == M0_LAYOUT_OP_ADD ||
		    layout->l_op == M0_LAYOUT_OP_DELETE) {
			M0_ASSERT(l != NULL);

			/* TODO m0_layout_size(const struct *l) should be used
			 * in the future to calculate buffer size large enough
			 * for any type of layout.
			 */
			layout->l_buf.b_count = m0_layout_max_recsize(
						&csb->csb_layout_dom);
			layout->l_buf.b_addr = m0_alloc(layout->l_buf.b_count);
			if (layout->l_buf.b_addr == NULL) {
				rc = -ENOMEM;
				break;
			}

			bv = (struct m0_bufvec)
			       M0_BUFVEC_INIT_BUF((void**)&layout->l_buf.b_addr,
					 (m0_bcount_t *)&layout->l_buf.b_count);
			m0_bufvec_cursor_init(&cur, &bv);

			m0_mutex_lock(&l->l_lock);
			rc = m0_layout_encode(l, M0_LXO_BUFFER_OP, NULL, &cur);
			m0_mutex_unlock(&l->l_lock);
		}
		break;
	}
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
		rc = -ENOSYS;
		break;
	}

	return rc;
}

static int m0t1fs_mds_cob_op(struct m0t1fs_sb            *csb,
			     const struct m0t1fs_mdop    *mo,
			     struct m0_fop_type          *ftype,
			     void                       **rep)
{
	int                          rc;
	struct m0_fop               *fop;
	struct m0_rpc_session       *session;
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
	struct m0_fop_layout_rep    *layout_rep;
	struct m0_fop_setxattr_rep  *setxattr_rep;
	struct m0_fop_getxattr_rep  *getxattr_rep;
	struct m0_fop_listxattr_rep *listxattr_rep;
	struct m0_fop_delxattr_rep  *delxattr_rep;
	void                        *reply_fop;

	M0_PRE(ftype != NULL);

	M0_ENTRY();

	/**
	   TODO: This needs to be fixed later.

	   Container 0 and its session are currently reserved for mdservice.
	   We hardcoding its using here temporary because of the following:
	   - we can't use @mo->mo_attr.ca_pfid because it is set to root
	     fid, which has container != 0 and we cannot change it as it
	     will conflict with cob io objects in case they share the same
	     db;
	   - we can't use @mo->mo_attr.ca_tfid it is not always set, some ops
	     do not require it;
	   - using @ci is not an option as well as it is not always used.
	     For example, it is not available for lookup.
	 */
	session = m0t1fs_container_id_to_session(csb, 0);
	M0_ASSERT(session != NULL);

	fop = m0_fop_alloc(ftype, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		M0_LOG(M0_ERROR, "m0_fop_alloc() failed with %d", rc);
		goto out;
	}

	rc = m0t1fs_mds_cob_fop_populate(csb, mo, fop);
	if (rc != 0) {
		M0_LOG(M0_ERROR,
		       "m0t1fs_mds_cob_fop_populate() failed with %d", rc);
		goto out;
	}

	M0_LOG(M0_DEBUG, "Send md operation %u to session %lu",
		m0_fop_opcode(fop), (unsigned long)session->s_session_id);

	rc = m0_rpc_client_call(fop, session, NULL, 0 /* deadline */);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "m0_rpc_client_call(%x) failed with %d",
		       m0_fop_opcode(fop), rc);
		goto out;
	}

	reply_fop = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
	*rep = reply_fop;

	switch (m0_fop_opcode(fop)) {
	case M0_MDSERVICE_CREATE_OPCODE:
		create_rep = reply_fop;
		rc = create_rep->c_body.b_rc;
		break;
	case M0_MDSERVICE_STATFS_OPCODE:
		statfs_rep = reply_fop;
		rc = statfs_rep->f_rc;
		break;
	case M0_MDSERVICE_LOOKUP_OPCODE:
		lookup_rep = reply_fop;
		rc = lookup_rep->l_body.b_rc;
		break;
	case M0_MDSERVICE_LINK_OPCODE:
		link_rep = reply_fop;
		rc = link_rep->l_body.b_rc;
		break;
	case M0_MDSERVICE_UNLINK_OPCODE:
		unlink_rep = reply_fop;
		rc = unlink_rep->u_body.b_rc;
		break;
	case M0_MDSERVICE_RENAME_OPCODE:
		rename_rep = reply_fop;
		rc = rename_rep->r_body.b_rc;
		break;
	case M0_MDSERVICE_SETATTR_OPCODE:
		setattr_rep = reply_fop;
		rc = setattr_rep->s_body.b_rc;
		break;
	case M0_MDSERVICE_GETATTR_OPCODE:
		getattr_rep = reply_fop;
		rc = getattr_rep->g_body.b_rc;
		break;
	case M0_MDSERVICE_OPEN_OPCODE:
		open_rep = reply_fop;
		rc = open_rep->o_body.b_rc;
		break;
	case M0_MDSERVICE_CLOSE_OPCODE:
		close_rep = reply_fop;
		rc = close_rep->c_body.b_rc;
		break;
	case M0_MDSERVICE_READDIR_OPCODE:
		readdir_rep = reply_fop;
		rc = readdir_rep->r_body.b_rc;
		break;
	case M0_LAYOUT_OPCODE:
		layout_rep = reply_fop;
		rc = layout_rep->lr_rc;
		break;
	case M0_MDSERVICE_SETXATTR_OPCODE:
		setxattr_rep = reply_fop;
		rc = setxattr_rep->s_body.b_rc;
		break;
	case M0_MDSERVICE_GETXATTR_OPCODE:
		getxattr_rep = reply_fop;
		rc = getxattr_rep->g_body.b_rc;
		break;
	case M0_MDSERVICE_LISTXATTR_OPCODE:
		listxattr_rep = reply_fop;
		rc = listxattr_rep->l_body.b_rc;
		break;
	case M0_MDSERVICE_DELXATTR_OPCODE:
		delxattr_rep = reply_fop;
		rc = delxattr_rep->d_body.b_rc;
		break;
	default:
		M0_LOG(M0_ERROR, "Unexpected fop opcode %x", m0_fop_opcode(fop));
		rc = -ENOSYS;
		goto out;
	}

out:
	if (fop != NULL)
		m0_fop_put(fop);
	M0_LEAVE("%d", rc);
	return rc;
}


int m0t1fs_layout_op(struct m0t1fs_sb *csb, enum m0_layout_opcode op,
		     uint64_t lid, struct m0_layout **l_out)
{
	struct m0t1fs_mdop        mo = { {  { 0 } } };
	struct m0_fop_layout_rep *rep = NULL;
	int                       rc;
	struct m0_layout         *layout = NULL;
	struct m0_layout_domain  *ldom = &csb->csb_layout_dom;

	M0_ENTRY();

	M0_LOG(M0_DEBUG, "layout op = %u lid = %llu", op, lid);
	if (op == M0_LAYOUT_OP_ADD || op == M0_LAYOUT_OP_DELETE) {
		layout = m0_layout_find(ldom, lid);
		if (layout == NULL)
			return M0_RC(-ENOENT);
	}

	mo.mo_layout_op   = op;
	mo.mo_attr.ca_lid = lid;
	mo.mo_layout      = layout;

	rc = m0t1fs_mds_cob_op(csb, &mo, &m0_fop_layout_fopt, (void **)&rep);
	M0_LOG(M0_DEBUG, "layout rep rc = %d", rc);
	if (rc == 0) {
		M0_LOG(M0_DEBUG, "layout rep->lr_rc = %d", rep->lr_rc);

		if (op == M0_LAYOUT_OP_LOOKUP) {
			struct m0_bufvec               bv;
			struct m0_bufvec_cursor        cur;
			struct m0_layout              *l;
			struct m0_layout_type         *lt;
			M0_ASSERT(l_out != NULL);

			bv = (struct m0_bufvec)
				M0_BUFVEC_INIT_BUF((void**)&rep->lr_buf.b_addr,
					   (m0_bcount_t*)&rep->lr_buf.b_count);
			m0_bufvec_cursor_init(&cur, &bv);

			/* FIXME This hard coding of 'lt' will be gotten rid of
			 * once we enhance the layout id to contain the layout
			 * type as well.
			 */
			lt = &m0_pdclust_layout_type;
			rc = lt->lt_ops->lto_allocate(ldom, lid, &l);
			if (rc == 0) {
				rc = m0_layout_decode(l, &cur, M0_LXO_BUFFER_OP,
						      NULL);
				/* release lock held by ->lto_allocate() */
				m0_mutex_unlock(&l->l_lock);
				if (rc == 0) {
					/* m0_layout_put() should be called
					 * after use of this l_out */
					*l_out = l;
				} else {
					m0_layout_put(l);
				}
			}
		}
	}

	if (layout != NULL)
		m0_layout_put(layout); /* dual to m0_layout_find() */

	return M0_RC(rc);
}

int m0t1fs_mds_statfs(struct m0t1fs_sb *csb, struct m0_fop_statfs_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, NULL, &m0_fop_statfs_fopt, (void **)rep);
}

int m0t1fs_mds_cob_create(struct m0t1fs_sb          *csb,
			  const struct m0t1fs_mdop  *mo,
			  struct m0_fop_create_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_create_fopt, (void **)rep);
}

int m0t1fs_mds_cob_unlink(struct m0t1fs_sb          *csb,
			  const struct m0t1fs_mdop  *mo,
			  struct m0_fop_unlink_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_unlink_fopt, (void **)rep);
}

int m0t1fs_mds_cob_link(struct m0t1fs_sb          *csb,
			const struct m0t1fs_mdop  *mo,
			struct m0_fop_link_rep   **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_link_fopt, (void **)rep);
}

int m0t1fs_mds_cob_lookup(struct m0t1fs_sb          *csb,
			  const struct m0t1fs_mdop  *mo,
			  struct m0_fop_lookup_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_lookup_fopt, (void **)rep);
}

int m0t1fs_mds_cob_getattr(struct m0t1fs_sb           *csb,
			   const struct m0t1fs_mdop   *mo,
			   struct m0_fop_getattr_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_getattr_fopt, (void **)rep);
}

int m0t1fs_mds_cob_setattr(struct m0t1fs_sb           *csb,
			   const struct m0t1fs_mdop   *mo,
			   struct m0_fop_setattr_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_setattr_fopt, (void **)rep);
}

int m0t1fs_mds_cob_readdir(struct m0t1fs_sb           *csb,
			   const struct m0t1fs_mdop   *mo,
			   struct m0_fop_readdir_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_readdir_fopt, (void **)rep);
}

int m0t1fs_mds_cob_setxattr(struct m0t1fs_sb            *csb,
			    const struct m0t1fs_mdop    *mo,
			    struct m0_fop_setxattr_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_setxattr_fopt, (void **)rep);
}

int m0t1fs_mds_cob_getxattr(struct m0t1fs_sb            *csb,
			    const struct m0t1fs_mdop    *mo,
			    struct m0_fop_getxattr_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_getxattr_fopt, (void **)rep);
}

int m0t1fs_mds_cob_listxattr(struct m0t1fs_sb             *csb,
			     const struct m0t1fs_mdop     *mo,
			     struct m0_fop_listxattr_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_listxattr_fopt,
				 (void **)rep);
}

int m0t1fs_mds_cob_delxattr(struct m0t1fs_sb            *csb,
			    const struct m0t1fs_mdop    *mo,
			    struct m0_fop_delxattr_rep **rep)
{
	return m0t1fs_mds_cob_op(csb, mo, &m0_fop_delxattr_fopt, (void **)rep);
}

static int m0t1fs_ios_cob_create(struct m0t1fs_sb    *csb,
				 const struct m0_fid *cob_fid,
				 const struct m0_fid *gob_fid,
				 uint32_t cob_idx)
{
	return m0t1fs_ios_cob_op(csb, cob_fid, gob_fid, cob_idx,
				 &m0_fop_cob_create_fopt);
}

static int m0t1fs_ios_cob_delete(struct m0t1fs_sb *csb,
				 const struct m0_fid *cob_fid,
				 const struct m0_fid *gob_fid,
				 uint32_t cob_idx)
{
	return m0t1fs_ios_cob_op(csb, cob_fid, gob_fid, cob_idx,
				 &m0_fop_cob_delete_fopt);
}

static int m0t1fs_ios_cob_op(struct m0t1fs_sb    *csb,
			     const struct m0_fid *cob_fid,
			     const struct m0_fid *gob_fid,
			     uint32_t cob_idx,
			     struct m0_fop_type  *ftype)
{
	int                         rc;
	bool                        cobcreate;
	struct m0_fop              *fop;
	struct m0_rpc_session      *session;
	struct m0_fop_cob_op_reply *reply;

	M0_PRE(csb != NULL);
	M0_PRE(cob_fid != NULL);
	M0_PRE(gob_fid != NULL);
	M0_PRE(ftype != NULL);

	M0_ENTRY();

	session = m0t1fs_container_id_to_session(csb, cob_fid->f_container);
	M0_ASSERT(session != NULL);

	fop = m0_fop_alloc(ftype, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		M0_LOG(M0_ERROR, "Memory allocation for struct "
		       "m0_fop_cob_create failed");
		goto out;
	}

	M0_ASSERT(m0_is_cob_create_delete_fop(fop));
	cobcreate = m0_is_cob_create_fop(fop);

	rc = m0t1fs_ios_cob_fop_populate(csb, fop, cob_fid, gob_fid, cob_idx);
	if (rc != 0)
		goto fop_put;

	M0_LOG(M0_DEBUG, "Send %s "FID_F" to session %lu",
	       cobcreate ? "cob_create" : "cob_delete",
	       FID_P(cob_fid), (unsigned long)session->s_session_id);

	rc = m0_rpc_client_call(fop, session, NULL, 0 /* deadline */);
	if (rc != 0)
		goto fop_put;

	/*
	 * The reply fop received event is a generic event which does not
	 * distinguish between types of rpc item/fop. Custom m0_rpc_item_ops
	 * vector can be used if type specific things need to be done for
	 * given fop type only.
	 */
	reply = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
	if (reply->cor_rc == M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH) {
		struct m0_pool_version_numbers *cli;
		struct m0_pool_version_numbers *srv;
		struct m0_fv_event             *event;
		uint32_t                        i = 0;

		/* Retrieve the latest server version and updates and apply
		 * to the client's copy. When -EAGAIN is return, this system
		 * call will be restarted.
		 */
		rc = -EAGAIN;
		cli = &csb->csb_pool.po_mach->pm_state->pst_version;
		srv = (struct m0_pool_version_numbers *)&reply->cor_fv_version;
		*cli = *srv;
		while (i < reply->cor_fv_updates.fvu_count) {
			event = &reply->cor_fv_updates.fvu_events[i];
			m0_poolmach_state_transit(csb->csb_pool.po_mach,
						  (struct m0_pool_event*)event,
						  NULL);
			i++;
		}
	} else
		rc = reply->cor_rc;

	M0_LOG(M0_DEBUG, "Finished ioservice op with %d", rc);

fop_put:
	m0_fop_put(fop);
out:
	M0_LEAVE("%d", rc);
	return rc;
}

static int m0t1fs_ios_cob_fop_populate(struct m0t1fs_sb    *csb,
				       struct m0_fop       *fop,
				       const struct m0_fid *cob_fid,
				       const struct m0_fid *gob_fid,
				       uint32_t cob_idx)
{
	struct m0_fop_cob_common       *common;
	struct m0_pool_version_numbers *cli;
	struct m0_pool_version_numbers  curr;

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type != NULL);
	M0_PRE(cob_fid != NULL);
	M0_PRE(gob_fid != NULL);

	M0_ENTRY();

	common = m0_cobfop_common_get(fop);
	M0_ASSERT(common != NULL);

	/* fill in the current client known version */
	m0_poolmach_current_version_get(csb->csb_pool.po_mach, &curr);
	cli = (struct m0_pool_version_numbers*)&common->c_version;
	*cli = curr;

	common->c_gobfid = *gob_fid;
	common->c_cobfid = *cob_fid;
	common->c_cob_idx = cob_idx;

	M0_LEAVE("%d", 0);
	return 0;
}
