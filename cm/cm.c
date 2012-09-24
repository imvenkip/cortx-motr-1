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

#include "lib/trace.h"  /* C2_LOG */
#include "lib/bob.h"    /* C2_BOB_DEFINE */
#include "lib/misc.h"   /* C2_SET0 */
#include "lib/assert.h" /* C2_PRE, C2_POST */
#include "colibri/magic.h"

#include "cm/cm.h"
#include "cm/ag.h"
#include "cm/cp.h"
#include "reqh/reqh.h"

/**
   @page CMDLD Copy Machine DLD

   - @ref CMDLD-ovw
   - @ref CMDLD-def
   - @ref CMDLD-req
   - @ref CMDLD-highlights
   - @subpage CMDLD-fspec
   - @ref CMDLD-lspec
      - @ref CMDLD-lspec-state
      - @ref CMDLD-lspec-cm-setup
      - @ref CMDLD-lspec-cm-start
         - @ref CMDLD-lspec-cm-cp-pump
         - @ref CMDLD-lspec-cm-active
      - @ref CMDLD-lspec-cm-stop
      - @ref CMDLD-lspec-thread
   - @ref CMDLD-conformance
   - @ref CMDLD-ut
   - @ref DLD-O
   - @ref CMDLD-ref

   <hr>
   @section CMDLD-ovw Overview
   This document explains the detailed level design for generic part of the
   copy machine module.

   <hr>
   @section CMDLD-def Definitions
    Please refer to "Definitions" section in "HLD of copy machine and agents"
    in @ref CMDLD-ref

   <hr>
   @section CMDLD-req Requirements
   The requirements below are grouped by various milestones.

  @subsection cm-setup-req Copy machine Requirements
   - @b r.cm.generic.invoke Copy machine generic routines should be invoked from
        copy machine service specific code.
   - @b r.cm.generic.specific Copy machine generic routines should accordingly
        invoke copy machine specific operations.
   - @b r.cm.synchronise Copy machine generic should provide synchronised access
        to members of cm.
   - @b r.cm.resource.manage Copy machine specific resources should be managed
        by copy machine specific implementation, e.g. buffer pool, etc.
   - @b r.cm.failure Copy machine should handle various types of failures.

   <hr>
   @section CMDLD-highlights Design Highlights
   - Copy machine is implemented as colibri state machine.
   - All the registered types of copy machines can be initialised using various
     interfaces and also from colibri setup.
   - Once started, each copy machine type is registered with the request handler
     as a service.
   - A copy machine service can be started using "colibri setup" utility or
     separately.
   - Once started copy machine remains idle until further event happens.
   - Copy machine copy packets are implemented as foms, and are started by copy
     machine using request handler.
   - Copy machine type specific event triggers copy machine operation, (e.g.
     TRIGGER FOP for SNS Repair). This allocates copy machine specific resources
     and creates copy packets.
  -  The complete data restructuring process of copy machine follows non-blocking
     processing model of Colibri design.
  -  Copy machine maintains the list of aggregation groups being processed and
     implements a sliding window over this list to keep track of restructuring
     process and manage resources efficiently.

   <hr>
   @section CMDLD-lspec Logical Specification
    Please refer to "Logical Specification" section in "HLD of copy machine and
    agents" in @ref CMDLD-ref
   - @ref CMDLD-lspec-state
   - @ref CMDLD-lspec-cm-setup
   - @ref CMDLD-lspec-cm-start
      - @ref CMDLD-lspec-cm-cp-pump
      - @ref CMDLD-lspec-cm-active
   - @ref CMDLD-lspec-cm-stop

   @subsection CMDLD-lspec-state Copy Machine State diagram
   @dot
   digraph copy_machine_states {
       size = "8,12"
       node [shape=record, fontsize=12]
       INIT [label="INTIALISING"]
       IDLE [label="IDLE"]
       READY [label="READY"]
       ACTIVE [label="ACTIVE"]
       FAIL [label="FAIL"]
       STOP [label="STOP"]
       INIT -> IDLE [label="Configuration received from confc"]
       IDLE -> READY [label="Initialisation complete.Broadcast READY fop"]
       IDLE -> FAIL [label="Timed out or self destruct"]
       READY -> ACTIVE [label="All READY fops received"]
       READY -> FAIL [label="Timed out waiting for READY fops"]
       ACTIVE -> FAIL [label="Operation failure"]
       ACTIVE -> STOP [label="Receive ABORT fop or completed restructuring / failure"]
       FAIL -> IDLE [label="All replies received"]
       STOP -> IDLE [label="All STOP fops received"]
       STOP -> FAIL [label="Timed out waiting for STOP fops"]
   }
   @enddot

   @subsection CMDLD-lspec-cm-setup Copy machine setup
   After copy machine is successfully initialised (c2_cm_init()), it is
   configured as part of the copy machine service startup by invoking
   c2_cm_setup(). This performs copy machine specific setup by invoking
   c2_cm_ops::cmo_setup(). Once successfully completed the copy machine
   is transitioned into C2_CMS_IDLE state. In case of setup failure, copy
   machine is transitioned to C2_CMS_FAIL state, this also fails copy machine
   service startup, and thus copy machine is finalised during copy machine
   service finalisation.

   @subsection CMDLD-lspec-cm-start Copy machine operation start
   After copy machine service is successfully started, it is ready to perform
   its respective tasks (e.g. SNS Repair). On receiving a trigger event (i.e
   failure in case of sns repair) copy machine transitions into C2_CMS_ACTIVE
   state once copy machine specific startup tasks are complete (c2_cm_start()).
   In case of copy machine startup failure, copy machine transitions into
   C2_CMS_FAIL state, once failure is handled, copy machine transitions back
   into C2_CMS_IDLE state and waits for further events.

   @subsubsection CMDLD-lspec-cm-cp-pump Copy packet pump
   Copy machine implements a special FOM type, viz. copy packet pump FOM to
   create copy packets (@see @ref CPDLD). Copy packet pump FOM creates copy
   packets until resources permit and goes to sleep if no more packets can be
   created. Using non-blocking FOM infrastructure to create copy packets enables
   copy machine to handle blocking operations performed while acquiring various
   resources efficiently. Copy packet pump FOM is created when the copy machine
   operation starts and is woken up (iff it was IDLE) as the required resources
   become available (e.g. when a copy packet is finalised and its corresponding
   buffer is released to copy machine specific buffer pool).
   @see struct c2_cm_cp_pump

   @subsubsection CMDLD-lspec-cm-active Copy machine activation
   After creating initial number of copy packets, copy machine broadcasts READY
   FOPs with its corresponding sliding window information to all its replicas
   in the pool. Every copy machine replica, after receiving READY FOPs from all
   its replicas in the pool, transitions into C2_CMS_ACTIVE state.

   @subsection CMDLD-lspec-cm-stop Copy machine stop
   Once operation completes successfully, copy machine performs required tasks,
   (e.g. updating layouts, etc.) by invoking c2_cm_stop(), this transitions copy
   machine back to C2_CMS_IDLE state. Copy machine invokes c2_cm_stop() also in
   case of operational failure to broadcast STOP FOPs to its other replicas in
   the pool, indicating failure. This is handled specific to the copy machine
   type.

   @subsection CMDLD-lspec-thread Threading and Concurrency Model
   - Copy machine is implemented as a state machine, and thus do
     not have its own thread. It runs in the context of reqh threads.
   - Copy machine starts as a service and is registered with the request
     handler.
   - The cmtype_mutex is used to serialise the operation on cmtypes_list.
   - Access to the members of struct c2_cm is serialised using the
     c2_cm::c2_sm_group::s_mutex.

   <hr>
   @section CMDLD-conformance Conformance
   This section briefly describes interfaces and structures conforming to above
   mentioned copy machine requirements.
   - @b i.cm.generic.invoke Copy machine generic routines are invoked from copy
        machine specific code.
   - @b i.cm.generic.specific Copy machine generic routines accordingly
        invoke copy machine specific operations.
   - @b i.cm.synchronise Copy machine provides synchronised access to its
        members using c2_cm::cm_sm_group::s_mutex.
   - @b i.cm.failure Copy machine handles various types of failures through
        c2_cm_fail() interface.
   - @b i.cm.resource.manage Copy machine specific resources are managed by copy
        machine specific implementation, e.g. buffer pool, etc.

   @section DLD-Agents-addb ADDB events
   - <b>cm_init_fail</b> Copy machine failed to initialise.
   - <b>cm_setup_fail</b> Copy machine setup failed.
   - <b>cm_start_fail</b> Copy machine failed to start operation.

   <hr>
   @section CMDLD-ut Unit Tests

   @subsection CMSETUP-ut CM SETUP Unit Tests
     - Start copy machine and SNS cm service. Check all the states of
	copy machine such that they align to the state diagram.
     - Stop copy machine and check cleanup.

   <hr>
    @section CMDLD-st System Tests
    NA

   <hr>
   @section DLD-O Analysis
   NA

   <hr>
   @section CMDLD-ref References
   Following are the references to the documents from which the design is
   derived,
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1FX-TTaM5VttwoG4w
   d0Q4-AbyVUi3XL_Oc6cnC4lxLB0/edit">Copy Machine redesign.</a>
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1ZlkjayQoXVm-prMx
   Tkzxb1XncB6HU19I19kwrV-8eQc/edit?hl=en_US">HLD of copy machine and agents.</a
   >
   - <a href="https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkN
   Xg4cXpfMTc5ZjYybjg4Y3Q&hl=en_US">HLD of SNS Repair.</a>
 */

/**
   @addtogroup CM
   @{
*/

/** List containing the copy machines registered on a colibri server. */
static struct c2_tl cmtypes;

/** Protects access to the list c2_cmtypes. */
static struct c2_mutex cmtypes_mutex;

C2_TL_DESCR_DEFINE(cmtypes, "copy machine types", static,
                   struct c2_cm_type, ct_linkage, ct_magix,
                   CM_TYPE_LINK_MAGIX, CM_TYPE_HEAD_MAGIX);

C2_TL_DEFINE(cmtypes, static, struct c2_cm_type);

static struct c2_bob_type cmtypes_bob;
C2_BOB_DEFINE(static, &cmtypes_bob, c2_cm_type);

C2_TL_DESCR_DEFINE(cm_ag, "aggregation groups", static, struct c2_cm_aggr_group,
		   cag_cm_linkage, cag_magic, CM_AG_LINK_MAGIX, CM_AG_HEAD_MAGIX);
C2_TL_DEFINE(cm_ag, static, struct c2_cm_aggr_group);

C2_ADDB_EV_DEFINE(cm_setup_fail, "cm_setup_fail",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

C2_ADDB_EV_DEFINE(cm_start_fail, "cm_start_fail",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

C2_ADDB_EV_DEFINE(cm_stop_fail, "cm_stop_fail",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

const struct c2_addb_loc c2_cm_addb_loc = {
	.al_name = "copy machine"
};

const struct c2_addb_ctx_type c2_cm_addb_ctx = {
	.act_name = "copy machine"
};

static void cm_move(struct c2_cm *cm, int rc, enum c2_cm_state state,
		    enum c2_cm_failure failure);

int c2_cm_data_next(struct c2_cm *cm, struct c2_cm_cp *cp)
{
	int rc;

	C2_PRE(c2_cm_invariant(cm));
	C2_PRE(c2_cm_is_locked(cm));
	C2_PRE(cp != NULL);

	rc = cm->cm_ops->cmo_data_next(cm, cp);

	C2_POST(ergo(rc == 0, cp->c_data != NULL));

	return rc;
}

/* Copy packet pump start */

static const struct c2_fom_type_ops cm_cp_pump_fom_type_ops = {
        .fto_create = NULL
};

static struct c2_fom_type cm_cp_pump_fom_type;

static int cpp_alloc(struct c2_cm_cp_pump *cp_pump)
{
	struct c2_cm_cp *cp;
	struct c2_cm    *cm;
	struct c2_fom   *fom;

	cm = container_of(cp_pump, struct c2_cm, cm_cp_pump);
	fom = &cp_pump->p_fom;
	cp = cm->cm_ops->cmo_cp_alloc(cm);
	if (cp == NULL)
		c2_fom_phase_set(fom, C2_CPP_FAIL);
	else {
		cp_pump->p_cp = cp;
		c2_fom_phase_set(fom, C2_CPP_DATA_NEXT);
	}

	return C2_FSO_AGAIN;
}

static int cpp_data_next(struct c2_cm_cp_pump *cp_pump)
{
	struct c2_cm_cp  *cp;
	struct c2_cm     *cm;
	struct c2_fom    *fom;
	int               rc;

	fom = &cp_pump->p_fom;
	cm = container_of(cp_pump, struct c2_cm, cm_cp_pump);
	cp = cp_pump->p_cp;
	C2_ASSERT(cp != NULL);
	/*
	 * Save allocated copy packet, in-case c2_cm_data_next() blocks.
	 */
	rc = c2_cm_data_next(cm, cp);
	if (rc < 0)
		goto fail;
	if (rc == C2_FSO_AGAIN) {
		C2_ASSERT(c2_cm_cp_invariant(cp));
		c2_cm_cp_init(cp);
		c2_cm_cp_enqueue(cm, cp);
		c2_fom_phase_set(fom, C2_CPP_ALLOC);
	}
	goto out;
fail:
	/* Destroy copy packet allocated in C2_CPP_ALLOC phase. */
	cp->c_ops->co_free(cp);
	fom->fo_rc = rc;
	c2_fom_phase_set(fom, C2_CPP_FAIL);
	rc = C2_FSO_AGAIN;
out:
	return rc;
}

static int cpp_fail(struct c2_cm_cp_pump *cp_pump)
{
	struct c2_cm *cm;

	C2_PRE(cp_pump != NULL);

	cm = container_of(cp_pump, struct c2_cm, cm_cp_pump);
	cm_move(cm, cp_pump->p_fom.fo_rc, C2_CMS_ACTIVE, C2_CM_ERR_START);
	c2_fom_phase_set(&cp_pump->p_fom, C2_CPP_IDLE);

	return C2_FSO_WAIT;
}

static int cpp_fini(struct c2_cm_cp_pump *cp_pump)
{
	c2_fom_fini(&cp_pump->p_fom);
	return C2_FSO_WAIT;
}

static const struct c2_sm_state_descr cm_cp_pump_sd[C2_CPP_NR] = {
	[C2_CPP_ALLOC] = {
		.sd_flags   = C2_SDF_INITIAL,
		.sd_name    = "copy packet allocate",
		.sd_allowed = C2_BITS(C2_CPP_DATA_NEXT, C2_CPP_FAIL)
	},
	[C2_CPP_IDLE] = {
		.sd_flags   = 0,
		.sd_name    = "copy packet pump idle",
		.sd_allowed = C2_BITS(C2_CPP_ALLOC, C2_CPP_FINI)
	},
	[C2_CPP_DATA_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "copy packet data next",
		.sd_allowed = C2_BITS(C2_CPP_ALLOC, C2_CPP_FAIL)
	},
	[C2_CPP_FAIL] = {
		.sd_flags   = 0,
		.sd_name    = "copy packet pump fail",
		.sd_allowed = C2_BITS(C2_CPP_IDLE)
	},
	[C2_CPP_FINI] = {
		.sd_flags   = C2_SDF_TERMINAL,
		.sd_name    = "copy packet pump finish",
		.sd_allowed = 0
	},
};

static const struct c2_sm_conf cm_cp_pump_conf = {
	.scf_name      = "sm: cp pump conf",
	.scf_nr_states = ARRAY_SIZE(cm_cp_pump_sd),
	.scf_state     = cm_cp_pump_sd
};

static const struct c2_cm_cp_pump_ops cpp_ops = {
	.po_action = {
		[C2_CPP_ALLOC]     = cpp_alloc,
		[C2_CPP_DATA_NEXT] = cpp_data_next,
		[C2_CPP_FAIL]      = cpp_fail,
		[C2_CPP_FINI]      = cpp_fini
	},
	.po_action_nr = C2_CPP_NR,
};

static uint64_t cm_cp_pump_fom_locality(const struct c2_fom *fom)
{
	/*
	 * It doesn't matter which reqh locality the cp pump FOM is put into.
	 * Thus returning 0 by default.
	 */
        return 0;
}

static int cm_cp_pump_fom_tick(struct c2_fom *fom)
{
	struct c2_cm_cp_pump *cp_pump;
        int                   phase = c2_fom_phase(fom);

	cp_pump = container_of(fom, struct c2_cm_cp_pump, p_fom);
	return cp_pump->p_ops->po_action[phase](cp_pump);
}

static void cm_cp_pump_fom_fini(struct c2_fom *fom)
{
	c2_fom_fini(fom);
}

static const struct c2_fom_ops cm_cp_pump_fom_ops = {
        .fo_fini          = cm_cp_pump_fom_fini,
        .fo_tick          = cm_cp_pump_fom_tick,
        .fo_home_locality = cm_cp_pump_fom_locality
};

/* Copy packet pump end */

static const struct c2_sm_state_descr cm_state_descr[C2_CMS_NR] = {
	[C2_CMS_INIT] = {
		.sd_flags	= C2_SDF_INITIAL,
		.sd_name	= "cm_init",
		.sd_allowed	= C2_BITS(C2_CMS_IDLE, C2_CMS_FAIL, C2_CMS_FINI)
	},
	[C2_CMS_IDLE] = {
		.sd_flags	= 0,
		.sd_name	= "cm_idle",
		.sd_allowed	= C2_BITS(C2_CMS_FAIL, C2_CMS_READY,
					  C2_CMS_STOP, C2_CMS_FINI)
	},
	[C2_CMS_READY] = {
		.sd_flags	= 0,
		.sd_name	= "cm_ready",
		.sd_allowed	= C2_BITS(C2_CMS_ACTIVE, C2_CMS_FAIL)
	},
	[C2_CMS_ACTIVE] = {
		.sd_flags	= 0,
		.sd_name	= "cm_active",
		.sd_allowed	= C2_BITS(C2_CMS_STOP, C2_CMS_FAIL)
	},
	[C2_CMS_FAIL] = {
		.sd_flags	= C2_SDF_FAILURE,
		.sd_name	= "cm_fail",
		.sd_allowed	= C2_BITS(C2_CMS_IDLE, C2_CMS_FINI, C2_CMS_STOP)
	},
	[C2_CMS_STOP] = {
		.sd_flags	= 0,
		.sd_name	= "cm_stop",
		.sd_allowed	= C2_BITS(C2_CMS_FAIL, C2_CMS_IDLE, C2_CMS_FINI)
	},
	[C2_CMS_FINI] = {
		.sd_flags	= C2_SDF_TERMINAL,
		.sd_name	= "cm_fini",
		.sd_allowed	= 0
	},
};

static const struct c2_sm_conf cm_sm_conf = {
	.scf_name	= "sm:cm conf",
	.scf_nr_states  = C2_CMS_NR,
	.scf_state	= cm_state_descr
};

void c2_cm_fail(struct c2_cm *cm, enum c2_cm_failure failure, int rc)
{
	C2_ENTRY();

	C2_PRE(cm != NULL);
	C2_PRE(rc < 0);

	/*
	 * Set the corresponding error code in sm and move the copy machine
	 * to failed state.
	 */
	c2_sm_fail(&cm->cm_mach, C2_CMS_FAIL, rc);

	/*
	 * Send the addb message corresponding to the failure.
	 * @todo A better implementation would have been creating a failure
	 * descriptor table which would contain a ADDB event and a
	 * "failure_action" op for each failure. This would also involve
	 * modifying the C2_ADDB_ADD macro.
	 */
	switch (failure) {
	case C2_CM_ERR_SETUP:
		/*
		 * Send the corresponding ADDB message and keep the copy
		 * machine in "failed" state. Service specific code will
		 * ensure that the copy machine is finalised by calling
		 * c2_cm_fini().
		 */
		C2_ADDB_ADD(&cm->cm_addb , &c2_cm_addb_loc, cm_setup_fail,
			    "cm_setup_fail", rc);
		break;

	case C2_CM_ERR_START:
		/*
		 * Reset the rc and move the copy machine to IDLE state.
		 */
		C2_ADDB_ADD(&cm->cm_addb , &c2_cm_addb_loc, cm_start_fail,
			    "cm_start_fail", rc);
		cm->cm_mach.sm_rc = 0;
		c2_cm_state_set(cm, C2_CMS_IDLE);
		break;

	case C2_CM_ERR_STOP:
		C2_ADDB_ADD(&cm->cm_addb , &c2_cm_addb_loc, cm_stop_fail,
			    "cm_stop_fail", rc);
		cm->cm_mach.sm_rc = 0;
		c2_cm_state_set(cm, C2_CMS_IDLE);
		break;

	default:
		C2_ASSERT(failure >= C2_CM_ERR_NR);
	}
	C2_LEAVE();
}

void c2_cm_lock(struct c2_cm *cm)
{
	c2_sm_group_lock(&cm->cm_sm_group);
}

void c2_cm_unlock(struct c2_cm *cm)
{
	c2_sm_group_unlock(&cm->cm_sm_group);
}

bool c2_cm_is_locked(const struct c2_cm *cm)
{
	return c2_mutex_is_locked(&cm->cm_sm_group.s_lock);
}

enum c2_cm_state c2_cm_state_get(const struct c2_cm *cm)
{
	C2_PRE(c2_cm_is_locked(cm));

	return (enum c2_cm_state)cm->cm_mach.sm_state;
}

void c2_cm_state_set(struct c2_cm *cm, enum c2_cm_state state)
{
	C2_PRE(c2_cm_is_locked(cm));

	c2_sm_state_set(&cm->cm_mach, state);
	C2_LOG("CM:%s%lu: %i", (char *)cm->cm_type->ct_stype.rst_name,
	       cm->cm_id, c2_cm_state_get(cm));
}

bool c2_cm_invariant(const struct c2_cm *cm)
{
	int state = cm->cm_mach.sm_state;

	return
		/* NULL checks */
		cm != NULL && cm->cm_ops != NULL && cm->cm_type != NULL &&
		c2_sm_invariant(&cm->cm_mach) &&
		/* Copy machine state sanity checks */
		ergo(C2_IN(state, (C2_CMS_IDLE, C2_CMS_READY, C2_CMS_ACTIVE,
				   C2_CMS_STOP)),
		     c2_reqh_service_invariant(&cm->cm_service));
}

static void cm_move(struct c2_cm *cm, int rc, enum c2_cm_state state,
		    enum c2_cm_failure failure)
{
	C2_ENTRY();
	C2_PRE(rc <= 0);
	rc != 0 ? c2_cm_fail(cm, failure, rc) : c2_cm_state_set(cm, state);
}

int c2_cm_setup(struct c2_cm *cm)
{
	int	rc;

	C2_ENTRY();
	C2_PRE(cm != NULL);
	C2_PRE(cm->cm_type != NULL);

	c2_cm_lock(cm);
	C2_PRE(c2_cm_state_get(cm) == C2_CMS_INIT);
	C2_PRE(c2_cm_invariant(cm));

	rc = cm->cm_ops->cmo_setup(cm);
	cm_move(cm, rc, C2_CMS_IDLE, C2_CM_ERR_SETUP);

	C2_POST(c2_cm_invariant(cm));
	c2_cm_unlock(cm);
	C2_LEAVE();
	return rc;
}

static void cm_cp_pump_start(struct c2_cm *cm)
{
	cm->cm_cp_pump.p_ops = &cpp_ops;
	c2_fom_init(&cm->cm_cp_pump.p_fom, &cm_cp_pump_fom_type,
		    &cm_cp_pump_fom_ops, NULL, NULL);
	c2_fom_queue(&cm->cm_cp_pump.p_fom, cm->cm_service.rs_reqh);
}

static void cm_cp_pump_stop(struct c2_cm *cm)
{
	C2_PRE(c2_fom_phase(&cm->cm_cp_pump.p_fom) == C2_CPP_IDLE);
	c2_fom_phase_set(&cm->cm_cp_pump.p_fom, C2_CPP_FINI);
	c2_fom_wakeup(&cm->cm_cp_pump.p_fom);
}

int c2_cm_start(struct c2_cm *cm)
{
	int	rc;

	C2_ENTRY();
	C2_PRE(cm != NULL);
	C2_PRE(cm->cm_type != NULL);

	c2_cm_lock(cm);
	C2_PRE(c2_cm_state_get(cm) == C2_CMS_IDLE);
	C2_PRE(c2_cm_invariant(cm));

	rc = cm->cm_ops->cmo_start(cm);
	cm_move(cm, rc, C2_CMS_ACTIVE, C2_CM_ERR_START);
	/* Submit c2_cm::cm_cp_pump to reqh, to start creating copy packets. */
	if (rc == 0)
		cm_cp_pump_start(cm);

	C2_POST(c2_cm_invariant(cm));
	c2_cm_unlock(cm);
	C2_LEAVE();

	return rc;
}

int c2_cm_stop(struct c2_cm *cm)
{
	int	rc;

	C2_ENTRY();
	C2_PRE(cm != NULL);

	c2_cm_lock(cm);
	C2_PRE(C2_IN(c2_cm_state_get(cm), (C2_CMS_ACTIVE, C2_CMS_IDLE)));
	C2_PRE(c2_cm_invariant(cm));

	/*
	 * We set the state here to C2_CMS_STOP and send STOP fop to all other
	 * replicas and wait for STOP fops from all other replies and move
	 * the copy machine to C2_CMS_IDLE state if successful.
	 */
	rc = cm->cm_ops->cmo_stop(cm);
	if (rc == 0)
		cm_cp_pump_stop(cm);
	cm_move(cm, rc, C2_CMS_IDLE, C2_CM_ERR_STOP);

	C2_POST(c2_cm_invariant(cm));
	c2_cm_unlock(cm);
	C2_LEAVE();
	return rc;
}

int c2_cm_module_init(void)
{
	C2_ENTRY();
	cmtypes_tlist_init(&cmtypes);
	c2_bob_type_tlist_init(&cmtypes_bob, &cmtypes_tl);
	c2_mutex_init(&cmtypes_mutex);
	c2_fom_type_init(&cm_cp_pump_fom_type, &cm_cp_pump_fom_type_ops, NULL,
			 &cm_cp_pump_conf);
	c2_cm_cp_module_init();
	C2_LEAVE();
        return 0;
}

void c2_cm_module_fini(void)
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
static uint64_t cm_id_generate(void)
{
	static uint64_t id = 0;
	return ++id;
}

int c2_cm_init(struct c2_cm *cm, struct c2_cm_type *cm_type,
	       const struct c2_cm_ops *cm_ops)
{
	C2_ENTRY();
	C2_PRE(cm != NULL && cm_type != NULL && cm_ops != NULL &&
	       cmtypes_tlist_contains(&cmtypes, cm_type));

	cm->cm_type = cm_type;
	cm->cm_ops = cm_ops;
	cm->cm_id = cm_id_generate();
	c2_addb_ctx_init(&cm->cm_addb, &c2_cm_addb_ctx,
			 &c2_addb_global_ctx);
	c2_sm_group_init(&cm->cm_sm_group);
	c2_sm_init(&cm->cm_mach, &cm_sm_conf, C2_CMS_INIT,
		   &cm->cm_sm_group, &cm->cm_addb);
	/*
	 * We lock the copy machine here just to satisfy the
	 * pre-condition of c2_cm_state_get and not to control
	 * concurrency to c2_cm_init.
	 */
	c2_cm_lock(cm);
	C2_ASSERT(c2_cm_state_get(cm) == C2_CMS_INIT);
	c2_cm_unlock(cm);

	cm_ag_tlist_init(&cm->cm_aggr_grps);

	C2_POST(c2_cm_invariant(cm));
	C2_LEAVE();
	return 0;
}

void c2_cm_fini(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);
	C2_PRE(C2_IN(cm->cm_mach.sm_state, (C2_CMS_INIT, C2_CMS_IDLE,
					    C2_CMS_FAIL)));

	C2_ASSERT(c2_cm_invariant(cm));
	/* Call ->cmo_fini() only if c2_cm_setup() was successful. */
	if (C2_IN(cm->cm_mach.sm_state, (C2_CMS_IDLE, C2_CMS_FAIL)))
		cm->cm_ops->cmo_fini(cm);
	C2_LOG("CM: %s:%lu: %i", (char *)cm->cm_type->ct_stype.rst_name,
	       cm->cm_id, cm->cm_mach.sm_state);
	/*
	 * We lock the copy machine here to satisfy the precondition in
	 * c2_cm_state_set() and not to control concurrency of calls to
	 * c2_cm_fini().
	 */
	c2_cm_lock(cm);
	c2_cm_state_set(cm, C2_CMS_FINI);
	c2_cm_unlock(cm);

	c2_sm_fini(&cm->cm_mach);
	c2_sm_group_fini(&cm->cm_sm_group);
	c2_addb_ctx_fini(&cm->cm_addb);
	C2_LEAVE();
}

int c2_cm_type_register(struct c2_cm_type *cmtype)
{
	int	rc;

	C2_PRE(cmtype != NULL);
	C2_PRE(!cmtypes_tlink_is_in(cmtype));
	C2_ENTRY();

	rc = c2_reqh_service_type_register(&cmtype->ct_stype);
	if (rc == 0) {
		c2_cm_type_bob_init(cmtype);
		c2_mutex_lock(&cmtypes_mutex);
		cmtypes_tlink_init_at_tail(cmtype, &cmtypes);
		c2_mutex_unlock(&cmtypes_mutex);
	}
	C2_LEAVE();
	return rc;
}

void c2_cm_type_deregister(struct c2_cm_type *cmtype)
{
	C2_PRE(cmtype != NULL && c2_cm_type_bob_check(cmtype));
	C2_PRE(cmtypes_tlist_contains(&cmtypes, cmtype));
	C2_ENTRY();

	c2_mutex_lock(&cmtypes_mutex);
	cmtypes_tlink_del_fini(cmtype);
	c2_mutex_unlock(&cmtypes_mutex);
	c2_cm_type_bob_fini(cmtype);
	c2_reqh_service_type_unregister(&cmtype->ct_stype);
	C2_LEAVE();
}

void c2_cm_sw_fill(struct c2_cm *cm)
{
	struct c2_cm_cp_pump *cp_pump;

	C2_PRE(c2_cm_invariant(cm));
	C2_PRE(c2_cm_is_locked(cm));

	cp_pump = &cm->cm_cp_pump;

	c2_fom_phase_set(&cm->cm_cp_pump.p_fom, C2_CPP_ALLOC);
	c2_fom_wakeup(&cm->cm_cp_pump.p_fom);
}

/** @} endgroup CM */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
