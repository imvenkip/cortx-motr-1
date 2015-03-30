/* -*- C -*- */
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/15/2010
 */

#pragma once

#ifndef __MERO_POOL_POOL_H__
#define __MERO_POOL_POOL_H__

#include "format/format.h"     /* m0_format_header */
#include "lib/rwlock.h"
#include "lib/tlist.h"
#include "reqh/reqh_service.h" /* m0_reqh_service_ctx */
#include "conf/obj.h"
#include "layout/pdclust.h"    /* m0_pdclust_attr */
#include "pool/pool_machine.h"

M0_TL_DESCR_DECLARE(pools, M0_EXTERN);
M0_TL_DESCR_DECLARE(pool_version, M0_EXTERN);

/**
   @defgroup pool Storage pools.

   @{
 */

/* import */
struct m0_io_req;
struct m0_be_seg;

/* export */
struct m0_pool;
struct m0_pool_spare_usage;

enum {
	PM_DEFAULT_NR_NODES = 10,
	PM_DEFAULT_NR_DEV = 80,
	PM_DEFAULT_MAX_NODE_FAILURES = 1,
	PM_DEFAULT_MAX_DEV_FAILURES = 80,
	POOL_MAX_RPC_NR_IN_FLIGHT = 100
};

enum map_type {
	IOS = 1,
	MDS
};

struct m0_pool {
	struct m0_fid   po_id;

	/** List of pool versions in this pool. */
	struct m0_tl    po_vers;

	/** Linkage into list of pools. */
	struct m0_tlink po_linkage;

	uint64_t        po_magic;
};

/**
 * Pool version is the subset of devices from the filesystem.
 * Pool version is associated with a pool machine and contains
 * a device to ioservice map.
 */
struct m0_pool_version {
	struct m0_fid                pv_id;

	/** Layout attributes associated with this pool version. */
	struct m0_pdclust_attr       pv_attr;

	/** Total number of nodes in this pool version. */
	uint32_t                     pv_nr_nodes;

	/** Base pool of this pool version. */
	struct m0_pool              *pv_pool;

	struct m0_pools_common      *pv_pc;

	/** Pool machine associated with this pool version. */
	struct m0_poolmach           pv_mach;

	/**
	 * An array of size m0_poolversion::pv_attr:pa_P.
	 * Maps device to io service.
	 * Each pv_dev_to_ios_map[i] entry points to instance of
	 * struct m0_reqh_service_ctx which has established rpc connections
	 * with the given service endpoints.
	 */
	struct m0_reqh_service_ctx **pv_dev_to_ios_map;

	/**
	 * Linkage into list of pool versions.
	 * @see struct m0_pool::po_vers
	 */
	struct m0_tlink              pv_linkage;

	/** M0_POOL_VERSION_MAGIC */
	uint64_t                     pv_magic;
};

/**
 * Contains resources that are shared among the pools in the filesystem.
 */
struct m0_pools_common {
	struct m0_tl                 pc_pools;

	struct m0_confc             *pc_confc;

	struct m0_rpc_machine       *pc_rmach;

	/** service context of MGS.*/
	struct m0_reqh_service_ctx   pc_mgs;

	/**
	  List of m0_reqh_service_ctx objects hanging using sc_link.
	  tlist descriptor: svc_ctx_tl
	  */
	struct m0_tl                 pc_svc_ctxs;

	/**
	  Array of pools_common_svc_ctx_tlist_length() valid elements.
	  The array size is same as the total number of service contexts,
	  pc_mds_map[i] points to m0_reqh_service_ctx of mdservice whose
	  index is i.
	  */
	struct m0_reqh_service_ctx **pc_mds_map;

	/** RM service context */
	struct m0_reqh_service_ctx  *pc_rm_ctx;

	/** Stats service context. */
	struct m0_reqh_service_ctx  *pc_ss_ctx;

	/**
	 * Each ith element in the array gives the total number of services
	 * of its corresponding type, e.g. element at M0_CST_MDS gives number
	 * of meta-data services in the filesystem.
	 */
	uint64_t                     pc_nr_svcs[M0_CST_NR];

	/** Metadata redundancy count. */
	uint32_t                     pc_md_redundancy;
	/** Pool of ioservices used to store meta data cobs. */
	struct m0_pool              *pc_md_pool;
	/** Layout instance of the mdpool. */
	struct m0_layout_instance   *pc_md_pool_linst;
};

M0_TL_DESCR_DECLARE(pools_common_svc_ctx, M0_EXTERN);
M0_TL_DECLARE(pools_common_svc_ctx, M0_EXTERN, struct m0_reqh_service_ctx);

M0_TL_DESCR_DECLARE(pool_version, M0_EXTERN);
M0_TL_DECLARE(pool_version, M0_EXTERN, struct m0_pool_version);

M0_INTERNAL void m0_pools_common_init(struct m0_pools_common *pc,
				      struct m0_rpc_machine *rmach,
				      struct m0_conf_filesystem *fs);

M0_INTERNAL void m0_pools_common_fini(struct m0_pools_common *pc);


M0_INTERNAL int m0_pools_service_ctx_create(struct m0_pools_common *pc,
					    struct m0_conf_filesystem *fs);
M0_INTERNAL void m0_pools_service_ctx_destroy(struct m0_pools_common *pc);

M0_INTERNAL int m0_pool_init(struct m0_pool *pool, struct m0_fid *id);
M0_INTERNAL void m0_pool_fini(struct m0_pool *pool);

/**
 * Initialises pool version from configuration data.
 */
M0_INTERNAL int m0_pool_version_init_by_conf(struct m0_pool_version *pv,
					     struct m0_conf_pver *pver,
					     struct m0_pool *pool,
					     struct m0_pools_common *pc,
					     struct m0_be_seg *be_seg,
					     struct m0_sm_group *sm_grp,
					     struct m0_dtm *dtm);

M0_INTERNAL int m0_pool_version_init(struct m0_pool_version *pv,
				     const struct m0_fid *id,
				     struct m0_pool *pool,
				     uint32_t pool_width,
				     uint32_t nodes,
				     uint32_t nr_data,
				     uint32_t nr_failures,
				     struct m0_be_seg *be_seg,
				     struct m0_sm_group  *sm_grp,
				     struct m0_dtm       *dtm);

M0_INTERNAL struct m0_pool_version *
m0__pool_version_find(struct m0_pool *pool, const struct m0_fid *id);

M0_INTERNAL struct m0_pool_version *
m0_pool_version_find(struct m0_pools_common *pc, const struct m0_fid *id);

M0_INTERNAL void m0_pool_version_fini(struct m0_pool_version *pv);

M0_INTERNAL int m0_pool_versions_init_by_conf(struct m0_pool *pool,
					      struct m0_pools_common *pc,
					      const struct m0_conf_pool *cp,
					      struct m0_be_seg *be_seg,
					      struct m0_sm_group  *sm_grp,
					      struct m0_dtm       *dtm);

M0_INTERNAL void m0_pool_versions_fini(struct m0_pool *pool);

M0_INTERNAL int m0_pools_init(void);
M0_INTERNAL void m0_pools_fini(void);

M0_INTERNAL int m0_pools_setup(struct m0_pools_common *pc,
			       struct m0_conf_filesystem *fs,
			       struct m0_be_seg *be_seg,
			       struct m0_sm_group *sm_grp,
			       struct m0_dtm *dtm);

M0_INTERNAL void m0_pools_destroy(struct m0_pools_common *pc);

M0_INTERNAL int m0_pool_versions_setup(struct m0_pools_common *pc,
				       struct m0_conf_filesystem *fs,
				       struct m0_be_seg *be_seg,
				       struct m0_sm_group *sm_grp,
				       struct m0_dtm *dtm);

M0_INTERNAL void m0_pool_versions_destroy(struct m0_pools_common *pc);

M0_INTERNAL struct m0_pool *m0_pool_find(struct m0_pools_common *pc,
					 const struct m0_fid *id);

/**
 * Creates service contexts from given struct m0_conf_service.
 * Creates service context for each endpoint in m0_conf_service::cs_endpoints.
 */

M0_INTERNAL struct m0_reqh_service_ctx *
m0_pools_common_service_ctx_find(const struct m0_pools_common *pc,
				 const struct m0_fid *id,
				 enum m0_conf_service_type type);

M0_INTERNAL struct m0_reqh_service_ctx *
m0_pools_common_service_ctx_find_by_type(const struct m0_pools_common *pc,
					 enum m0_conf_service_type type);

/**
 * A state that a pool node/device can be in.
 */
enum m0_pool_nd_state {
	/** a node/device is online and serving IO */
	M0_PNDS_ONLINE,

	/** a node/device is considered failed */
	M0_PNDS_FAILED,

	/** a node/device turned off-line by an administrative request */
	M0_PNDS_OFFLINE,

	/** a node/device is active in sns repair. */
	M0_PNDS_SNS_REPAIRING,

	/**
	 * a node/device completed sns repair. Its data is re-constructed
	 * on its corresponding spare space
	 */
	M0_PNDS_SNS_REPAIRED,

	/** a node/device is active in sns re-balance. */
	M0_PNDS_SNS_REBALANCING,

	/** number of state */
	M0_PNDS_NR
};

/**
 * pool node. Data structure representing a node in a pool.
 *
 * Pool node and pool server are two different views of the same physical
 * entity. A pool node is how a server looks "externally" to other nodes.
 * "struct poolnode" represents a particular server on other servers. E.g.,
 * when a new server is added to the pool, "struct poolnode" is created on
 * every server in the pool. "struct poolserver", on the other hand, represents
 * a server state machine locally on the server where it runs.
 *
 * @see pool server
 */
struct m0_poolnode {
	struct m0_format_header pn_header;
	enum m0_pool_nd_state   pn_state;
	char                    pn_pad[4];
	/** Pool node identity. */
	struct m0_fid           pn_id;
	struct m0_format_footer pn_footer;
};
M0_BASSERT(sizeof(enum m0_pool_nd_state) == 4);

/** Storage device in a pool. */
struct m0_pooldev {
	struct m0_format_header pd_header;
	/** device state (as part of pool machine state). This field is only
	    meaningful when m0_pooldev::pd_node.pn_state is PNS_ONLINE */
	enum m0_pool_nd_state   pd_state;
	char                    pd_pad[4];
	/** pool device identity */
	struct m0_fid           pd_id;
	/* a node this storage device is attached to */
	struct m0_poolnode     *pd_node;
	struct m0_format_footer pd_footer;
};

/**
 * Tracking spare slot usage.
 * If spare slot is not used for repair/rebalance, its :psp_device_index is -1.
 */
struct m0_pool_spare_usage {
	struct m0_format_header psu_header;
	/**
	 * Index of the device from m0_poolmach_state::pst_devices_array in the
	 * pool associated with this spare slot.
	 */
	uint32_t                psu_device_index;

	/** state of the device to use this spare slot */
	enum m0_pool_nd_state   psu_device_state;
	struct m0_format_footer psu_footer;
};

/** @} end group pool */

/**
   @defgroup servermachine Server machine
   @{
*/

/**
   resource limit

   Data structure to describe the fraction of resource usage limitation:
   0  : resource cannot be used at all.
   100: resource can be used entirely without limitation.
   0 < value < 100: fraction of resources can be used.
*/
struct m0_rlimit {
       int rl_processor_throughput;
       int rl_memory;
       int rl_storage_throughput;
       int rl_network_throughput;
};

/**
   pool server

   Pool server represents a pool node plus its state machines, lives locally on
   the server where it runs.

   @see pool node
*/
struct m0_poolserver {
	struct m0_poolnode      ps_node;
	/* struct m0_persistent_sm ps_mach; */
	struct m0_rlimit	ps_rl_usage; /**< the current resource usage */
};

M0_INTERNAL int m0_poolserver_init(struct m0_poolserver *srv);
M0_INTERNAL void m0_poolserver_fini(struct m0_poolserver *srv);
M0_INTERNAL int m0_poolserver_reset(struct m0_poolserver *srv);
M0_INTERNAL int m0_poolserver_on(struct m0_poolserver *srv);
M0_INTERNAL int m0_poolserver_off(struct m0_poolserver *srv);
M0_INTERNAL int m0_poolserver_io_req(struct m0_poolserver *srv,
				     struct m0_io_req *req);
M0_INTERNAL int m0_poolserver_device_join(struct m0_poolserver *srv,
					  struct m0_pooldev *dev);
M0_INTERNAL int m0_poolserver_device_leave(struct m0_poolserver *srv,
					   struct m0_pooldev *dev);

/**
 * State of SNS repair with respect to given global fid.
 * Used during degraded mode write IO.
 * During normal IO, the UNINITIALIZED enum value is used.
 * The next 2 states are used during degraded mode write IO.
 */
enum sns_repair_state {
	/**
	 * Used by IO requests done during healthy state of storage pool.
	 * Initialized to -1 in order to sync it with output of API
	 * m0_sns_cm_fid_repair_done().
	 * */
	SRS_UNINITIALIZED = 1,

	/**
	 * Assumes a distributed lock has been acquired on the associated
	 * global fid and SNS repair is yet to start on given global fid.
	 */
	SRS_REPAIR_NOTDONE,

	/**
	 * Assumes a distributed lock has been acquired on associated
	 * global fid and SNS repair has completed for given fid.
	 */
	SRS_REPAIR_DONE,

	SRS_NR,
};

/** @} end of servermachine group */
#endif /* __MERO_POOL_POOL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
