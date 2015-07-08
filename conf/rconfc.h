/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Igor Vartanov
 * Original creation date: 03-Mar-2015
 */

#pragma once

#ifndef __MERO_CONF_RCONFC_H__
#define __MERO_CONF_RCONFC_H__

struct m0_rconfc;

/**
 * @page rconfc-fspec Redundant Configuration Client (rconfc)
 *
 * Redundant configuration client library -- rconfc -- provides an interface for
 * Mero applications working in cluster with multiple configuration servers
 * (confd).
 *
 * Rconfc supplements confc functionality and, transparently for confc
 * consumers, processes situations when cluster dynamically changes
 * configuration or loses connection to some of confd servers.
 *
 * - @ref rconfc-fspec-data
 * - @ref rconfc-fspec-sub
 * - @ref rconfc-fspec-routines
 * - @ref rconfc_dfspec "Detailed Functional Specification"
 *
 * <hr> <!------------------------------------------------------------>
 * @section rconfc-fspec-data Data Structures
 * - m0_rconfc --- an instance of redundant configuration client.
 *
 * m0_rconfc structure includes a dedicated m0_confc instance
 * m0_rconfc::rc_confc, which is intended for conventional configuration
 * reading.
 *
 * Beside of that, it contains two lists of confc instances, the one used for
 * communication with multiple confd servers in cluster environment during
 * configuration version election, and the other one used for keeping list of
 * discovered confd servers that run the same version that has won in the most
 * recent election.
 *
 * <hr> <!------------------------------------------------------------>
 * @section rconfc-fspec-sub Subroutines
 *
 * - m0_rconfc_init() initialises rconfc internals, including the two confc
 *   lists, and carries out configuration version election. During election all
 *   known confd servers are polled for a number of version they run. The
 *   elected version, i.e. the version having the quorum, is used in further
 *   configuration reading operation. In case quorum is reached, m0_rconfc
 *   connects the dedicated m0_rconfc::rc_confc to one of the confd servers from
 *   active list.
 *
 * - m0_rconfc_fini() finalises rconfc instance resulting in finalisation of
 *   the dedicated confc as well as dismantling internal structures served for
 *    proper reaction to outer events from cluster environment.
 *
 * - m0_rconfc_quorum_is_reached() and m0_rconfc_quorum_is_reached_lock()
 *   provide information whether quorum was successfully reached, or it failed.
 *
 * - m0_rconfc_ver_max_read() reports the maximum configuration version number
 *   the confd servers responded with during the most recent version election.
 *
 * <hr> <!------------------------------------------------------------>
 * @section rconfc-fspec-routines Initialisation, finalisation and reading data
 *
 * To access configuration data the configuration consumer is allowed to follow
 * two different scenarios:
 *
 * - reading with standalone configuration client (confc)
 * - reading with confc exposed from redundant configuration client (rconfc)
 *
 * In the first case the consumer reads data from particular confd server
 * with no regard to whether its version reached quorum or not.
 *
 * In the second case the consumer is guaranteed to read data from configuration
 * that reached the quorum in the cluster, i.e. the most recent consistent
 * one. As well, the consumer is guaranteed to be notified about the fact of
 * configuration change, to let accommodate with the change if required. In
 * addition, the ability to switch among confd servers in case of network error
 * transparently for the consumer sufficiently strengthens the reading
 * reliability, and due to this, cluster robustness in whole.
 *
 * In the latter case the consumer initialises m0_rconfc instance instead of
 * m0_confc, and performs all further reading via m0_rconfc::rc_confc.
 *
 * Example:
 *
 * @code
 * #include "conf/rconfc.h"
 * #include "conf/obj.h"
 *
 * struct m0_sm_group    *grp = ...;
 * struct m0_rpc_machine *rpcm = ...;
 * struct m0_rconfc       rconfc;
 * char                  *confd-ep-addr[] = { confd1, confd2, .. , NULL };
 *
 * void conf_exp_cb(struct m0_rconfc *rconfc)
 * {
 *         ...   clean up all local copies of configuration data    ...
 *         ... close all m0_conf_obj instances retained by consumer ...
 * }
 *
 * startup(const struct m0_fid *profile, ...)
 * {
 *         rc = m0_rconfc_init(&rconfc, confd-ep-addr,
 *                             res-mgr-addr, grp, profile,
 *                             rpcm, 0, conf_exp_cb, NULL);
 *         ...
 * }
 *
 * ... Access configuration objects, using confc interfaces. ...
 *
 * reading(...)
 * {
 *        struct m0_conf_obj *obj;
 *        struct m0_confc    *confc = &rconfc.rc_confc;
 *        int                 rc;
 *        rc = m0_confc_open_sync(&obj, confc->cc_root, ... );
 *        ... read object data ...
 *        m0_confc_close(obj);
 * }
 *
 * shutdown(...)
 * {
 *         m0_rconfc_fini(&rconfc);
 * }
 * @endcode
 *
 * @note Consumer is allowed to use any standard approach for opening
 * configuration and traversing directories in accordance with confc
 * specification. Using rconfc puts no additional limitation other than
 * stipulated by proper m0_rconfc_init().
 *
 * @see @ref rconfc_dfspec "Detailed Functional Specification"
 */

/**
 * @defgroup rconfc_dfspec Redundant Configuration Client (rconfc)
 * @brief Detailed Functional Specification.
 *
 * @see @ref rconfc-fspec "Functional Specification"
 *
 * @{
 */

/**
 * Rconfc expiration callback is called when rconfc election happens. On the
 * event came in the consumer must close all configuration objects currently
 * held and invalidate locally stored configuration data copies if any.
 *
 * @note The callback is called when rconfc is being locked, so no configuration
 * read is granted at the time. Therefore, consumer should schedule reading for
 * some time in future.
 *
 * @note The callback is called as well in the situation when reaching quorum
 * appeared impossible. Consumer is able to find out this particular detail by
 * calling m0_rconfc_quorum_is_reached(). Please notice the use of non-locking
 * version of the API due to rconfc being already in locked state as said above.
 */
typedef void (*m0_rconfc_exp_cb_t)(struct m0_rconfc *rconfc);

/**
 * Rconfc cache drained callback is called when m0_rconfc::rc_confc::cc_cache
 * has become invalid because of configuration database change detected, and
 * respectively drained due to this reason.
 *
 * @note The callback is called from rconfc instance being in locked state.
 */
typedef void (*m0_rconfc_drained_cb_t)(struct m0_rconfc *rconfc);

/**
 * Redundant configuration client.
 */
struct m0_rconfc {
	/**
	 * A dedicated confc instance initialised during m0_rconfc_init() in
	 * case the read lock is successfully acquired. Initially connected to a
	 * confd of the elected version, an element of m0_rconfc::rc_active
	 * list. Later, it may be reconnected on the fly to another confd server
	 * of the same version in case previous confd communication failed, or
	 * version re-election occurred.
	 *
	 * Consumer is expected to use m0_rconfc::rc_confc instance for reading
	 * configuration data done any standard way in accordance with m0_confc
	 * specification. However, any explicit initialisation and finalisation
	 * of the instance must be avoided, as m0_rconfc is fully responsible
	 * for that part.
	 */
	struct m0_confc           rc_confc;
	/**
	 * Rconfc expiration callback. Installed during m0_rconfc_init(), but is
	 * allowed to be re-set later if required, on a locked rconfc instance.
	 */
	m0_rconfc_exp_cb_t        rc_exp_cb;
	/**
	 * Rconfc cache drained callback. Initially unset. Allowed to be
	 * installed later if required, on a locked rconfc instance.
	 */
	m0_rconfc_drained_cb_t    rc_drained_cb;
	/**
	 * Version number the quorum was reached for. Read-only. Value
	 * M0_CONF_VER_UNKNOWN indicates that the latest version election failed
	 * or never was carried out.
	 */
	uint64_t                  rc_ver;
	/**
	 * The minimum number of confc-s that must have the same version of
	 * configuration in order for this version to be elected. Read-only.
	 */
	uint32_t                  rc_quorum;

	/* Private part. Consumer is not welcomed to access the data below. */

	/** RPC machine the rconfc to work on. */
	struct m0_rpc_machine    *rc_rmach;
	/**
	 * Gating operations. Intended to control m0_rconfc::rc_confc ability to
	 * perform configuration reading operations.
	 */
	struct m0_confc_gate_ops  rc_gops;
	/** Channel used to wait on in m0_confc_gate_ops::go_check operation. */
	struct m0_chan            rc_gate;
	/** Lock protecting rc_gate. */
	struct m0_mutex           rc_gate_guard;
	/** Asynchronous system trap for deferred cache draining. */
	struct m0_sm_ast          rc_drain_ast;
	/** Lock protecting rconfc internals during modifications. */
	struct m0_mutex           rc_lock;
	/** Group to perform asynchronous operations. */
	struct m0_sm_group       *rc_sm_group;
	/** A list of confc instances used during election. */
	struct m0_tl              rc_herd;
	/** A list of confd servers running the elected version. */
	struct m0_tl              rc_active;
	/** Wait semaphore. Signals when version election ends. */
	struct m0_semaphore       rc_ver_done;
	/** Quorum calculation context. */
	void                     *rc_qctx;
	/** Read lock context. */
	void                     *rc_rlock_ctx;
};

/**
 * Rconfc client must supply a list of confd addresses the federated conf client
 * to work on.
 *
 * Initialisation starts with election, where allocated confc instances poll
 * corresponding confd for configuration version number they currently run. At
 * the same time confd availability is tested.
 *
 * Election ends when some version has a quorum. At the same time the active
 * list is populated with rconfc_link instances which confc points to confd of
 * the newly elected version.
 *
 * When quorum value Q == 0, the real Q is calculated based on the number of
 * confd_addr elements passed in: Q = Qdefault = Nconfd / 2 + 1;
 *
 * Otherwise, the value is accepted as is, under condition Q >= Qdefault
 *
 * @note Even with unsuccessful initialisation rconfc instance requires for
 * explicit m0_rconfc_fini(). The behavior is to provide an ability to call
 * m0_rconfc_ver_max_read() even after unsuccessful version election.
 *
 * @param rconfc     - rconfc instance
 * @param confd_addr - NULL-terminated array of net addresses of confd servers
 * @param rm_addr    - address of node running Resource Manager to talk to
 * @param sm_group   - state machine group to be used with confc. The group
 *                     is used when posting m0_rconfc::rc_drain_ast as well.
 * @param rmach      - RPC machine to be used to communicate with confd
 * @param quorum     - the quorum to be reached
 * @param exp_cb     - callback, a "configuration just expired" event
 */
M0_INTERNAL int m0_rconfc_init(struct m0_rconfc      *rconfc,
			       const char           **confd_addr,
			       const char            *rm_addr,
			       struct m0_sm_group    *sm_group,
			       struct m0_rpc_machine *rmach,
			       uint32_t               quorum,
			       m0_rconfc_exp_cb_t     exp_cb);

/**
 * @todo A potential problem here is possible late herd confc responses coming
 * after m0_rconfc_init() already returned. Finalisation in this situation is
 * potentially to cause an issue. Maybe it's worth to signal somehow all replies
 * being gathered and finalise strictly not before.
 */
M0_INTERNAL void m0_rconfc_fini(struct m0_rconfc *rconfc);

/**
 * Inquiry regarding if quorum reached to the moment. The call is unprotected
 * with the regular rconfc lock, and therefore is to be called in expiration
 * callback only.
 *
 * @see m0_rconfc_exp_cb_t
 */
M0_INTERNAL bool m0_rconfc_quorum_is_reached(struct m0_rconfc *rconfc);

/**
 * Inquiry regarding if quorum reached. A locked version.
 */
M0_INTERNAL bool m0_rconfc_quorum_is_reached_lock(struct m0_rconfc *rconfc);

/**
 * Maximum version number the herd confc elements gathered from their confd
 * peers.
 *
 * @note Supposed to be called internally, e.g. by spiel during transaction
 * opening.
 */
M0_INTERNAL uint64_t m0_rconfc_ver_max_read(struct m0_rconfc *rconfc);

/**
 * Helper macro for installing a callback function onto surely locked rconfc
 * instance. Intended to be used with m0_rconfc::rc_exp_cb and
 * m0_rconfc::rc_drained_cb.
 *
 * Example:
 * @code
 * void drained_cb(struct m0_rconfc *rconfc)
 * {
 *     _my_local_conf_data_fini();
 * }
 *
 * start_something(struct m0_rconfc *rconfc)
 * {
 *     M0_RCONFC_CB_SET_LOCK(rconfc, rc_drained_cb, drained_cb);
 * ...
 * }
 * @endcode
 *
 * @note The macro cannot be used in the callbacks themselves because of their
 * execution with already locked rconfc instance.
 */
#define M0_RCONFC_CB_SET_LOCK(obj_ptr, fn_ptr, cb) \
	({ typeof((obj_ptr)->fn_ptr) _cb = cb; \
	m0_mutex_lock(&(obj_ptr)->rc_lock);    \
	(obj_ptr)->fn_ptr = _cb;               \
	m0_mutex_unlock(&(obj_ptr)->rc_lock); })

/** @} rconfc_dfspec */
#endif /* __MERO_CONF_RCONFC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
