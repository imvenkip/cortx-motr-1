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
 * Original creation date: 16/04/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"

#include "sns/repair/cm.h"
#include "reqh/reqh.h"
#include "lib/finject.h"

/**
  @page DLD-snsrepair DLD of SNS-Repair copy machine
  - @ref DLD-snsrepair-ovw
  - @ref DLD-snsrepair-def
  - @ref DLD-snsrepair-req
  - @ref DLD-snsrepair-depends
  - @ref DLD-snsrepair-highlights
  - @subpage DLD-snsrepair-fspec
     - @ref DLD-snsrepair-lspec
     - @ref DLD-snsrepair-lspec-cm-start
     - @ref DLD-snsrepair-lspec-cm-stop
  - @ref DLD-snsrepair-conformance
  - @ref DLD-snsrepair-ut
  - @ref DLD-snsrepair-st
  - @ref DLD-snsrepair-O
  - @ref DLD-snsrepair-ref

  <hr>
  @section DLD-snsrepair-ovw Overview
  This module implements sns repair copy machine using generic copy machine
  infrastructure. SNS repair copy machine is built upon the request handler
  service infrastructure. SNS Repair copy machine is typically started during
  Colibri process startup, although it can also be started separately.

  <hr>
  @section DLD-snsrepair-def Definitions
  Refer to <a ref https://docs.google.com/a/xyratex.com/document/d/
  1Yz25F3GjgQVXzvM1sdlGQvVDSUu-v7FhdUvFhiY_vwM/edit#heading=h.4493e2a5a920

  Refer to <a ref https://docs.google.com/a/xyratex.com/document/d/
  1ZlkjayQoXVm-prMxTkzxb1XncB6HU19I19kwrV-8eQc/edit#heading=h.8c396c004f1f>


  <hr>
  @section DLD-snsrepair-req Requirements
  - @b r.sns.repair.trigger SNS repair copy machine should respond to triggers
    caused due to various kinds of failures as mentioned in the HLD of SNS
    Repair.

  - @b r.sns.repair.buffer.acquire The implementation should efficiently provide
    buffers for the repair operation without any deadlock.

  - @b r.sns.repair.transform The implementation should provide its specific
    implementation of transformation function for copy packets.

  - @b r.sns.repair.next.agent The implementation should provide an efficient
    next-agent phase function for the copy packet.

  - @b r.sns.repair.report.progress The implementation should efficiently report
    overall progress of data re-structuring and update corresponding layout
    information for re-structured objects.

  <hr>
  @section DLD-snsrepair-depends Dependencies
  - @b r.sns.repair.resources.manage It must be possible to efficiently
    manage and throttle resources.

  Refer to <a ref https://docs.google.com/a/xyratex.com/document/d/
  1Yz25F3GjgQVXzvM1sdlGQvVDSUu-v7FhdUvFhiY_vwM/edit#heading=h.c7533697f11c

  <hr>
  @section DLD-snsrepair-highlights Design Highlights
  - SNS Repair copy machine uses request handler service infrastructure.
  - SNS Repair copy machine specific data structure embeds generic copy machine
    and other sns repair specific objects.
  - Incoming and outgoing buffer pools are specific to sns repair copy machine.
  - Copy machine specific operations are invoked from generic copy machine
    interfaces.
  - Copy machine state transitions are performed by generic copy machine
    interfaces.
  - SNS Repair defines its specific aggregation group structure which embeds
    generic aggregation group.

  <hr>
  @section DLD-snsrepair-lspec Logical specification
  - @ref DLD-snsrepair-lspec-cm-start
  - @ref DLD-snsrepair-lspec-cm-stop

  @subsection DLD-snsrepair-lspec-comps Component overview
  The focus of sns repair copy machine is on re-structuring the data
  efficiently. The re-structuring operation is split into various copy packet
  phases.

  @subsection DLD-snsrepair-lspec-cm-start Copy machine startup
  SNS Repair defines its following specific data structure to represent a
  copy machine.
  @code
  struct c2_sns_repair_cm {
          struct c2_cm               sr_base;
          struct c2_net_buffer_pool *sr_bp_in;
          struct c2_net_buffer_pool *sr_bp_out;
  };
  @endcode

  SNS Repair implements its specific operations for
  struct c2_reqh_service_type_ops, struct c2_reqh_service_ops, and
  struct c2_cm_ops.

  Note: Please refer reqh/reqh_service.c for further details on request handler
  service.

  SNS Repair copy machine is initialised and started during corresponding
  service initialisation and startup.
  During service startup, sns repair copy machine service start routine invokes
  generic c2_cm_start(), which invokes sns repair copy machine start operation.
  This can be illustrated by below psuedo code.
  @code
  int service_start(service)
  {
    // Get cm reference containing this service
    c2_cm_start(cm);
  }

  int c2_cm_start(cm)
  {
	//Perform cm state transition and invoke sns specific
	//implementation of c2_cm_ops::cmo_start()
	cm->cm_ops->cmo_start();
  }

  cm_start(cm)
  {
	//Fetch configuration information for the node.
	//Start copy machine agents.
  }
  @endcode

  Note:
  - Copy machine start operation can block as it fetches configuration
    information from configuration service, which can block.

  @subsection DLD-snsrepair-lspec-cm-stop Copy machine stop
  SNS Repair copy machine is stopped when its corresponding service is
  stopped. Again, the service stop operation invokes generic c2_cm_stop(),
  which further invokes sns repair copy machine specific stop operation.
  This stops all the copy machine agents.

  @subsection DLD-snsrepair-lspec-thread Threading and Concurrency Model
  SNS Repair copy machine is implemented as a request handler service, thus
  it shares the request handler threading model and does not create its own
  threads. Thus all the copy machine operations are performed in context of
  request handler threads.

  SNS Repair copy machine uses generic copy machine infrastructure, which
  implements copy machine state machine using generic Colibri state machine
  infrastructure.
  @ref State machine <!-- sm/sm.h -->

  @subsection DLD-snsrepair-lspec-numa NUMA optimizations
  N/A

  <hr>
  @section DLD-snsrepair-conformance Conformance
  @b i.sns.repair.trigger Various triggers are reported through FOPs, which create
  corresponding FOMs. FOMs invoke sns repair specific copy machine operations
  through generic copy machine interfaces which cause copy machine state
  transitions.

  @b i.sns.repair.report.progress Progress is reported using sliding window and
  layout updates.
  @todo Layout updates will be implemented at later stages of sns repair.

  @b i.sns.repair.buffer.acquire SNS Repair implements separate buffer pools for
  incoming and outgoing buffers for corresponding agents.

  @b i.sns.repair.transform SNS Repair implements its specific transformation
  function and its corresponding copy machine operations.

  @b i.sns.repair.next.agent SNS Repair implements is specific next_agent function
  as a copy machine operation.

  <hr>
  @section DLD-snsrepair-ut Unit tests

  <hr>
  @section DLD-snsrepair-st System tests
  N/A

  <hr>
  @section DLD-snsrepair-O Analysis
  N/A

  <hr>
  @section DLD-snsrepair-ref References
  @see @ref DLD-cm-ref
*/

/**
  @addtogroup snsrepair
  SNS Repair copy machine implements a copy machine in-order to re-structure
  data efficiently in event of a failure. It uses GOB (global file object) and
  COB (component object) infrastructure and parity de-clustering layout.

  SNS Repair copy machine is typically started during colibri setup, but can
  also be started later. SNS Repair copy machine mainly uses reqh and generic
  copy machine framework, and thus implements their corresponding operations.

  @{
*/

/** Copy machine operations.*/
static int cm_start(struct c2_cm *cm);
static int cm_config(struct c2_cm *cm);
static int cm_transform(struct c2_cm_cp *packet);
static int cm_next_agent(struct c2_cm *cm, struct c2_cm_cp *packet);
static int cm_incoming(struct c2_cm *cm, struct c2_fom *fom);
static void cm_done(struct c2_cm *cm);
static void cm_stop(struct c2_cm *cm);
static void cm_fini(struct c2_cm *cm);

const struct c2_cm_ops cm_ops = {
	.cmo_start      = cm_start,
	.cmo_config     = cm_config,
	.cmo_next_agent = cm_next_agent,
	.cmo_transform  = cm_transform,
	.cmo_incoming   = cm_incoming,
	.cmo_done       = cm_done,
	.cmo_stop       = cm_stop,
	.cmo_fini       = cm_fini
};

/** Copy machine callbacks.*/
static void cm_cb_container(struct c2_cm *cm, uint64_t cid);
static void cm_cb_device(struct c2_cm *cm, uint64_t devid);
static void cm_cb_sw(struct c2_cm *cm, struct c2_cm_sw *sw);

const struct c2_cm_cb cm_cb = {
	.cmcb_container = cm_cb_container,
	.cmcb_device    = cm_cb_device,
	.cmcb_sl_window = cm_cb_sw
};

extern struct c2_cm_type sns_repair_cmt;

int c2_sns_repair_cm_type_register(void)
{
	return c2_cm_type_register(&sns_repair_cmt);
}

void c2_sns_repair_cm_type_deregister(void)
{
	c2_cm_type_deregister(&sns_repair_cmt);
}

static int cm_start(struct c2_cm *cm)
{
	int rc = 0;

	C2_ENTRY();

	C2_LEAVE();
	return rc;
}

/**
 * Transformation function for sns repair.
 *
 * Finds aggregation group c2_sns_aggr_group corresponding to the incoming
 * copy packet. Calculates the total number of copy packets
 * c2_sns_aggr_group::sag_cp_nr belonging
 * to c2_sns_aggr_group and checks it with the number of copy packets which are
 * transformed (c2_cm_aggr_group::cag_transformed_cp_nr).
 * If all the copy packets belonging to the aggregation group are transformed,
 * then creates a new copy packet and sends it to the next agent.
 *
 * Transformation involves XORing the c2_net_buffer's from copy packet
 * c2_cm_cp::cp_nb with c2_sns_aggr_group::sag_transformed_nb.
 * XORing is done using parity math operations like c2_parity_math_calculate().
 *
 * When first copy packet of the aggregation group is transformed, its
 * corresponding c2_cm_cp::cp_nb is set to
 * c2_sns_aggr_group::sag_transformed_nb.
 * Typically, all the copy packets will have same buffer size. Hence, there is
 * no need for any complex buffer manipulation like growing or shrinking the
 * c2_sns_aggr_group::sag_transformed_nb.
 *
 * Every copy packet once transformed is freed. It is safe to do so since the
 * collecting agent does not typically interact with remote agents. So there is
 * no risk of transformed copy packet getting lost during network
 * communication.
 *
 * @pre cp!= NULL && cp->cp_state == CPS_COLLECTING_WAIT
 * @param cp Copy packet that has to be transformed.
 */
static int cm_transform(struct c2_cm_cp *cp)
{
	return 0;
}

/**
 * Defines the next_agent() for a sns repair copy machine.
 *
 * This routine return the next agent in [out] next_agent_id for this packet.
 * Caller will use the agent id to find the proper agent in its domain.
 *
 * Usually, copy packets flow from agent to agent, from storage-in agent,
 * to network-out agent, to network-in agent, to collecting agent, and finally
 * are written onto device/containers by storage-out agent. This forms the copy
 * packet pipeline.
 * For different copy packets, their pipeline may be different, because these
 * copy packets belong to different data/parity groups.
 *
 * Please @note a remote agent may be returned. The copy packet needs to
 * be transported on network between a network-in and network-out agent pair.
 */
static int cm_next_agent(struct c2_cm *cm, struct c2_cm_cp *packet)
{
	int rc = -ENOENT;

	C2_PRE(cm != NULL);
	C2_PRE(packet != NULL);
/*
	switch (packet->cp_state) {
	case C2_CPS_STORAGE_IN_WAIT: {
		struct c2_sns_repair_cp	*scp;
		struct c2_cm_agent      *ca;
		struct c2_tl		*tl;
		uint64_t                 agents_nr;
		uint32_t                 agents_idx;
		enum c2_cm_agent_type    type;
*/
		/*
		 * Finds a collecting agent as next agent.
		 * TODO: This is temporary for single node demo. When layout
		 * and configuration is ready, we will use them to find proper
		 * next agent.
		 */
/*
		type = C2_CM_AGENT_COLLECTING;
		c2_cm_group_lock(cm);
		tl = &cm->cm_agents;
*/
		/*
		 * it's just a simple hash: hash with the fid key.
		 * Assumptions: the number of agents will not change after sns
		 * setup.
		 */
/*
		agents_nr = cm->cm_agent_id_max;
		scp = container_of(packet, struct c2_sns_repair_cp, rc_base);
		agents_idx = scp->rc_gfid.f_key % agents_nr;
		ca = agent_tlist_head(tl);
		C2_ASSERT(ca != NULL);
		while (agents_idx != 0) {
			ca = agent_tlist_next(tl, ca);
			C2_ASSERT(ca != NULL);
			--agents_idx;
		}
*/
		/* return this collecting agent */
/*
		*next_agent_id = ca->a_id;
		c2_cm_group_unlock(cm);
		rc = 0;
		break;
	}
	case C2_CPS_STORAGE_OUT_WAIT:
*/
		/*
		 * This is the end of pipeline. next_agent() should not
		 * be called for this agent.
		 */
/*
		C2_ASSERT(0);
		rc = -ENOENT;
		break;
	case C2_CPS_NETWORK_IN_WAIT:
	case C2_CPS_NETWORK_OUT_WAIT:
		rc = -ENOENT;
		break;
	case C2_CPS_COLLECTING_WAIT: {
		struct c2_sns_repair_cp	       *scp;
		struct c2_cm_agent             *a;
		struct c2_tl		       *tl;
		struct c2_cm_storage_out_agent *so;
*/
		/*
		 * Finds a storage-out agent as next agent.
		 *
		 * At this point, the cob fid and cob ext of current copy
		 * packet should be ready and valid.
		 *
		 * TODO: This is temporary. When layout and configuration
		 * is ready, they should be used to find proper next agent.
		 */
/*
		c2_cm_group_lock(cm);
		tl = &cm->cm_agents;
		scp = container_of(packet, struct c2_sns_repair_cp, rc_base);
		c2_tl_for(agent, tl, a) {
			so = container_of(a, struct c2_cm_storage_out_agent,
					  so_agent);
			if (so->so_container_id == scp->rc_cfid.f_container) {
*/
				/* return this storage-out agent */
/*
				*next_agent_id = a->a_id;
				rc = 0;
				break;
			}
		} c2_tl_endfor;
		C2_ASSERT(rc == 0);
		c2_cm_group_unlock(cm);
		break;
	}
	default:
*/
		/*
		 * This should not happen.
		 */
/*
		C2_ASSERT(0);
		rc = -ENOENT;
		break;
	}
*/
	return rc;
}

static int cm_incoming(struct c2_cm *cm, struct c2_fom *fom)
{
	return 0;

}

static int cm_config(struct c2_cm *cm)
{
	return 0;
}

static void cm_done(struct c2_cm *cm)
{

    /*
     * Broadcast DONE FOPs to other replicas
     * and wait for DONE FOPs from all the replicas.
     * Transition cm to IDLE state.
     */
}

static void cm_stop(struct c2_cm *cm)
{
	C2_PRE(cm != NULL);

    /*
     * Broadcast STOP FOPs to all other replicas and wait for
     * for STOP FOPs from all other replicas.
     * Transition CM to STOP state.
     */
}

static void cm_fini(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);
}

static void cm_cb_container(struct c2_cm *cm, uint64_t cid)
{
}

static void cm_cb_device(struct c2_cm *cm, uint64_t devid)
{
}

static void cm_cb_sw(struct c2_cm *cm, struct c2_cm_sw *sw)
{
}

/** @} snsrepair */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
