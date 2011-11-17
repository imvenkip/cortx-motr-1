#ifndef __COLIBRI_C2T1FS_H
#define __COLIBRI_C2T1FS_H

#include <linux/fs.h>
#include <linux/slab.h>

#include "fid/fid.h"
#include "lib/mutex.h"
#include "lib/assert.h"
#include "net/net.h"
#include "rpc/rpccore.h"
#include "lib/list.h"

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

struct c2_pdclust_layout;
struct c2t1fs_dir_ent;

int c2t1fs_init(void);
void c2t1fs_fini(void);

enum {
	/* 0x\C\2\T\1 */
	C2T1FS_SUPER_MAGIC = 0x43325431,
	MAX_NR_EP_PER_SERVICE_TYPE = 10,
	C2T1FS_MAX_NAME_LEN = 8,
	C2T1FS_RPC_TIMEOUT = 10, /* seconds */
	C2T1FS_NR_SLOTS_PER_SESSION = 10,
	C2T1FS_MAX_NR_RPC_IN_FLIGHT = 100,
	C2T1FS_DEFAULT_NR_DATA_UNITS = 1,
	C2T1FS_DEFAULT_NR_PARITY_UNITS = 0,
	C2T1FS_DEFAULT_NR_CONTAINERS = 1,
	C2T1FS_DEFAULT_STRIPE_UNIT_SIZE = PAGE_SIZE,
	C2T1FS_MAX_NR_CONTAINERS = 1024,
};

/** Anything that is global to c2t1fs module goes in this singleton structure */
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
	char *mo_profile;
	char *mo_mgs_ep_addr;
	int   mo_nr_mds_ep;
	char *mo_mds_ep_addr[MAX_NR_EP_PER_SERVICE_TYPE];
	int   mo_nr_ios_ep;
	char *mo_ios_ep_addr[MAX_NR_EP_PER_SERVICE_TYPE];
	int   mo_nr_containers; /* P */
	int   mo_nr_data_units; /* N */
	int   mo_nr_parity_units; /* K */
};

enum c2t1fs_service_type {
	C2T1FS_ST_MGS = 1,
	C2T1FS_ST_MDS,
	C2T1FS_ST_IOS
};

struct c2t1fs_service_context
{
	struct c2t1fs_sb         *sc_csb;
	enum c2t1fs_service_type  sc_type;
	char                     *sc_addr;
	struct c2_rpc_conn        sc_conn;
	struct c2_rpc_session     sc_session;
	int                       sc_nr_containers;
	uint64_t                 *sc_container_ids;
	struct c2_list_link       sc_link;
};

struct c2t1fs_container_location_map
{
	/** Array of csb_nr_container elements */
	struct c2t1fs_service_context *clm_map[C2T1FS_MAX_NR_CONTAINERS];
};

struct c2t1fs_sb
{
	struct c2_mutex        csb_mutex;
	struct c2t1fs_mnt_opts csb_mnt_opts;
	uint64_t               csb_flags;
	struct c2_rpc_conn     csb_mgs_conn;
	struct c2_rpc_session  csb_mgs_session;
	int                    csb_nr_active_contexts;
	struct c2_list         csb_service_contexts;

	int                    csb_nr_containers;
	int                    csb_nr_data_units;
	int                    csb_nr_parity_units;

	struct c2t1fs_container_location_map csb_cl_map;
};


struct c2t1fs_dir_ent
{
	char          de_name[C2T1FS_MAX_NAME_LEN + 1];
	struct c2_fid de_fid;
};

enum {
	C2T1FS_MAX_NR_DIR_ENTS = 10,
};

struct c2t1fs_inode
{
	struct inode              ci_inode;
	struct c2_fid             ci_fid;

	struct c2_pdclust_layout *ci_pd_layout;
	uint64_t                  ci_unit_size;

	int                       ci_nr_dir_ents;
	struct c2t1fs_dir_ent     ci_dir_ents[C2T1FS_MAX_NR_DIR_ENTS];
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

void c2t1fs_fs_lock(struct c2t1fs_sb *csb);
void c2t1fs_fs_unlock(struct c2t1fs_sb *csb);

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
int c2t1fs_inode_layout_init(struct c2t1fs_inode *ci, int N, int K, int P);

void c2t1fs_service_context_init(struct c2t1fs_service_context *ctx,
				 struct c2t1fs_sb              *csb,
				 enum c2t1fs_service_type       type,
				 char                          *ep_addr);

void c2t1fs_service_context_fini(struct c2t1fs_service_context *ctx);

int
c2t1fs_container_location_map_init(struct c2t1fs_container_location_map *map,
				   int nr_containers);

void
c2t1fs_container_location_map_fini(struct c2t1fs_container_location_map *map);

int c2t1fs_container_location_map_build(struct c2t1fs_sb *sb);

struct c2_rpc_session * c2t1fs_container_id_to_session(struct c2t1fs_sb *csb,
						       uint64_t container_id);

struct c2_fid c2t1fs_target_fid(const struct c2_fid gob_fid, int index);
#endif /* __COLIBRI_C2T1FS_H */
