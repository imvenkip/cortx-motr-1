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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>,
 *                  Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 03/30/2010
 */

#pragma once

#ifndef __COLIBRI_SNS_REPAIR_H__
#define __COLIBRI_SNS_REPAIR_H__

#include "lib/ext.h"
#include "lib/refs.h"
#include "lib/rwlock.h"
#include "sm/sm.h"

struct c2_io_req;
struct c2_fop;
struct c2_dtm;
struct c2_dtx;

/**
   @page snsrepair SNS repair detailed level design specification.

   @section Overview

   SNS repair is a mechanism that restores functional and redundancy related
   properties of a pool after a failure. High level design of SNS repair is
   described in "HLD of SNS Repair" (https://docs.google.com/a/horizontalscale.com/Doc?docid=0AblBNh6IJyYvZGh0d2prNTVfMjV3NWN0Y2M5) document.

   SNS repair is designed to scalably reconstruct data or meta-data in a
   redundant striping pool after a data loss due to a failure. Redundant
   striping stores a cluster-wide object in a collection of components,
   according to a particular striping pattern.

   @section def Definitions and requirements

   See HLD for definitions and requirements.

   @section repairfuncspec Functional specification

   SNS repair component is dormant during normal cluster operation. It is
   activated by liveness events such as server or storage device failure or
   recovery. On the first failure in an otherwise functional pool, SNS rebuild
   directs a collection of "agents" associated with pool devices and network
   interfaces to start a reconstruction of data conatined on the lost
   unit. Reconstructions process is designed to utilize the fraction of
   available storage bandwidth on all pool devices (on average). Reconstructed
   data are stored in a "distributed spare space" uniformly scattered over all
   storage devices in the pool.

   Should an additional failure happen during data reconstruction, the
   reconstruction process is stopped, reconfigured to take additional failure
   into account and re-started. After reconstructions finishes, the pool can
   experience further failures. Reconstruction of data lost due to those uses
   data ecavuated to the distributed spare by the previous reconstructions.

   When a new server or a new storage device are added to the pool, the pool is
   "re-balanced" to free used spare space.

   A pool is configured to sustain a given number of server and device failures
   (these parameters determine redundancy of striping patterns used by the pool
   and the amount of allocated spare space). Once all allowed failures happened,
   any further failure transfres the pool into a "dud" state, where availability
   guarantees are rescinded.

   While data reconstruction is ongoing, external (client) IO against the pool
   proceeds in a "degraded" mode with clients doing reconstruction on demand and
   helping SNS repair with their writes.

   @section repairlogspec Logical specification

   SNS repair is implemented as a system of two collaborating sub-components:

   \li a pool machine: a replicated state machine responsible for organising IO
   with a pool. External events such as failures, liveness layer and management
   tool call-backs incur state transitions in a pool machine. A pool machine, in
   turn, interacts with entities such as layout manager and layout IO engines to
   control pool IO. A pool machine uses quorum based consensus to function in
   the face of failures;

   \li a copy machine: replicated state machine responsible for carrying out a particular
   instance of a data restructuring. A copy machine is used to move, duplicate
   or reconstruct data for various scenarios including

       @ref poolmach
       @ref copymachine
 */

/**
   @defgroup poolmach Pool machine
   @{
*/

enum c2_poolmach_version {
	PVE_READ,
	PVE_WRITE,
	PVE_NR
};

/** A state that a pool node can be in as far as a pool machine is concerned */
enum c2_poolnode_state {
	/** a node is online and serving IO */
	PNS_ONLINE,
	/** a node is considered failed */
	PNS_FAILED,
	/** a node turned off-line by an administrative request */
	PNS_OFFLINE,
	/** a node is active, but not yet serving IO */
	PNS_RECOVERING
};

/** A state that a storage device attached to a pool node can be in as far as a
    pool machine is concerned */
enum c2_pooldev_state {
	/** a device is online and serving IO */
	PDS_ONLINE,
	/** a device is considered failed */
	PDS_FAILED,
	/** a device turned off-line by an administrative request */
	PDS_OFFLINE
};

/**
   pool node. Data structure representing a node in a pool.

   Pool node and pool server are two different views of the same physical
   entity. A pool node is how a server looks "externally" to other nodes.
   "struct poolnode" represents a particular server on other servers. E.g.,
   when a new server is added to the pool, "struct poolnode" is created on
   every server in the pool. "struct poolserver", on the other hand, represents
   a server state machine locally on the server where it runs.

   @see pool server
*/
struct c2_poolnode {
	enum c2_poolnode_state  pn_state;
	struct c2_server       *pn_id;
};

/**
   pool device

   Data structure representing a storage device in a pool.
*/
struct c2_pooldev {
	/** device state (as part of pool machine state). This field is only
	    meaningful when c2_pooldev::pd_node.pn_state is PNS_ONLINE */
	enum c2_pooldev_state  pd_state;
	struct c2_device      *pd_id;
	/* a node this storage devie is attached to */
	struct c2_poolnode    *pd_node;
};

/**
   Persistent pool machine state.

   Copies of this struct are maintained by every node that thinks it is a part
   of the pool. This state is updated by a quorum protocol.
*/
struct c2_poolmach_state {
	/**
	 * pool version numbers vector updated on a failure.
	 *
	 * Matching pool version numbers must be presented by a client to do IO
	 * against the pool. Usually pool version numbers vector is delivered to
	 * a client together with a layout or a lock. Version numbers change on
	 * failures effectively invalidating layouts affected by the failure.
	 *
	 * @note In addition to or instead of pool version numbers vector, every
	 * layout can be tagged with a bitmask of alive pool nodes and devices
	 * (such a bitmask is needed anyway to direct degraded mode IO). Version
	 * numbers are advantageous for very large pools, where bitmask would be
	 * too large.
	 */
	uint64_t            pst_version[PVE_NR];
	/** number of nodes currently in the pool */
	uint32_t            pst_nr_nodes;
	/** identity and state of every node in the pool */
	struct c2_poolnode *pst_node;
	/** number of devices currently in the pool */
	uint32_t            pst_nr_devices;
	/** identity and state of every device in the pool */
	struct c2_pooldev  *pst_device;

	/** maximal number of node failures the pool is configured to sustain */
	uint32_t            pst_max_node_failures;
	/** maximal number of device failures the pool is configured to
	    sustain */
	uint32_t            pst_max_device_failures;
};

/**
   pool machine. Data structure representing replicated pool state machine.

   Concurrency control: pool machine state is protected by a single read-write
   blocking lock. "Normal" operations, e.g., client IO, including degraded mode
   IO, take this lock in a read mode, because they only inspect pool machine
   state (e.g., version numbers vector) never modifying it. "Configuration"
   events such as node or device failures, addition or removal of a node or
   device and administrative actions against the pool, all took the lock in a
   write mode.
*/
struct c2_poolmach {
	/* struct c2_persistent_sm  pm_mach; */
	struct c2_poolmach_state pm_state;
	struct c2_rwlock         pm_lock;
};


int  c2_poolmach_init(struct c2_poolmach *pm, struct c2_dtm *dtm);
void c2_poolmach_fini(struct c2_poolmach *pm);

int  c2_poolmach_device_join (struct c2_poolmach *pm, struct c2_pooldev *dev);
int  c2_poolmach_device_leave(struct c2_poolmach *pm, struct c2_pooldev *dev);

int  c2_poolmach_node_join (struct c2_poolmach *pm, struct c2_poolnode *node);
int  c2_poolmach_node_leave(struct c2_poolmach *pm, struct c2_poolnode *node);

/** @} end of poolmach group */

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
struct c2_rlimit {
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
struct c2_poolserver {
	struct c2_poolnode      ps_node;
	/* struct c2_persistent_sm ps_mach; */
	struct c2_rlimit	ps_rl_usage; /**< the current resource usage */
};

int  c2_poolserver_init(struct c2_poolserver *srv);
void c2_poolserver_fini(struct c2_poolserver *srv);
int  c2_poolserver_reset(struct c2_poolserver *srv);
int  c2_poolserver_on(struct c2_poolserver *srv);
int  c2_poolserver_off(struct c2_poolserver *srv);
int  c2_poolserver_io_req(struct c2_poolserver *srv, struct c2_io_req *req);
int  c2_poolserver_device_join(struct c2_poolserver *srv,
			       struct c2_pooldev *dev);
int  c2_poolserver_device_leave(struct c2_poolserver *srv,
				struct c2_pooldev *dev);

/** @} end of servermachine group */

/**
   @defgroup copymachine Copy machine
   @note Copy machine should go into a dedicated file (or a directory).
   @{
*/

struct c2_cm;

/** copy machine stats */
struct c2_cm_stats {
	int cm_progess;
	int cm_error;
};

/** copy machine input set description */
struct c2_cm_iset {
};

/** copy machine output set description */
struct c2_cm_oset {
};

int  c2_cm_iset_init(struct c2_cm_iset *iset);
void c2_cm_iset_fini(struct c2_cm_iset *iset);

int  c2_cm_oset_init(struct c2_cm_oset *oset);
void c2_cm_oset_fini(struct c2_cm_oset *oset);

struct c2_cm_copy_packet;
/**
   copy machine operations

   A copy machine has a handler which handles FOP requests. A copy machine is
   responsible to create corresponding agents to do the actual work.
   A copy machine registers its 'fop handler' to main fop handler. Then this
   copy machine will handle copy machine related requests.
*/
struct c2_cm_operations {
	int (*cmops_init)   (struct c2_cm *self);
	int (*cmops_stop)   (struct c2_cm *self, int force);
	int (*cmops_config) (struct c2_cm *self, struct c2_cm_iset *iset,
		             struct c2_cm_oset *oset, struct c2_rlimit *rlimit);
	int (*cmops_handler)(struct c2_cm *self, struct c2_fop *req);
	int (*cmops_queue)  (struct c2_cm_copy_packet *cp);
	int (*cmops_stats)  (const struct c2_cm *cm,
                             struct c2_cm_stats *stats /**< [out] output stats */
			    );
	/** adjust resource limitation parameters dynamically. */
	int (*cmops_adjust_rlimit)(struct c2_cm *cm, struct c2_rlimit *new_rl);
};


struct c2_container;
struct c2_device;
struct c2_layout;

struct c2_cm_iset_cursor {
	struct c2_cm_iset    *ic_iset;
	struct c2_poolserver *ic_server;
	struct c2_device     *ic_device;
	struct c2_container  *ic_container;
	struct c2_layout     *ic_layout;
	/** an extent within the container. */
	struct c2_ext         ic_extent;
};

/** copy machine input set cursor operations */
struct c2_cm_iset_cursor_operations {
	int  (*c2_cm_iset_cursor_init)   (struct c2_cm_iset_cursor *cur);
	void (*c2_cm_iset_cursor_fini)   (struct c2_cm_iset_cursor *cur);
	void (*c2_cm_iset_cursor_copy)   (struct c2_cm_iset_cursor *dst,
 				          const struct c2_cm_iset_cursor *src);
	int  (*c2_cm_iset_cursor_cmp)    (const struct c2_cm_iset_cursor *c0,
				          const struct c2_cm_iset_cursor *c1);

	int  (*c2_cm_iset_server_next)   (struct c2_cm_iset_cursor *cur);
	int  (*c2_cm_iset_device_next)   (struct c2_cm_iset_cursor *cur);
	int  (*c2_cm_iset_container_next)(struct c2_cm_iset_cursor *cur);
	int  (*c2_cm_iset_layout_next)   (struct c2_cm_iset_cursor *cur);
	int  (*c2_cm_iset_extent_next)   (struct c2_cm_iset_cursor *cur);
};

struct c2_cm_callbacks {
	void (*cb_group)     (struct c2_cm *mach, struct c2_dtx *tx /*,XXX*/);
	void (*cb_layout)    (struct c2_cm *mach, struct c2_dtx *tx,
			      struct c2_layout *layout, c2_bindex_t upto);
	void (*cb_container) (struct c2_cm *mach, struct c2_dtx *tx,
			      struct c2_container *container, c2_bindex_t upto);
	void (*cb_device)    (struct c2_cm *mach, struct c2_dtx *tx,
			      struct c2_device *device, c2_bindex_t upto);
	void (*cb_server)    (struct c2_cm *mach, struct c2_dtx *tx,
			      struct c2_poolserver *server);
	void (*cb_everything)(struct c2_cm *mach, struct c2_dtx *tx, bool done);
};

/**
   Aggregation group
*/
struct c2_cm_aggrg_group {
	/**
	   aggrg group base buffer

	   xform method need base buffer to hold the temporary result.
	   The first copy packet for this group will be used as the base buffer.
	   When this group is done, this buffer will be released.
	*/
	struct c2_cm_copy_packet *cag_buffer;

	/**
	  XXX TODO How to represent all the containers/devices in this
	  aggregation group? Some are on remote nodes, and some are local.
	*/
	struct c2_device        **cag_devices;
};

struct c2_cm_agent;

/**
   Copy machine aggregation method
*/
struct c2_cm_aggrg {
	/** finds an extent at cursor that maps to a single aggregation
	    group. */
	int (*cag_group_get)(const struct c2_cm_agent *agent,
			     const struct c2_cm_iset_cursor *cur,
			     struct c2_ext *ext,
			     struct c2_cm_aggrg_group *group);
	/** check if group has buffer already */
	int (*cag_has_buffer)(struct c2_cm_aggrg_group *group);
	/** check if the group has received all copy packets */
	int (*cag_is_done)(struct c2_cm_aggrg_group *group);
	/** use this copy packet as the group's buffer */
	int (*cag_use_this_packet_as_buffer)(struct c2_cm_aggrg_group *group,
					     struct c2_cm_copy_packet *cp);

	/** check if this group has containers on this server */
	int (*cag_group_on_the_server)(struct c2_cm_aggrg_group *group,
				       struct c2_poolserver *server);

};

/**
   Copy machine transformation method
*/
struct c2_cm_xform {
	/** perform sns transform method to this copy packet */
	int (*cx_sns)(struct c2_cm_aggrg_group *group,
		      struct c2_cm_copy_packet *cp);
};

/**
   copy packet

   Copy packet is the data structure used to describe the packet flowing between
   various copy machine agents. E.g., it is allocated by storage-in agent,
   queued by copy machine, sent to network-in agent, or storage-out agent, or
   collecting agent.
   Copy packet is linked to the copy machine global list via a list head.
*/
struct c2_cm_copy_packet {
	uint32_t	   cp_type;    /**< type of this copy packet */
	uint32_t	   cp_magic;   /**< magic number */
	uint64_t	   cp_seqno;   /**< sequence number */
	/* struct c2_checksum cp_checksum; */ /**< checksum of the data */
	struct c2_list_link   cp_linkage; /**< linkage to the global list */
	struct c2_ref	   cp_ref;     /**< reference count */
	int (*cp_complete)(struct c2_cm_copy_packet *cp); /**< completion cb*/

	void 		  *cp_data;    /**< pointer to data */
	uint32_t	   cp_len;     /**< data length */
};

/** copy machine */
struct c2_cm {
	/* struct c2_persistent_sm cm_mach; */          /**< persistent state machine */
	struct c2_cm_stats	cm_stats;         /**< stats */
	struct c2_rlimit  	cm_rlimit;        /**< resource limitation */
	struct c2_cm_iset	cm_iset;          /**< input set description */
	struct c2_cm_oset	cm_oset;          /**< output set description */
	struct c2_cm_operations cm_operations;    /**< operations of this cm */
	struct c2_cm_callbacks  cm_callbacks;     /**< callbacks of this cm */
};

struct c2_cm_agent;
struct c2_cm_agent_config { /* TODO */ };

/**
   copy machine agent operations

   A copy machine has a handler which handles FOP requests. A copy machine is
   responsible to create corresponding agents to do the actual work.
*/
struct c2_cm_agent_operations {
	int (*agops_init)  (struct c2_cm_agent *self, struct c2_cm *parent);
	int (*agops_stop)  (struct c2_cm_agent *self, int force);
	/** config this agent with specified parameters */
	int (*agops_config)(struct c2_cm_agent *self,
			    struct c2_cm_agent_config *config);
	/** main loop of this agent. To quit, call its agops_stop() method */
	int (*agops_run)   (struct c2_cm_agent *self);
};

enum c2_cm_agent_type {
	C2_CM_STORAGE_IN_AGENT,
	C2_CM_STORAGE_OUT_AGENT,
	C2_CM_NETWORK_IN_AGENT,
	C2_CM_NETWORK_OUT_AGENT,
	C2_CM_COLLECTING_AGENT
};

/**
   copy machine agent

   Copy machine agent is the base class for all agents: storage-in, storage-out,
   network-in, network-out, collecting, ...
   Copy machine agent has the basic properties and functions shared by all
   agents. Some information of the agent will be logged onto persistent storage,
   and they will survive node failures with distributed replications. E.g. the
   progress of the operation, refected by sequence number of the copy packet
   should go onto persistent storage.
*/
struct c2_cm_agent {
	/* struct c2_persistent_sm       ag_mach; */
	struct c2_cm		     *ag_parent; /**< pointer to parent cm */
	enum c2_cm_agent_type	      ag_type;   /**< agent type */

	struct c2_cm_aggrg	      ag_aggrg;  /**< agent aggregation method*/
	struct c2_cm_xform	      ag_xform;  /**< agent transform method  */
	struct c2_cm_agent_operations ag_operations; /**< agent operations    */

	/** copy packet in flight of this agent */
	struct c2_list	      	      ag_cp_in_flight; /**< list of all cp */

	bool			      ag_quit:1; /** flag to quit */
};

/** storage-in agent */
struct c2_cm_storage_in_agent {
	struct c2_cm_agent  ci_agent;
	struct c2_device   *ci_device; /**< the device the agent attched on */
};

/** storage-out agent */
struct c2_cm_storage_out_agent {
	struct c2_cm_agent  co_agent;
	struct c2_device   *ci_device; /**< the device the agent attched on */
};

/** network-in agent */
struct c2_cm_network_in_agent {
	struct c2_cm_agent   ni_agent;
	struct c2_transport *ni_transport; /**< something like "export" */
};

/** network-out agent */
struct c2_cm_network_out_agent {
	struct c2_cm_agent   no_agent;
	struct c2_transport *no_transport; /**< something like "import" */
};

/** collecting agent */
struct c2_cm_collecting_agent {
	struct c2_cm_agent c_agent;
};


/** allocate a new storage-in agent, and return pointer to its base class */
struct c2_cm_agent *alloc_storage_in_agent();
struct c2_cm_agent *alloc_storage_out_agent();
struct c2_cm_agent *alloc_network_in_agent();
struct c2_cm_agent *alloc_network_out_agent();
struct c2_cm_agent *alloc_collecting_agent();

/**
   alloc a copy packet

   allocate a new copy packet for future use.
   @retval NULL failed to allocate a new copy packet.
   @retval non-NULL pointer to the newly allocated copy packet.
   @post refcount == 1
   @post linkage is empty
   @post cp_data == NULL
   @post cp_len == 0
*/
struct c2_cm_copy_packet *c2_cm_cp_alloc();

/**
   alloc data area for a copy packet

   allocate data area with specified size for a copy packet.
   @retval 0 success.
   @retval != 0 failure.
   @pre (cp_data == NULL)
   @post (retval != 0 || cp_data != NULL)
*/
int c2_cm_cp_alloc_data(struct c2_cm_copy_packet *cp, int len);

/** add reference to a copy packet */
int c2_cm_cp_refadd(struct c2_cm_copy_packet *cp);
/** release reference from a copy packet. When refcount drops to 0, free it */
int c2_cm_cp_refdel(struct c2_cm_copy_packet *cp);



/** @} end of copymachine group */

/* __COLIBRI_SNS_REPAIR_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
