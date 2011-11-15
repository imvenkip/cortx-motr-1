#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "c2t1fs/c2t1fs.h"
#include "lib/misc.h"

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
			      struct c2_fid        fid);

static int c2t1fs_readdir(struct file *f,
			  void        *dirent,
			  filldir_t    filldir);

static int c2t1fs_unlink(struct inode *dir, struct dentry *dentry);

static int c2t1fs_create_target_objects(struct c2t1fs_inode *ci);
int c2t1fs_cob_create(struct c2t1fs_sb *csb, struct c2_fid cob_fid);
int c2t1fs_inode_layout_init(struct c2t1fs_inode *ci, int N, int K, int P);

struct file_operations c2t1fs_dir_file_operations = {
	.read    = generic_read_dir,
	.readdir = c2t1fs_readdir,
	.fsync   = simple_fsync,
	.llseek  = generic_file_llseek,
};

struct inode_operations c2t1fs_dir_inode_operations = {
	.create = c2t1fs_create,
	.lookup = c2t1fs_lookup,
	.unlink = c2t1fs_unlink
};

static struct c2_fid c2t1fs_fid_alloc(void)
{
	struct c2_fid fid;
	static int    key = 3;

	fid.f_container = 0;
	fid.f_key = key++;

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

	START();

	/* Flat file system. create allowed only on root directory */
	C2_ASSERT(c2t1fs_inode_is_root(dir));

	inode = new_inode(sb);
	if (inode == NULL) {
		rc = -ENOSPC;
		goto out;
	}
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	inode->i_blocks = 0;
	inode->i_op = &c2t1fs_reg_inode_operations;
	inode->i_fop = &c2t1fs_reg_file_operations;
	inode->i_mode = mode;

	ci = C2T1FS_I(inode);
	ci->ci_fid = c2t1fs_fid_alloc();
	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	rc = c2t1fs_inode_layout_init(ci, csb->csb_nr_data_units,
					  csb->csb_nr_parity_units,
					  csb->csb_nr_containers);
	if (rc != 0)
		goto out;

	rc = c2t1fs_create_target_objects(ci);
	if (rc != 0)
		goto out;

	rc = c2t1fs_dir_ent_add(dir, dentry->d_name.name, dentry->d_name.len,
					ci->ci_fid);
	if (rc != 0)
		goto out;

	d_instantiate(dentry, inode);
	END(0);
	return 0;
out:
	inode_dec_link_count(inode);
	iput(inode);
	END(rc);
	return rc;
}

static int c2t1fs_dir_ent_add(struct inode        *dir,
			      const unsigned char *name,
			      int                  namelen,
			      struct c2_fid        fid)
{
	struct c2t1fs_inode   *ci;
	struct c2t1fs_dir_ent *de;
	int                    rc;

	TRACE("name=\"%s\" namelen=%d\n", name, namelen);

	C2_ASSERT(c2t1fs_inode_is_root(dir));

	if (namelen == 0) {
		rc = -ENOENT;
		goto out;
	}

	if (namelen >= C2T1FS_MAX_NAME_LEN) {
		rc = -ENAMETOOLONG;
		goto out;
	}

	ci = C2T1FS_I(dir);
	if (ci->ci_nr_dir_ents > C2T1FS_MAX_NR_DIR_ENTS) {
		rc = -ENOSPC;
		goto out;
	}

	de = &ci->ci_dir_ents[ci->ci_nr_dir_ents++];
	memcpy(&de->de_name, name, namelen);
	de->de_name[namelen] = '\0';
	de->de_fid = fid;

	TRACE("Added name: %s[%lu:%lu]\n", de->de_name,
					   (unsigned long)fid.f_container,
					   (unsigned long)fid.f_key);

	mark_inode_dirty(dir);
	rc = 0;
out:
	END(rc);
	return rc;
}

static bool c2t1fs_name_cmp(int len, const unsigned char *name, char *buf)
{
	bool rc;

	START();

	if (len <= C2T1FS_MAX_NAME_LEN && buf[len] != '\0') {
		rc = false;
		goto out;
	}
	TRACE("buf: \"%s\"\n", buf);
	rc = memcmp(name, buf, len) == 0;

out:
	END(rc);
	return rc;
}
static struct c2t1fs_dir_ent *c2t1fs_dir_ent_find(struct inode        *dir,
						  const unsigned char *name,
						  int                  namelen)
{
	struct c2t1fs_inode   *ci;
	struct c2t1fs_dir_ent *de = NULL;
	int                    i;

	START();

	TRACE("Name: \"%s\"\n", name);

	ci = C2T1FS_I(dir);
	for (i = 0; i < ci->ci_nr_dir_ents; i++) {
		de = &ci->ci_dir_ents[i];
		C2_ASSERT(de != NULL);

		if (c2t1fs_name_cmp(namelen, name, de->de_name)) {
			END(de);
			return de;
		}
	}
	END(NULL);
	return NULL;
}
static struct dentry *c2t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    struct nameidata *nd)
{
	struct c2t1fs_dir_ent *de;
	struct inode          *inode = NULL;

	START();

	if (dentry->d_name.len > C2T1FS_MAX_NAME_LEN) {
		END(-ENAMETOOLONG);
		return ERR_PTR(-ENAMETOOLONG);
	}

	/* XXX This is unsafe. d_name.name is not a '\0' terminated string */
	TRACE("Name: \"%s\"\n", dentry->d_name.name);

	de = c2t1fs_dir_ent_find(dir, dentry->d_name.name, dentry->d_name.len);
	if (de != NULL) {
		inode = c2t1fs_iget(dir->i_sb, &de->de_fid);
		if (IS_ERR(inode)) {
			END(ERR_CAST(inode));
			return ERR_CAST(inode);
		}
	}

	d_add(dentry, inode);
	END(NULL);
	return NULL;
}

static int c2t1fs_readdir(struct file *f,
			  void        *dirent,
			  filldir_t    filldir)
{
	struct dentry       *dentry;
	struct inode        *dir;
	struct c2t1fs_inode *ci;
	ino_t                ino;
	int                  i;
	int                  j;
	int                  rc;

	START();

	dentry = f->f_path.dentry;
	dir    = dentry->d_inode;
	ci     = C2T1FS_I(dir);
	i      = f->f_pos;

	switch (i) {
	case 0:
		ino = dir->i_ino;
		if (filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
			break;
		TRACE("filled: \".\"\n");
		f->f_pos++;
		i++;
		/* Fallthrough */
	case 1:
		ino = parent_ino(dentry);
		if (filldir(dirent, "..", 2, i, ino, DT_DIR) < 0)
			break;
		TRACE("filled: \"..\"\n");
		f->f_pos++;
		i++;
		/* Fallthrough */
	default:
		j = i - 2;
		while (j < ci->ci_nr_dir_ents) {
			struct c2t1fs_dir_ent *de;
			char                  *name;
			int                    namelen;

			de      = &ci->ci_dir_ents[j];
			name    = de->de_name;
			namelen = strlen(name);

			rc = filldir(dirent, name, namelen, f->f_pos,
					i, DT_REG);
			if (rc < 0)
				goto out;
			TRACE("filled: \"%s\"\n", name);

			j++;
			f->f_pos++;
		}
	}
out:
	END(0);
	return 0;
}

static int c2t1fs_dir_ent_remove(struct inode *dir, struct c2t1fs_dir_ent *de)
{
	struct c2t1fs_inode   *ci;
	int                    rc;
	int                    i;
	int                    nr_de;

	START();

	ci = C2T1FS_I(dir);
	nr_de = ci->ci_nr_dir_ents;

	i = de - &ci->ci_dir_ents[0];

	TRACE("nr_de %d del entry at %d\n", nr_de, i);

	if (nr_de == 0 || i < 0 || i >= nr_de) {
		rc = -ENOENT;
		goto out;
	}

	if (nr_de > 1)
		ci->ci_dir_ents[i] = ci->ci_dir_ents[nr_de - 1];

	C2_SET0(&ci->ci_dir_ents[nr_de - 1]);
	ci->ci_nr_dir_ents--;
	rc = 0;
out:
	END(rc);
	return rc;
}
static int c2t1fs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct c2t1fs_dir_ent *de;
	struct inode          *inode;
	int                    rc;

	START();

	TRACE("Name: \"%s\"\n", dentry->d_name.name);

	inode = dentry->d_inode;

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
	END(rc);
	return rc;
}

struct c2_fid c2t1fs_target_fid(const struct c2_fid gob_fid, int index)
{
	struct c2_fid fid;

	START();

	fid.f_container = index;
	fid.f_key       = gob_fid.f_key;

	TRACE("Out: [%lu:%lu]\n", (unsigned long)fid.f_container,
				  (unsigned long)fid.f_key);
	END(0);
	return fid;
}
static int c2t1fs_create_target_objects(struct c2t1fs_inode *ci)
{
	struct c2t1fs_sb *csb;
	struct c2_fid     gob_fid;
	struct c2_fid     cob_fid;
	int               nr_containers;
	int               i;
	int rc;

	START();

	gob_fid = ci->ci_fid;

	TRACE("Create target objects for [%lu:%lu]\n",
				(unsigned long)gob_fid.f_container,
				(unsigned long)gob_fid.f_key);

	csb = C2T1FS_SB(ci->ci_inode.i_sb);
	nr_containers = csb->csb_nr_containers;
	C2_ASSERT(nr_containers >= 1);

	/* XXX temporary check just to allow testing without connecting to
		any service */
	if (csb->csb_mnt_opts.mo_nr_ios_ep == 0)
		return 0;

	for (i = 1; i <= nr_containers; i++) {
		cob_fid = c2t1fs_target_fid(gob_fid, i);
		rc = c2t1fs_cob_create(csb, cob_fid);
		if (rc != 0) {
			TRACE("Failed: create [%lu:%lu]\n",
				(unsigned long)cob_fid.f_container,
				(unsigned long)cob_fid.f_key);
			goto out;
		}
	}
out:
	END(rc);
	return rc;
}

int c2t1fs_cob_create(struct c2t1fs_sb *csb, struct c2_fid cob_fid)
{
	struct c2_rpc_session *session;

	START();

	session = c2t1fs_container_id_to_session(csb, cob_fid.f_container);
	C2_ASSERT(session != NULL);

	TRACE("Send cob_create [%lu:%lu] to session %lu\n",
				(unsigned long)cob_fid.f_container,
				(unsigned long)cob_fid.f_key,
				(unsigned long)session->s_session_id);
	END(0);
	return 0;
}
