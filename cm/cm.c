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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
		    Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 30/11/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <lib/arith.h>  /* c2_u128() */
#include <lib/trace.h>  /* C2_LOG() */
#include <lib/bob.h>    /* C2_BOB_DEFINE */
#include <lib/misc.h>   /* C2_SET0() */
#include <lib/assert.h> /* C2_PRE(), C2_POST() */
#include <cm/cm.h>
#include <cm/cm_internal.h>
#include <reqh/reqh.h>

/**
   @page DLD-cm DLD of Copy Machine

   - @ref DLD-cm-ovw
   - @ref DLD-cm-def
   - @ref DLD-cm-req
   - @ref DLD-cm-highlights
   - @subpage DLD-cm-fspec
   - @ref DLD-cm-lspec
      - @ref DLD-cm-lspec-state
      - @ref DLD-cm-lspec-thread
   - @ref DLD-cm-conformance
   - @ref DLD-cm-ut
   - @ref DLD-O
   - @ref DLD-cm-ref

   <hr>
   @section DLD-cm-ovw Overview
   This document explains the detailed level design for generic part of the
   copy machine module.

   <hr>
   @section DLD-cm-def Definitions
    Please refer to "Definitions" section in "HLD of copy machine and agents"
    in @ref DLD-cm-ref

   <hr>
   @section DLD-cm-req Requirements
   The requirements below are grouped by various milestones.

  @subsection cm-setup-req CM-SETUP Requirements
   - @b r.cm.generic.invoke All the copy machine generic routines should be
        invoked from copy machine service specific code.
   - @b r.cm.generic.specific Copy machine generic routines should accordingly
        invoke copy machine specific operations.
   - @b r.cm.state.transition Copy machine state transition  should be performed
        by cm generic routines only.
   - @b r.cm.synchronise Copy machine generic should provide synchronised access
        to members of cm.
   - @b r.cm.start Generic api's should be available to initialise and start
        any type of copy machine.
   - @b r.cm.failure Copy machine should handle various types of failures.

   <hr>
   @section DLD-cm-highlights Design Highlights
   - Copy machine is implemented as colibri state machine.
   - All the registered types of copy machines can be initialised
     using various interfaces and also from colibri setup.
   - Once started, each copy machine type is registered with the
     request handler as a service.
   - Copy machine agents are implemented as FOMs. Thus the non blocking
     architechture of colibri is exploited.
   - A copy machine service can be started using "colibri setup" utility or
     separately.

   <hr>
   @section DLD-cm-lspec Logical Specification
    Please refer to "Logical Specification" section in "HLD of copy machine and
    agents" in @ref DLD-cm-ref

   @subsection DLD-cm-lspec-state State diagram
   @dot
   digraph copy_machine_states {
       size = "8,12"
       node [shape=record, fontsize=12]
       INIT [label="INTIALISING"]
       IDLE [label="IDLE"]
       READY [label="READY"]
       ACTIVE [label="ACTIVE"]
       FAIL [label="FAIL"]
       DONE [label="DONE"]
       STOP [label="STOP"]
       INIT -> IDLE [label="Configuration received from confc"]
       IDLE -> READY [label="Initialisation complete.Broadcast READY fop"]
       IDLE -> FAIL [label="Timed out or self destruct"]
       READY -> ACTIVE [label="All READY fops received"]
       READY -> FAIL [label="Timed out waiting for READY fops"]
       ACTIVE -> FAIL [label="Agent failure"]
       ACTIVE -> DONE [label="Broadcast DONE fop"]
       ACTIVE -> STOP [label="Received ABORT fop"]
       FAIL -> IDLE [label="All replies received"]
       ACTIVE -> FAIL [label="Agent failure"]
       DONE -> IDLE [label="All DONE fops received"]
       DONE -> FAIL [label="Timed out waiting for DONE fops"]
       STOP -> IDLE [label="All STOP fops received"]
       STOP -> FAIL [label="Timed out waiting for STOP fops"]
   }
   @enddot

   @subsection DLD-aggr_group-lspec-state State diagram
   @dot
   digraph aggregation_group_states {
       size = "8,12"
       node [shape=record, fontsize=12]
       INITIALISED [label="INTIALISED"]
       INPROCESS [label="IN-PROCESS"]
       FINALISED [label="FINALISED"]
       INITIALISED -> INPROCESS [label="All cps added to aggr grp list"]
       INPROCESS -> FINALISED [label="All cps in aggr grp have been processed"]
   }
   @enddot

   @subsection DLD-cm-lspec-agents Copy machine interaction with agents
   - Copy machine agents are implemented as foms.
   - Copy machine creates its agents during copy machine service startup.
   - All the started copy machine agents remain in request handler wait
     queue in idle state.
   - On receiving an operational fop, copy machine builds an input set
     and configures appropriate agents.

   @subsection DLD-cm-lspec-thread Threading and Concurrency Model
   - Copy machine and its agents are implemented as state machines, and thus do
     not have their own threads. They run in the context of reqh threads.
   - Copy machine starts as a service and is registered with the request
     handler.
   - Copy machine agents are started as foms and they register corresponding
     c2_chan with the request handler (c2_fom_block_at()). This puts the agent
     fom onto the reqh wait queue. The reqh thread is free to run the other reqh
     requests. Once an operational fop is received, the c2_chan is signalled and
     agent fom is removed from the wait queue and added to reqh run queue.
   - Copy machine agents are started as foms and wait on their c2_chan for
     further event, until then the agents as foms, wait in the request handler's
     wait queue.
   - The cmtype_mutex is used to serialise the operation on cmtypes_list.

   <hr>
   @section DLD-cm-conformance Conformance
   This section briefly describes interfaces and structures conforming to above
   mentioned copy machine requirements.
   - @b I.cm.type.register Different types of copy machines are registered as
      colibri service types.
   - @b I.cm.start Generic api's are available to initialise and start
        any type of copy machine.
   - @b I.cm.agent.create. Once a copy machine instance is intialised it
        creates and starts copy machine type specific agents.
   - @b I.cm.agent.fom  Copy machine agents are implemented as foms, and are
        started by copy machine using request handler.
   - @b I.cm.agent.async Every read-write (receive-send) by any agent follows
        follows non-blocking processing model of Colibri design.
   - @b I.cm.failure Copy machine handles various types of failures.

   @section DLD-Agents-addb ADDB events
   - <b>cm_init_fail</b> Copy machine failed to initialise.
   - <b>cm_start_fail</b> Copy machine failed to start.

   <hr>
   @section DLD-cm-ut Unit Tests
   - Start copy machine and SNS cm service. Check all the states of fom and copy
     machine such that they align to the state diagram.
   - Check if multiple agents get started. Check their configuration parameters.
   - Stop copy machine and check cleanup.

   <hr>
    @section DLD-cm-st System Tests
    NA

   <hr>
   @section DLD-O Analysis
   NA

   <hr>
   @section DLD-cm-ref References
   Following are the references to the documents from which the design is
   derived,

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1ZlkjayQoXVm-prMx
   Tkzxb1XncB6HU19I19kwrV-8eQc/edit?hl=en_US">HLD of copy machine and agents.</a
   >
   - <a href="https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkN
   Xg4cXpfMTc5ZjYybjg4Y3Q&hl=en_US">HLD of SNS Repair.</a>
 */

/**
   @addtogroup cm
   @{
*/

enum {
	/** Hex value of "ag_link". */
	AGGR_GROUP_LINK_MAGIC = 0x61675f6c696e6b,
	/** Hex value of "ag_head". */
	AGGR_GROUP_LINK_HEAD = 0x61675f68656164,
	/** Hex value of "CMT_HEAD" */
	CM_TYPE_HEAD_MAGIX = 0x434D545F48454144,
	/** Hex value of "CMT_LINK" */
	CM_TYPE_LINK_MAGIX = 0x434D545F4C494E4B
};

/** List containing the copy machines registered on a colibri server. */
static struct c2_tl	cmtypes;

/** Protects access to the list c2_cmtypes. */
static struct c2_mutex	cmtypes_mutex;

C2_TL_DESCR_DEFINE(cmtypes, "copy machine types", ,
                   struct c2_cm_type, ct_linkage, ct_magix,
                   CM_TYPE_LINK_MAGIX, CM_TYPE_HEAD_MAGIX);

C2_TL_DEFINE(cmtypes, , struct c2_cm_type);

static struct c2_bob_type cmtypes_bob;
C2_BOB_DEFINE( , &cmtypes_bob, c2_cm_type);

C2_TL_DESCR_DEFINE(aggr_grps, "aggr_grp_list_descr", ,
		  struct c2_cm_aggr_group, cag_sw_linkage, cag_magic,
		  AGGR_GROUP_LINK_MAGIC, AGGR_GROUP_LINK_HEAD);

C2_TL_DEFINE(aggr_grps, extern, struct c2_cm_aggr_group);

C2_ADDB_EV_DEFINE(cm_init_fail, "cm_init_fail",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

C2_ADDB_EV_DEFINE(cm_start_fail, "cm_start_fail",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

const struct c2_addb_loc c2_cm_addb_loc = {
	.al_name = "copy machine"
};

const struct c2_addb_ctx_type c2_cm_addb_ctx = {
	.act_name = "copy machine"
};

static void failure_exit(struct c2_sm *mach);

const struct c2_sm_state_descr c2_cm_state_descr[C2_CMS_NR] = {
	[C2_CMS_INIT] = {
		.sd_flags	= C2_SDF_INITIAL,
		.sd_name	= "cm_init",
		.sd_in		= NULL,
		.sd_ex		= NULL,
		.sd_invariant	= NULL,
		.sd_allowed	= (1 << C2_CMS_IDLE)|(1 << C2_CMS_FAIL)|
				  (1 << C2_CMS_FINI)
	},
	[C2_CMS_IDLE] = {
		.sd_flags	= 0,
		.sd_name	= "cm_idle",
		.sd_in		= NULL,
		.sd_ex		= NULL,
		.sd_invariant	= NULL,
		.sd_allowed	= (1 << C2_CMS_FAIL)|(1 << C2_CMS_READY)|
				  (1 << C2_CMS_STOP)
	},
	[C2_CMS_READY] = {
		.sd_flags	= 0,
		.sd_name	= "cm_ready",
		.sd_in		= NULL,
		.sd_ex		= NULL,
		.sd_invariant	= NULL,
		.sd_allowed	= (1 << C2_CMS_ACTIVE)|(1 << C2_CMS_FAIL)
	},
	[C2_CMS_ACTIVE] = {
		.sd_flags	= 0,
		.sd_name	= "cm_active",
		.sd_in		= NULL,
		.sd_ex		= NULL,
		.sd_invariant	= NULL,
		.sd_allowed	= (1 << C2_CMS_DONE)|(1 << C2_CMS_STOP)|
				  (1 << C2_CMS_FAIL)
	},
	[C2_CMS_FAIL] = {
		.sd_flags	= C2_SDF_FAILURE,
		.sd_name	= "cm_fail",
		.sd_in		= NULL,
		.sd_ex		= failure_exit,
		.sd_invariant	= NULL,
		.sd_allowed	= (1 << C2_CMS_IDLE)|(1 << C2_CMS_FINI)
	},
	[C2_CMS_DONE] = {
		.sd_flags	= 0,
		.sd_name	= "cm_done",
		.sd_in		= NULL,
		.sd_ex		= NULL,
		.sd_invariant	= NULL,
		.sd_allowed	= (1 << C2_CMS_FAIL)|(1 << C2_CMS_IDLE)
	},
	[C2_CMS_STOP] = {
		.sd_flags	= 0,
		.sd_name	= "cm_stop",
		.sd_in		= NULL,
		.sd_ex		= NULL,
		.sd_invariant	= NULL,
		.sd_allowed	= (1 << C2_CMS_FAIL)|(1 << C2_CMS_IDLE)|
				  (1 << C2_CMS_FINI)
	},
	[C2_CMS_FINI] = {
		.sd_flags	= C2_SDF_TERMINAL,
		.sd_name	= "cm_fini",
		.sd_in		= NULL,
		.sd_ex		= NULL,
		.sd_invariant	= NULL,
		.sd_allowed	= 0
	},
};

const struct c2_sm_conf c2_cm_sm_conf = {
	.scf_name	= "sm:cm conf",
	.scf_nr_states  = C2_CMS_NR,
	.scf_state	= c2_cm_state_descr
};

void c2_cm_group_lock(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);

	c2_sm_group_lock(&cm->cm_sm_group);
	C2_LEAVE();
}

void c2_cm_group_unlock(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);

	c2_sm_group_unlock(&cm->cm_sm_group);
	C2_LEAVE();
}

bool c2_cm_group_is_locked(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);

	C2_LEAVE();
	return c2_mutex_is_locked(&cm->cm_sm_group.s_lock);
}

int c2_cm_state_get(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);
	C2_PRE(c2_cm_group_is_locked(cm));

	C2_LEAVE();
	return cm->cm_mach.sm_state;
}

void c2_cm_state_set(struct c2_cm *cm, int state)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);
	C2_PRE(c2_cm_group_is_locked(cm));

	c2_sm_state_set(&cm->cm_mach, state);
	C2_LEAVE();
}

bool c2_cm_invariant(struct c2_cm *cm)
{
	int state = cm->cm_mach.sm_state;

	return
		/* NULL checks */
		cm != NULL && cm->cm_ops != NULL && cm->cm_cb != NULL &&
		cm->cm_type != NULL &&
		/* Copy machine error code checks */
		C2_IN(cm->cm_mach.sm_rc, (C2_CM_SUCCESS, C2_CM_ERR_START,
		C2_CM_ERR_CONF, C2_CM_ERR_OP, C2_CM_ERR_AGENT)) &&
		/* Agent list invariant checks */
		ergo(C2_IN(state, (C2_CMS_IDLE, C2_CMS_READY, C2_CMS_ACTIVE,
		     C2_CMS_DONE, C2_CMS_STOP)) || (state == C2_CMS_FAIL &&
		     C2_IN(cm->cm_mach.sm_rc, (C2_CM_ERR_CONF, C2_CM_ERR_OP))),
		     c2_reqh_service_invariant(&cm->cm_service));
}

int c2_cm_start(struct c2_cm *cm)
{
	int	rc;

	C2_ENTRY();
	C2_PRE(cm != NULL);
	C2_PRE(cm->cm_type != NULL);

	c2_cm_group_lock(cm);
	C2_PRE(c2_cm_state_get(cm) == C2_CMS_INIT);
	C2_PRE(c2_cm_invariant(cm));

	rc = cm->cm_ops->cmo_start(cm);
	if (rc == 0) {
		c2_cm_state_set(cm, C2_CMS_IDLE);
		cm->cm_mach.sm_rc = C2_CM_SUCCESS;
		C2_LOG("CM:%s copy machine:ID: %lu: STATE: %i",
		       (char *)cm->cm_type->ct_stype.rst_name, cm->cm_id,
		        c2_cm_state_get(cm));
	} else {
		c2_sm_fail(&cm->cm_mach, C2_CMS_FAIL, C2_CM_ERR_START);
		C2_ADDB_ADD(&cm->cm_addb, &c2_cm_addb_loc,cm_start_fail,
			    "c2_cm_start", cm->cm_mach.sm_rc);
	}
	C2_POST(c2_cm_invariant(cm));
	c2_cm_group_unlock(cm);
	C2_LEAVE();
	return rc;
}

void c2_cm_stop(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);

	c2_cm_group_lock(cm);
	C2_PRE(C2_IN(c2_cm_state_get(cm), (C2_CMS_ACTIVE, C2_CMS_IDLE)));
	C2_PRE(c2_cm_invariant(cm));

	cm->cm_ops->cmo_stop(cm);
	c2_cm_state_set(cm, C2_CMS_STOP);
	C2_LOG("CM:%s copy machine:ID: %lu: STATE: %i",
	      (char *)cm->cm_type->ct_stype.rst_name, cm->cm_id,
	      c2_cm_state_get(cm));
	C2_POST(c2_cm_invariant(cm));
	c2_cm_group_unlock(cm);
	C2_LEAVE();
}

int c2_cm_configure(struct c2_cm *cm, struct c2_fop *fop)
{
	return 0;
}

int c2_cm_operation_abort(struct c2_cm *cm)
{
	return 0;
}

int c2_cms_init(void)
{
	C2_ENTRY();
	cmtypes_tlist_init(&cmtypes);
	c2_bob_type_tlist_init(&cmtypes_bob, &cmtypes_tl);
	c2_mutex_init(&cmtypes_mutex);
	C2_LEAVE();
        return 0;
}

void c2_cms_fini(void)
{
	C2_ENTRY();
	cmtypes_tlist_fini(&cmtypes);
	c2_mutex_fini(&cmtypes_mutex);
	C2_LEAVE();
}

/**
 * Temporary implementation to generate unique cm ids.
 * @todo Rewrite this when mechanism to generate unique ids is in place.
 */
static uint64_t cm_id_generate(const char* s)
{
	uint64_t seed = (uint64_t)s + s[0];
	return c2_rnd(~0ULL >> 16, &seed);
}

static void cm_sm_init(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);

	cm->cm_mach.sm_grp = &cm->cm_sm_group;
	c2_sm_init(&cm->cm_mach, &c2_cm_sm_conf, C2_CMS_INIT,
		   cm->cm_mach.sm_grp, &cm->cm_addb);
	C2_ASSERT(cm->cm_mach.sm_state == C2_CMS_INIT);
	C2_LEAVE();
}

static void cm_sm_fini(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);

	cm->cm_mach.sm_state = C2_CMS_FINI;
	c2_sm_fini(&cm->cm_mach);
	C2_LEAVE();
}

int c2_cm_init(struct c2_cm *cm, struct c2_cm_type *cm_type,
	       const struct c2_cm_ops *cm_ops, const struct c2_cm_cb *cm_cb,
	       const struct c2_cm_sw_ops *sw_ops)
{
	int	 rc;

	C2_ENTRY();
	C2_PRE(cm != NULL && cm_type != NULL && cm_ops != NULL &&
	       cm_cb != NULL && sw_ops!=NULL);

	cm->cm_type = cm_type;
	cm->cm_ops = cm_ops;
	cm->cm_cb = cm_cb;
	C2_ASSERT(cm->cm_type->ct_stype.rst_name != NULL);

	c2_sm_group_init(&cm->cm_sm_group);
	c2_addb_ctx_init(&cm->cm_addb, &c2_cm_addb_ctx, &c2_addb_global_ctx);
	rc = c2_cm_sw_init(&cm->cm_sw, sw_ops);
	if (rc == 0) {
		cm->cm_mach.sm_rc = C2_CM_SUCCESS;
		C2_LOG("CM:%s copy machine:ID: %lu: STATE: %i",
		       (char *)cm_type->ct_stype.rst_name, cm->cm_id,
		        cm->cm_mach.sm_state);
	} else {
		C2_ADDB_ADD(&cm->cm_addb, &c2_cm_addb_loc, cm_init_fail,
			    "c2_cm_init", cm->cm_mach.sm_rc);
		c2_addb_ctx_fini(&cm->cm_addb);
		goto out;
	}

	cm_sm_init(cm);
	cm->cm_id = cm_id_generate(cm->cm_type->ct_stype.rst_name);
	C2_POST(c2_cm_invariant(cm));
out:
	C2_LEAVE();
	return rc;
}

void c2_cm_fini(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);
	C2_PRE(C2_IN(cm->cm_mach.sm_state, (C2_CMS_STOP, C2_CMS_INIT,
					   C2_CMS_FAIL)));

	cm_sm_fini(cm);
	C2_ASSERT(c2_cm_invariant(cm));
	cm->cm_ops->cmo_fini(cm);
	c2_cm_sw_fini(&cm->cm_sw);
	c2_addb_ctx_fini(&cm->cm_addb);
	C2_LOG("CM:Copy Machine: %s:ID: %lu: STATE: %i",
	       (char *)cm->cm_type->ct_stype.rst_name, cm->cm_id,
	        cm->cm_mach.sm_state);

	c2_sm_group_fini(&cm->cm_sm_group);
	C2_LEAVE();
}

int c2_cm_type_register(struct c2_cm_type *cmtype)
{
	int	rc;

	C2_PRE(cmtype != NULL);
	C2_ENTRY();

	rc = c2_reqh_service_type_register(&cmtype->ct_stype);
	if (rc == 0) {
		cmtypes_tlink_init(cmtype);
		c2_cm_type_bob_init(cmtype);
		c2_mutex_lock(&cmtypes_mutex);
		cmtypes_tlist_add_tail(&cmtypes, cmtype);
		c2_mutex_unlock(&cmtypes_mutex);
	}
	C2_LEAVE();
	return rc;
}

void c2_cm_type_deregister(struct c2_cm_type *cmtype)
{
	C2_PRE(cmtype != NULL && c2_cm_type_bob_check(cmtype));
	C2_ENTRY();

	cmtypes_tlink_del_fini(cmtype);
	c2_cm_type_bob_fini(cmtype);
	c2_reqh_service_type_unregister(&cmtype->ct_stype);
	C2_LEAVE();
}

/*
 * Currently only resets c2_sm::sm_rc, as failure is already handled and the
 * state machine can now transition to next state.
 * This can be further enhanced to do more better things if required.
 * This is called when the c2_cm::cm_mach leaves C2_CMS_FAIL state.
 */
static void failure_exit(struct c2_sm *sm)
{
	C2_PRE(sm->sm_rc != 0);

	sm->sm_rc = 0;
}

struct c2_chan *c2_cm_signal(struct c2_cm *cm)
{
        return &cm->cm_mach.sm_grp->s_chan;
}

int c2_cm_failure_handle(struct c2_cm *cm)
{
	return 0;
}

int c2_cm_done(struct c2_cm *cm)
{
	return 0;
}

/** @} endgroup cm */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
