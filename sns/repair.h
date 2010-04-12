/* -*- C -*- */

#ifndef __COLIBRI_SNS_REPAIR_H__
#define __COLIBRI_SNS_REPAIR_H__

#include <sm/sm.h>

/**
   @page snsrepair SNS repair detailed level design specification.

   @section Overview

   @section Definitions

   @section repairfuncspec Functional specification

   @section repairlogspec Logical specification

       @ref poolmachine
       @ref copymachine
 */

/**
   @defgroup poolmachine Pool machine
   @{
*/

/**
   pool machine

   Data structure representing replicated pool state machine.
*/
struct c2_poolmachine {
	struct c2_persistent_sm pm_mach;
};

/**
   pool node

   Data structure representing a node in a pool.
   Pool node and pool server are two different views of the same physical
   entity. A pool node is how a server looks "externally" to other nodes.
   "struct poolnode" represents a particular server on other servers. E.g.,
   when a new server is added to the pool, "struct poolnode" is created on
   every server in the pool. "struct poolserver", on the other hand, represents
   a server state machine locally on the server where it runs.

   @see pool server
*/
struct c2_poolnode {
};

/**
   pool device

   Data structure representing a storage device in a pool.
*/
struct c2_pooldev {
};

int  c2_poolmachine_init(struct c2_poolmachine *pm);
void c2_poolmachine_fini(struct c2_poolmachine *pm);

int  c2_poolmachine_device_join (struct c2_poolmachine *pm, 
				 struct c2_pooldev *dev);
int  c2_poolmachine_device_leave(struct c2_poolmachine *pm, 
				 struct c2_pooldev *dev);

int  c2_poolmachine_node_join (struct c2_poolmachine *pm, 
			       struct c2_poolnode *node);
int  c2_poolmachine_node_leave(struct c2_poolmachine *pm, 
			       struct c2_poolnode *node);

/** @} end of poolmachine group */

/**
   @defgroup servermachine Server machine
   @{
*/

/** 
   pool server

   Pool server represents a pool node plus its state machines, lives locally on
   the server where it runs.

   @see pool node
*/
struct c2_poolserver {
	struct c2_poolnode      ps_node;
	struct c2_persistent_sm ps_mach;
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
	int (*init)   (struct c2_cm *this);
	int (*stop)   (struct c2_cm *this, int force);
	int (*config) (struct c2_cm *this, struct c2_cm_iset *iset,
		       struct c2_cm_oset *oset, struct c2_rlimit *rlimit);
	int (*handler)(struct c2_cm *this, struct c2_fop *req);
	int (*queue)  (struct c2_cm_copy_packet *cp);
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
};

/**
   Copy machine transformation method
*/
struct c2_cm_xform {
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
	struct c2_checksum cp_checksum;/**< checksum of the data */
	struct c2_list_link   cp_linkage; /**< linkage to the global list */
	struct c2_ref	   cp_ref;     /**< reference count */

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
	int (*agops_config)(struct c2_cm_agent *self, struct c2_cm_agent_config *config);
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

struct c2_cm_storage_in_agent {
	struct c2_cm_agent  ci_agent;
	struct c2_device   *ci_device; /**< the device the agent attched on */
};
struct c2_cm_storage_out_agent {
	struct c2_cm_agent  co_agent;
	struct c2_device   *ci_device; /**< the device the agent attched on */
};

struct c2_cm_network_in_agent {
	struct c2_cm_agent   ni_agent;
	struct c2_transport *ni_transport; /**< something like "export" */
};

struct c2_cm_network_out_agent {
	struct c2_cm_agent   no_agent;
	struct c2_transport *no_transport; /**< something like "import" */
};

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
   @postcondition refcount == 1
*/
struct c2_cm_copy_packet *c2_cm_cp_alloc();
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
