#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/smp_lock.h>
#include <linux/vfs.h>
#include <linux/uio.h>
#include <linux/errno.h>
#include <linux/inet.h>
#include <linux/in.h>

#include "c2t1fs.h"

/**
   @page c2t1fs C2T1FS detailed level design specification.

   @section Overview

   C2T1FS is native linux nodev file system, which lies exactly between colibri
   block device and colibri servers. It is in fact client file system for the
   colibri cluster.

   It is decided that colibri block device is based on loop.c loop back driver.
   This dictates, despite the requirements, specific impelementation, which will
   be discussed below.

   @section def Definitions and requirements

   Here is brief list of requirements:

   @li direct IO support (all the caching is done on upper layet). In
       our case this means no page cache in IO functions. All the IO,
       no matter how big, gets imidiately sent to the server. Do not
       mix it up with cache on upper layer. For example,
       ->prepare_write and ->commit_write methods work with pages from
       page cache but they belong to upper layer cache. When loop
       device driver works with pages it delegates some works to
       underlaying FS;

   @li no read ahead (nothing to say more);

   @li no ACL or selinux support. Unix security model (permission
       masks) is followed with client inventing it;

   @li loop back device driver with minimal changes should work and
       losetup tool should also work with C2T1FS;

   @li readdir() is only supported for root dir. A single regular file named
       with object number is filled in the root dir.

   @li read/write, readv/writev methods should work. Asynchronous
       interface should be supported;

   @li file exported by the server and which we want to use as a
       back-end for the block device should be specified as part of
       device specification in mount command in a way like this:

       mount -t c2t1fs -o objid=<objid> ipaddr:port /mnt/c2t1fs

     where objid is object id exported by the server.

     If the server does not know this object - a error is returned and mount
     fails and meaningful error is reported.

   @section c2t1fsfuncspec Functional specification

   There are three interaces we need to interact with:

   @li linux VFS - super_block operations should have
       c2t1fs_get_super() and c2t1fs_fill_super() methods
       implemented. Root inode and dentry should be created in mount
       time;

   @li loop back device driver interface: ->write(),
       ->prepare_write/commit_write() and ->sendfile() methods should be
       implemented;

   @li networking layer needs: connect/disconnect rpc. Connect should
       have one field: obj_id, that is, what object we are attaching
       to. We need also read/write rpcs capable to work with iovec
       structures.

   @section c2t1fslogspec Logical specification

   C2T1FS is implemented as linux native nodev file system. It may be used in
   a way like this:

   modprobe c2t1fs
   mount -t c2t1fs -o objid=<objid> ipaddr:port /mnt/c2t1fs
   losetup /dev/loop0 /mnt/c2t1fs/$objid
   dd if=/dev/zero of=/dev/loop0 bs=1M count=10

   To support this functionality, we implement the following parts:

   @li mount (super block init), which parses device name, sends
       connect rpc to the server and creates root inode and dentry
       upon success. We also create inode and dentry for the file
       exported by the server. It is easier to handle wrong file id
       during mount rather than in file IO time;

   @li losetup part requires ->lookup method;

   @li working IO part requires ->prepare_write/commit_write(),
       ->sendfile() and ->write() file operations to being implemented.

 */
static kmem_cache_t *c2t1fs_inode_cachep = NULL;

MODULE_AUTHOR("Yuriy V. Umanets <yuriy.umanets@clusterstor.com>");
MODULE_DESCRIPTION("Colibri C2T1 File System");
MODULE_LICENSE("GPL");

static struct c2t1fs_sb_info *c2t1fs_init_csi(struct super_block *sb)
{
        struct c2t1fs_sb_info *csi;

	csi = kmalloc(sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return NULL;
        s2csi_nocast(sb) = csi;
        memset(csi, 0, sizeof *csi);

        atomic_set(&csi->csi_mounts, 1);
        return csi;
}

static int c2t1fs_free_csi(struct super_block *sb)
{
        struct c2t1fs_sb_info *csi = s2csi(sb);

	if (csi->csi_srvid.ssi_host)
                kfree(csi->csi_srvid.ssi_host);
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
}

static int c2t1fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
        buf->f_type   = dentry->d_sb->s_magic;
        buf->f_bsize  = PAGE_SIZE;
        buf->f_blocks = 1024 * 1024;
        buf->f_bfree  = 1024 * 1024;
        buf->f_bavail = 1024 * 1024;
        buf->f_namelen = NAME_MAX;
        return 0;
}


struct super_operations c2t1fs_super_operations = {
        .alloc_inode   = c2t1fs_alloc_inode,
        .destroy_inode = c2t1fs_destroy_inode,
        .put_super     = c2t1fs_put_super,
        .statfs        = c2t1fs_statfs,
};

static int c2t1fs_parse_options(struct super_block *sb, char *options)
{
        struct c2t1fs_sb_info *csi = s2csi(sb);
        char *s1, *s2, *objid = NULL;

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

                if (strncmp(s1, "objid=", 6) == 0) {
                        objid = s1 + 6;
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

        if (objid) {
                csi->csi_objid = simple_strtol(objid, NULL, 0);
                if (csi->csi_objid <= 0) {
                        printk(KERN_ERR "Invalid device_id=%lld specified\n", csi->csi_objid);
                        return -EINVAL;
                }
        } else {
                printk(KERN_ERR "No device id specified (need mount option 'objid=...')\n");
                return -EINVAL;
        }

        return 0;
}

/* common rw function for c2t1fs, it just does sync RPC. */
static ssize_t c2t1fs_read_write(struct file *file, char *buf, size_t count,
                                 loff_t pos, int rw)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct c2t1fs_sb_info *csi = s2csi(inode->i_sb);
        unsigned long addr;
        struct mm_struct *mm;
        struct page **pages;
        int npages;
        int off;
        int rc;

        addr = (unsigned long)buf;
        off = (addr & (PAGE_SIZE - 1));
        addr = addr & PAGE_MASK;
        npages = ((count + PAGE_SIZE - 1) >> PAGE_SHIFT) + !!off;
        pages = kmalloc(sizeof(*pages) * npages, GFP_KERNEL);
        if (pages == NULL)
                return -ENOMEM;

        printk("addr = %lx, count = %ld, npages = %d, off = %d\n", addr, count, npages, off);
        mm = addr > PAGE_OFFSET ? &init_mm : current->mm;
        rc = get_user_pages(current, mm, addr, npages, rw == WRITE, 1, pages, NULL);
        if (rc != npages) {
                printk("expect %d, got %d\n", npages, rc);
                npages = rc > 0 ? rc : 0;
                rc = -EFAULT;
                goto out;
        }

        rc = ksunrpc_read_write(csi->csi_xprt, csi->csi_objid, pages, npages, off, count, pos, rw);
        printk("call ksunrpc_read_write returns %d\n", rc);

out:
        for (off = 0; off < npages; off++)
                put_page(pages[off]);
        kfree(pages);
        return rc;
}

#ifdef HAVE_FILE_READV
static ssize_t c2t1fs_file_readv(struct file *file, const struct iovec *iov,
                                 unsigned long nr_segs, loff_t *ppos)
{
        ssize_t count = 0;
        ssize_t rc;
        int i;

        if (nr_segs == 0)
                return 0;

        /* TODO: a fake readv, will fix it after we have a real c2t1fs */
        for (i = 0; i < nr_segs; i++) {
                rc = file->f_op->read(file, iov->iov_base, iov->iov_len,
                                      ppos);
                if (rc <= 0)
                        break;

                count += rc;
                ++iov;
        }

        return count ? : rc;
}

static ssize_t c2t1fs_file_writev(struct file *file, const struct iovec *iov,
                                  unsigned long nr_segs, loff_t *ppos)
{
        ssize_t count = 0;
        ssize_t rc;
        int i;

        if (nr_segs == 0)
                return 0;

        /* TODO: a fake readv, will fix it after we have a real c2t1fs */
        for (i = 0; i < nr_segs; i++) {
                rc = file->f_op->read(file, iov->iov_base, iov->iov_len,
                                      ppos);
                if (rc <= 0)
                        break;

                count += rc;
                ++iov;
        }

        return count ? : rc;
}
#endif

#ifdef HAVE_FILE_AIO_READ
static ssize_t c2t1fs_file_aio_read(struct kiocb *iocb, char *buf,
                                    size_t count, loff_t ppos)
{
        printk("what is read/write? %s:%d\n", __FUNCTION__, __LINE__);
        return c2t1fs_read_write(iocb->ki_filp, buf, count, iocb->ki_pos, READ);
}

static ssize_t c2t1fs_file_aio_write(struct kiocb *iocb, const char *buf,
                                     size_t count, loff_t ppos)
{
        printk("what is read/write? %s:%d\n", __FUNCTION__, __LINE__);
        return c2t1fs_read_write(iocb->ki_filp, (char *)buf, count, iocb->ki_pos,
                                 WRITE);
}

#else

static ssize_t c2t1fs_file_read(struct file *file, char *buf, size_t count,
                                loff_t *ppos)
{
        ssize_t cnt;
        printk("what is read/write? %s:%d\n", __FUNCTION__, __LINE__);
        cnt = c2t1fs_read_write(file, buf, count, *ppos, READ);
        if (cnt > 0)
                *ppos += cnt;
        return cnt;
}

static ssize_t c2t1fs_file_write(struct file *file, const char *buf, size_t count,
                                 loff_t *ppos)
{
        ssize_t cnt;
        printk("what is read/write? %s:%d\n", __FUNCTION__, __LINE__);
        return c2t1fs_read_write(file, (char *)buf, count, *ppos, WRITE);
        if (cnt > 0)
                *ppos += cnt;
        return cnt;
}
#endif /* HAVE_FILE_AIO_READ */

#ifdef HAVE_SENDFILE
/*
 * Send file content (through pagecache) somewhere with helper
 */
static ssize_t c2t1fs_file_sendfile(struct file *in_file, loff_t *ppos,
                                    size_t count, read_actor_t actor,
                                    void *target)
{
        /* Do not support buffer ops, just make loop driver happy. */
        return -ENOSYS;
}
#endif

struct inode_operations c2t1fs_file_inode_operations = {
};

struct file_operations c2t1fs_file_operations = {
#ifdef HAVE_FILE_READV
        .readv          = c2t1fs_file_readv,
        .writev         = c2t1fs_file_writev,
#endif

#ifdef HAVE_FILE_AIO_READ
        .aio_read       = c2t1fs_file_aio_read,
        .aio_write      = c2t1fs_file_aio_write,
        .read           = do_sync_read,
        .write          = do_sync_write,
#else
        .read           = c2t1fs_file_read,
        .write          = c2t1fs_file_write,
#endif

#ifdef HAVE_SENDFILE
        .sendfile       = c2t1fs_file_sendfile,
#endif
};

static int c2t1fs_prepare_write(struct file *file, struct page *page,
                                unsigned from, unsigned to)
{
        return -ENOSYS;
}

static int c2t1fs_commit_write(struct file *file, struct page *page,
                               unsigned from, unsigned to)
{
        return -ENOSYS;
}

#ifdef HAVE_WRITE_BEGIN_END
static int c2t1fs_write_begin(struct file *file, struct address_space *mapping,
                              loff_t pos, unsigned len, unsigned flags,
                              struct page **pagep, void **fsdata)
{
        pgoff_t index = pos >> PAGE_CACHE_SHIFT;
        struct page *page;
        int rc;
        unsigned from = pos & (PAGE_CACHE_SIZE - 1);

        page = grab_cache_page_write_begin(mapping, index, flags);
        if (!page)
                return -ENOMEM;

        *pagep = page;

        rc = c2t1fs_prepare_write(file, page, from, from + len);
        if (rc) {
                unlock_page(page);
                page_cache_release(page);
        }
        return rc;
}

static int c2t1fs_write_end(struct file *file, struct address_space *mapping,
                            loff_t pos, unsigned len, unsigned copied,
                            struct page *page, void *fsdata)
{
        unsigned from = pos & (PAGE_CACHE_SIZE - 1);
        int rc;

        rc = c2t1fs_commit_write(file, page, from, from + copied);

        unlock_page(page);
        page_cache_release(page);
        return rc ? rc : copied;
}
#endif

struct address_space_operations c2t1fs_file_aops = {
#ifdef HAVE_WRITE_BEGIN_END
        .write_begin    = c2t1fs_write_begin,
        .write_end      = c2t1fs_write_end
#else
        .prepare_write  = c2t1fs_prepare_write,
        .commit_write   = c2t1fs_commit_write
#endif
};

struct address_space_operations c2t1fs_dir_aops = {
};

static struct dentry *c2t1fs_lookup(struct inode *dir, struct dentry *dentry,
                                    struct nameidata *nd);

static int
c2t1fs_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
        struct inode *inode = filp->f_dentry->d_inode;
        struct c2t1fs_sb_info *csi = s2csi(inode->i_sb);
        unsigned int ino;
        int i;

        ino = inode->i_ino;
	if (ino != C2T1FS_ROOT_INODE)
		return 0;

        i = filp->f_pos;
        switch (i) {
        case 0:
                if (filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
                        goto out;
                i++;
                filp->f_pos++;
                /* fall thru */
        case 1:
                if (filldir(dirent, "..", 2, i, ino, DT_DIR) < 0)
                        goto out;
                i++;
                filp->f_pos++;
                /* fall thru */
	case 2:
	{
		char fn[256];
		sprintf(fn, "%d", (int)csi->csi_objid);
                if (filldir(dirent, fn, strlen(fn), i, csi->csi_objid, DT_REG) < 0)
                        goto out;
                i++;
                filp->f_pos++;
                /* fall thru */
	}
	default:
		break;
	}
out:
	return 0;
}



struct inode_operations c2t1fs_dir_inode_operations = {
        .lookup = c2t1fs_lookup
};

struct file_operations c2t1fs_dir_operations = {
        .readdir        = c2t1fs_readdir,
};

static int c2t1fs_update_inode(struct inode *inode, void *opaque)
{
        ino_t ino = *((ino_t *)opaque);
        __u32 mode;

        inode->i_ino = ino;

        /* FIXME: This is a hack to make it mount (we need root dir) */
        if (inode->i_ino == C2T1FS_ROOT_INODE)
                mode = ((S_IRWXUGO | S_ISVTX) & ~current->fs->umask) | S_IFDIR;
        else
                mode = ((S_IRUGO | S_IXUGO) & ~current->fs->umask) | S_IFREG;
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

        if (S_ISDIR(inode->i_mode)) {
                inode->i_size = PAGE_SIZE;
                inode->i_blocks = 1;
        } else {
                /* FIXME: This should be taken from an getattr rpc */
                /* Before that, let's have this size */
                inode->i_size = PAGE_SIZE * 4;
                inode->i_blocks = 16;
        }

        return 0;
}

static int c2t1fs_read_inode(struct inode *inode, void *opaque)
{
        C2TIME_S(inode->i_mtime) = 0;
        C2TIME_S(inode->i_atime) = 0;
        C2TIME_S(inode->i_ctime) = 0;
        inode->i_rdev = 0;

        c2t1fs_update_inode(inode, opaque);

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
        /* FIXME: Set inode info from passed rpc data */
        return 0;
}

static struct inode *c2t1fs_iget(struct super_block *sb, ino_t hash)
{
        struct inode *inode;

        inode = iget5_locked(sb, hash, c2t1fs_test_inode, c2t1fs_set_inode, &hash);
        if (inode) {
                if (inode->i_state & I_NEW) {
                        c2t1fs_read_inode(inode, &hash);
                        unlock_new_inode(inode);
                } else {
                        if (!(inode->i_state & (I_FREEING | I_CLEAR)))
                                c2t1fs_update_inode(inode, &hash);
                }
        }

        return inode;
}

static struct dentry *c2t1fs_lookup(struct inode *dir, struct dentry *dentry,
                                    struct nameidata *nd)
{
	struct inode *inode = NULL;
	unsigned long ino;

	lock_kernel();
	ino = simple_strtol(dentry->d_name.name, NULL, 0);
	if (!ino) {
	        unlock_kernel();
	        return ERR_PTR(-EINVAL);
	}
        inode = c2t1fs_iget(dir->i_sb, ino);
        if (!inode) {
                unlock_kernel();
                return ERR_PTR(-ENOENT);
        }
	unlock_kernel();
	d_add(dentry, inode);
	return NULL;
}

static int c2t1fs_fill_super(struct super_block *sb, void *data, int silent)
{
        struct c2t1fs_sb_info *csi;
        struct inode          *root;
        int rc;

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
        struct c2t1fs_sb_info *csi;
	struct super_block    *sb;
        char *hostname;
	char *port;
        char *endptr;
	int   tcp_port;
        int   rc;

	printk("flags=%x devname=%s, data=%s\n", flags, devname, (char*)data);

	hostname = kstrdup(devname, GFP_KERNEL);

        port = strchr(hostname, ':');
	if (port == NULL || hostname == port || *(port+1) == '\0') {
		printk("server:port is expected as the device\n");
		kfree(hostname);
		return -EINVAL;
	}

	if (in_aton(hostname) == 0) {
		printk("only dotted ipaddr is accepted now: e.g. 1.2.3.4\n");
		kfree(hostname);
		return -EINVAL;
	}

	*port++ = 0;
	printk("server/port=%s/%s\n", hostname, port);
	tcp_port = simple_strtol(port, &endptr, 10);
        if (*endptr != '\0' || tcp_port <= 0) {
		printk("invalid port number\n");
		kfree(hostname);
                return -EINVAL;
	}

        rc = get_sb_nodev(fs_type, flags, data, c2t1fs_fill_super, mnt);
        if (rc < 0) {
		kfree(hostname);
                return rc;
	}

        sb  = mnt->mnt_sb;
        csi = s2csi(sb);
        csi->csi_sockaddr.sin_family      = AF_INET;
        csi->csi_sockaddr.sin_port        = htons(tcp_port);
        csi->csi_sockaddr.sin_addr.s_addr = in_aton(hostname);

        csi->csi_srvid.ssi_host       = hostname;
        csi->csi_srvid.ssi_sockaddr   = &csi->csi_sockaddr;
        csi->csi_srvid.ssi_addrlen    = strlen(hostname) + 1;
        csi->csi_srvid.ssi_port       = csi->csi_sockaddr.sin_port;

        csi->csi_xprt = ksunrpc_xprt_init(&csi->csi_srvid);
        if (IS_ERR(csi->csi_xprt)) {
		/* hostname will be freed in c2t1fs_put_csi() */
                c2t1fs_put_csi(sb);
                return PTR_ERR(csi->csi_xprt);
	}

        rc = ksunrpc_create(csi->csi_xprt, csi->csi_objid);
        if (rc)
                printk("Creaete objid %llu failed %d, loop device may not work\n", csi->csi_objid, rc);

        return 0;
}

static void c2t1fs_kill_super(struct super_block *sb)
{
        struct c2t1fs_sb_info *csi = s2csi(sb);

        /* FIME: Disconnect from server here. */
        GETHERE;
        if (csi && csi->csi_xprt)
                ksunrpc_xprt_fini(csi->csi_xprt);
        GETHERE;
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

        printk(KERN_INFO "Colibri C2T1 File System (http://www.clusterstor.com)\n");

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
        if (rc)
                printk(KERN_INFO "Colibri C2T1 File System cleanup: %d\n", rc);
}
