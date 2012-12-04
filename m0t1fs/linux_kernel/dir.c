/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
#include "m0t1fs/linux_kernel/m0t1fs.h"

extern const struct m0_rpc_item_ops cob_req_rpc_item_ops;
M0_INTERNAL void m0t1fs_inode_bob_init(struct m0t1fs_inode *bob);
M0_INTERNAL bool m0t1fs_inode_bob_check(struct m0t1fs_inode *bob);

static int m0t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 int               mode,
			 struct nameidata *nd);

static struct dentry *m0t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    struct nameidata *nd);

static int m0t1fs_readdir(struct file *f,
			  void        *dirent,
			  filldir_t    filldir);

static int m0t1fs_opendir(struct inode *inode, struct file *file);
static int m0t1fs_releasedir(struct inode *inode, struct file *file);

static int m0t1fs_unlink(struct inode *dir, struct dentry *dentry);
static int m0t1fs_link(struct dentry *old, struct inode *dir,
		       struct dentry *new);

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

const struct inode_operations m0t1fs_dir_inode_operations = {
	.create  = m0t1fs_create,
	.lookup  = m0t1fs_lookup,
	.unlink  = m0t1fs_unlink,
	.link    = m0t1fs_link,
        .setattr = m0t1fs_setattr,
        .getattr = m0t1fs_getattr
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
        body->b_valid = valid;
}


/**
   Allocate fid of global file.

   See "Containers and component objects" section in m0t1fs.h for
   more information.

   XXX temporary.
 */
static struct m0_fid m0t1fs_fid_alloc(struct m0t1fs_sb *csb)
{
	struct m0_fid fid;

	M0_PRE(m0t1fs_fs_is_locked(csb));
        m0_fid_set(&fid, 0, csb->csb_next_key++);

	return fid;
}

static int m0t1fs_create(struct inode     *dir,
                         struct dentry    *dentry,
                         int               mode,
                         struct nameidata *nd)
{
	struct super_block       *sb = dir->i_sb;
	struct m0t1fs_sb         *csb = M0T1FS_SB(sb);
	struct m0_fop_create_rep *rep = NULL;
	struct m0t1fs_inode      *ci;
	struct m0t1fs_mdop        mo;
	struct inode             *inode;
	int                       rc;

	M0_ENTRY();

	/* Flat file system. create allowed only on root directory */
	M0_ASSERT(m0t1fs_inode_is_root(dir));

	/* new_inode() will call m0t1fs_alloc_inode() using super_operations */
	inode = new_inode(sb);
	if (inode == NULL) {
		M0_LEAVE("rc: %d", -ENOMEM);
		return -ENOMEM;
	}

	m0t1fs_fs_lock(csb);

	inode->i_uid    = 0;
	inode->i_gid    = 0;
	inode->i_mtime  = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	inode->i_blocks = 0;
	inode->i_op     = &m0t1fs_reg_inode_operations;
	inode->i_fop    = &m0t1fs_reg_file_operations;
	inode->i_mode   = mode;

	ci               = M0T1FS_I(inode);
	ci->ci_fid       = m0t1fs_fid_alloc(csb);
	ci->ci_layout_id = csb->csb_layout_id;

	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	rc = m0t1fs_inode_layout_init(ci);
	if (rc != 0)
		goto out;

        /** No hierarchy so far, all live in root */
        M0_SET0(&mo);
        mo.mo_attr.ca_uid       = inode->i_uid;
        mo.mo_attr.ca_gid       = inode->i_gid;
        mo.mo_attr.ca_atime     = inode->i_atime.tv_sec;
        mo.mo_attr.ca_ctime     = inode->i_ctime.tv_sec;
        mo.mo_attr.ca_mtime     = inode->i_mtime.tv_sec;
        mo.mo_attr.ca_blocks    = inode->i_blocks;
        mo.mo_attr.ca_mode      = inode->i_mode;
        mo.mo_attr.ca_pfid      = csb->csb_root_fid;
        mo.mo_attr.ca_tfid      = ci->ci_fid;
        mo.mo_attr.ca_nlink     = inode->i_nlink;
        mo.mo_attr.ca_flags     = (M0_COB_UID | M0_COB_GID | M0_COB_ATIME |
                                   M0_COB_CTIME | M0_COB_MTIME | M0_COB_MODE |
                                   M0_COB_BLOCKS | M0_COB_SIZE | M0_COB_NLINK);
        m0_buf_init(&mo.mo_attr.ca_name, (char *)dentry->d_name.name, dentry->d_name.len);

        rc = m0t1fs_mds_cob_create(csb, &mo, &rep);
        if (rc != 0)
                goto out;

	rc = m0t1fs_component_objects_op(ci, m0t1fs_ios_cob_create);
	if (rc != 0)
		goto out;

	m0t1fs_fs_unlock(csb);

	d_instantiate(dentry, inode);
	M0_LEAVE("rc: 0");
	return 0;
out:
	inode_dec_link_count(inode);
	m0t1fs_fs_unlock(csb);
	iput(inode);

	M0_LEAVE("rc: %d", rc);
	return rc;
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
        mo.mo_attr.ca_pfid = csb->csb_root_fid;
        m0_buf_init(&mo.mo_attr.ca_name, (char *)dentry->d_name.name,
                    dentry->d_name.len);

	rc = m0t1fs_mds_cob_lookup(csb, &mo, &rep);
	if (rc == 0) {
                mo.mo_attr.ca_tfid = rep->l_body.b_tfid;
                inode = m0t1fs_iget(dir->i_sb, &mo.mo_attr.ca_tfid, &rep->l_body);
	        if (IS_ERR(inode)) {
		        M0_LEAVE("ERROR: %p", ERR_CAST(inode));
	                m0t1fs_fs_unlock(csb);
		        return ERR_CAST(inode);
	        }
	}

	m0t1fs_fs_unlock(csb);
	d_add(dentry, inode);
	M0_LEAVE("NULL");
	return NULL;
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

static int m0t1fs_releasedir(struct inode *inode, struct file *file) {
        struct m0t1fs_filedata *fd = file->private_data;
        m0_free(fd->fd_dirpos);
        m0_free(file->private_data);
        file->private_data = NULL;
        return 0;
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
        if (fd->fd_direof) {
                rc = 0;
                goto out;
        }

        M0_SET0(&mo);
        /**
           In case that readdir will be interrupted by enomem situation (filldir fails)
           on big dir and finishes before eof is reached, it has chance to start from
           there. This way f->f_pos and string readdir pos are in sync.
         */
        m0_buf_init(&mo.mo_attr.ca_name, m0_bitstring_buf_get(fd->fd_dirpos),
                    m0_bitstring_len_get(fd->fd_dirpos));
        mo.mo_attr.ca_tfid = ci->ci_fid;

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

                for (ent = dirent_first(rep); ent != NULL; ent = dirent_next(ent)) {
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
                                   TODO: Entry type is hardcoded to regular
                                   files, should be fixed later.
                                 */
                                ino = ++i;
                                type = DT_REG;
                        }

                        M0_LOG(M0_DEBUG, "filled off %lu ino %lu name \"%*s\"",
                               (unsigned long)f->f_pos, (unsigned long)ino,
                               ent->d_namelen, (char *)ent->d_name);

                        over = filldir(buf, ent->d_name, ent->d_namelen,
                                       f->f_pos, ino, type);
                        if (over) {
                                rc = 0;
                                goto out;
                        }
                        f->f_pos++;
                }
                m0_bitstring_copy(fd->fd_dirpos, rep->r_end.s_buf, rep->r_end.s_len);
                m0_buf_init(&mo.mo_attr.ca_name, m0_bitstring_buf_get(fd->fd_dirpos),
                            m0_bitstring_len_get(fd->fd_dirpos));

                M0_LOG(M0_DEBUG, "set position to \"%*s\" rc == %d",
                       (int)mo.mo_attr.ca_name.b_nob,
                       (char *)mo.mo_attr.ca_name.b_addr, rc);
                /**
                   Return codes for m0t1fs_mds_cob_readdir() are the following:
                   - <0 - some error occured;
                   - >0 - EOF signaled by mdservice, some number of entries available;
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
        mo.mo_attr.ca_pfid = csb->csb_root_fid;
        mo.mo_attr.ca_tfid  = ci->ci_fid;
        mo.mo_attr.ca_nlink = inode->i_nlink + 1;
        mo.mo_attr.ca_ctime = now.tv_sec;
        mo.mo_attr.ca_flags = (M0_COB_CTIME | M0_COB_NLINK);
        m0_buf_init(&mo.mo_attr.ca_name, (char *)new->d_name.name, new->d_name.len);

        rc = m0t1fs_mds_cob_link(csb, &mo, &link_rep);
        if (rc != 0) {
                M0_LOG(M0_ERROR, "mdservive link fop failed: %d\n", rc);
                goto out;
        }

        inc_nlink(inode);
        inode->i_ctime = now;
        mark_inode_dirty(inode);
        atomic_inc(&inode->i_count);
        d_instantiate(new, inode);

out:
        m0t1fs_fs_unlock(csb);
        M0_LEAVE("rc: %d", rc);
        return rc;
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
        mo.mo_attr.ca_pfid = csb->csb_root_fid;
        mo.mo_attr.ca_tfid = ci->ci_fid;
        mo.mo_attr.ca_nlink = inode->i_nlink - 1;
        mo.mo_attr.ca_ctime = now.tv_sec;
        mo.mo_attr.ca_flags = (M0_COB_NLINK | M0_COB_CTIME);
        m0_buf_init(&mo.mo_attr.ca_name,
                    (char *)dentry->d_name.name, dentry->d_name.len);

        rc = m0t1fs_mds_cob_lookup(csb, &mo, &lookup_rep);
        if (rc != 0)
                goto out;

        rc = m0t1fs_mds_cob_unlink(csb, &mo, &unlink_rep);
        if (rc != 0) {
                M0_LOG(M0_ERROR, "mdservive unlink fop failed: %d\n", rc);
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
        mo.mo_attr.ca_tfid  = csb->csb_root_fid;
        mo.mo_attr.ca_ctime = now.tv_sec;
        mo.mo_attr.ca_mtime = now.tv_sec;
        mo.mo_attr.ca_flags = (M0_COB_CTIME | M0_COB_MTIME);

        rc = m0t1fs_mds_cob_setattr(csb, &mo, &setattr_rep);
        if (rc != 0) {
                M0_LOG(M0_ERROR, "Setattr on parent dir failed with %d", rc);
                goto out;
        }

        inode->i_ctime = dir->i_ctime = dir->i_mtime = now;
        inode_dec_link_count(inode);
out:
        m0t1fs_fs_unlock(csb);
        M0_LEAVE("rc: %d", rc);
        return rc;
}

M0_INTERNAL int m0t1fs_getattr(struct vfsmount *mnt, struct dentry *dentry,
                               struct kstat *stat)
{
        struct m0_fop_getattr_rep       *getattr_rep;
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

        m0t1fs_fs_lock(csb);

        M0_SET0(&mo);
        mo.mo_attr.ca_tfid  = ci->ci_fid;

        /**
           TODO: When we have dlm locking working, this will be changed to
           revalidate inode with checking cached lock. If lock is cached
           (not canceled), which means inode did not change, then we don't
           have to do getattr and can just use @inode cached data.
         */
        rc = m0t1fs_mds_cob_getattr(csb, &mo, &getattr_rep);
        if (rc != 0)
                goto out;

        /** Update inode fields with data from @getattr_rep. */
        rc = m0t1fs_inode_update(inode, &getattr_rep->g_body);
        if (rc != 0)
                goto out;

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
        M0_LEAVE("rc: %d", rc);
        return rc;
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
        mo.mo_attr.ca_tfid  = ci->ci_fid;
        mo.mo_attr.ca_size = newsize;
        mo.mo_attr.ca_flags |= M0_COB_SIZE;

        rc = m0t1fs_mds_cob_setattr(csb, &mo, &setattr_rep);
        if (rc != 0)
                goto out;
        inode->i_size = newsize;
out:
        m0t1fs_fs_unlock(csb);
        return rc;
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
        mo.mo_attr.ca_tfid  = ci->ci_fid;

        if (attr->ia_valid & ATTR_CTIME) {
                mo.mo_attr.ca_ctime = attr->ia_ctime.tv_sec;
                mo.mo_attr.ca_flags |= M0_COB_CTIME;
        }

        if (attr->ia_valid & ATTR_MTIME) {
                mo.mo_attr.ca_mtime = attr->ia_mtime.tv_sec;
                mo.mo_attr.ca_flags |= M0_COB_MTIME;
        }

        if (attr->ia_valid & ATTR_ATIME) {
                mo.mo_attr.ca_atime = attr->ia_atime.tv_sec;
                mo.mo_attr.ca_flags |= M0_COB_ATIME;
        }

        if (attr->ia_valid & ATTR_SIZE) {
                mo.mo_attr.ca_size = attr->ia_size;
                mo.mo_attr.ca_flags |= M0_COB_SIZE;
        }

        if (attr->ia_valid & ATTR_MODE) {
                mo.mo_attr.ca_mode = attr->ia_mode;
                mo.mo_attr.ca_flags |= M0_COB_MODE;
        }

        if (attr->ia_valid & ATTR_UID) {
                mo.mo_attr.ca_uid = attr->ia_uid;
                mo.mo_attr.ca_flags |= M0_COB_UID;
        }

        if (attr->ia_valid & ATTR_GID) {
                mo.mo_attr.ca_gid = attr->ia_gid;
                mo.mo_attr.ca_flags |= M0_COB_GID;
        }

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

	M0_PRE(ci->ci_fid.f_container == 0);
	M0_PRE(ci->ci_layout_instance != NULL);
	M0_PRE(index >= 0);

	le = m0_layout_instance_to_enum(ci->ci_layout_instance);

	m0_layout_enum_get(le, index, &ci->ci_fid, &fid);

	M0_LEAVE("fid: [%lu:%lu]", (unsigned long)fid.f_container,
				   (unsigned long)fid.f_key);
	return fid;
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
	int               rc;

	M0_PRE(ci != NULL);
	M0_PRE(func != NULL);

	M0_ENTRY();

	M0_LOG(M0_DEBUG, "Component object %s for [%lu:%lu]",
		func == m0t1fs_ios_cob_create? "create" : "delete",
		(unsigned long)ci->ci_fid.f_container,
		(unsigned long)ci->ci_fid.f_key);

	csb = M0T1FS_SB(ci->ci_inode.i_sb);
	pool_width = csb->csb_pool_width;
	M0_ASSERT(pool_width >= 1);

	for (i = 0; i < pool_width; ++i) {
		cob_fid = m0t1fs_ios_cob_fid(ci, i);
		rc      = func(csb, &cob_fid, &ci->ci_fid, i);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "Cob %s [%lu:%lu] failed with %d",
				func == m0t1fs_ios_cob_create ? "create" : "delete",
				(unsigned long)cob_fid.f_container,
				(unsigned long)cob_fid.f_key, rc);
			goto out;
		}
	}
out:
	M0_LEAVE("rc: %d", rc);
	return rc;
}

static int m0t1fs_mds_cob_fop_populate(const struct m0t1fs_mdop *mo,
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
        struct m0_fop_cob       *req;
        int                      rc = 0;

        switch (m0_fop_opcode(fop)) {
        case M0_MDSERVICE_CREATE_OPCODE:
                create = m0_fop_data(fop);
                req = &create->c_body;

                req->b_pfid = mo->mo_attr.ca_pfid;
                req->b_tfid = mo->mo_attr.ca_tfid;
                body_mem2wire(req, &mo->mo_attr, mo->mo_attr.ca_flags);
                rc = name_mem2wire(&create->c_name, &mo->mo_attr.ca_name);
                break;
        case M0_MDSERVICE_LINK_OPCODE:
                link = m0_fop_data(fop);
                req = &link->l_body;

                req->b_pfid = mo->mo_attr.ca_pfid;
                req->b_tfid = mo->mo_attr.ca_tfid;
                body_mem2wire(req, &mo->mo_attr, mo->mo_attr.ca_flags);
                rc = name_mem2wire(&link->l_name, &mo->mo_attr.ca_name);
                break;
        case M0_MDSERVICE_UNLINK_OPCODE:
                unlink = m0_fop_data(fop);
                req = &unlink->u_body;

                req->b_pfid = mo->mo_attr.ca_pfid;
                req->b_tfid = mo->mo_attr.ca_tfid;
                body_mem2wire(req, &mo->mo_attr, mo->mo_attr.ca_flags);
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
                body_mem2wire(req, &mo->mo_attr, mo->mo_attr.ca_flags);
                break;
        case M0_MDSERVICE_READDIR_OPCODE:
                readdir = m0_fop_data(fop);
                req = &readdir->r_body;

                req->b_tfid = mo->mo_attr.ca_tfid;
                rc = name_mem2wire(&readdir->r_pos, &mo->mo_attr.ca_name);
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

        rc = m0t1fs_mds_cob_fop_populate(mo, fop);
        if (rc != 0) {
                M0_LOG(M0_ERROR,
                       "m0t1fs_mds_cob_fop_populate() failed with %d", rc);
                m0_fop_free(fop);
                goto out;
        }

        M0_LOG(M0_DEBUG, "Send md operation %x to session %lu\n",
                m0_fop_opcode(fop), (unsigned long)session->s_session_id);

        rc = m0_rpc_client_call(fop, session, &m0_fop_default_item_ops,
                                0 /* deadline */, M0T1FS_RPC_TIMEOUT);

        if (rc != 0) {
                M0_LOG(M0_ERROR,
                       "m0_rpc_client_call(%x) failed with %d", m0_fop_opcode(fop), rc);
                goto out;
        }

        switch (m0_fop_opcode(fop)) {
        case M0_MDSERVICE_CREATE_OPCODE:
                create_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = create_rep->c_body.b_rc;
                *rep = create_rep;
                break;
        case M0_MDSERVICE_STATFS_OPCODE:
                statfs_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = statfs_rep->f_rc;
                *rep = statfs_rep;
                break;
        case M0_MDSERVICE_LOOKUP_OPCODE:
                lookup_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = lookup_rep->l_body.b_rc;
                *rep = lookup_rep;
                break;
        case M0_MDSERVICE_LINK_OPCODE:
                link_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = link_rep->l_body.b_rc;
                *rep = link_rep;
                break;
        case M0_MDSERVICE_UNLINK_OPCODE:
                unlink_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = unlink_rep->u_body.b_rc;
                *rep = unlink_rep;
                break;
        case M0_MDSERVICE_RENAME_OPCODE:
                rename_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = rename_rep->r_body.b_rc;
                *rep = rename_rep;
                break;
        case M0_MDSERVICE_SETATTR_OPCODE:
                setattr_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = setattr_rep->s_body.b_rc;
                *rep = setattr_rep;
                break;
        case M0_MDSERVICE_GETATTR_OPCODE:
                getattr_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = getattr_rep->g_body.b_rc;
                *rep = getattr_rep;
                break;
        case M0_MDSERVICE_OPEN_OPCODE:
                open_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = open_rep->o_body.b_rc;
                *rep = open_rep;
                break;
        case M0_MDSERVICE_CLOSE_OPCODE:
                close_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = close_rep->c_body.b_rc;
                *rep = close_rep;
                break;
        case M0_MDSERVICE_READDIR_OPCODE:
                readdir_rep = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = readdir_rep->r_body.b_rc;
                *rep = readdir_rep;
                break;
        default:
                M0_LOG(M0_ERROR, "Unexpected fop opcode %x", m0_fop_opcode(fop));
                rc = -ENOSYS;
                goto out;
        }

        /*
         * Fop is deallocated by rpc layer using
         * cob_req_rpc_item_ops->rio_free() rpc item ops.
         */
out:
        M0_LEAVE("%d", rc);
        return rc;
}

int m0t1fs_mds_statfs(struct m0t1fs_sb              *csb,
                      struct m0_fop_statfs_rep     **rep)
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
	if (rc != 0) {
		m0_fop_free(fop);
		goto out;
	}

	M0_LOG(M0_DEBUG, "Send %s [%lu:%lu] to session %lu\n",
		cobcreate ? "cob_create" : "cob_delete",
		(unsigned long)cob_fid->f_container,
		(unsigned long)cob_fid->f_key,
		(unsigned long)session->s_session_id);

	rc = m0_rpc_client_call(fop, session, &cob_req_rpc_item_ops,
				0 /* deadline */, M0T1FS_RPC_TIMEOUT);

	if (rc != 0)
		goto out;

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
		cli = &csb->csb_pool.po_mach->pm_state.pst_version;
		srv = (struct m0_pool_version_numbers *)&reply->cor_fv_version;
		*cli = *srv;
		while (i < reply->cor_fv_updates.fvu_count) {
			event = &reply->cor_fv_updates.fvu_events[i];
			m0_poolmach_state_transit(csb->csb_pool.po_mach,
						  (struct m0_pool_event*)event);
			i++;
		}
	} else
		rc = reply->cor_rc;

        M0_LOG(M0_DEBUG, "Finished ioservice op with %d", rc);
	/*
	 * Fop is deallocated by rpc layer using
	 * cob_req_rpc_item_ops->rio_free() rpc item ops.
	 */

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
