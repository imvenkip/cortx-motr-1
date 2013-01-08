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

/*
 * Define the ADDB types in this file.
 */
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "cm/cm_addb.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM

#include "lib/trace.h"   /* M0_LOG */
#include "lib/bob.h"     /* M0_BOB_DEFINE */
#include "lib/misc.h"    /* M0_SET0 */
#include "lib/assert.h"  /* M0_PRE, M0_POST */
#include "lib/errno.h"
#include "lib/finject.h"
#include "mero/magic.h"
#include "ioservice/io_device.h"

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
      - @ref CMDLD-lspec-cm-fini
      - @ref CMDLD-lspec-thread
   - @ref CMDLD-conformance
   - @ref CMDLD-addb
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
   - Copy machine is implemented as mero state machine.
   - All the registered types of copy machines can be initialised using various
     interfaces and also from mero setup.
   - Once started, each copy machine type is registered with the request handler
     as a service.
   - A copy machine service can be started using "mero setup" utility or
     separately.
   - Once started copy machine remains idle until further event happens.
   - Copy machine copy packets are implemented as foms, and are started by copy
     machine using request handler.
   - Copy machine type specific event triggers copy machine operation, (e.g.
     TRIGGER FOP for SNS Repair). This allocates copy machine specific resources
     and creates copy packets.
  -  The complete data restructuring process of copy machine follows
     non-blocking processing model of Mero design.
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
   - @ref CMDLD-lspec-cm-fini

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
   After copy machine is successfully initialised (m0_cm_init()), it is
   configured as part of the copy machine service startup by invoking
   m0_cm_setup(). This performs copy machine specific setup by invoking
   m0_cm_ops::cmo_setup(). Once successfully completed the copy machine
   is transitioned into M0_CMS_IDLE state. In case of setup failure, copy
   machine is transitioned to M0_CMS_FAIL state, this also fails copy machine
   service startup, and thus copy machine is finalised during copy machine
   service finalisation.

   @subsection CMDLD-lspec-cm-start Copy machine operation start
   After copy machine service is successfully started, it is ready to perform
   its respective tasks (e.g. SNS Repair). On receiving a trigger event (i.e
   failure in case of sns repair) copy machine transitions into M0_CMS_ACTIVE
   state once copy machine specific startup tasks are complete (m0_cm_start()).
   In case of copy machine startup failure, copy machine transitions into
   M0_CMS_FAIL state, once failure is handled, copy machine transitions back
   into M0_CMS_IDLE state and waits for further events.

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
   @see struct m0_cm_cp_pump

   @subsubsection CMDLD-lspec-cm-active Copy machine activation
   After creating initial number of copy packets, copy machine broadcasts READY
   FOPs with its corresponding sliding window information to all its replicas
   in the pool. Every copy machine replica, after receiving READY FOPs from all
   its replicas in the pool, transitions into M0_CMS_ACTIVE state.

   @subsection CMDLD-lspec-cm-stop Copy machine stop
   Once operation completes successfully, copy machine performs required tasks,
   (e.g. updating layouts, etc.) by invoking m0_cm_stop(), this transitions copy
   machine back to M0_CMS_IDLE state. Copy machine invokes m0_cm_stop() also in
   case of operational failure to broadcast STOP FOPs to its other replicas in
   the pool, indicating failure. This is handled specific to the copy machine
   type.

   @subsection CMDLD-lspec-cm-fini Copy machine finalisation
   As copy machine is implemented as a m0_reqh_service, the copy machine
   finalisation path is m0_reqh_service_stop()->rso_stop()->m0_cm_fini(). Now,
   before invoking m0_reqh_service_stop(), m0_reqh_shutdown_wait() is called,
   this returns when all the FOMs in the given reqh are finalised. Although
   there is a possibilty that the copy machine operation is in-progress while
   the reqh is being shutdown, this situation is taken care by
   m0_reqh_shutdown() mechanism as mentioned above. Thus the copy machine pump
   FOM (m0_cm::cm_cp_pump) is created when copy machine operation starts and
   destroyed when copy machine operation stops, until then it is alive within
   the reqh. Thus using m0_reqh_shutdown_wait() mechanism we are sure that copy
   machine is IDLE and operation is completed before the m0_cm_fini() is
   invoked.
   @note Presently services are stopped only during reqh shutdown.

   @subsection CMDLD-lspec-thread Threading and Concurrency Model
   - Copy machine is implemented as a state machine, and thus do
     not have its own thread. It runs in the context of reqh threads.
   - Copy machine starts as a service and is registered with the request
     handler.
   - The cmtype_mutex is used to serialise the operation on cmtypes_list.
   - Access to the members of struct m0_cm is serialised using the
     m0_cm::m0_sm_group::s_mutex.

   <hr>
   @section CMDLD-conformance Conformance
   This section briefly describes interfaces and structures conforming to above
   mentioned copy machine requirements.
   - @b i.cm.generic.invoke Copy machine generic routines are invoked from copy
        machine specific code.
   - @b i.cm.generic.specific Copy machine generic routines accordingly
        invoke copy machine specific operations.
   - @b i.cm.synchronise Copy machine provides synchronised access to its
        members using m0_cm::cm_sm_group::s_mutex.
   - @b i.cm.failure Copy machine handles various types of failures through
        m0_cm_fail() interface.
   - @b i.cm.resource.manage Copy machine specific resources are managed by copy
        machine specific implementation, e.g. buffer pool, etc.

   @section CMDLD-addb ADDB events
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

/** List containing the copy machines registered on a mero server. */
static struct m0_tl cmtypes;

/** Protects access to the list m0_cmtypes. */
static struct m0_mutex cmtypes_mutex;

M0_TL_DESCR_DEFINE(cmtypes, "copy machine types", static,
                   struct m0_cm_type, ct_linkage, ct_magix,
                   CM_TYPE_LINK_MAGIX, CM_TYPE_HEAD_MAGIX);

M0_TL_DEFINE(cmtypes, static, struct m0_cm_type);

static struct m0_bob_type cmtypes_bob;
M0_BOB_DEFINE(static, &cmtypes_bob, m0_cm_type);

struct m0_addb_ctx m0_cm_mod_ctx;

static void cm_move(struct m0_cm *cm, int rc, enum m0_cm_state state,
		    enum m0_cm_failure failure);

static const struct m0_sm_state_descr cm_state_descr[M0_CMS_NR] = {
	[M0_CMS_INIT] = {
		.sd_flags	= M0_SDF_INITIAL,
		.sd_name	= "cm_init",
		.sd_allowed	= M0_BITS(M0_CMS_IDLE, M0_CMS_FAIL, M0_CMS_FINI)
	},
	/**
	 * @todo Transition the state to M0_CMS_READY instead of M0_CMS_ACTIVE
	 * when "READY" fops are implemented.
	 */
	[M0_CMS_IDLE] = {
		.sd_flags	= 0,
		.sd_name	= "cm_idle",
		.sd_allowed	= M0_BITS(M0_CMS_FAIL, M0_CMS_ACTIVE,
					  M0_CMS_FINI)
	},
	[M0_CMS_READY] = {
		.sd_flags	= 0,
		.sd_name	= "cm_ready",
		.sd_allowed	= M0_BITS(M0_CMS_ACTIVE, M0_CMS_FAIL)
	},
	[M0_CMS_ACTIVE] = {
		.sd_flags	= 0,
		.sd_name	= "cm_active",
		.sd_allowed	= M0_BITS(M0_CMS_STOP, M0_CMS_FAIL)
	},
	[M0_CMS_FAIL] = {
		.sd_flags	= M0_SDF_FAILURE,
		.sd_name	= "cm_fail",
		.sd_allowed	= M0_BITS(M0_CMS_IDLE, M0_CMS_FINI, M0_CMS_STOP)
	},
	[M0_CMS_STOP] = {
		.sd_flags	= 0,
		.sd_name	= "cm_stop",
		.sd_allowed	= (1 << M0_CMS_IDLE)
	},
	[M0_CMS_FINI] = {
		.sd_flags	= M0_SDF_TERMINAL,
		.sd_name	= "cm_fini",
		.sd_allowed	= 0
	},
};

static const struct m0_sm_conf cm_sm_conf = {
	.scf_name	= "sm:cm conf",
	.scf_nr_states  = M0_CMS_NR,
	.scf_state	= cm_state_descr
};

M0_INTERNAL void m0_cm_fail(struct m0_cm *cm, enum m0_cm_failure failure,
			    int rc)
{
	struct m0_addb_mc *addb_mc;

	M0_ENTRY("cm: %p fc: %u rc: %d", cm, failure, rc);

	M0_PRE(cm != NULL);
	M0_PRE(rc < 0);
	M0_PRE(cm->cm_service.rs_reqh != NULL);

	addb_mc = &cm->cm_service.rs_reqh->rh_addb_mc;

	/*
	 * Set the corresponding error code in sm and move the copy machine
	 * to failed state.
	 */
	m0_sm_fail(&cm->cm_mach, M0_CMS_FAIL, rc);

	/*
	 * Send the addb message corresponding to the failure.
	 * @todo A better implementation would have been creating a failure
	 * descriptor table which would contain a ADDB event and a
	 * "failure_action" op for each failure. This would also involve
	 * modifying the addb call.
	 */
	switch (failure) {
	case M0_CM_ERR_SETUP:
		/*
		 * Send the corresponding ADDB message and keep the copy
		 * machine in "failed" state. Service specific code will
		 * ensure that the copy machine is finalised by calling
		 * m0_cm_fini().
		 */
		M0_ADDB_FUNC_FAIL(addb_mc, M0_CM_ADDB_LOC_SETUP_FAIL, rc,
				  &m0_cm_mod_ctx, &cm->cm_service.rs_addb_ctx);
		break;

	case M0_CM_ERR_START:
		/*
		 * Reset the rc and move the copy machine to IDLE state.
		 */
		M0_ADDB_FUNC_FAIL(addb_mc, M0_CM_ADDB_LOC_START_FAIL, rc,
				  &m0_cm_mod_ctx, &cm->cm_service.rs_addb_ctx);
		cm->cm_mach.sm_rc = 0;
		m0_cm_state_set(cm, M0_CMS_IDLE);
		break;

	case M0_CM_ERR_STOP:
		M0_ADDB_FUNC_FAIL(addb_mc, M0_CM_ADDB_LOC_STOP_FAIL, rc,
				  &m0_cm_mod_ctx, &cm->cm_service.rs_addb_ctx);
		cm->cm_mach.sm_rc = 0;
		m0_cm_state_set(cm, M0_CMS_IDLE);
		break;

	default:
		M0_ASSERT(failure >= M0_CM_ERR_NR);
	}
	M0_LEAVE();
}

M0_INTERNAL void m0_cm_lock(struct m0_cm *cm)
{
	m0_sm_group_lock(&cm->cm_sm_group);
}

M0_INTERNAL void m0_cm_unlock(struct m0_cm *cm)
{
	m0_sm_group_unlock(&cm->cm_sm_group);
}

M0_INTERNAL bool m0_cm_is_locked(const struct m0_cm *cm)
{
	return m0_mutex_is_locked(&cm->cm_sm_group.s_lock);
}

M0_INTERNAL enum m0_cm_state m0_cm_state_get(const struct m0_cm *cm)
{
	M0_PRE(m0_cm_is_locked(cm));

	return (enum m0_cm_state)cm->cm_mach.sm_state;
}

M0_INTERNAL void m0_cm_state_set(struct m0_cm *cm, enum m0_cm_state state)
{
	M0_PRE(m0_cm_is_locked(cm));

	m0_sm_state_set(&cm->cm_mach, state);
	M0_LOG(M0_INFO, "CM:%s%lu: %i", (char *)cm->cm_type->ct_stype.rst_name,
	       cm->cm_id, m0_cm_state_get(cm));
}

M0_INTERNAL bool m0_cm_invariant(const struct m0_cm *cm)
{
	int state = cm->cm_mach.sm_state;

	return
		/* NULL checks */
		cm != NULL && cm->cm_ops != NULL && cm->cm_type != NULL &&
		m0_sm_invariant(&cm->cm_mach) &&
		/* Copy machine state sanity checks */
		ergo(M0_IN(state, (M0_CMS_IDLE, M0_CMS_READY, M0_CMS_ACTIVE,
				   M0_CMS_STOP)),
		     m0_reqh_service_invariant(&cm->cm_service));
}

static void cm_move(struct m0_cm *cm, int rc, enum m0_cm_state state,
		    enum m0_cm_failure failure)
{
	M0_PRE(rc <= 0);
	rc != 0 ? m0_cm_fail(cm, failure, rc) : m0_cm_state_set(cm, state);
}

M0_INTERNAL int m0_cm_setup(struct m0_cm *cm)
{
	int	rc;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(cm != NULL);
	M0_PRE(cm->cm_type != NULL);

	if (M0_FI_ENABLED("setup_failure"))
		return -EINVAL;

	m0_cm_lock(cm);
	M0_PRE(m0_cm_state_get(cm) == M0_CMS_INIT);
	M0_PRE(m0_cm_invariant(cm));

	rc = cm->cm_ops->cmo_setup(cm);
	if (M0_FI_ENABLED("setup_failure_2"))
		rc = -EINVAL;
	cm_move(cm, rc, M0_CMS_IDLE, M0_CM_ERR_SETUP);

	M0_POST(m0_cm_invariant(cm));
	m0_cm_unlock(cm);

	M0_LEAVE("rc: %d", rc);
	return rc;
}

M0_INTERNAL int m0_cm_start(struct m0_cm *cm)
{
	int	rc;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(cm != NULL);
	M0_PRE(cm->cm_type != NULL);

	m0_cm_lock(cm);
	M0_PRE(m0_cm_state_get(cm) == M0_CMS_IDLE);
	M0_PRE(m0_cm_invariant(cm));

        cm->cm_pm = m0_ios_poolmach_get(cm->cm_service.rs_reqh);
        if (cm->cm_pm == NULL)
                return -EINVAL;

	rc = cm->cm_ops->cmo_start(cm);
	cm_move(cm, rc, M0_CMS_ACTIVE, M0_CM_ERR_START);
	/* Start pump FOM to create copy packets. */
	if (rc == 0)
		m0_cm_cp_pump_start(cm);

	M0_POST(m0_cm_invariant(cm));
	m0_cm_unlock(cm);

	M0_LEAVE("rc: %d", rc);
	return rc;
}

M0_INTERNAL int m0_cm_stop(struct m0_cm *cm)
{
	int	rc;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(cm != NULL);

	m0_cm_lock(cm);
	M0_PRE(m0_cm_state_get(cm) == M0_CMS_ACTIVE);
	M0_PRE(m0_cm_invariant(cm));
	/*
	 * We set the state here to M0_CMS_STOP and once copy machine
	 * specific resources are released copy machine transitions back to
	 * M0_CMS_IDLE state as m0_cm_start() expects copy machine to be idle
	 * before starting new restructuring operation.
	 */
	m0_cm_state_set(cm, M0_CMS_STOP);
	rc = cm->cm_ops->cmo_stop(cm);
	if (rc == 0)
		m0_cm_cp_pump_stop(cm);
	cm_move(cm, rc, M0_CMS_IDLE, M0_CM_ERR_STOP);

	M0_POST(m0_cm_invariant(cm));
	m0_cm_unlock(cm);

	M0_LEAVE("rc: %d", rc);
	return rc;
}

M0_INTERNAL int m0_cm_module_init(void)
{
	M0_ENTRY();
	m0_addb_ctx_type_register(&m0_addb_ct_cm_mod);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_cm_mod_ctx,
			 &m0_addb_ct_cm_mod, &m0_addb_proc_ctx);
	cmtypes_tlist_init(&cmtypes);
	m0_bob_type_tlist_init(&cmtypes_bob, &cmtypes_tl);
	m0_mutex_init(&cmtypes_mutex);
	m0_cm_cp_pump_init();
	m0_cm_cp_module_init();
	M0_LEAVE();
        return 0;
}

M0_INTERNAL void m0_cm_module_fini(void)
{
	M0_ENTRY();
	cmtypes_tlist_fini(&cmtypes);
	m0_mutex_fini(&cmtypes_mutex);
        m0_addb_ctx_fini(&m0_cm_mod_ctx);
	M0_LEAVE();
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

M0_INTERNAL int m0_cm_init(struct m0_cm *cm, struct m0_cm_type *cm_type,
			   const struct m0_cm_ops *cm_ops)
{
	M0_ENTRY("cm_type: %p cm: %p", cm_type, cm);
	M0_PRE(cm != NULL && cm_type != NULL && cm_ops != NULL &&
	       cmtypes_tlist_contains(&cmtypes, cm_type));

	if (M0_FI_ENABLED("init_failure"))
		return -EINVAL;

	cm->cm_type = cm_type;
	cm->cm_ops = cm_ops;
	cm->cm_id = cm_id_generate();
	m0_sm_group_init(&cm->cm_sm_group);
	/* Note: ADDB context not initialized until m0_reqh_service_start() */
	m0_sm_init(&cm->cm_mach, &cm_sm_conf, M0_CMS_INIT,
		   &cm->cm_sm_group, &cm->cm_service.rs_addb_ctx);
	/*
	 * We lock the copy machine here just to satisfy the
	 * pre-condition of m0_cm_state_get and not to control
	 * concurrency to m0_cm_init.
	 */
	m0_cm_lock(cm);
	M0_ASSERT(m0_cm_state_get(cm) == M0_CMS_INIT);
	aggr_grps_tlist_init(&cm->cm_aggr_grps);

	M0_POST(m0_cm_invariant(cm));
	m0_cm_unlock(cm);

	M0_LEAVE();
	return 0;
}

M0_INTERNAL void m0_cm_fini(struct m0_cm *cm)
{
	M0_ENTRY("cm: %p", cm);
	M0_PRE(cm != NULL);

	m0_cm_lock(cm);
	M0_PRE(M0_IN(m0_cm_state_get(cm), (M0_CMS_INIT, M0_CMS_IDLE,
					   M0_CMS_FAIL)));
	M0_PRE(m0_cm_invariant(cm));

	cm->cm_ops->cmo_fini(cm);
	M0_LOG(M0_INFO, "CM: %s:%lu: %i",
	      (char *)cm->cm_type->ct_stype.rst_name,
	      cm->cm_id, cm->cm_mach.sm_state);
	m0_cm_state_set(cm, M0_CMS_FINI);
	m0_cm_unlock(cm);

	m0_sm_fini(&cm->cm_mach);
	m0_sm_group_fini(&cm->cm_sm_group);

	M0_LEAVE();
}

M0_INTERNAL int m0_cm_type_register(struct m0_cm_type *cmtype)
{
	int	rc;

	M0_ENTRY("cmtype: %p", cmtype);
	M0_PRE(cmtype != NULL);
	M0_PRE(m0_reqh_service_type_find(cmtype->ct_stype.rst_name) == NULL);

	rc = m0_reqh_service_type_register(&cmtype->ct_stype);
	if (rc == 0) {
		m0_cm_type_bob_init(cmtype);
		m0_mutex_lock(&cmtypes_mutex);
		cmtypes_tlink_init_at_tail(cmtype, &cmtypes);
		m0_mutex_unlock(&cmtypes_mutex);
		M0_ASSERT(cmtypes_tlink_is_in(cmtype));
	}

	M0_LEAVE("rc: %d", rc);
	return rc;
}

M0_INTERNAL void m0_cm_type_deregister(struct m0_cm_type *cmtype)
{
	M0_ENTRY("cmtype: %p", cmtype);
	M0_PRE(cmtype != NULL && m0_cm_type_bob_check(cmtype));
	M0_PRE(cmtypes_tlist_contains(&cmtypes, cmtype));

	m0_mutex_lock(&cmtypes_mutex);
	cmtypes_tlink_del_fini(cmtype);
	m0_mutex_unlock(&cmtypes_mutex);
	m0_cm_type_bob_fini(cmtype);
	m0_reqh_service_type_unregister(&cmtype->ct_stype);

	M0_LEAVE();
}

M0_INTERNAL void m0_cm_sw_fill(struct m0_cm *cm)
{
	M0_ENTRY("cm: %p", cm);
	M0_PRE(m0_cm_invariant(cm));

	m0_cm_cp_pump_wakeup(cm);

	M0_LEAVE();
}

M0_INTERNAL int m0_cm_data_next(struct m0_cm *cm, struct m0_cm_cp *cp)
{
	int rc;

	M0_ENTRY("cm: %p cp: %p", cm, cp);
	M0_PRE(m0_cm_invariant(cm));
	M0_PRE(m0_cm_is_locked(cm));
	M0_PRE(cp != NULL);

	rc = cm->cm_ops->cmo_data_next(cm, cp);

	M0_POST(ergo(rc == 0, cp->c_data != NULL));

	M0_LEAVE("rc: %d", rc);
	return rc;
}

M0_INTERNAL bool m0_cm_has_more_data(const struct m0_cm *cm)
{
	M0_PRE(m0_cm_invariant(cm));

	return !m0_cm_cp_pump_is_complete(&cm->cm_cp_pump);
}

#undef M0_TRACE_SUBSYSTEM

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
