/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 *                  Huang Hua <hua_huang@xyratex.com>
 *                  Anatoliy Bilenko
 * Original creation date: 05/04/2010
 */

#ifndef __COLIBRI_C2T1FS_H
#define __COLIBRI_C2T1FS_H

#include <linux/fs.h>
#include <linux/pagemap.h>

#include "lib/tlist.h"
#include "lib/mutex.h"
#include "net/net.h"    /* c2_net_domain */
#include "rpc/rpc2.h"
#include "pool/pool.h"  /* c2_pool */
#include "net/buffer_pool.h"

/**
  @defgroup c2t1fs c2t1fs

  @section Overview

  c2t1fs is a colibri client file-system for linux. It is implemented as a
  kernel module.

  @section c2t1fsfuncspec Function Specification

  c2t1fs has flat file-system structure i.e. no directories except root.
  c2t1fs does not support caching. All read-write requests are directly
  forwarded to servers.

  By default c2t1fs uses end-point address 0@lo:12345:45:6 as its local
  address. This address can be changed with local_addr module parameter.
  e.g. to make c2t1fs use 172.18.50.40@o2ib1:12345:34:1 as its end-point address
  load module with command:

  sudo insmod kcolibri.ko local_addr="172.18.50.40@o2ib1:12345:34:1"

  c2t1fs can be mounted with mount command:

  mount -t c2t1fs -o <options_list> dontcare <dir_name>

  where <options_list> is a comma separated list of option=value elements.
  Currently supported list of options is:

  - mgs [value type: end-point address e.g. 192.168.50.40@tcp:12345:34:1 ]
      end-point address of management service or confd.
      @note terms 'mgs' and 'confd' are used interchangably in the text.

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
      Default value is C2T1FS_DEFAULT_NR_DATA_UNITS.

  - nr_parity_units [value type: number]
      Number of parity units in one parity group. Optional parameter.
      Default value is C2T1FS_DEFAULT_NR_PARITY_UNITS.

  - pool_width [value type: number]
      Number of component objects over which file contents are striped.
      Optional parameter.
      Default value is computed as sum of effective nr_data_units and
      (2 * nr_parity_units).
      pool_width >= nr_data_units + 2 * nr_parity_units. (2 to account for
      nr_spare_units which is equal to nr_parity_units. P >= N + 2 * K)

  - unit_size [value type: number]
      Size of each stripe unit. Optional parameter. Default value is
      C2T1FS_DEFAULT_STRIPE_UNIT_SIZE (=PAGE_CACHE_SIZE).

   'device' argument of mount command is ignored.

   c2t1fs supports following operations:
   - Creating up to C2T1FS_MAX_NR_DIR_ENTS number of regular files
   - Remove a regular file
   - Listing files in root directory
   - file read/write of full-stripe width

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

   <B> Containers and component objects: </B>

   An io service provides access to storage objects and md service
   provides access to md objects. Containers are used to migrate and
   locate object. Each container is identified by container-id. Storage
   objects and md objects are identified by fid which is a pair
   <container-id, key>. All objects belonging to same container have same
   value for fid.container_id which is equal to id of that container.

   "Container location map" maps container-id to service.

   Even if containers are not yet implemented, notion of
   container id is required, to be able to locate a service serving some
   object identified by fid.

   Currently c2t1fs implements simple and temporary mechanism to build
   container location map. Number of containers is assumed to be equal to
   pool_width(i.e. P) + 1. pool_width is a mount option. And additional 1 for
   meta-data container.

   In the absense of feature of layout (called layout_enumeration), c2t1fs
   implements a primitive mechanism to obtain fid of component objects.

   Assume a user-visible file F. A gob representing F is assigned fid
   <0, K>, where K is taken from a monotonically increasing counter
   (c2t1fs_sb::csb_next_key). Container-id 0 is mapped to md-service,
   by container location map.
   There are P number of component objects of file F, having fids
   { <i, K> | i = 1, 2..., P}. Here P is equal to pool_width mount option.
   Container location map, maps container-ids from 1 to P, to io-services.

   Container location map is populated at mount time.

   <B> Directory Operations: </B>

   To create a regular file, c2t1fs sends cob create requests to mds (for global
   object aka gob) and io-service (for component objects). Because, mds is not
   yet implemented, c2t1fs does not send cob create request to any mds.
   Instead all directory entries are maintained in an in-memory list in root
   inode itself.

   If component object creation fails, c2t1fs does not attempt to cleanup
   component objects that were successfully created. This should be handled by
   dtm component, which is not yet implemented.

   <B> Read/Write: </B>

   c2t1fs currently supports only full stripe IO
   i.e. (iosize % (nr_data_units * stripe_unit_size) == 0)

   read-write operations on file are not synchronised.

   c2t1fs does not cache any data.

   For simplicity, c2t1fs does synchronous rpc with io-services, to read/write
   component objects.
 */

/** @} end of c2t1fs group */

/**
  @defgroup c2t1fs-metadata Metadata

  @defgroup c2t1fs-metadata-rq Requirements
  
  The following requirements should be met:
  
  - @b R.DLD.MDSERVICE - all md operations operations implemented by c2t1fs
  should talk to mdservice in ordre to provide required functionality
  
  - @b R.DLD.POSIX - c2t1fs should conform to POSIX in a volume not less than
  Lustre
 */
  
/** @} end of c2t1fs-metadata-rq group */

/**
  @section Overview
  
  The main direction of this work is mostly to follow normal linux filesystem
  driver requirements as for the number and meaning of operations to handle by
  the driver.
  
  This means number of thigs as follow bellow:

  - all fops we describe and use are following normal filesystem driver operations,
  such as link, unlink, create and so on
  - server side service is connected by c2t1fs in mount time and returns root inode
  attributes and (optionally) filesystem wide information, such as free blocks, etc.,
  that is usually needed by statfs
  - inode may also contain some information neeed for performing I/O (layout) and this
  data may be sent to client as part of getattr fop
  
  The following fops are defined:
  
  @code
  
  DEF(c2_fop_str, SEQUENCE,
    _(s_len, U32),
    _(s_buf, BYTE));

  DEF(c2_fop_cob, RECORD,
    _(b_index, U64),
    _(b_version, U64),
    _(b_flags, U32),
    _(b_valid, U32),
    _(b_mode, U32),
    _(b_size, U64),
    _(b_blksize, U64),
    _(b_blocks, U64),
    _(b_nlink, U32),
    _(b_uid, U32),
    _(b_gid, U32),
    _(b_sid, U32),
    _(b_nid, U64),
    _(b_rdev, U32),
    _(b_atime, U32),
    _(b_mtime, U32),
    _(b_ctime, U32),
    _(b_pfid, c2_fop_fid),
    _(b_tfid, c2_fop_fid));

  DEF(c2_fop_create, RECORD,
    _(c_body,   c2_fop_cob),
    _(c_target, c2_fop_str),
    _(c_path,   c2_fop_str),
    _(c_name,   c2_fop_str));

  DEF(c2_fop_create_rep, RECORD,
    _(c_body, c2_fop_cob));

  DEF(c2_fop_link, RECORD,
    _(l_body, c2_fop_cob),
    _(l_spath, c2_fop_str),
    _(l_tpath, c2_fop_str),
    _(l_name, c2_fop_str));

  DEF(c2_fop_link_rep, RECORD,
    _(l_body, c2_fop_cob));

  DEF(c2_fop_unlink, RECORD,
    _(u_body, c2_fop_cob),
    _(u_path, c2_fop_str),
    _(u_name, c2_fop_str));

  DEF(c2_fop_unlink_rep, RECORD,
    _(u_body, c2_fop_cob));

  DEF(c2_fop_rename, RECORD,
    _(r_sbody, c2_fop_cob),
    _(r_tbody, c2_fop_cob),
    _(r_spath, c2_fop_str),
    _(r_tpath, c2_fop_str),
    _(r_sname, c2_fop_str),
    _(r_tname, c2_fop_str));

  DEF(c2_fop_rename_rep, RECORD,
    _(r_body, c2_fop_cob));

  DEF(c2_fop_open, RECORD,
    _(o_path, c2_fop_str),
    _(o_body, c2_fop_cob));

  DEF(c2_fop_open_rep, RECORD,
    _(o_body, c2_fop_cob));

  DEF(c2_fop_close, RECORD,
    _(c_body, c2_fop_cob),
    _(c_path, c2_fop_str));

  DEF(c2_fop_close_rep, RECORD,
    _(c_body, c2_fop_cob));

  DEF(c2_fop_setattr, RECORD,
    _(s_body, c2_fop_cob),
    _(s_path, c2_fop_str));

  DEF(c2_fop_setattr_rep, RECORD,
    _(s_body, c2_fop_cob));

  DEF(c2_fop_getattr, RECORD,
    _(g_body, c2_fop_cob),
    _(g_path, c2_fop_str));

  DEF(c2_fop_getattr_rep, RECORD,
    _(g_body, c2_fop_cob));

  DEF(c2_fop_readdir, RECORD,
    _(r_body, c2_fop_cob),
    _(r_path, c2_fop_str),
    _(r_pos,  c2_fop_str));

  DEF(c2_fop_buf, SEQUENCE,
    _(b_count, U32),
    _(b_addr, BYTE));

  DEF(c2_fop_readdir_rep, RECORD,
    _(r_end,  c2_fop_str),
    _(r_body, c2_fop_cob),
    _(r_buf,  c2_fop_buf));

  @endcode
  
  @defgroup c2t1fs-metadata-fs Detailed Functional Specification
  
  Some functions will see modifications in order to implement the
  client-server communication and not just create all metadata in
  memory.

  @section Initialization
  
  c2t1fs_init() - c2_mdservice_fop_init() is added to initialize md
  operations fops.
  
  c2t1fs_fini() - c2_mdservice_fop_fini() is added to finalize md
  operations fops.
  
  c2t1fs_fill_super() -> c2t1fs_connect_to_all_services() - will
  have also to connect the mdservice.
  
  It also may have some fields from space csb->csb_* initialized
  not from mount options but rather from connect reply information.
  
  c2t1fs_kill_sb() -> c2t1fs_disconnect_from_all_services() - will
  have also to disconnect from mdservice.
  
  @section Inode operations
  
  c2t1fs_mknod() - is to be added to c2t1fs_dir_inode_operations.
  This is one of variants of create fop.
  
  c2t1fs_create() - sending creare fop is added. Layout speicified
  with mount options or obtained in mount time is to be packed into
  create fop and sent to the server. Errors should be handled on
  all stages.
  
  c2t1fs_lookup() - sending getattr/lookup fop is added. Errors are
  handled.
  
  c2t1fs_unlink() - sending unlink fop is added. Errors handled.

  c2t1fs_link() - is to be added to c2t1fs_dir_inode_operations.
  This is normal hard-link create function that sends link fop to
  mdservice.
  
  c2t1fs_mkdir() - is to be added to c2t1fs_dir_inode_operations.
  This function sends create fop initialized in slightly different
  way than for creating regular file.
  
  c2t1fs_rmdir() - is to be added to c2t1fs_dir_inode_operations.
  This function sends unlink fop.

  c2t1fs_symlink() - is to be added to c2t1fs_dir_inode_operations.
  This function sends create fop with mode initialized for symlinks.
  
  c2t1fs_rename() - is to be added to c2t1fs_dir_inode_operations.
  This function sends rename fop.
  
  c2t1fs_setattr() - is to be added to c2t1fs_dir_inode_operations
  and c2t1fs_special_inode_operations. This function sends setattr
  fop.
  
  c2t1fs_getattr() - is to be added to c2t1fs_dir_inode_operations
  and c2t1fs_special_inode_operations.
  
  This function sends getattr fop and returns server side inode
  attributes.
  
  c2t1fs_permission() - is to be added to c2t1fs_dir_inode_operations
  and c2t1fs_special_inode_operations.
  
  The following extended attributes operations to be added. We need
  them in order to run lustre on top of colibri.
  
  c2t1fs_setxattr() - is to be added to c2t1fs_dir_inode_operations
  and c2t1fs_special_inode_operations.
  
  c2t1fs_getxattr() - is to be added to c2t1fs_dir_inode_operations
  and c2t1fs_special_inode_operations.
  
  c2t1fs_listxattr() - is to be added to c2t1fs_dir_inode_operations
  and c2t1fs_special_inode_operations.
  
  c2t1fs_removexattr() - is to be added to c2t1fs_dir_inode_operations
  and c2t1fs_special_inode_operations.
  
  There is no fop for these calls yet, we will add them later as part
  of xattr support work.
  
  @section File operations
  
  c2t1fs_open() - is to be added to c2t1fs_dir_file_operations and
  c2t1fs_reg_file_operations.
  
  This function sends open fop and handles reply and errors in a
  standard way.
  
  c2t1fs_release() - is to be added to c2t1fs_dir_file_operations and
  c2t1fs_reg_file_operations.
  
  This function sends close fop on last release call.
  
  c2t1fs_readdir() - sending readdir and/or getpage fop is added.
  Errors handling and page cache management code should be added.

  @sections Misc changes

  fid_hash() - hash should be gnerated including ->f_container
  and f_key.
 */
  
/** @} end of c2t1fs-metadata-fs group */

/**
  @defgroup c2t1fs-metadata-ls Logical Specification
  
  - @ref c2t1fs-metadata-fs
  - @ref c2t1fs-metadata-rq
  
  All the fs callback functions, implemented in c2t1fs filesystem driver,
  are to be backed with corresponding fop request communicating mdservice
  on server side.
  
 */
  
/** @} end of c2t1fs-metadata-ls group */

/**
  @defgroup c2t1fs-metadata-cf Conformance

  - @b R.DLD.MDSERVICE - talking to mdservice for implementing mdops is conforming
  to this requirement.
    
  - @b R.DLD.POSIX - following standard linux fs driver callback functions and reqs
  guarantees basic posix conformance. More fine-graned conformance may be met with
  using "standard" test suites for filessytems, such as dbench, which is mentioned
  in Testing section (@ref c2t1fs-metadata-ts).
 */
  
/** @} end of c2t1fs-metadata-cf group */

/**
  @defgroup c2t1fs-metadata-ts Testing
  
  c2t1fs should pass standard fs tests such as dbench as part of integration testing.
  As for unit testing, that may run on "before-commit" basis,  small and fast set
  of basic fs operations (10-20 operations) can be added to standard UT framework.
  
 */
  
/** @} end of c2t1fs-metadata-ts group */
  
/**
  @defgroup c2t1fs-metadata-dp Dependencies
  
  - layout. It would be good to have layout functionality in place but without it
  most of work also can be done.
  - recovery. Recovery principles better to be ready to finish this work. Some tests
  may load client node so much that connection problems can occur.
  
 */
  
/** @} end of c2t1fs-metadata-dp group */

/** @} end of c2t1fs-metadata group */
 
struct c2_pdclust_layout;
struct c2t1fs_dir_ent;

int  c2t1fs_init(void);
void c2t1fs_fini(void);

enum {
	C2T1FS_SUPER_MAGIC              = 0x4332543153555052, /* "C2T1SUPR" */
	MAX_NR_EP_PER_SERVICE_TYPE      = 10,
	C2T1FS_MAX_NAME_LEN             = 256,
	C2T1FS_RPC_TIMEOUT              = 10, /* seconds */
	C2T1FS_NR_SLOTS_PER_SESSION     = 10,
	C2T1FS_MAX_NR_RPC_IN_FLIGHT     = 100,
	C2T1FS_DEFAULT_NR_DATA_UNITS    = 1,
	C2T1FS_DEFAULT_NR_PARITY_UNITS  = 1,
	C2T1FS_DEFAULT_STRIPE_UNIT_SIZE = PAGE_CACHE_SIZE,
	C2T1FS_MAX_NR_CONTAINERS        = 1024,
	C2T1FS_COB_ID_STRLEN		= 34,
};

/** Anything that is global to c2t1fs module goes in this singleton structure.
    There is only one, global, instance of this type. */
struct c2t1fs_globals {
	struct c2_net_xprt        *g_xprt;
	/** local endpoint address */
	char                     *g_laddr;
	char                     *g_db_name;
	struct c2_cob_domain_id   g_cob_dom_id;
	struct c2_net_domain      g_ndom;
	struct c2_rpc_machine     g_rpc_machine;
	struct c2_cob_domain      g_cob_dom;
	struct c2_dbenv           g_dbenv;
	struct c2_net_buffer_pool g_buffer_pool;
};

extern struct c2t1fs_globals c2t1fs_globals;

/** Parsed mount options */
struct c2t1fs_mnt_opts {
	/** Input mount options */
	char    *mo_options;

	char    *mo_profile;

	char    *mo_mgs_ep_addr;
	char    *mo_mds_ep_addr[MAX_NR_EP_PER_SERVICE_TYPE];
	char    *mo_ios_ep_addr[MAX_NR_EP_PER_SERVICE_TYPE];

	uint32_t mo_nr_mds_ep;
	uint32_t mo_nr_ios_ep;

	uint32_t mo_pool_width;      /* P */
	uint32_t mo_nr_data_units;   /* N */
	uint32_t mo_nr_parity_units; /* K */

	uint32_t mo_unit_size;
};

enum c2t1fs_service_type {
	/** management service */
	C2T1FS_ST_MGS = 1,

	/** meta-data service */
	C2T1FS_ST_MDS,

	/** io service */
	C2T1FS_ST_IOS
};

enum {
	MAGIC_SVC_CTX  = 0x5356435f435458,   /* "SVC_CTX" */
	MAGIC_SVCCTXHD = 0x5356434354584844, /* "SVCCTXHD" */
};

/**
   For each <mounted_fs, target_service> pair, there is one instance of
   c2t1fs_service_context.

   c2t1fs_service_context is an association class, associating mounted
   file-system and a service.

   Allocated at mount time and freed during unmount.
 */
struct c2t1fs_service_context {
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
struct c2t1fs_container_location_map {
	/**
	   Array of c2t1fs_sb::csb_nr_container valid elements.
	   clm_map[i] points to c2t1fs_service_context of a service
	   that is serving objects belonging to container i
	 */
	struct c2t1fs_service_context *clm_map[C2T1FS_MAX_NR_CONTAINERS];
};

/**
   In memory c2t1fs super block. One instance per mounted file-system.
   super_block::s_fs_info points to instance of this type.
 */
struct c2t1fs_sb {
	/** Parsed mount options */
	struct c2t1fs_mnt_opts        csb_mnt_opts;

	/** service context of MGS. Not a member of csb_service_contexts */
	struct c2t1fs_service_context csb_mgs;

	/** number of contexts in csb_service_contexts list, that have
	    ACTIVE rpc connection and rpc session.
	    csb_nr_active_contexts <= c2_tlist_length(&csb_service_contexts) */
	uint32_t                      csb_nr_active_contexts;

	/** list of c2t1fs_service_context objects hanging using sc_link.
	    tlist descriptor: svc_ctx_tl */
	struct c2_tl                  csb_service_contexts;

	/** Total number of containers. */
	uint32_t                      csb_nr_containers;

	struct c2_pool                csb_pool;

	/** Number of data units per parity group. N */
	uint32_t                      csb_nr_data_units;

	/** Number of parity units per group. K */
	uint32_t                      csb_nr_parity_units;

	/** Stripe unit size */
	uint32_t                      csb_unit_size;

	/** used by temporary implementation of c2t1fs_fid_alloc(). */
	uint64_t                      csb_next_key;

	struct c2t1fs_container_location_map csb_cl_map;

	/** mutex that serialises all file and directory operations */
	struct c2_mutex               csb_mutex;
};

enum {
	MAGIC_DIRENT   = 0x444952454e54,     /* "DIRENT" */
	MAGIC_DIRENTHD = 0x444952454e544844  /* "DIRENTHD" */
};

/**
   Directory entry.
 */
struct c2t1fs_dir_ent {
	char            de_name[C2T1FS_MAX_NAME_LEN + 1];
	struct c2_fid   de_fid;

	/** Link in c2t1fs_inode::ci_dir_ents list.
	    List descriptor dir_ents_tl */
	struct c2_tlink de_link;

	/** magic == MAGIC_DIRENT */
	uint64_t        de_magic;
};

/**
   Inode representing global file.
 */
struct c2t1fs_inode {
	/** vfs inode */
	struct inode              ci_inode;

	/** fid of gob */
	struct c2_fid             ci_fid;

	/** layout of file's data */
	struct c2_layout         *ci_layout;

	/** List of c2t1fs_dir_ent objects placed using de_link.
	    List descriptor dir_ents_tl. Valid for only directory inode.
	    Empty for regular file inodes. */
	struct c2_tl              ci_dir_ents;
};

static inline struct c2t1fs_sb *C2T1FS_SB(const struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct c2t1fs_inode *C2T1FS_I(const struct inode *inode)
{
	return container_of(inode, struct c2t1fs_inode, ci_inode);
}

extern const struct file_operations c2t1fs_dir_file_operations;
extern const struct file_operations c2t1fs_reg_file_operations;

extern const struct inode_operations c2t1fs_dir_inode_operations;
extern const struct inode_operations c2t1fs_reg_inode_operations;

/* super.c */

/**
   For now, fid of root directory is assumed to be a constant.
 */
extern const struct c2_fid c2t1fs_root_fid;

bool c2t1fs_inode_is_root(const struct inode *inode);

int c2t1fs_get_sb(struct file_system_type *fstype,
		  int                      flags,
		  const char              *devname,
		  void                    *data,
		  struct vfsmount         *mnt);

void c2t1fs_kill_sb(struct super_block *sb);

void c2t1fs_fs_lock     (struct c2t1fs_sb *csb);
void c2t1fs_fs_unlock   (struct c2t1fs_sb *csb);
bool c2t1fs_fs_is_locked(const struct c2t1fs_sb *csb);

struct c2_rpc_session *
c2t1fs_container_id_to_session(const struct c2t1fs_sb *csb,
			       uint64_t                container_id);

/* inode.c */

int  c2t1fs_inode_cache_init(void);
void c2t1fs_inode_cache_fini(void);

struct inode *c2t1fs_root_iget(struct super_block *sb);
struct inode *c2t1fs_iget(struct super_block *sb, const struct c2_fid *fid);

struct inode *c2t1fs_alloc_inode(struct super_block *sb);
void          c2t1fs_destroy_inode(struct inode *inode);

int c2t1fs_inode_layout_init(struct c2t1fs_inode *ci,
			     struct c2_pool      *pool,
			     uint32_t             N,
			     uint32_t             K,
			     uint64_t             unit_size);

struct c2_fid c2t1fs_cob_fid(const struct c2_fid *gob_fid, int index);

C2_TL_DESCR_DECLARE(dir_ents, extern);
C2_TL_DECLARE(dir_ents, extern, struct c2t1fs_dir_ent);

void c2t1fs_dir_ent_init(struct c2t1fs_dir_ent *de,
			 const unsigned char   *name,
			 int                    namelen,
			 const struct c2_fid   *fid);

void c2t1fs_dir_ent_fini(struct c2t1fs_dir_ent *de);

#endif /* __COLIBRI_C2T1FS_H */
