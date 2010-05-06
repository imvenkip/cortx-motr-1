#ifndef __COLIBRI_C2T1FS_H
#define __COLIBRI_C2T1FS_H

#define s2csi(sb)       \
        ((struct c2t1fs_sb_info *)((sb)->s_fs_info))

#define s2csi_nocast(sb) \
        ((sb)->s_fs_info)

/* 0x\C\2\T\1 */
#define C2T1FS_SUPER_MAGIC    0x43325431
#define C2T1FS_ROOT_INODE     0x10000000

#define C2TIME_S(time)        (time.tv_sec)

/* 1M */
#define C2_MAX_BRW_BITS       (20)

/* 4*1M */
#define C2_MAX_BLKSIZE_BITS   (22)

#ifndef log2
#define log2(n) ffz(~(n))
#endif

struct c2t1fs_sb_info {
        atomic_t        csi_mounts;
        int             csi_flags;
        int             csi_devid;
        char           *csi_server;
};

struct c2t1fs_inode_info {
        struct inode    cii_vfs_inode;
};

static inline struct c2t1fs_inode_info *i2cii(struct inode *inode)
{
        return container_of(inode, struct c2t1fs_inode_info, cii_vfs_inode);
}

#endif
