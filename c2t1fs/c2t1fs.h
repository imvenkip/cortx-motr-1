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

/**
  @defgroup c2t1fs c2t1fs

  @section Overview

  c2t1fs is a colibri client file-system for linux. It is implemented as a
  kernel module.

  @section c2t1fsfuncspec Function Specification

  c2t1fs has flat file-system structure i.e. no directories except root.
  c2t1fs does not support caching. All read-write requests are directly
  forwarded to servers.

  c2t1fs can be mounted with mount command:

  mount -t c2t1fs -o <options_list> dontcare <dir_name>

  where <options_list> is a comma separated list of option=value elements.
  Currently supported list of options is:

  - mgs [value type: end-point address e.g. 127.0.0.1:123321:1 ]
      end-point address of management service or confd.

  - ios [value type: end-point address]
      end-point address of io-service. multiple io-services can be specified
      as ios=<end-point-addr1>,ios=<end-point-addr2>

  - mds [value type: end-point address]
      end-point address of meta-data service. Currently only one mds is
      allowed.

  - profile [value type: string]
      configuration profile. Used while fetching configuration from mgs.

  - nr_data_units [value type: number]
      Number of data units in one parity group. Optional parameter.
      Default value is C2T1FS_DEFAULT_NR_DATA_UNITS (=1).

  - nr_parity_units [value type: number]
      Number of parity units in one parity group. Optional parameter.
      Default value is C2T1FS_DEFAULT_NR_PARITY_UNITS (=0).

  - nr_containers [value type: number]
      Number of containers. Optional parameter. Default value
      is C2T1FS_DEFAULT_NR_CONTAINERS (=1).
      nr_containers should be <= C2T1FS_MAX_NR_CONTAINERS (=1024).
      nr_containers >= nr_data_units + 2 * nr_parity_units. (2 to account for
      nr_spare_units which is equal to nr_parity_units. P = N + 2 * K)

   stripe unit size is not taken as mount option. Instead it is always set to
   C2T1FS_DEFAULT_STRIPE_UNIT_SIZE (=4K).

   'device' argument of mount command is ignored.

   c2t1fs supports following operations:
   - Creating upto C2T1FS_MAX_NR_DIR_ENTS number of regular files
   - Remove a regular file
   - Listing files in root directory

   @section c2t1fslogspec Logical Specification

   <B>mount/unmount:</B>

   c2t1fs currently takes io/metadata service end-point address and striping
   parameters as mount options. Once mgs/confd is ready, all this information
   should be fetched from mgs. In which case, mgs address and profile name
   will be the only required mount options.

   c2t1fs establishes rpc-connections and rpc-sessions with all the services
   specified in the mount options. If multiple services have same end-point
   address, separate rpc-connection is established with each service i.e.
   if N services have same end-point address, there will be N rpc-connections
   leading to same target end-point.

   The rpc-connections and rpc-sessions will be terminated at unmount time.

   <B> Containers and target objects: </B>

   One service can serve zero or more number of containers. Given an id of a
   container, "container location map", gives service that is serving
   the container. Even if containers are not yet implemented, notion of
   container id is required, to be able to locate the service serving some
   object identified by fid.

   Number of containers is specified as a mount option. If number of
   containers is P, then

   - container id 0 will be a meta-data container and its container location
     map entry will point to md-service. Hence, container field of global
     object's fid, will have value 0.

   - container id 1,2..,P will be served by io-services. P number of containers
     will be equally divided among available io-services.
     For a global file object having fid <0, K>, there will be P number of
     target objects having fids {<i, K> | i = 1, 2, ..., P}.
     Data of global file will be striped across these P target objects, using
     parity declustered layout with N, K parameters coming from mount options
     nr_data_units and nr_parity units respectively.

   Container location map is populated at mount time.

   <B> Directory Operations: </B>

   To create a regular file, c2t1fs sends cob create requests to mds(for global
   object aka gob) and io-service (for target objects). Because, mds is not
   yet implemented, c2t1fs does not send cob create request to any mds.
   Instead all directory entries are stored in the in-memory root inode itself.
   In memory c2t1fs inode has statically allocated array of
   C2T1FS_MAX_NR_DIR_ENT directory entries. "." and ".." are not stored in
   this directory entry array. c2t1fs generates fid of new file and its target
   objects. All target objects have same fid.key as that of global file's fid.
   They differ only in fid.container field. Key of global file is taken from
   value of a monotonically increasing counter.

   If target object creation fails, c2t1fs does not attempt to cleanup
   target objects that were successfully created. This should be handled by
   dtm component, which is not yet implemented.

   <B> Read/Write: </B>

   c2t1fs currently supports only full stripe IO
   i.e. iosize = nr_data_units * stripe_unit_size (where stripe_unit_size = 4K).

   read-write operations on file are not synchronised.

   c2t1fs does not cache any data.

   For simplicity, c2t1fs does synchronous rpc with io-services, to read/write
   target objects.
 */

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
	C2T1FS_MAX_NR_DIR_ENTS = 10,
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

/** mount options */
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

enum {
	MAGIC_SVC_CTX  = 0x5356435f435458, /* "SVC_CTX" */
	MAGIC_SVCCTXHD = 0x5356434354584844, /* "SVCCTXHD" */
};

/**
   For each <mounted_fs, target_service> pair, there is one instance of
   c2t1fs_service_context.

   Allocated at mount time and freed during unmount.

   XXX Better name???
 */
struct c2t1fs_service_context
{
	/** Superblock associated with this service context */
	struct c2t1fs_sb         *sc_csb;

	/** Service type */
	enum c2t1fs_service_type  sc_type;

	/** end-point address of service */
	char                     *sc_addr;

	struct c2_rpc_conn        sc_conn;
	struct c2_rpc_session     sc_session;

	/** link in c2t1fs_sb::csb_service_contexts list */
	struct c2_tlink           sc_link;

	/** magic = MAGIC_SVC_CTX */
	uint64_t                  sc_magic;
};

/**
   Given a container id of a container, map give service context of a service
   that is serving the container.
 */
struct c2t1fs_container_location_map
{
	/**
	   Array of c2t1fs_sb::csb_nr_container elements.
	   clm_map[i] points to c2t1fs_service_context of a service
	   that is serving container i
	 */
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
	struct c2_tl           csb_service_contexts;

	int                    csb_nr_containers;
	int                    csb_nr_data_units;
	int                    csb_nr_parity_units;

	struct c2t1fs_container_location_map csb_cl_map;

	uint64_t               csb_magic;
};


struct c2t1fs_dir_ent
{
	char          de_name[C2T1FS_MAX_NAME_LEN + 1];
	struct c2_fid de_fid;
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
