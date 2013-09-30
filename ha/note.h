/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 10-May-2013
 */


#pragma once

#ifndef __MERO___HA_NOTE_H__
#define __MERO___HA_NOTE_H__


/**
 * @defgroup ha-note HA notification
 *
 * This module defines protocols and functions used to communicate HA-related
 * events between HA and Mero core.
 *
 * Any HA-related event is represented as a state change of a configuration
 * object. Configuration objects are stored in the Mero configuration data-base
 * hosted by confd services and accessible to Mero and HA instances through
 * confc module. A configuration object is identified by a unique 128-bit
 * identifier.
 *
 * HA-related state of a configuration object is represented by enum
 * m0_ha_obj_state. It is important to understand that this state is *not*
 * stored in confd. confd stores the "basic" information describing the
 * nomenclature of system elements (nodes, services, devices, pools, etc.) and
 * their relationships. HA maintains additional state on top of confd, which
 * describes the run-time behaviour of configuration elements.
 *
 * Among other things, confd stores, for certain types of objects, their
 * "delegation pointers". A delegation pointer of an object X is some object Y
 * that should be used when X fails. For example, to organise a fail-over pair,
 * 2 services should have delegation pointers set to each other. The delegation
 * pointer of a pool points to the pool to which writes should be re-directed in
 * case of an NBA event. When an object fails, the chain formed by delegation
 * pointers is followed until a usable object is found. If the chain is
 * exhausted before a usable object is found, a system error is declared. All
 * consecutive attempts to use the object would return the error until HA state
 * or confd state changes.
 *
 * <b>Use cases</b>
 *
 * 0. Mero initialisation
 *
 * On startup, Mero instance connects to confd and populates its local confc
 * with configuration objects. Then, Mero instance calls m0_ha_state_get() (one
 * or more times). This function accepts as an input parameter a vector
 * (m0_ha_nvec) that identifies objects for which state is queried
 * (m0_ha_note::no_state field is ignored). m0_ha_state_get() constructs a fop
 * of m0_ha_state_get_fopt type with nvec as data and sends it to the local HA
 * instance (via supplied session).
 *
 * HA replies with the same vector with m0_ha_note::no_state fields
 * set. m0_ha_state_get() stores received object states in confc and notifies
 * the caller about completion through the supplied channel.
 *
 * 1. Mero core notifies HA about failure.
 *
 * On detecting a failure, Mero core calls m0_ha_state_set(), which takes as an
 * input an nvec (with filled states), describing the failures, constructs
 * m0_ha_state_set_fopt fop and sends it to the local HA instance via supplied
 * session. HA replies with generic fop reply (m0_fop_generic_reply).
 *
 * @note that there is a separate mechanism, based on m0ctl, which is used to
 * notify HA about failures which cannot be reported through RPC.
 *
 * 2. HA notifies Mero about failure.
 *
 * When HA agrees about a failure, it sends to each Mero instance a
 * m0_ha_state_set_fopt fop. Mero replies with generic fop reply.
 *
 * m0_ha_state_accept() is called when a m0_ha_state_set_fopt fop is received.
 *
 * @{
 */

/* import */
#include "fid/fid.h"
#include "fid/fid_xc.h"
#include "lib/chan.h"
#include "lib/types.h"
#include "rpc/rpc.h"
#include "xcode/xcode_attr.h"

/* export */
struct m0_ha_note;
struct m0_ha_nvec;

/* foward declaration */
struct m0_confc;

/** Intializes the notification interface. */
M0_INTERNAL int m0_ha_state_init(void);

/** Finalizes the notification interface. */
M0_INTERNAL void m0_ha_state_fini(void);

/**
 * Enumeration of possible object states.
 */
enum m0_ha_obj_state {
	/** Object state is unknown. */
	M0_NC_UNKNOWN,
	/** Object can be used normally. */
	M0_NC_ACTIVE,
	/**
	 * Object failed and cannot service requests. HA will notify Mero when
	 * the object is available again.
	 */
	M0_NC_FAILED,
	/**
	 * Object experienced a temporary failure. Mero should resume using the
	 * object after an implementation defined timeout.
	 */
	M0_NC_TRANSIENT,
	/**
	 * Object is in degraded mode. The meaning of this state is object type
	 * dependent.
	 */
	M0_NC_DEGRADED,
	/**
	 * Object is recovering from a failure. Object type specific protocol is
	 * used to tell users when the object has recovered.
	 */
	M0_NC_RECOVERING,
	/**
	 * Object is removed from the active service by an administrator.
	 */
	M0_NC_OFFLINE,
	/**
	 * Object is ex-communicated from the chur^H^H^Hluster. Don't talk to
	 * it.
	 */
	M0_NC_ANATHEMISED,

	M0_NC_NR
};

/**
 * Note describes (changed) object state.
 */
struct m0_ha_note {
	/** Object identifier. */
	struct m0_fid no_id;
	/** State, from enum m0_ha_obj_state. */
	uint8_t       no_state;
} M0_XCA_RECORD;

/**
 * "Note vector" describes changes in system state.
 */
struct m0_ha_nvec {
	uint32_t           nv_nr;
	struct m0_ha_note *nv_note;
} M0_XCA_SEQUENCE;

/**
 * Queries HA about the current the failure state for a set of objects.
 *
 * Constructs a m0_ha_state_get_fopt from the "note" parameter and
 * sends it to an HA instance, returning immediately after the fop is sent.
 * When the reply (m0_ha_state_get_rep_fopt) is received, fills
 * m0_ha_note::no_state from the reply and signals the provided channel.
 *
 * On error (e.g., time-out), the function signals the channel, leaving
 * m0_ha_note::no_state intact, so that the caller can determine that
 * failure state wasn't fetched.
 *
 * Use cases:
 *
 *     this function is called by a Mero instance when it joins the cluster
 *     right after it received configuration information from the confd or
 *     afterwards, when the instance wants to access an object for the first
 *     time. The caller of m0_ha_state_get() is likely to call
 *     m0_ha_state_accept() when the reply is received.
 *
 * @pre m0_forall(i, note->nv_nr, note->nv_note[i].no_state == M0_NC_UNKNOWN &&
 *                                m0_conf_fid_is_valid(&note->nv_note[i].no_id))
 */
M0_INTERNAL int m0_ha_state_get(struct m0_rpc_session *session,
				struct m0_ha_nvec *note, struct m0_chan *chan);
/**
 * Notifies HA about tentative change in the failure state for a set of
 * objects.
 *
 * Constructs a m0_ha_state_set_fopt from the "note" parameter, sends it
 * to an HA instance and returns immediately.
 * This function is used to report failures (and "unfailures") to HA.
 *
 * Use cases:
 *
 *     this function is called by a Mero instance when it detects a
 *     change in object behaviour. E.g., a timeout or increased
 *     latency of a particular service or device.
 *
 * Note that the failure state change is only tentative. It is up to HA to
 * accumulate and analyse the stream of failure notifications and to declare
 * failures. Specifically, a Mero instance should not assume that the object
 * failure state changed, unless explicitly told so by HA.
 *
 * Because failure state change is tentative, no error reporting is needed.
 *
 * @pre m0_forall(i, note->nv_nr, note->nv_note[i].no_state != M0_NC_UNKNOWN &&
 *                                m0_conf_fid_is_valid(&note->nv_note[i].no_id))
 */
M0_INTERNAL void m0_ha_state_set(struct m0_rpc_session *session,
				 struct m0_ha_nvec *note);
/**
 * Incorporates received failure state changes in the local confc cache.
 *
 * Failure states of configuration objects are received from HA (not confd),
 * but are stored in the same data-structure (conf client cache  (m0_confc)
 * consisting of configuration objects (m0_conf_obj)).
 *
 * This function updates failures states of configuration objects according to
 * "nvec".
 *
 * Use cases:
 *
 *     this function is called when a Mero instance receives a failure state
 *     update (m0_ha_state_set_fopt) from HA. This is a "push" notification
 *     mechanism (HA sends updates) as opposed to m0_ha_state_get(), where Mero
 *     "pulls" updates.
 *
 * @note: m0_conf_obj should be modified to hold HA-related state.
 * Valery Vorotyntsev (valery_vorotyntsev@xyratex.com) is the configuration
 * sub-system maintainer.
 *
 * @pre m0_forall(i, note->nv_nr, note->nv_note[i].no_state != M0_NC_UNKNOWN &&
 *                                m0_conf_fid_is_valid(&note->nv_note[i].no_id))
 */
M0_INTERNAL void m0_ha_state_accept(struct m0_confc *confc,
				    const struct m0_ha_nvec *note);

/** @} end of ha-note group */

#endif /* __MERO___HA_NOTE_H__ */


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
