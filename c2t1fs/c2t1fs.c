#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/vfs.h>
#include "c2t1fs.h"

static kmem_cache_t *c2t1fs_inode_cachep = NULL;

MODULE_AUTHOR("Yuriy V. Umanets <yuriy.umanets@clusterstor.com>");
MODULE_DESCRIPTION("Colibri C2 T1 File System");
MODULE_LICENSE("GPL");

static struct c2t1fs_sb_info *c2t1fs_init_csi(struct super_block *sb)
{
        struct c2t1fs_sb_info *csi;
        
	csi = kmalloc(sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return NULL;
        s2csi_nocast(sb) = csi;
        csi->csi_metadata_server = NULL; 
        csi->csi_data_server = NULL; 
        atomic_set(&csi->csi_mounts, 1);
        csi->csi_flags = 0;
	return csi;
}

static int c2t1fs_free_csi(struct super_block *sb)
{
        struct c2t1fs_sb_info *csi = s2csi(sb);

        kfree(csi);
        s2csi_nocast(sb) = NULL;

        return 0;
}

static int c2t1fs_put_csi(struct super_block *sb)
{
        struct c2t1fs_sb_info *csi = s2csi(sb);

        if (atomic_dec_and_test(&csi->csi_mounts)) {
                c2t1fs_free_csi(sb);
                return 1;
        }

        return 0;
}

static struct inode *c2t1fs_alloc_inode(struct super_block *sb)
{
	struct c2t1fs_inode_info *cii;
	
	cii = kmem_cache_alloc(c2t1fs_inode_cachep, SLAB_KERNEL);
	if (!cii)
		return NULL;
	inode_init_once(&cii->cii_vfs_inode);
	return &cii->cii_vfs_inode;
}

static void c2t1fs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(c2t1fs_inode_cachep, i2cii(inode));
}

void c2t1fs_put_super(struct super_block *sb)
{
        c2t1fs_put_csi(sb);
        module_put(THIS_MODULE);
}

struct super_operations c2t1fs_super_operations = {
        .alloc_inode   = c2t1fs_alloc_inode,
        .destroy_inode = c2t1fs_destroy_inode,
        .put_super     = c2t1fs_put_super
};

static int c2t1fs_parse_options(struct super_block *sb, char *options)
{
        struct c2t1fs_sb_info *csi = s2csi(sb);
        char *s1, *s2;
        
        if (!options) {
                printk(KERN_ERR "Missing mount data: check that "
                       "/sbin/mount.c2t1fs is installed.\n");
                return -EINVAL;
        }
        
        s1 = options;
        while (*s1) {
                int clear = 0;

                while (*s1 == ' ' || *s1 == ',')
                        s1++;

                if (strncmp(s1, "ms=", 3) == 0) {
                        csi->csi_metadata_server = s1 + 3; 
                        clear++;
                } else if (strncmp(s1, "ds=", 3) == 0) {
                        csi->csi_data_server = s1 + 3; 
                        clear++;
                }

                /* Find next opt */
                s2 = strchr(s1, ',');
                if (s2 == NULL) {
                        if (clear)
                                *s1 = '\0';
                        break;
                }
                s2++;
                if (clear)
                        memmove(s1, s2, strlen(s2) + 1);
                else
                        s1 = s2;
        }

        if (!csi->csi_metadata_server || !csi->csi_data_server) {
                printk(KERN_ERR "No servers specified "
                       "(need mount option 'metadata_server=...,data_server=...')\n");
                return -EINVAL;
        }

        return 0;
}

static ssize_t c2t1fs_file_read(struct file *file, char *buf, size_t count,
                                loff_t *ppos)
{
        return 0;
}

#ifdef HAVE_FILE_READV
static ssize_t c2t1fs_file_readv(struct file *file, const struct iovec *iov,
                                 unsigned long nr_segs, loff_t *ppos)
{
#else
static ssize_t c2t1fs_file_aio_read(struct kiocb *iocb, char __user *buf, 
                                    size_t count, loff_t pos)
{
#endif
        return 0;
}

static ssize_t c2t1fs_file_write(struct file *file, const char *buf, size_t count,
                                 loff_t *ppos)
{
        return 0;
}

#ifdef HAVE_FILE_WRITEV
static ssize_t c2t1fs_file_writev(struct file *file, const struct iovec *iov,
                                  unsigned long nr_segs, loff_t *ppos)
{
#else /* AIO stuff */
static ssize_t c2t1fs_file_aio_write(struct kiocb *iocb, const char __user *buf, 
                                     size_t count, loff_t pos)
{
#endif
        return 0;
}

static int c2t1fs_file_open(struct inode *inode, struct file *file)
{
        return -ENOSYS;
}

static int c2t1fs_file_release(struct inode *inode, struct file *file)
{
        return -ENOSYS;
}

struct inode_operations c2t1fs_file_inode_operations = {
};

struct file_operations c2t1fs_file_operations = {
        .read           = c2t1fs_file_read,
#ifdef HAVE_FILE_READV
        .readv          = c2t1fs_file_readv,
#else
        .aio_read       = c2t1fs_file_aio_read,
#endif
        .write          = c2t1fs_file_write,
#ifdef HAVE_FILE_WRITEV
        .writev         = c2t1fs_file_writev,
#else
        .aio_write      = c2t1fs_file_aio_write,
#endif
        .open           = c2t1fs_file_open,
        .release        = c2t1fs_file_release
};

static int 
c2t1fs_readdir(struct file *filp, void *cookie, filldir_t filldir)
{
        return -ENOSYS;
}

struct inode_operations c2t1fs_dir_inode_operations = {
};

struct file_operations c2t1fs_dir_operations = {
        .open     = c2t1fs_file_open,
        .release  = c2t1fs_file_release,
        .read     = generic_read_dir,
        .readdir  = c2t1fs_readdir
};

struct address_space_operations c2t1fs_file_aops = {
};

struct address_space_operations c2t1fs_dir_aops = {
};

static int c2t1fs_update_inode(struct inode *inode)
{
        __u32 mode;
        
        if (inode->i_ino == C2T1FS_ROOT_INODE)
                mode = ((S_IRWXUGO|S_ISVTX) & ~current->fs->umask) | S_IFDIR;
        else
                mode = S_IFREG;
        inode->i_mode = (inode->i_mode & S_IFMT) | (mode & ~S_IFMT);
        inode->i_mode = (inode->i_mode & ~S_IFMT) | (mode & S_IFMT);
        if (S_ISREG(inode->i_mode)) {
                /* FIXME: This needs to be taken from rpc layer */
                inode->i_blkbits = min(C2_MAX_BRW_BITS + 1, C2_MAX_BLKSIZE_BITS);
        } else {
                inode->i_blkbits = inode->i_sb->s_blocksize_bits;
        }
#ifdef HAVE_INODE_BLKSIZE
        inode->i_blksize = 1 << inode->i_blkbits;
#endif

        /* FIXME: This needs to be taken from an getattr rpc */
        if (inode->i_ino == C2T1FS_ROOT_INODE)
                inode->i_nlink = 2;
        else
                inode->i_nlink = 1;

        /* FIXME: This should be taken from an getattr rpc */
        if (S_ISDIR(inode->i_mode)) {
                inode->i_size = PAGE_SIZE;
        } else {
                inode->i_size = 0;
        }
        
        /* FIXME: This should be taken from an getattr rpc */
        inode->i_blocks = 0;
        return 0;
}

static int c2t1fs_read_inode(struct inode *inode)
{
        C2TIME_S(inode->i_mtime) = 0;
        C2TIME_S(inode->i_atime) = 0;
        C2TIME_S(inode->i_ctime) = 0;
        inode->i_rdev = 0;

        c2t1fs_update_inode(inode);

        if (S_ISREG(inode->i_mode)) {
                inode->i_op = &c2t1fs_file_inode_operations;
                inode->i_fop = &c2t1fs_file_operations;
                inode->i_mapping->a_ops = &c2t1fs_file_aops;
        } else if (S_ISDIR(inode->i_mode)) {
                inode->i_op = &c2t1fs_dir_inode_operations;
                inode->i_fop = &c2t1fs_dir_operations;
                inode->i_mapping->a_ops = &c2t1fs_dir_aops;
        } else {
                return -ENOSYS;
        }
        return 0;
}

/* called from iget5_locked->find_inode() under inode_lock spinlock */
static int c2t1fs_test_inode(struct inode *inode, void *opaque)
{
        ino_t *ino = opaque;
        return inode->i_ino == *ino;
}

static int c2t1fs_set_inode(struct inode *inode, void *opaque)
{
        return 0;
}

static struct inode *c2t1fs_iget(struct super_block *sb, ino_t hash)
{
        struct inode *inode;

        inode = iget5_locked(sb, hash, c2t1fs_test_inode, c2t1fs_set_inode, &hash);
        if (inode) {
                if (inode->i_state & I_NEW) {
                        c2t1fs_read_inode(inode);
                        unlock_new_inode(inode);
                } else {
                        if (!(inode->i_state & (I_FREEING | I_CLEAR)))
                                c2t1fs_update_inode(inode);
                }
        }

        return inode;
}

static int c2t1fs_fill_super(struct super_block *sb, void *data, int silent)
{
        struct c2t1fs_sb_info *csi;
        struct inode          *root;
        int rc;
        
        try_module_get(THIS_MODULE);
        
        csi = c2t1fs_init_csi(sb);
        if (!csi)
                return -ENOMEM;
                
        rc = c2t1fs_parse_options(sb, (char *)data);
        if (rc) {
                c2t1fs_put_csi(sb);
                return rc;
        }
        sb->s_blocksize = PAGE_SIZE;
        sb->s_blocksize_bits = log2(PAGE_SIZE);
        sb->s_magic = C2T1FS_SUPER_MAGIC;
        sb->s_maxbytes = MAX_LFS_FILESIZE;
        sb->s_op = &c2t1fs_super_operations;

        /* make root inode */
        root = c2t1fs_iget(sb, C2T1FS_ROOT_INODE);
        if (root == NULL || is_bad_inode(root)) {
                c2t1fs_put_csi(sb);
                return -EBADF;
        }

        sb->s_root = d_alloc_root(root);
        return 0;
}

static int c2t1fs_get_super(struct file_system_type *fs_type,
                            int flags, const char *devname, void *data,
                            struct vfsmount *mnt)
{
        return get_sb_nodev(fs_type, flags, data, c2t1fs_fill_super, mnt);
}

static void c2t1fs_kill_super(struct super_block *sb)
{
        kill_anon_super(sb);
}

struct file_system_type c2t1fs_fs_type = {
        .owner        = THIS_MODULE,
        .name         = "c2t1fs",
        .get_sb       = c2t1fs_get_super,
        .kill_sb      = c2t1fs_kill_super,
        .fs_flags     = FS_BINARY_MOUNTDATA | FS_REQUIRES_DEV
};

static int c2t1fs_init_inodecache(void)
{
	c2t1fs_inode_cachep = kmem_cache_create("c2t1fs_inode_cache",
					        sizeof(struct c2t1fs_inode_info),
					        0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (c2t1fs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void c2t1fs_destroy_inodecache(void)
{
        if (!c2t1fs_inode_cachep)
                return;

	if (kmem_cache_destroy(c2t1fs_inode_cachep))
		printk(KERN_ERR "c2t1fs_destroy_inodecache: not all structures were freed\n");
}

int init_module(void) 
{
        int rc;
        
        printk(KERN_INFO "Colibri C2 T1 File System init: http://www.clusterstor.com\n");
        rc = c2t1fs_init_inodecache();
        if (rc)
                return rc;
        rc = register_filesystem(&c2t1fs_fs_type);
        if (rc)
                c2t1fs_destroy_inodecache();

        return rc;
}
        
void cleanup_module(void)
{
        int rc;
        
        rc = unregister_filesystem(&c2t1fs_fs_type);
        c2t1fs_destroy_inodecache();
        printk(KERN_INFO "Colibri C2 T1 File System cleanup: %d\n", rc);
}
