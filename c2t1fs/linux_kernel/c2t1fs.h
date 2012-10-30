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

#pragma once

#ifndef __COLIBRI_C2T1FS_C2T1FS_H__
#define __COLIBRI_C2T1FS_C2T1FS_H__

#include <linux/fs.h>
#include <linux/pagemap.h>

#include "lib/tlist.h"
#include "lib/mutex.h"
#include "net/net.h"    /* c2_net_domain */
#include "rpc/session.h"
#include "rpc/rpc_machine.h"
#include "pool/pool.h"  /* c2_pool */
#include "net/buffer_pool.h"
#include "fid/fid.h"
#include "cob/cob.h"    /* c2_cob_domain_id */
#include "layout/layout.h"  /* c2_layout_domain */
#include "layout/pdclust.h" /* c2_pdclust_instance */

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
   pool_width (i.e. P) + 1. pool_width is a mount option and additional 1 for
   meta-data container.

   Assume a user-visible file F. A gob representing F is assigned fid
   <0, K>, where K is taken from a monotonically increasing counter
   (c2t1fs_sb::csb_next_key). Container-id 0 is mapped to md-service,
   by container location map.
   There are P number of component objects of file F, having fids
   { <i, K> | i = 1, 2..., P}. Here P is equal to pool_width mount option.
   Mapping from <gob_fid, cob_index> -> cob_fid is implemented using
   linear enumeration (B * x + A) with both A and B parameters set to 1.
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

struct c2t1fs_dir_ent;

int  c2t1fs_init(void);
void c2t1fs_fini(void);

enum {
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
	struct c2_net_xprt       *g_xprt;
	/** local endpoint address */
	char                     *g_laddr;
	char                     *g_db_name;
	struct c2_cob_domain_id   g_cob_dom_id;
	struct c2_net_domain      g_ndom;
	struct c2_rpc_machine     g_rpc_machine;
	struct c2_cob_domain      g_cob_dom;
	struct c2_dbenv           g_dbenv;
	struct c2_net_buffer_pool g_buffer_pool;
	struct c2_layout_domain   g_layout_dom;
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

	/** magic = C2_T1FS_SVC_CTX_MAGIC */
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

	/** pool width */
	uint32_t                      csb_pool_width;

	/** used by temporary implementation of c2t1fs_fid_alloc(). */
	uint64_t                      csb_next_key;

	struct
	c2t1fs_container_location_map csb_cl_map;

	/** mutex that serialises all file and directory operations */
	struct c2_mutex               csb_mutex;

	/** Layout for file */
	struct c2_layout             *csb_file_layout;

	/**
         * Flag indicating if c2t1fs mount is active or not.
         * Flag is set when c2t1fs is mounted and is reset by unmount thread.
         */
        bool                          csb_active;

        /**
         * Instantaneous count of pending io requests.
         * Every io request increments this value while initializing
         * and decrements it while finalizing.
         */
        struct c2_atomic64            csb_pending_io_nr;

        /** Special thread which runs ASTs from io requests. */
        struct c2_thread              csb_astthread;

        /**
         * Channel on which unmount thread will wait. It will be signalled
         * by AST thread while exiting.
         */
        struct c2_chan                csb_iowait;

        /** State machine group used for all IO requests. */
        struct c2_sm_group            csb_iogroup;

	/** File layout ID */
	uint64_t                      csb_layout_id;
};

/**
   Directory entry.
 */
struct c2t1fs_dir_ent {
	char            de_name[C2T1FS_MAX_NAME_LEN + 1];
	struct c2_fid   de_fid;

	struct dentry  *de_dentry;

	/** Link in c2t1fs_inode::ci_dir_ents list.
	    List descriptor dir_ents_tl */
	struct c2_tlink de_link;

	/** magic == C2_T1FS_DIRENT_MAGIC */
	uint64_t        de_magic;
};

/**
   Inode representing global file.
 */
struct c2t1fs_inode {
	/** vfs inode */
	struct inode               ci_inode;

	/** fid of gob */
	struct c2_fid              ci_fid;

	/** layout and related information for the file's data */
	struct c2_layout_instance *ci_layout_instance;

	/** File layout ID */
	uint64_t                   ci_layout_id;

	/** List of c2t1fs_dir_ent objects placed using de_link.
	    List descriptor dir_ents_tl. Valid for only directory inode.
	    Empty for regular file inodes. */
	struct c2_tl               ci_dir_ents;

	uint64_t                   ci_magic;
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

int c2t1fs_inode_layout_init(struct c2t1fs_inode *ci);

struct c2_fid c2t1fs_cob_fid(const struct c2t1fs_inode *ci, int index);

C2_TL_DESCR_DECLARE(dir_ents, extern);
C2_TL_DECLARE(dir_ents, extern, struct c2t1fs_dir_ent);

void c2t1fs_dir_ent_init(struct c2t1fs_dir_ent *de,
			 const unsigned char   *name,
			 int                    namelen,
			 const struct c2_fid   *fid);

int c2t1fs_dir_ent_remove(struct c2t1fs_dir_ent *de);

void c2t1fs_dir_ent_fini(struct c2t1fs_dir_ent *de);

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

#endif /* __COLIBRI_C2T1FS_C2T1FS_H__ */
