/* -*- C -*- */

#ifndef __COLIBRI_SNS_REPAIR_H__
#define __COLIBRI_SNS_REPAIR_H__

#include <sm/sm.h>
#include <lib/refs.h>

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

enum c2_poolnode_state {
	PNS_ONLINE,
	PNS_FAILED,
	PNS_OFFLINE,
	PNS_RECOVERING
};

enum c2_pooldev_state {
	PDS_ONLINE,
	PDS_FAILED,
	PDS_OFFLINE,
	PDS_RECOVERING
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
	enum c2_poolnode_state pn_state;
	struct c2_node_id      pn_id;
};

/**
   pool device

   Data structure representing a storage device in a pool.
*/
struct c2_pooldev {
	enum c2_pooldev_state pd_state;
	struct c2_dev_id      pd_id;
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
	struct c2_poolnode *pst_node;
	struct c2_pooldev  *pst_dev;
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
	struct c2_persistent_sm  pm_mach;
	struct c2_poolmach_state pm_state;
	struct c2_rwlock        pm_lock;
};


int  c2_poolmach_init(struct c2_poolmach *pm);
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
	struct c2_persistent_sm ps_mach;
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
*/
struct c2_cm_operations {
	int (*cmops_init)   (struct c2_cm *self);
	int (*cmops_stop)   (struct c2_cm *self, int force);
	int (*cmops_config) (struct c2_cm *self, struct c2_cm_iset *iset,
		             struct c2_cm_oset *oset, struct c2_rlimit *rlimit);
	int (*cmops_handler)(struct c2_cm *self, struct c2_fop *req);
	int (*cmops_queue)  (struct c2_cm_copy_packet *cp);
};

/** get stats from copy machine */
int c2_cm_stats(const struct c2_cm *cm,
                struct c2_cm_stats *stats /**< [out] output stats */
               );

/** adjust resource limitation parameters dynamically. */
int c2_cm_adjust_rlimit(struct c2_cm *cm, struct c2_rlimit *new_rl);

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
			      struct c2_layout *layout, c2_foff_t upto);
	void (*cb_container) (struct c2_cm *mach, struct c2_dtx *tx,
			      struct c2_container *container, c2_coff_t upto);
	void (*cb_device)    (struct c2_cm *mach, struct c2_dtx *tx,
			      struct c2_device *device, c2_doff_t upto);
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
	int (*cag_is_first_packet)(struct c2_cm_aggrg_group *group);
	int (*cag_is_done)(struct c2_cm_aggrg_group *group);
	int (*cag_use_this_packet_as_buffer)(struct c2_cm_aggrg_group *group,
					     struct c2_cm_copy_packet *cp);

	int (*cag_group_on_the_server)(struct c2_cm_aggrg_group *group,
				       struct c2_poolserver *server);

};

/**
   Copy machine transformation method
*/
struct c2_cm_xform {
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
	struct c2_checksum cp_checksum;/**< checksum of the data */
	struct c2_list_link   cp_linkage; /**< linkage to the global list */
	struct c2_ref	   cp_ref;     /**< reference count */
	int (*cp_complete)(struct c2_cm_copy_packet *cp); /**< completion cb*/

	void 		  *cp_data;    /**< pointer to data */
	uint32_t	   cp_len;     /**< data length */
};

/** copy machine */
struct c2_cm {
	struct c2_persistent_sm cm_mach;          /**< persistant state machine */
	struct c2_cm_stats	cm_stats;         /**< stats */
	struct c2_rlimit  	cm_rlimit;        /**< resource limitation */
	struct c2_cm_iset	cm_iset;          /**< input set description */
	struct c2_cm_oset	cm_oset;          /**< output set description */
	struct c2_cm_operations cm_operations;    /**< operations of this cm */
	struct c2_cm_callbacks  cm_callbacks;     /**< callbacks of this cm */
	struct c2_list_link	cm_copy_packets;  /**< link all copy packets */
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
	int (*agops_config)(struct c2_cm_agent *self,
			    struct c2_cm_agent_config *config);
	int (*agops_run)   (struct c2_cm_agent *self);
};

/**
   copy machine agent

   Copy machine agent is the base class for all agents: storage-in, storage-out,
   network-in, network-out, collecting, ...
   Copy machine agent has the basic properties and functions shared by all
   agents.
*/
struct c2_cm_agent {
	struct c2_persistent_sm       ag_mach;
	struct c2_cm		     *ag_parent; /**< pointer to parent cm */

	struct c2_cm_aggrg	      ag_aggrg;
	struct c2_cm_xform	      ag_xform;
	struct c2_cm_agent_operations ag_operations;
	
	int			      ag_quit:1;
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
