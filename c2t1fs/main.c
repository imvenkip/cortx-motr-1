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
	return 0;
}

static void c2t1fs_kill_sb(struct super_block *sb)
{

}
