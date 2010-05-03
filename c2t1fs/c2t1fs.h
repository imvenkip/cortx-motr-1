#ifndef __COLIBRI_C2T1FS_H
#define __COLIBRI_C2T1FS_H

#define s2csi(sb)       \
        ((struct c2t1fs_sb_info *)((sb)->s_fs_info))

#define s2csi_nocast(sb) \
        ((sb)->s_fs_info)

/* should be enough so far */
#define C2T1FS_MAX_SERVER_LEN 64

/* 0x\C\2\T\1 */
#define C2T1FS_SUPER_MAGIC    0x43325431

#define C2T1FS_ROOT_INODE     0x10000000

struct c2t1fs_sb_info {
        atomic_t        csi_mounts;
        int             csi_flags;
        char            csi_data_server[C2T1FS_MAX_SERVER_LEN];
        char            csi_metadata_server[C2T1FS_MAX_SERVER_LEN];
};

struct c2t1fs_inode_info {
        struct inode    cii_vfs_inode;
};

static inline struct c2t1fs_inode_info *i2cii(struct inode *inode)
{
        return container_of(inode, struct c2t1fs_inode_info, cii_vfs_inode);
}

#endif
