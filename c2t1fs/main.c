#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "c2t1fs/c2t1fs.h"

static int c2t1fs_get_sb(struct file_system_type *fstype,
			 int                      flags,
			 const char              *devname,
			 void                    *data,
			 struct vfsmount         *mnt);

static void c2t1fs_kill_sb(struct super_block *sb);

static int c2t1fs_fill_super(struct super_block *sb, void *data, int silent);

static int c2t1fs_mnt_opts_parse(char                   *options,
				 struct c2t1fs_mnt_opts *mnt_opts);

static struct file_system_type c2t1fs_fs_type = {
	.owner        = THIS_MODULE,
	.name         = "c2t1fs",
	.get_sb       = c2t1fs_get_sb,
	.kill_sb      = c2t1fs_kill_sb,
	.fs_flags     = FS_BINARY_MOUNTDATA | FS_REQUIRES_DEV
};

static struct super_operations c2t1fs_super_operations = {
	.alloc_inode   = c2t1fs_alloc_inode,
	.destroy_inode = c2t1fs_destroy_inode,
	.drop_inode    = generic_delete_inode
};

const struct c2_fid c2t1fs_root_fid = {
	.f_container = 0,
	.f_key = 2
};

int c2t1fs_init(void)
{
	int rc;

	START();

	rc = c2t1fs_inode_cache_init();
	if (rc != 0) {
		END(rc);
		return rc;
	}

	rc = register_filesystem(&c2t1fs_fs_type);
	if (rc != 0)
		c2t1fs_inode_cache_fini();

	END(rc);
	return rc;
}

void c2t1fs_fini(void)
{
	int rc;

	START();

	rc = unregister_filesystem(&c2t1fs_fs_type);
	c2t1fs_inode_cache_fini();

	END(rc);
}

static int c2t1fs_get_sb(struct file_system_type *fstype,
			 int                      flags,
			 const char              *devname,
			 void                    *data,
			 struct vfsmount         *mnt)
{
	int rc;

	TRACE("flags: 0x%x, devname: %s, data: %s\n", flags, devname,
							(char *)data);

	rc = get_sb_nodev(fstype, flags, data, c2t1fs_fill_super, mnt);
	if (rc != 0) {
		END(rc);
		return rc;
	}

	/* Establish connections and sessions with all the services */

	END(rc);
	return rc;
}

static int c2t1fs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct c2t1fs_sb *csb;
	struct inode     *root_inode;
	int               rc;

	START();

	csb = kmalloc(sizeof (*csb), GFP_KERNEL);
	if (csb == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2t1fs_sb_init(csb);
	if (rc != 0) {
		kfree(csb);
		csb = NULL;
		goto out;
	}

	rc = c2t1fs_mnt_opts_parse(data, &csb->csb_mnt_opts);
	if (rc != 0)
		goto out;

	sb->s_fs_info = csb;

	sb->s_blocksize      = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic          = C2T1FS_SUPER_MAGIC;
	sb->s_maxbytes       = MAX_LFS_FILESIZE;
	sb->s_op             = &c2t1fs_super_operations;

	/* XXX Talk to confd and fetch configuration */
	/* XXX construct root inode */
	root_inode = c2t1fs_root_iget(sb);
	if (root_inode == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	sb->s_root = d_alloc_root(root_inode);
	if (sb->s_root == NULL) {
		iput(root_inode);
		rc = -ENOMEM;
		goto out;
	}
	return 0;

out:
	if (csb != NULL) {
		c2t1fs_sb_fini(csb);
		kfree(csb);
	}
	sb->s_fs_info = NULL;
	END(rc);
	return rc;
}
static void c2t1fs_kill_sb(struct super_block *sb)
{
	struct c2t1fs_sb *sbi;

	START();

	sbi = C2T1FS_SB(sb);
	c2t1fs_sb_fini(sbi);
	kfree(sbi);
	kill_anon_super(sb);

	END(0);
}

int c2t1fs_sb_init(struct c2t1fs_sb *csb)
{
	START();

	c2_mutex_init(&csb->csb_mutex);
	csb->csb_flags = 0;
	csb->csb_mnt_opts.mo_options = NULL;

	END(0);
	return 0;
}
void c2t1fs_sb_fini(struct c2t1fs_sb *csb)
{
	START();

	c2_mutex_fini(&csb->csb_mutex);

	END(0);
}

static int c2t1fs_mnt_opts_parse(char                   *options,
				 struct c2t1fs_mnt_opts *mnt_opts)
{
	int rc = 0;

	START();

	mnt_opts->mo_options = kstrdup(options, GFP_KERNEL);
	if (mnt_opts->mo_options == NULL)
		rc = -ENOMEM;

	/* XXX Parse mount options */

	END(rc);
	return rc;
}
