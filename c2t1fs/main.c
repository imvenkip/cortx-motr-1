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

static int c2t1fs_inode_cache_init(void);
static void c2t1fs_inode_cache_fini(void);

struct kmem_cache *c2t1fs_inode_cachep = NULL;

static struct file_system_type c2t1fs_fs_type = {
	.owner        = THIS_MODULE,
	.name         = "c2t1fs",
	.get_sb       = c2t1fs_get_sb,
	.kill_sb      = c2t1fs_kill_sb,
	.fs_flags     = FS_BINARY_MOUNTDATA | FS_REQUIRES_DEV
};

static struct super_operations c2t1fs_super_operations = {
	.alloc_inode   = NULL,
	.destroy_inode = NULL,
	.put_super     = NULL,
	.statfs        = NULL,
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

static int c2t1fs_inode_cache_init(void)
{
	int rc = 0;

	START();

	c2t1fs_inode_cachep = kmem_cache_create("c2t1fs_inode_cache",
						sizeof(struct c2t1fs_inode_info),
						0, SLAB_HWCACHE_ALIGN, NULL);
	if (c2t1fs_inode_cachep == NULL)
		rc = -ENOMEM;

	END(rc);
	return rc;
}

static void c2t1fs_inode_cache_fini(void)
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
	struct c2t1fs_sb_info *csi;
	int                    rc;
	START();

	csi = kmalloc(sizeof (*csi), GFP_KERNEL);
	if (csi == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2t1fs_sb_info_init(csi);
	if (rc != 0)
		goto out;

	rc = c2t1fs_mnt_opts_parse(data, &csi->csi_mnt_opts);
	if (rc != 0)
		goto out;

	sb->s_fs_info = csi;

	sb->s_blocksize      = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic          = C2T1FS_SUPER_MAGIC;
	sb->s_maxbytes       = MAX_LFS_FILESIZE;
	sb->s_op             = &c2t1fs_super_operations;

out:
	if (csi != NULL)
		kfree(csi);
	sb->s_fs_info = NULL;
	END(rc);
	return rc;
}
static void c2t1fs_kill_sb(struct super_block *sb)
{

}

int c2t1fs_sb_info_init(struct c2t1fs_sb_info *csi)
{
	START();

	c2_mutex_init(&csi->csi_mutex);
	csi->csi_flags = 0;
	csi->csi_mnt_opts.mo_options = NULL;

	END(0);
	return 0;
}
void c2t1fs_sb_info_fini(struct c2t1fs_sb_info *csi)
{
	START();

	c2_mutex_fini(&csi->csi_mutex);

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

	/* Parse mount options */

	END(rc);
	return rc;
}
