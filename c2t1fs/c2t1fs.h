#ifndef __COLIBRI_C2T1FS_H
#define __COLIBRI_C2T1FS_H

#include <linux/fs.h>
#include <linux/slab.h>

#include "fid/fid.h"
#include "lib/mutex.h"
#include "lib/assert.h"
#include "net/net.h"
#include "rpc/rpccore.h"

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

struct c2t1fs_dir_ent;

int c2t1fs_init(void);
void c2t1fs_fini(void);

enum {
	/* 0x\C\2\T\1 */
	C2T1FS_SUPER_MAGIC = 0x43325431,
	MAX_NR_EP_PER_SERVICE_TYPE = 10,
	C2T1FS_MAX_NAME_LEN = 8,
};

/** Anything that is global to c2t1fs module should go in this structure */
struct c2t1fs_globals
{
	struct c2_net_xprt      *g_xprt;
	char                    *g_laddr;
	char                    *g_db_name;
	struct c2_cob_domain_id  g_cob_dom_id;

	struct c2_net_domain     g_ndom;
	struct c2_rpcmachine     g_rpcmachine;
	struct c2_cob_domain     g_cob_dom;
	struct c2_dbenv          g_dbenv;
};

extern struct c2t1fs_globals c2t1fs_globals;

struct c2t1fs_mnt_opts
{
	char *mo_options;
	int   mo_nr_mds_ep;
	char *mo_mds_ep[MAX_NR_EP_PER_SERVICE_TYPE];
	int   mo_nr_ios_ep;
	char *mo_ios_ep[MAX_NR_EP_PER_SERVICE_TYPE];
};

struct c2t1fs_sb
{
	struct c2_mutex        csb_mutex;
	struct c2t1fs_mnt_opts csb_mnt_opts;
	uint64_t               csb_flags;
};


struct c2t1fs_dir_ent
{
	char          de_name[C2T1FS_MAX_NAME_LEN + 1];
	struct c2_fid de_fid;
};

enum {
	C2T1FS_MAX_NR_DIR_ENTS = 10,
	C2T1FS_MAX_FILE_SIZE = C2T1FS_MAX_NR_DIR_ENTS *
					sizeof (struct c2t1fs_dir_ent),
};

struct c2t1fs_inode
{
	struct inode  ci_inode;
	struct c2_fid ci_fid;
	int           ci_nr_dir_ents;
	union {
		struct c2t1fs_dir_ent ci_dir_ents[C2T1FS_MAX_NR_DIR_ENTS];
		char                  ci_data[C2T1FS_MAX_FILE_SIZE];
	};
};

static inline struct c2t1fs_sb *C2T1FS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct c2t1fs_inode *C2T1FS_I(struct inode *inode)
{
	return container_of(inode, struct c2t1fs_inode, ci_inode);
}

extern const struct c2_fid c2t1fs_root_fid;
bool c2t1fs_inode_is_root(struct inode *inode);

int c2t1fs_get_sb(struct file_system_type *fstype,
		  int                      flags,
		  const char              *devname,
		  void                    *data,
		  struct vfsmount         *mnt);

void c2t1fs_kill_sb(struct super_block *sb);


int c2t1fs_sb_init(struct c2t1fs_sb *sbi);
void c2t1fs_sb_fini(struct c2t1fs_sb *sbi);

int c2t1fs_inode_cache_init(void);
void c2t1fs_inode_cache_fini(void);

struct inode *c2t1fs_root_iget(struct super_block *sb);
struct inode *c2t1fs_iget(struct super_block *sb, struct c2_fid *fid);

extern struct file_operations c2t1fs_dir_file_operations;
extern struct file_operations c2t1fs_reg_file_operations;

extern struct inode_operations c2t1fs_dir_inode_operations;
extern struct inode_operations c2t1fs_reg_inode_operations;

struct inode *c2t1fs_alloc_inode(struct super_block *sb);
void c2t1fs_destroy_inode(struct inode *inode);
#endif /* __COLIBRI_C2T1FS_H */
