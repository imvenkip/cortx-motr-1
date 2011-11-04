#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "c2t1fs/c2t1fs.h"

static int c2t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 int               mode,
			 struct nameidata *nd);

static struct kmem_cache *c2t1fs_inode_cachep = NULL;

static struct inode_operations c2t1fs_dir_inode_operations = {
	.create = c2t1fs_create,
	.lookup = simple_lookup,
	.unlink = simple_unlink
};

static struct inode_operations c2t1fs_inode_operations = {
	NULL
};

static void init_once(void *foo)
{
	struct c2t1fs_inode_info *cii = foo;

	START();

	cii->cii_fid.f_container = 0;
	cii->cii_fid.f_key = 0;

	inode_init_once(&cii->cii_inode);

	END(0);
}
int c2t1fs_inode_cache_init(void)
{
	int rc = 0;

	START();

	c2t1fs_inode_cachep = kmem_cache_create("c2t1fs_inode_cache",
					sizeof(struct c2t1fs_inode_info),
					0, SLAB_HWCACHE_ALIGN, init_once);
	if (c2t1fs_inode_cachep == NULL)
		rc = -ENOMEM;

	END(rc);
	return rc;
}

void c2t1fs_inode_cache_fini(void)
{
	START();

	if (c2t1fs_inode_cachep == NULL) {
		END(0);
		return;
	}

	kmem_cache_destroy(c2t1fs_inode_cachep);
	c2t1fs_inode_cachep = NULL;

	END(0);
}

struct inode *c2t1fs_alloc_inode(struct super_block *sb)
{
	struct c2t1fs_inode_info *cii;

	START();

	cii = kmem_cache_alloc(c2t1fs_inode_cachep, GFP_KERNEL);
	if (cii == NULL) {
		END(NULL);
		return NULL;
	}

	END(&cii->cii_inode);
	return &cii->cii_inode;
}

void c2t1fs_destroy_inode(struct inode *inode)
{
	START();
	kmem_cache_free(c2t1fs_inode_cachep, C2T1FS_I(inode));
	END(0);
}

struct inode *c2t1fs_root_iget(struct super_block *sb)
{
	struct inode *inode;

	START();

	inode = new_inode(sb);
	if (inode != NULL) {
		inode->i_mode = S_IFDIR | 0755;
		inode->i_atime = inode->i_mtime = CURRENT_TIME;
		inode->i_ctime = CURRENT_TIME;
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_nlink = 2;
		inode->i_op = &c2t1fs_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
	}
	END(inode);
	return inode;
}

static int c2t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 int               mode,
			 struct nameidata *nd)
{
	struct inode *inode;
	int           error = -ENOSPC;

	START();

	inode = new_inode(dir->i_sb);
	if (inode != NULL) {
		inode->i_mode = S_IFREG | 0755;
		inode->i_atime = inode->i_mtime = CURRENT_TIME;
		inode->i_ctime = CURRENT_TIME;
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_nlink = 1;
		inode->i_op = &c2t1fs_inode_operations;
		inode->i_fop = &c2t1fs_file_operations;

		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
	}
	END(error);
	return error;
}
