#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "c2t1fs/c2t1fs.h"

static int c2t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 int               mode,
			 struct nameidata *nd);

struct file_operations c2t1fs_dir_file_operations = { NULL };

struct inode_operations c2t1fs_dir_inode_operations = {
	.create = c2t1fs_create,
	.lookup = simple_lookup,
	.unlink = simple_unlink
};

static int c2t1fs_create(struct inode     *dir,
                         struct dentry    *dentry,
                         int               mode,
                         struct nameidata *nd)
{
	struct inode *inode;
	int           error = -ENOSPC;
	static int    key = 3;
	struct c2_fid fid;

	START();

	fid.f_container = 0;
	fid.f_key = key++;

	inode = c2t1fs_iget(dir->i_sb, &fid);
	if (inode != NULL) {
		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
	}
	END(error);
	return error;
}

