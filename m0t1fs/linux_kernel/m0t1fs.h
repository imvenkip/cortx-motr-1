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
 *                  Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 05/04/2010
 */

#pragma once

#ifndef __MERO_M0T1FS_M0T1FS_H__
#define __MERO_M0T1FS_M0T1FS_H__

#include <linux/fs.h>
#include <linux/pagemap.h>

#include "lib/tlist.h"
#include "lib/mutex.h"
#include "net/net.h"              /* m0_net_domain */
#include "rpc/rpc.h"
#include "pool/pool.h"            /* m0_pool */
#include "net/buffer_pool.h"
#include "fid/fid.h"
#include "cob/cob.h"              /* m0_cob_domain_id */
#include "layout/layout.h"
#include "ioservice/io_fops.h"    /* m0_fop_cob_create_fopt */
#include "mdservice/md_fops.h"    /* m0_fop_create_fopt */
#include "mdservice/md_fops_xc.h" /* m0_fop_create */
#include "conf/schema.h"          /* m0_conf_service_type */

/**
  @defgroup m0t1fs m0t1fs

  @section Overview

  m0t1fs is a mero client file-system for linux. It is implemented as a
  kernel module.

  @section m0t1fsfuncspec Function Specification

  m0t1fs has flat file-system structure i.e. no directories except root.
  m0t1fs does not support caching. All read-write requests are directly
  forwarded to servers.

  By default m0t1fs uses end-point address 0@lo:12345:45:6 as its local
  address. This address can be changed with local_addr module parameter.
  e.g. to make m0t1fs use 172.18.50.40@o2ib1:12345:34:1 as its end-point address
  load module with command:

  sudo insmod m0mero.ko local_addr="172.18.50.40@o2ib1:12345:34:1"

  m0t1fs can be mounted with mount command:

  mount -t m0t1fs -o <options_list> dontcare <dir_name>

  where <options_list> is a comma separated list of option=value elements.
  Currently supported list of options is:

  - confd [value type: end-point address, e.g. 192.168.50.40@tcp:12345:34:1]
      end-point address of confd (a.k.a. management service, mgs).

  - profile [value type: string]
      configuration profile. Used while fetching configuration data from confd.

  - local_conf [value type: string]
      configuration string, containing data to pre-load configuration
      cache with (see @ref conf-fspec-preload).

  - ios [value type: end-point address]
      end-point address of io-service. Multiple io-services can be specified
      as ios=<end-point-addr1>,ios=<end-point-addr2>

  - nr_data_units [value type: number]
      Number of data units in one parity group. Optional parameter.
      Default value is M0T1FS_DEFAULT_NR_DATA_UNITS.

  - nr_parity_units [value type: number]
      Number of parity units in one parity group. Optional parameter.
      Default value is M0T1FS_DEFAULT_NR_PARITY_UNITS.

  - pool_width [value type: number]
      Number of component objects over which file contents are striped.
      Optional parameter.
      Default value is computed as sum of effective nr_data_units and
      (2 * nr_parity_units).
      pool_width >= nr_data_units + 2 * nr_parity_units. (2 to account for
      nr_spare_units which is equal to nr_parity_units. P >= N + 2 * K)

  - unit_size [value type: number]
      Size of each stripe unit. Optional parameter. Default value is
      M0T1FS_DEFAULT_STRIPE_UNIT_SIZE (=PAGE_CACHE_SIZE).

   'device' argument of mount command is ignored.

   m0t1fs supports following operations:
   - Creating up to M0T1FS_MAX_NR_DIR_ENTS number of regular files
   - Remove a regular file
   - Listing files in root directory
   - file read/write of full-stripe width

   @section m0t1fslogspec Logical Specification

   <B>mount/unmount:</B>

   m0t1fs currently takes io/metadata service end-point address and striping
   parameters as mount options. Once mgs/confd is ready, all this information
   should be fetched from mgs. In which case, mgs address and profile name
   will be the only required mount options.

   m0t1fs establishes rpc-connections and rpc-sessions with all the services
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

   Currently m0t1fs implements simple (and temporary) mechanism to
   build container location map. Number of containers is equal to
   P + 1, where P is pool width and 1 is used by a meta-data container.
   Pool width is a file-system parameter, obtained from configuration.

   Assume a user-visible file F. A gob representing F is assigned fid
   <0, K>, where K is taken from a monotonically increasing counter
   (m0t1fs_sb::csb_next_key). Container-id 0 is mapped to md-service,
   by container location map.
   There are P number of component objects of file F, having fids
   { <i, K> | i = 1, 2..., P}. Here P is equal to pool_width mount option.
   Mapping from <gob_fid, cob_index> -> cob_fid is implemented using
   linear enumeration (B * x + A) with both A and B parameters set to 1.
   Container location map, maps container-ids from 1 to P, to io-services.

   Container location map is populated at mount time.

   <B> Directory Operations: </B>

   To create a regular file, m0t1fs sends cob create requests to mds (for global
   object aka gob) and io-service (for component objects). Because, mds is not
   yet implemented, m0t1fs does not send cob create request to any mds.
   Instead all directory entries are maintained in an in-memory list in root
   inode itself.

   If component object creation fails, m0t1fs does not attempt to cleanup
   component objects that were successfully created. This should be handled by
   dtm component, which is not yet implemented.

   <B> Read/Write: </B>

   m0t1fs currently supports only full stripe IO
   i.e. (iosize % (nr_data_units * stripe_unit_size) == 0)

   read-write operations on file are not synchronised.

   m0t1fs does not cache any data.

   For simplicity, m0t1fs does synchronous rpc with io-services, to read/write
   component objects.
 */

/**
  @section m0t1fs-metadata Metadata

  @section m0t1fs-metadata-rq Requirements

  The following requirements should be met:

  - @b R.DLD.MDSERVICE - all md operations implemented by m0t1fs
  should talk to mdservice in order to provide required functionality. In
  other words they should not generate hardcoded attributes locally on client
  and cache in inode/dentry cache. Required functionality here - is to provide
  mounts persistence.

  - @b R.DLD.POSIX - implemented operations should conform to POSIX
 */

/**
  @section Overview

  The main direction of this work (@ref m0t1fs-metadata-rq) is mostly to follow
  normal linux filesystem driver requirements as for the number and meaning of
  operations handled by the driver.

  This means number of things as follows bellow:

  - the fops presented below, correspond to the usual operations implemented
  by filesystem driver, such as link, unlink, create, etc;
  - mdservice on server side is connected by m0t1fs at mount time. Root inode
  attributes and (optionally) filesystem information, such as free blocks, etc.,
  that is usually needed by statfs, can be retrieved by m0t1fs from mount fop
  response;
  - inode attributes may also contain some information needed to perform I/O
  (layout) and this data may be sent to client as part of getattr fop.

  Fops defined by mdservice are described at @ref mdservice-fops-definition
  In this work we use only some of them in order to provide persistence
  between mounts.

  They are the following:
  - m0_fop_create and m0_fop_create_rep
  - m0_fop_unlink and m0_fop_unlink_rep
  - m0_fop_setattr and m0_fop_setattr_rep
  - m0_fop_getattr and m0_fop_getattr_rep

  @section m0t1fs-metadata-fs Detailed Functional Specification

  Some functions will see modifications in order to implement the
  client-server communication and not just create all metadata in
  memory.

  Only minimal set of operations is needed to support persitency
  between mounts.

  In order to implement mount persistency functionality, the following
  functionality should be implemented:

  - get mdservice volume information enough for statfs and get root
  fid at mount time
  - no hierarchy is needed, that is, no directories support will be
  added in this work
  - list of regular files in root directory should be supported
  - operations with regular files should be supported

  @section Initialization

  m0t1fs_init() - m0_mdservice_fop_init() is added to initialize md
  operations fops.

  m0t1fs_fini() - m0_mdservice_fop_fini() is added to finalize md
  operations fops.

  m0t1fs_fill_super() -> m0t1fs_connect_to_services() - will have also
  to connect the mdservice.

  It also may have some fields from space csb->csb_* initialized
  not from mount options but rather from connect reply information.

  m0t1fs_kill_sb() -> m0t1fs_disconnect_from_services() - will have
  also to disconnect from mdservice.

  @section Inode operations

  m0t1fs_mknod() - is to be added to m0t1fs_dir_inode_operations.
  This is one of variants of create fop.

  m0t1fs_create() - sending create fop is added. Layout speicified
  with mount options or obtained in mount time is to be packed into
  create fop and sent to the server. Errors should be handled on
  all stages.

  m0t1fs_lookup() - sending getattr/lookup fop is added. Errors are
  handled.

  m0t1fs_unlink() - sending unlink fop is added. Errors are handled
  (not needed for this work).

  m0t1fs_link() - is to be added to m0t1fs_dir_inode_operations.
  This is normal hard-link create function that sends link fop to
  mdservice (not needed for this work).

  m0t1fs_mkdir() - is to be added to m0t1fs_dir_inode_operations.
  This function sends create fop initialized in slightly different
  way than for creating regular file (not needed for this work).

  m0t1fs_rmdir() - is to be added to m0t1fs_dir_inode_operations.
  This function sends unlink fop (not needed for this work).

  m0t1fs_symlink() - is to be added to m0t1fs_dir_inode_operations.
  This function sends create fop with mode initialized for symlinks
  (not needed for this work).

  m0t1fs_rename() - is to be added to m0t1fs_dir_inode_operations.
  This function sends rename fop (not needed for this work).

  m0t1fs_setattr() - is to be added to m0t1fs_dir_inode_operations
  and m0t1fs_special_inode_operations. This function sends setattr
  fop.

  m0t1fs_getattr() - is to be added to m0t1fs_dir_inode_operations
  and m0t1fs_special_inode_operations. This function sends getattr
  fop and returns server side inode attributes.

  m0t1fs_permission() - is to be added to m0t1fs_dir_inode_operations
  and m0t1fs_special_inode_operations (not needed for this work).

  The following extended attributes operations to be added. We need
  them in order to run lustre on top of mero.

  m0t1fs_setxattr() - is to be added to m0t1fs_dir_inode_operations
  and m0t1fs_special_inode_operations (not needed for this work).

  m0t1fs_getxattr() - is to be added to m0t1fs_dir_inode_operations
  and m0t1fs_special_inode_operations (not needed for this work).

  m0t1fs_listxattr() - is to be added to m0t1fs_dir_inode_operations
  and m0t1fs_special_inode_operations (not needed for this work).

  m0t1fs_removexattr() - is to be added to m0t1fs_dir_inode_operations
  and m0t1fs_special_inode_operations (not needed for this work).

  Mdservice does not support xattrs fop operations yet. We will add them
  later as part of xattr support work.

  @section File operations

  m0t1fs_open() - is to be added to m0t1fs_dir_file_operations and
  m0t1fs_reg_file_operations.

  This function sends open fop and handles reply and errors in a
  standard way.

  m0t1fs_release() - is to be added to m0t1fs_dir_file_operations and
  m0t1fs_reg_file_operations.

  This function sends close fop on last release call.

  m0t1fs_readdir() - sending readdir and/or getpage fop is added.
  Errors handling and page cache management code should be added.

  @sections Misc changes

  fid_hash() - hash should be generated including ->f_container
  and f_key. This can be used for generating inode numbers in future.
  Still, changing inode numbers allocation is out of scope of this work.
 */

/**
  @section m0t1fs-metadata-ls Logical Specification

  - @ref m0t1fs-metadata-fs
  - @ref m0t1fs-metadata-rq

  All the fs callback functions, implemented in m0t1fs filesystem driver,
  are to be backed with corresponding fop request communicating mdservice
  on server side.

 */

/**
  @section m0t1fs-metadata-cf Conformance

  - @b R.DLD.MDSERVICE - Add support of meta-data operations (above) to m0t1fs.
  Implement a set of fops to be used for communicating meta-data operations
  between m0t1fs and mdservice.

  - @b R.DLD.POSIX - following standard linux fs driver callback functions
  and reqs guarantees basic POSIX conformance. More fine-graned conformance
  may be met with using "standard" test suites for file sytems, such as dbench,
  which is mentioned in Testing section (@ref m0t1fs-metadata-ts). This will
  not be applied in current work as only miimal set of operations will be
  implemented to support between mounts persistence.
 */

/**
  @section m0t1fs-metadata-ts Testing

  In principle full featured m0t1fs should pass standard fs tests such as
  dbench as part of integration testing. This work scope testing will cover
  the basic functionality that includes mount, create files, re-mount and check
  if files exist and have the same attributes as before re-mount.

  As for unit testing, that may run on "before-commit" basis,  small and fast
  set of basic fs operations (10-20 operations) can be added to standard UT
  framework.

 */

/**
  @section m0t1fs-metadata-dp Dependencies

  - layout. It would be good to have layout functionality in place but without
  it most of work also can be done.
  - recovery. Recovery principles better to be ready to finish this work. Some
  tests may load client node so much that connection problems can occur.

 */

/*
 ******************************************************************************
 * Kernel client ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_M0T1FS_MOD	= 500,
	M0_ADDB_CTXID_M0T1FS_MOUNTP	= 501,
	M0_ADDB_CTXID_M0T1FS_OP_READ	= 502,
	M0_ADDB_CTXID_M0T1FS_OP_WRITE	= 503,
};

M0_ADDB_CT(m0_addb_ct_m0t1fs_mod,	M0_ADDB_CTXID_M0T1FS_MOD);
M0_ADDB_CT(m0_addb_ct_m0t1fs_mountp,	M0_ADDB_CTXID_M0T1FS_MOUNTP);
M0_ADDB_CT(m0_addb_ct_m0t1fs_op_read,	M0_ADDB_CTXID_M0T1FS_OP_READ);
M0_ADDB_CT(m0_addb_ct_m0t1fs_op_write,	M0_ADDB_CTXID_M0T1FS_OP_WRITE);

extern struct m0_addb_ctx m0t1fs_addb_ctx;

enum {
	M0T1FS_ADDB_LOC_AIO_READ		= 10,
	M0T1FS_ADDB_LOC_AIO_WRITE		= 20,
	M0T1FS_ADDB_LOC_AIO_REQ			= 30,
	M0T1FS_ADDB_LOC_DBUF_ALLOI_BUF		= 40,
	M0T1FS_ADDB_LOC_IOMAPS_PREP_GRPARR	= 50,
	M0T1FS_ADDB_LOC_IOMAPS_PREP_MAP		= 60,
	M0T1FS_ADDB_LOC_IOMAPS_PREP_MAPS	= 70,
	M0T1FS_ADDB_LOC_IOMAP_INIT_DBUFS_COL	= 80,
	M0T1FS_ADDB_LOC_IOMAP_INIT_DBUFS_ROW	= 90,
	M0T1FS_ADDB_LOC_IOMAP_INIT_IV		= 100,
	M0T1FS_ADDB_LOC_IOMAP_INIT_PBUFS_COL	= 110,
	M0T1FS_ADDB_LOC_IOMAP_INIT_PBUFS_ROW	= 120,
	M0T1FS_ADDB_LOC_IOREQ_INIT_BVECB	= 130,
	M0T1FS_ADDB_LOC_IOREQ_INIT_BVECC	= 140,
	M0T1FS_ADDB_LOC_IOREQ_INIT_IV		= 150,
	M0T1FS_ADDB_LOC_IOREQ_INIT_PGATTRS	= 160,
	M0T1FS_ADDB_LOC_IVEC_CREAT_IV		= 170,
	M0T1FS_ADDB_LOC_PARITY_RECALC_DBUFS	= 180,
	M0T1FS_ADDB_LOC_PARITY_RECALC_PBUFS	= 190,
	M0T1FS_ADDB_LOC_PARITY_RECALC_OLD_BUFS	= 200,
	M0T1FS_ADDB_LOC_TIOREQ_GET_TI		= 210,
	M0T1FS_ADDB_LOC_TIOREQ_MAP_QDEVST	= 215,
	M0T1FS_ADDB_LOC_TIOREQ_MAP_QSPSLOT	= 217,
	M0T1FS_ADDB_LOC_TI_FOP_PREP		= 220,
	M0T1FS_ADDB_LOC_TI_REQ_INIT_IV		= 230,
	M0T1FS_ADDB_LOC_DGMODE_PROCESS_1        = 240,
	M0T1FS_ADDB_LOC_DGMODE_PROCESS_2        = 250,
	M0T1FS_ADDB_LOC_DGMODE_RECOV_DATA       = 260,
	M0T1FS_ADDB_LOC_DGMODE_RECOV_PARITY     = 270,
	M0T1FS_ADDB_LOC_DGMODE_RECOV_FAILVEC    = 280,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_INIT      = 290,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_BVEC      = 300,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_BVEC_CNT  = 310,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_PAGEATTR  = 320,
	M0T1FS_ADDB_LOC_READVEC_ALLOC_IVEC_FAIL = 330,
};

struct m0_pdclust_layout;

M0_INTERNAL int m0t1fs_init(void);
M0_INTERNAL void m0t1fs_fini(void);

/**
   Return the value of the kernel node_uuid parameter.
 */
const char *m0t1fs_param_node_uuid_get(void);

enum {
	M0T1FS_RPC_TIMEOUT              = 10, /* seconds */
	M0T1FS_NR_SLOTS_PER_SESSION     = 10,
	M0T1FS_MAX_NR_RPC_IN_FLIGHT     = 100,
	M0T1FS_DEFAULT_NR_DATA_UNITS    = 1,
	M0T1FS_DEFAULT_NR_PARITY_UNITS  = 1,
	M0T1FS_DEFAULT_STRIPE_UNIT_SIZE = PAGE_CACHE_SIZE,
	M0T1FS_MAX_NR_CONTAINERS        = 1024,
	M0T1FS_COB_ID_STRLEN		= 34,
};

/** Anything that is global to m0t1fs module goes in this singleton structure.
    There is only one, global, instance of this type. */
struct m0t1fs_globals {
	struct m0_net_xprt       *g_xprt;
	/** local endpoint address module parameter */
	const char               *g_laddr;
	char                     *g_db_name;
	struct m0_cob_domain_id   g_cob_dom_id;
	struct m0_net_domain      g_ndom;
	struct m0_rpc_machine     g_rpc_machine;
	struct m0_cob_domain      g_cob_dom;
	struct m0_dbenv           g_dbenv;
	struct m0_net_buffer_pool g_buffer_pool;
	struct m0_layout_domain   g_layout_dom;
};

extern struct m0t1fs_globals m0t1fs_globals;

/**
   For each <mounted_fs, target_service> pair, there is one instance of
   m0t1fs_service_context.

   m0t1fs_service_context is an association class, associating mounted
   file-system and a service.

   Allocated at mount time and freed during unmount.
 */
struct m0t1fs_service_context {
	/** Superblock associated with this service context */
	struct m0t1fs_sb         *sc_csb;

	/** Service type */
	enum m0_conf_service_type sc_type;

	struct m0_rpc_conn        sc_conn;
	struct m0_rpc_session     sc_session;

	/** Link in m0t1fs_sb::csb_service_contexts list */
	struct m0_tlink           sc_link;

	/** Magic = M0_T1FS_SVC_CTX_MAGIC */
	uint64_t                  sc_magic;
};

/**
   Given a container id of a container, map give service context of a service
   that is serving the container.
 */
struct m0t1fs_container_location_map {
	/**
	   Array of m0t1fs_sb::csb_nr_container valid elements.
	   clm_map[i] points to m0t1fs_service_context of a service
	   that is serving objects belonging to container i
	 */
	struct m0t1fs_service_context *clm_map[M0T1FS_MAX_NR_CONTAINERS];
};

/**
   In memory m0t1fs super block. One instance per mounted file-system.
   super_block::s_fs_info points to instance of this type.
 */
struct m0t1fs_sb {
	/** service context of MGS. Not a member of csb_service_contexts */
	struct m0t1fs_service_context csb_mgs;

	/** number of contexts in csb_service_contexts list, that have
	    ACTIVE rpc connection and rpc session.
	    csb_nr_active_contexts <= m0_tlist_length(&csb_service_contexts) */
	uint32_t                      csb_nr_active_contexts;

	/** list of m0t1fs_service_context objects hanging using sc_link.
	    tlist descriptor: svc_ctx_tl */
	struct m0_tl                  csb_service_contexts;

	/** Total number of containers. */
	uint32_t                      csb_nr_containers;

	/** pool width */
	uint32_t                      csb_pool_width;

	struct m0_pool                csb_pool;

	/** used by temporary implementation of m0t1fs_fid_alloc(). */
	uint64_t                      csb_next_key;

	struct
	m0t1fs_container_location_map csb_cl_map;

	/** mutex that serialises all file and directory operations */
	struct m0_mutex               csb_mutex;

	/** Layout for file */
	struct m0_layout             *csb_file_layout;

	/**
         * Flag indicating if m0t1fs mount is active or not.
         * Flag is set when m0t1fs is mounted and is reset by unmount thread.
         */
        bool                          csb_active;

        /**
         * Instantaneous count of pending io requests.
         * Every io request increments this value while initializing
         * and decrements it while finalizing.
         */
        struct m0_atomic64            csb_pending_io_nr;

        /** Special thread which runs ASTs from io requests. */
        struct m0_thread              csb_astthread;

        /**
         * Channel on which unmount thread will wait. It will be signalled
         * by AST thread while exiting.
         */
        struct m0_chan                csb_iowait;

        /** State machine group used for all IO requests. */
        struct m0_sm_group            csb_iogroup;

	/** File layout ID */
	uint64_t                      csb_layout_id;

        /** Root fid, retrieved from mdservice in mount time. */
        struct m0_fid                 csb_root_fid;
        /** Maximal allowed namelen (retrived from mdservice) */
        int                           csb_namelen;

	/** Run-time addb context for each mount point */
	struct m0_addb_ctx            csb_addb_ctx;
};

struct m0t1fs_filedata {
        int                        fd_direof;
        struct m0_bitstring       *fd_dirpos;
};

/**
   Metadata operation helper structure.
 */
struct m0t1fs_mdop {
        struct m0_cob_attr         mo_attr;
};

/**
   Inode representing global file.
 */
struct m0t1fs_inode {
	/** vfs inode */
	struct inode               ci_inode;

	/** fid of gob */
	struct m0_fid              ci_fid;

	/** layout and related information for the file's data */
	struct m0_layout_instance *ci_layout_instance;

	/** File layout ID */
	uint64_t                   ci_layout_id;

	uint64_t                   ci_magic;
};

static inline struct m0t1fs_sb *M0T1FS_SB(const struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct m0t1fs_inode *M0T1FS_I(const struct inode *inode)
{
	return container_of(inode, struct m0t1fs_inode, ci_inode);
}

extern const struct file_operations m0t1fs_dir_file_operations;
extern const struct file_operations m0t1fs_reg_file_operations;

extern const struct inode_operations m0t1fs_dir_inode_operations;
extern const struct inode_operations m0t1fs_reg_inode_operations;

/* super.c */

M0_INTERNAL bool m0t1fs_inode_is_root(const struct inode *inode);

M0_INTERNAL int m0t1fs_get_sb(struct file_system_type *fstype,
			      int flags,
			      const char *devname,
			      void *data, struct vfsmount *mnt);

M0_INTERNAL void m0t1fs_kill_sb(struct super_block *sb);

M0_INTERNAL void m0t1fs_fs_lock(struct m0t1fs_sb *csb);
M0_INTERNAL void m0t1fs_fs_unlock(struct m0t1fs_sb *csb);
M0_INTERNAL bool m0t1fs_fs_is_locked(const struct m0t1fs_sb *csb);

M0_INTERNAL int m0t1fs_getattr(struct vfsmount *mnt, struct dentry *de,
                               struct kstat *stat);
M0_INTERNAL int m0t1fs_setattr(struct dentry *de, struct iattr *attr);
M0_INTERNAL int m0t1fs_inode_update(struct inode *inode,
                                    struct m0_fop_cob *body);

M0_INTERNAL struct m0_rpc_session *
m0t1fs_container_id_to_session(const struct m0t1fs_sb *csb,
			       uint64_t container_id);

/* inode.c */

M0_INTERNAL int m0t1fs_inode_cache_init(void);
M0_INTERNAL void m0t1fs_inode_cache_fini(void);

M0_INTERNAL struct inode *m0t1fs_root_iget(struct super_block *sb,
                                           struct m0_fid *root_fid);
M0_INTERNAL struct inode *m0t1fs_iget(struct super_block *sb,
				      const struct m0_fid *fid,
                          	      struct m0_fop_cob *body);

M0_INTERNAL struct inode *m0t1fs_alloc_inode(struct super_block *sb);
M0_INTERNAL void m0t1fs_destroy_inode(struct inode *inode);

M0_INTERNAL int m0t1fs_inode_layout_init(struct m0t1fs_inode *ci);

M0_INTERNAL struct m0_fid m0t1fs_ios_cob_fid(const struct m0t1fs_inode *ci,
					     int index);
/**
 * I/O mem stats.
 * Prefix "a_" stands for "allocate".
 * Prefix "d_" stands for "de-allocate".
 */
struct io_mem_stats {
	uint64_t a_ioreq_nr;
	uint64_t d_ioreq_nr;
	uint64_t a_pargrp_iomap_nr;
	uint64_t d_pargrp_iomap_nr;
	uint64_t a_target_ioreq_nr;
	uint64_t d_target_ioreq_nr;
	uint64_t a_io_req_fop_nr;
	uint64_t d_io_req_fop_nr;
	uint64_t a_data_buf_nr;
	uint64_t d_data_buf_nr;
	uint64_t a_page_nr;
	uint64_t d_page_nr;
};

M0_INTERNAL int m0t1fs_mds_cob_create(struct m0t1fs_sb          *csb,
                                      const struct m0t1fs_mdop  *mo,
                                      struct m0_fop_create_rep **rep);

M0_INTERNAL int m0t1fs_mds_cob_unlink(struct m0t1fs_sb          *csb,
                                      const struct m0t1fs_mdop  *mo,
                                      struct m0_fop_unlink_rep **rep);

M0_INTERNAL int m0t1fs_mds_cob_link(struct m0t1fs_sb          *csb,
                                    const struct m0t1fs_mdop  *mo,
                                    struct m0_fop_link_rep   **rep);

M0_INTERNAL int m0t1fs_mds_cob_lookup(struct m0t1fs_sb          *csb,
                                      const struct m0t1fs_mdop  *mo,
                                      struct m0_fop_lookup_rep **rep);

M0_INTERNAL int m0t1fs_mds_cob_getattr(struct m0t1fs_sb           *csb,
                                       const struct m0t1fs_mdop   *mo,
                                       struct m0_fop_getattr_rep **rep);

M0_INTERNAL int m0t1fs_mds_statfs(struct m0t1fs_sb                *csb,
                                  struct m0_fop_statfs_rep       **rep);

M0_INTERNAL int m0t1fs_mds_cob_setattr(struct m0t1fs_sb           *csb,
                                       const struct m0t1fs_mdop   *mo,
                                       struct m0_fop_setattr_rep **rep);

M0_INTERNAL int m0t1fs_mds_cob_readdir(struct m0t1fs_sb           *csb,
                                       const struct m0t1fs_mdop   *mo,
                                       struct m0_fop_readdir_rep **rep);

M0_INTERNAL int m0t1fs_size_update(struct inode *inode,
                                   uint64_t newsize);

#endif /* __MERO_M0T1FS_M0T1FS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
