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

/** pool server */
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

/** copy machine */
struct c2_cm {
	struct c2_persistent_sm cm_mach;
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

/** input set stat */
struct c2_cm_iset_stat {
       int is_progess;
       int is_error;
};

/** input set description */
struct c2_cm_iset {
       struct c2_rlimit  ci_rlimit;  /**< resource limitation of this input. */
       struct c2_cm_iset_stat ci_stat;
       struct list_head  ci_linkage; /**< link this input onto global list.  */
};

int  c2_cm_iset_init(struct c2_cm_iset *iset);
void c2_cm_iset_fini(struct c2_cm_iset *iset);

/** adjust resource limitation parameters for this input set. */
int c2_cm_iset_adjust_rlimit(struct c2_cm_iset *iset, struct c2_rlimit *new_rl);
int c2_cm_iset_stat(const struct c2_cm_iset *iset,
                   struct c2_cm_iset_stat *stat /**< [out] output stat */
                  );

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

int  c2_cm_iset_cursor_init   (struct c2_cm_iset_cursor *cur);
void c2_cm_iset_cursor_fini   (struct c2_cm_iset_cursor *cur);
void c2_cm_iset_cursor_copy   (struct c2_cm_iset_cursor *dst,
			       const struct c2_cm_iset_cursor *src);
int  c2_cm_iset_cursor_cmp    (const struct c2_cm_iset_cursor *c0,
			       const struct c2_cm_iset_cursor *c1);

int  c2_cm_iset_server_next   (struct c2_cm_iset_cursor *cur);
int  c2_cm_iset_device_next   (struct c2_cm_iset_cursor *cur);
int  c2_cm_iset_container_next(struct c2_cm_iset_cursor *cur);
int  c2_cm_iset_layout_next   (struct c2_cm_iset_cursor *cur);
int  c2_cm_iset_extent_next   (struct c2_cm_iset_cursor *cur);

struct c2_dtx;
struct c2_cm_callbacks {
	void (*cb_group)     (struct c2_cm *mach, struct c2_dtx *tx, ???);
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

/**
   Copy machine aggregation method
*/
struct c2_cm_aggrg {
	/** finds at extent at cursor that maps to a single aggregation
	    group. */
	int (*cag_group_get)(const struct c2_cm_aggrg *agg, 
			     const struct c2_cm_iset_cursor *cur,
			     struct c2_ext *ext, 
			     struct c2_cm_aggrg_group *group);
};

/**
   Copy machine transformation method
*/
struct c2_cm_xform {
};

struct c2_cm_agent {
	struct c2_persistent_sm ag_mach;
};

struct c2_cm_storage_in_agent {
	struct c2_cm_agent ci_agent;
};

struct c2_cm_storage_out_agent {
	struct c2_cm_agent co_agent;
};

struct c2_cm_network_in_agent {
	struct c2_cm_agent ni_agent;
};

struct c2_cm_network_out_agent {
	struct c2_cm_agent no_agent;
};

struct c2_cm_collecting_agent {
	struct c2_cm_agent c_agent;
};


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
