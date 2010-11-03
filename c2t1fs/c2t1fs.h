#ifndef __COLIBRI_C2T1FS_H
#define __COLIBRI_C2T1FS_H

#include <linux/in.h> /* for sockaddr_in */

#include "lib/list.h"
#include "net/ksunrpc/ksunrpc.h"
#include "config.h"

#define C2T1FS_DEBUG 1
#ifdef C2T1FS_DEBUG
#define ENTER   printk("Enter into function %s\n", __FUNCTION__)
#define LEAVE   printk("Leave function %s\n", __FUNCTION__)
#define GETHERE printk("Get %s line %d\n", __FUNCTION__, __LINE__)
#else
#define ENTER
#define LEAVE
#define GETHERE
#endif

#define s2csi(sb)       \
        ((struct c2t1fs_sb_info *)((sb)->s_fs_info))

#define s2csi_nocast(sb) \
        ((sb)->s_fs_info)

/* 0x\C\2\T\1 */
#define C2T1FS_SUPER_MAGIC    0x43325431
#define C2T1FS_ROOT_INODE     0x10000000
#define C2T1FS_INIT_OBJSIZE   (4 << 20)

#define C2TIME_S(time)        (time.tv_sec)

/* 1M */
#define C2_MAX_BRW_BITS       (20)

/* 4*1M */
#define C2_MAX_BLKSIZE_BITS   (22)

#ifndef log2
#define log2(n) ffz(~(n))
#endif


/**
   This is the data structure to describe a client transport,
   identified by its container id.
*/
struct c2t1fs_xprt_clt {
	/** container id */
	uint64_t                  xc_cid;

	/** node service id on which this container is running */
        struct ksunrpc_service_id xc_srvid;

	/** the connection transport for this container */
        struct ksunrpc_xprt      *xc_xprt;

	/** linkage in hash table */
	struct c2_list_link       xc_link;
};

struct c2t1fs_sb_info {
        atomic_t        csi_mounts;
        int             csi_flags;

        uint64_t        csi_objid;    /*< The object id will be mapped */
        uint64_t        csi_objsize;  /*< The initial object size */
        uint64_t        csi_layoutid; /*< layout id this client uses */

        struct ksunrpc_service_id csi_mgmt_srvid; /*< mgmt node service id */
        struct ksunrpc_xprt      *csi_mgmt_xprt;  /*< mgmt node xprt */

        struct c2_list  csi_xprt;     /*< transport list or hash table */
        struct c2_mutex csi_mutex;    /*< mutex to pretect this sb */
};

struct c2t1fs_inode_info {
        struct inode    cii_vfs_inode;
};

static inline struct c2t1fs_inode_info *i2cii(struct inode *inode)
{
        return container_of(inode, struct c2t1fs_inode_info, cii_vfs_inode);
}

#endif
