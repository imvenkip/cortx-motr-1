#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>

#include "c2t1fs/c2t1fs.h"

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

void c2t1fs_fini(void)
{
	int rc;

	START();

	rc = unregister_filesystem(&c2t1fs_fs_type);
	c2t1fs_inode_cache_fini();

	END(rc);
}

