#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "c2t1fs/c2t1fs.h"

static struct kmem_cache *c2t1fs_inode_cachep = NULL;
static struct inode_operations c2t1fs_dir_inode_operations = { NULL };

int c2t1fs_inode_cache_init(void)
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

struct inode *c2t1fs_root_iget(struct super_block *sb)
{
	struct inode *inode;
	__u32         mode;

	START();

	inode = new_inode(sb);
	if (inode != NULL) {
		inode->i_rdev = 0;
		mode = S_IRWXUGO | S_ISVTX | S_IFDIR;
		inode->i_mode = mode;
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_blkbits = inode->i_sb->s_blocksize_bits;
		inode->i_nlink = 2;
		inode->i_size = PAGE_SIZE;
		inode->i_blocks = 1;
		inode->i_op = &c2t1fs_dir_inode_operations;
		inode->i_fop = &c2t1fs_dir_operations;
		inode->i_mapping->a_ops = &c2t1fs_dir_aops;
	}
	END(inode);
	return inode;
}
