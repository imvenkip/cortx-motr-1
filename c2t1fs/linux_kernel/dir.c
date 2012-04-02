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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 10/14/2011
 */

#include "lib/misc.h"      /* C2_SET0() */
#include "lib/memory.h"    /* C2_ALLOC_PTR() */
#include "c2t1fs.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"     /* C2_LOG and C2_ENTRY */

static int c2t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 int               mode,
			 struct nameidata *nd);

static struct dentry *c2t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    struct nameidata *nd);

static int c2t1fs_dir_ent_add(struct inode        *dir,
			      const unsigned char *name,
			      int                  namelen,
			      const struct c2_fid *fid);

static int c2t1fs_readdir(struct file *f,
			  void        *dirent,
			  filldir_t    filldir);

static int c2t1fs_unlink(struct inode *dir, struct dentry *dentry);

static int c2t1fs_create_component_objects(struct c2t1fs_inode *ci);

static int c2t1fs_cob_create(struct c2t1fs_sb    *csb,
			     const struct c2_fid *cob_fid);

const struct file_operations c2t1fs_dir_file_operations = {
	.read    = generic_read_dir,    /* provided by linux kernel */
	.readdir = c2t1fs_readdir,
	.fsync   = simple_fsync,	/* provided by linux kernel */
	.llseek  = generic_file_llseek, /* provided by linux kernel */
};

const struct inode_operations c2t1fs_dir_inode_operations = {
	.create = c2t1fs_create,
	.lookup = c2t1fs_lookup,
	.unlink = c2t1fs_unlink
};

/**
   Allocate fid of global file.

   See "Containers and component objects" section in c2t1fs.h for
   more information.

   XXX temporary.
 */
static struct c2_fid c2t1fs_fid_alloc(struct c2t1fs_sb *csb)
{
	struct c2_fid fid;

	C2_PRE(c2t1fs_fs_is_locked(csb));

	fid.f_container = 0;
	fid.f_key       = csb->csb_next_key++;

	return fid;
}

static int c2t1fs_create(struct inode     *dir,
                         struct dentry    *dentry,
                         int               mode,
                         struct nameidata *nd)
{
	struct super_block  *sb = dir->i_sb;
	struct c2t1fs_sb    *csb = C2T1FS_SB(sb);
	struct c2t1fs_inode *ci;
	struct inode        *inode;
	int                  rc;

	C2_ENTRY();

	/* Flat file system. create allowed only on root directory */
	C2_ASSERT(c2t1fs_inode_is_root(dir));

	/* new_inode() will call c2t1fs_alloc_inode() using super_operations */
	inode = new_inode(sb);
	if (inode == NULL) {
		C2_LEAVE("rc: %d", -ENOMEM);
		return -ENOMEM;
	}

	c2t1fs_fs_lock(csb);

	inode->i_uid    = 0;
	inode->i_gid    = 0;
	inode->i_mtime  = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	inode->i_blocks = 0;
	inode->i_op     = &c2t1fs_reg_inode_operations;
	inode->i_fop    = &c2t1fs_reg_file_operations;
	inode->i_mode   = mode;

	ci              = C2T1FS_I(inode);
	ci->ci_fid      = c2t1fs_fid_alloc(csb);
	inode->i_ino    = ci->ci_fid.f_key;

	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	rc = c2t1fs_inode_layout_init(ci, csb->csb_nr_data_units,
					  csb->csb_nr_parity_units,
					  csb->csb_pool_width,
					  csb->csb_unit_size);
	if (rc != 0)
		goto out;

	rc = c2t1fs_create_component_objects(ci);
	if (rc != 0)
		goto out;

	rc = c2t1fs_dir_ent_add(dir, dentry->d_name.name, dentry->d_name.len,
					&ci->ci_fid);
	if (rc != 0)
		goto out;

	c2t1fs_fs_unlock(csb);

	d_instantiate(dentry, inode);
	C2_LEAVE("rc: 0");
	return 0;
out:
	inode_dec_link_count(inode);
	c2t1fs_fs_unlock(csb);
	iput(inode);

	C2_LEAVE("rc: %d", rc);
	return rc;
}

void c2t1fs_dir_ent_init(struct c2t1fs_dir_ent *de,
			 const unsigned char   *name,
			 int                    namelen,
			 const struct c2_fid   *fid)
{
	C2_ENTRY();

	memcpy(&de->de_name, name, namelen);
	de->de_name[namelen] = '\0';
	de->de_fid           = *fid;
	de->de_magic         = MAGIC_DIRENT;

	dir_ents_tlink_init(de);

	C2_LEAVE();
}

void c2t1fs_dir_ent_fini(struct c2t1fs_dir_ent *de)
{
	C2_ENTRY();

	dir_ents_tlink_fini(de);
	de->de_magic = 0;

	C2_LEAVE();
}

static int c2t1fs_dir_ent_add(struct inode        *dir,
			      const unsigned char *name,
			      int                  namelen,
			      const struct c2_fid *fid)
{
	struct c2t1fs_inode   *ci;
	struct c2t1fs_dir_ent *de;
	int                    rc;

	C2_ENTRY("name=\"%s\" namelen=%d", name, namelen);

	C2_ASSERT(c2t1fs_inode_is_root(dir));

	if (namelen == 0) {
		rc = -EINVAL;
		goto out;
	}

	if (namelen >= C2T1FS_MAX_NAME_LEN) {
		rc = -ENAMETOOLONG;
		goto out;
	}

	ci = C2T1FS_I(dir);

	C2_ALLOC_PTR(de);
	if (de == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	c2t1fs_dir_ent_init(de, name, namelen, fid);
	dir_ents_tlist_add_tail(&ci->ci_dir_ents, de);

	C2_LOG("Added name: %s[%lu:%lu]", (char *)de->de_name,
					  (unsigned long)fid->f_container,
					  (unsigned long)fid->f_key);

	mark_inode_dirty(dir);
	rc = 0;
out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static bool name_eq(const unsigned char *name, const char *buf, int len)
{
	bool rc;

	C2_ENTRY();

	if (len <= C2T1FS_MAX_NAME_LEN && buf[len] != '\0') {
		rc = false;
	} else {
		C2_LOG("buf: \"%s\" name: \"%s\" len: %d", buf, name, len);
		rc = (memcmp(name, buf, len) == 0);
	}

	C2_LEAVE("rc: %d", rc);
	return rc;
}

static struct c2t1fs_dir_ent *c2t1fs_dir_ent_find(struct inode        *dir,
						  const unsigned char *name,
						  int                  namelen)
{
	struct c2t1fs_inode   *ci;
	struct c2t1fs_sb      *csb;
	struct c2t1fs_dir_ent *de = NULL;

	C2_ENTRY();

	C2_ASSERT(name != NULL && dir != NULL);

	C2_LOG("Name: \"%s\"", name);

	ci  = C2T1FS_I(dir);
	csb = C2T1FS_SB(dir->i_sb);

	C2_ASSERT(c2t1fs_fs_is_locked(csb));

	c2_tlist_for(&dir_ents_tl, &ci->ci_dir_ents, de) {

		if (name_eq(name, de->de_name, namelen))
			break;

	} c2_tlist_endfor;

	C2_LEAVE("de: %p", de);
	return de;
}

static struct dentry *c2t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    struct nameidata *nd)
{
	struct c2t1fs_sb      *csb;
	struct c2t1fs_dir_ent *de;
	struct inode          *inode = NULL;

	C2_ENTRY();

	if (dentry->d_name.len > C2T1FS_MAX_NAME_LEN) {
		C2_LEAVE("ERR_PTR: %p", ERR_PTR(-ENAMETOOLONG));
		return ERR_PTR(-ENAMETOOLONG);
	}

	C2_LOG("Name: \"%s\"", dentry->d_name.name);

	csb = C2T1FS_SB(dir->i_sb);

	c2t1fs_fs_lock(csb);

	de = c2t1fs_dir_ent_find(dir, dentry->d_name.name, dentry->d_name.len);
	if (de != NULL) {
		inode = c2t1fs_iget(dir->i_sb, &de->de_fid);
		if (IS_ERR(inode)) {
			c2t1fs_fs_unlock(csb);
			C2_LEAVE("ERROR: %p", ERR_CAST(inode));
			return ERR_CAST(inode);
		}
	}

	c2t1fs_fs_unlock(csb);
	d_add(dentry, inode);
	C2_LEAVE("NULL");
	return NULL;
}

static int c2t1fs_readdir(struct file *f,
			  void        *buf,
			  filldir_t    filldir)
{
	struct c2t1fs_dir_ent *de;
	struct c2t1fs_inode   *ci;
	struct c2t1fs_sb      *csb;
	struct dentry         *dentry;
	struct inode          *dir;
	ino_t                  ino;
	int                    i;
	int                    skip;
	int                    rc;

	C2_ENTRY();

	dentry = f->f_path.dentry;
	dir    = dentry->d_inode;
	ci     = C2T1FS_I(dir);
	csb    = C2T1FS_SB(dir->i_sb);
	i      = f->f_pos;

	c2t1fs_fs_lock(csb);

	switch (i) {
	case 0:
		ino = dir->i_ino;
		C2_LOG("i = %d ino = %lu", i, (unsigned long)ino);
		if (filldir(buf, ".", 1, i, ino, DT_DIR) < 0)
			break;
		C2_LOG("filled: \".\"");
		f->f_pos++;
		i++;
		/* Fallthrough */
	case 1:
		ino = parent_ino(dentry);
		C2_LOG("i = %d ino = %lu", i, (unsigned long)ino);
		if (filldir(buf, "..", 2, i, 4, DT_DIR) < 0)
			break;
		C2_LOG("filled: \"..\"");
		f->f_pos++;
		i++;
		/* Fallthrough */
	default:
		/* previous call to readdir() returned f->f_pos number of
		   entries Now we should continue after that point */
		skip = i - 2;
		c2_tlist_for(&dir_ents_tl, &ci->ci_dir_ents, de) {
			char *name;
			int   namelen;

			if (skip != 0) {
				skip--;
				continue;
			}

			name    = de->de_name;
			namelen = strlen(name);

			C2_LOG("off %lu ino %lu", (unsigned long)f->f_pos,
					(unsigned long)i + 1);

			rc = filldir(buf, name, namelen, f->f_pos,
					++i, DT_REG);
			if (rc < 0)
				goto out;
			C2_LOG("filled: \"%s\"", name);

			f->f_pos++;
		} c2_tlist_endfor;
	}
out:
	c2t1fs_fs_unlock(csb);
	C2_LEAVE("rc: 0");
	return 0;
}

static int c2t1fs_dir_ent_remove(struct inode *dir, struct c2t1fs_dir_ent *de)
{
	C2_ENTRY();

	C2_LOG("Name: %s", (char *)de->de_name);
	dir_ents_tlist_del(de);
	c2t1fs_dir_ent_fini(de);
	c2_free(de);

	C2_LEAVE("rc: 0");
	return 0;
}

static int c2t1fs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct c2t1fs_sb      *csb;
	struct c2t1fs_dir_ent *de;
	struct inode          *inode;
	int                    rc;

	/* XXX c2t1fs_unlink() should remove component objects of a file */

	C2_ENTRY();

	C2_LOG("Name: \"%s\"", dentry->d_name.name);

	inode = dentry->d_inode;
	csb   = C2T1FS_SB(inode->i_sb);

	c2t1fs_fs_lock(csb);

	de = c2t1fs_dir_ent_find(dir, dentry->d_name.name, dentry->d_name.len);
	if (de == NULL) {
		rc = -ENOENT;
		goto out;
	}

	rc = c2t1fs_dir_ent_remove(dir, de);
	if (rc != 0)
		goto out;

	dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
	inode->i_ctime = dir->i_ctime;
	inode_dec_link_count(inode);

out:
	c2t1fs_fs_unlock(csb);
	C2_LEAVE("rc: %d", rc);
	return rc;
}

/**
   See "Containers and component objects" section in c2t1fs.h for
   more information.
 */
struct c2_fid c2t1fs_cob_fid(const struct c2_fid *gob_fid, int index)
{
	struct c2_fid fid;

	C2_ENTRY();

	/* index 0 is currently reserved for gob_fid.f_container */
	C2_ASSERT(gob_fid->f_container == 0);
	C2_ASSERT(index > 0);

	fid.f_container = index;
	fid.f_key       = gob_fid->f_key;

	C2_LEAVE("fid: [%lu:%lu]", (unsigned long)fid.f_container,
				   (unsigned long)fid.f_key);
	return fid;
}

static int c2t1fs_create_component_objects(struct c2t1fs_inode *ci)
{
	struct c2t1fs_sb *csb;
	struct c2_fid     gob_fid;
	struct c2_fid     cob_fid;
	int               pool_width;
	int               i;
	int rc;

	C2_ENTRY();

	gob_fid = ci->ci_fid;

	C2_LOG("Create component objects for [%lu:%lu]",
				(unsigned long)gob_fid.f_container,
				(unsigned long)gob_fid.f_key);

	csb = C2T1FS_SB(ci->ci_inode.i_sb);
	pool_width = csb->csb_pool_width;
	C2_ASSERT(pool_width >= 1);

	for (i = 1; i <= pool_width; i++) { /* i = 1 is intentional */

		cob_fid = c2t1fs_cob_fid(&gob_fid, i);
		rc      = c2t1fs_cob_create(csb, &cob_fid);
		if (rc != 0) {
			C2_LOG("Failed: create [%lu:%lu]",
				(unsigned long)cob_fid.f_container,
				(unsigned long)cob_fid.f_key);
			goto out;
		}

	}
out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int c2t1fs_cob_create(struct c2t1fs_sb    *csb,
			     const struct c2_fid *cob_fid)
{
	struct c2_rpc_session *session;

	C2_ENTRY();

	session = c2t1fs_container_id_to_session(csb, cob_fid->f_container);
	C2_ASSERT(session != NULL);

	C2_LOG("Send cob_create [%lu:%lu] to session %lu",
				(unsigned long)cob_fid->f_container,
				(unsigned long)cob_fid->f_key,
				(unsigned long)session->s_session_id);
	C2_LEAVE("rc: 0");
	return 0;
}
