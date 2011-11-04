#ifndef __COLIBRI_C2T1FS_H
#define __COLIBRI_C2T1FS_H

#include <linux/fs.h>
#include <linux/slab.h>

#include "fid/fid.h"
#include "lib/mutex.h"

#define C2T1FS_DEBUG 1

#ifdef C2T1FS_DEBUG

#   define TRACE(format, args ...)  \
	printk("c2t1fs: %s[%d]: " format, __FUNCTION__, __LINE__, ## args)
#   define START()   TRACE("Start\n")
#   define END(rc)   TRACE("End (0x%lx)\n", (unsigned long)(rc))

#else /* C2T1FS_DEBUG */

#   define TRACE(format, args ...)
#   define START()
#   define END(rc)

#endif /* C2T1FS_DEBUG */

int c2t1fs_init(void);
void c2t1fs_fini(void);

enum {
	/* 0x\C\2\T\1 */
	C2T1FS_SUPER_MAGIC = 0x43325431
};

struct c2t1fs_mnt_opts
{
	char *mo_options;
};

struct c2t1fs_sb_info
{
	struct c2_mutex        csi_mutex;
	struct c2t1fs_mnt_opts csi_mnt_opts;
	uint64_t               csi_flags;
};

struct c2t1fs_inode_info
{
	struct inode  cii_inode;
	struct c2_fid cii_fid;
};

static inline struct c2t1fs_sb_info *C2T1FS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct c2t1fs_inode_info *C2T1FS_I(struct inode *inode)
{
	return container_of(inode, struct c2t1fs_inode_info, cii_inode);
}

int c2t1fs_sb_info_init(struct c2t1fs_sb_info *sbi);
void c2t1fs_sb_info_fini(struct c2t1fs_sb_info *sbi);

int c2t1fs_inode_cache_init(void);
void c2t1fs_inode_cache_fini(void);

struct inode *c2t1fs_root_iget(struct super_block *sb);

extern struct file_operations c2t1fs_dir_operations;
extern struct address_space_operations c2t1fs_dir_aops;

extern struct file_operations c2t1fs_file_operations;

struct inode *c2t1fs_alloc_inode(struct super_block *sb);
void c2t1fs_destroy_inode(struct inode *inode);
#endif /* __COLIBRI_C2T1FS_H */
