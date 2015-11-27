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
 * Original author: Egor Nikulenkov
 * Original creation date: 03-Mar-2015
 */

#pragma once

#ifndef __MERO_CONF_RCONFC_H__
#define __MERO_CONF_RCONFC_H__

#include "lib/mutex.h"
#include "lib/chan.h"
#include "lib/tlist.h"
#include "fop/fop.h"    /* m0_fop */
#include "sm/sm.h"
#include "conf/confc.h"

struct m0_rconfc;
struct m0_rpc_machine;
struct m0_rpc_item;

/**
 * @page rconfc-fspec Redundant Configuration Client (rconfc)
 *
 * Redundant configuration client library -- rconfc -- provides an interface for
 * Mero applications working in cluster with multiple configuration servers
 * (confd).
 *
 * Rconfc supplements confc functionality and processes situations when cluster
 * dynamically changes configuration or loses connection to some of confd
 * servers.
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
 * - m0_rconfc_init() initialises rconfc internals.
 *
 * - m0_rconfc_start() carries out configuration version election. During
 *   election all known confd servers are polled for a number of version they
 *   run. The elected version, i.e. the version having the quorum, is used in
 *   further configuration reading operation. In case quorum is reached,
 *   m0_rconfc connects the dedicated m0_rconfc::rc_confc to one of the confd
 *   servers from active list.
 *
 * - m0_rconfc_start_sync() is synchronous version of m0_rconfc_start().
 *
 * - m0_rconfc_stop() disconnects dedicated confc and release other internal
 *   resources.
 *
 * - m0_rconfc_stop_sync() is synchronous version of m0_rconfc_stop().
 *
 * - m0_rconfc_fini() finalises rconfc instance.
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
 * In the latter case the consumer initialises and starts m0_rconfc instance
 * instead of m0_confc, and performs all further reading via
 * m0_rconfc::rc_confc.
 *
 * Also, the consumer using rconfc doesn't provide any confd addresses
 * explicitly. The list of confd servers and other related information is
 * centralised and is maintained by HA service. Rconfc queries this information
 * on startup using global HA session (via m0_ha_session_get()). Active global
 * HA session is a prerequisite for rconfc initialisation. It is assumed that HA
 * session is established to local HA agent and HA agent endpoint uniquely
 * identifies mero cluster configuration.
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
 *
 * void conf_exp_cb(struct m0_rconfc *rconfc)
 * {
 *         ...   clean up all local copies of configuration data    ...
 *         ... close all m0_conf_obj instances retained by consumer ...
 * }
 *
 * startup(const struct m0_fid *profile, ...)
 * {
 *         rc = m0_rconfc_init(&rconfc, grp, rpcm, conf_exp_cb);
 *         if (rc == 0) {
 *              rc = m0_rconfc_start_sync(&rconfc);
 *              if (rc != 0)
 *                      m0_rconfc_stop_sync(&rconfc);
 *         }
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
 *         m0_rconfc_stop_sync(&rconfc);
 *         m0_rconfc_fini(&rconfc);
 * }
 * @endcode
 *
 * For asynchronous rconfc start/stop please see details of @ref
 * m0_rconfc_start_sync, @ref m0_rconfc_stop_sync implementation or
 * documentation for @ref m0_rconfc_start, @ref m0_rconfc_stop.
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

enum m0_rconfc_state {
	RCS_INIT,
	RCS_ENTRYPOINT_GET,
	RCS_ENTRYPOINT_REPLIED,
	RCS_GET_RLOCK,
	RCS_VERSION_ELECT,
	RCS_IDLE,
	RCS_RLOCK_CONFLICT,
	RCS_CONDUCTOR_DRAIN,
	RCS_STOPPING,
	RCS_FAILURE,
	RCS_FINAL
};

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
 * checking rconfc->rc_ver against M0_CONF_VER_UNKNOWN.
 *
 * @see m0_rconfc_exp_cb_set
 */
typedef void (*m0_rconfc_exp_cb_t)(struct m0_rconfc *rconfc);

/**
 * Rconfc cache drained callback is called when m0_rconfc::rc_confc::cc_cache
 * has become invalid because of configuration database change detected, and
 * respectively drained due to this reason.
 *
 * @note The callback is called from rconfc instance being in locked state.
 *
 * @see m0_rconfc_drain_cb_set
 */
typedef void (*m0_rconfc_drained_cb_t)(struct m0_rconfc *rconfc);

/**
 * Redundant configuration client.
 */
struct m0_rconfc {
	/**
	 * A dedicated confc instance initialised during m0_rconfc_start() in
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
	/** Rconfc state machine */
	struct m0_sm              rc_sm;
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

	/** RPC machine the rconfc to work on. */
	struct m0_rpc_machine    *rc_rmach;
	/**
	 * Gating operations. Intended to control m0_rconfc::rc_confc ability to
	 * perform configuration reading operations.
	 */
	struct m0_confc_gate_ops  rc_gops;
	/** AST to run functions in context of rconfc sm group. */
	struct m0_sm_ast          rc_ast;
	/** Additional data to be used inside AST (i.e. failure error code). */
	int                       rc_datum;
	/** AST to be posted when user requests rconfc to stop. */
	struct m0_sm_ast          rc_stop_ast;
	/** A list of confc instances used during election. */
	struct m0_tl              rc_herd;
	/** A list of confd servers running the elected version. */
	struct m0_tl              rc_active;
	/** Clink to track unpinned conf objects during confc cache drop */
	struct m0_clink           rc_unpinned_cl;
	/** Quorum calculation context. */
	void                     *rc_qctx;
	/** Read lock context. */
	void                     *rc_rlock_ctx;
	/** Indicates whether user requests rconfc stopping. */
	bool                      rc_stopping;
	/**
	 * Indicates whether read lock conflict was observed and
	 * should be processed once rconfc is idle.
	 */
	bool                      rc_rlock_conflict;
	/** FOP is used to retrieve cluster entry point from HA. */
	struct m0_fop             rc_entrypoint_fop;
	/** Reply from HA with cluster entry point. */
	struct m0_rpc_item       *rc_entrypoint_reply;
	/**
	 * Confc instance artificially filled with objects having fids of
	 * current confd and top-level RM services got from HA. Artificial
	 * nature is because of filling its cache non-standard way at the time
	 * when conventional configuration reading is impossible due to required
	 * conf version number remaining unknown until election procedure is
	 * done.
	 *
	 * This instance is added to HA clients list and serves HA notifications
	 * about remote confd and RM service deaths. The intention is to
	 * automatically keep herd list and top-level RM endpoint address up to
	 * date during the entire rconfc instance life cycle.
	 *
	 * @attention Never use this confc for real configuration reading. No
	 *            RPC communication is possible with the instance due to the
	 *            way of its initialisation.
	 */
	struct m0_confc           rc_phony;
};

/**
 * Initialise redundant configuration client instance.
 *
 * If initialisation fails, then finalisation is done internally. No explicit
 * m0_rconfc_fini() is needed.
 *
 * @param rconfc     - rconfc instance
 * @param sm_group   - state machine group to be used with confc.
 *                     Opening conf objects later in context of this SM group is
 *                     prohibited, so providing locality SM group is a
 *                     bad choice. Use locality0 (@ref m0_locality0_get) or
 *                     some dedicated SM group.
 * @param rmach      - RPC machine to be used to communicate with confd
 * @param exp_cb     - callback, a "configuration just expired" event
 */
M0_INTERNAL int m0_rconfc_init(struct m0_rconfc      *rconfc,
			       struct m0_sm_group    *sm_group,
			       struct m0_rpc_machine *rmach,
			       m0_rconfc_exp_cb_t     exp_cb);

/**
 * Rconfc starts with obtaining all necessary information (cluster "entry
 * point") from HA service. Global HA session is used (m0_ha_session_get()), so
 * it should be set-up before rconfc start.
 *
 * Rconfc continues with election, where allocated confc instances poll
 * corresponding confd for configuration version number they currently run. At
 * the same time confd availability is tested.
 *
 * Election ends when some version has a quorum. At the same time the active
 * list is populated with rconfc_link instances which confc points to confd of
 * the newly elected version.
 *
 * Function is asynchronous, user can wait on rconfc->rc_sm.sm_chan until
 * rconfc->rc_sm.sm_state in (RCS_IDLE, RCS_FAILURE). RCS_FAILURE state means
 * that start failed, return code can be obtained from rconfc->rc_sm.sm_rc.
 *
 * @note Even with unsuccessful startup rconfc instance requires for
 * explicit m0_rconfc_stop(). The behavior is to provide an ability to call
 * m0_rconfc_ver_max_read() even after unsuccessful version election.
 */
M0_INTERNAL void m0_rconfc_start(struct m0_rconfc *rconfc);

/**
 * Synchronous version of @ref m0_rconfc_start.
 */
M0_INTERNAL int m0_rconfc_start_sync(struct m0_rconfc *rconfc);

/**
 * Finalises dedicated m0_rconfc::rc_confc instance and puts all the acquired
 * resources back.
 *
 * Function is asynchronous, user should wait on rconfc->rc_sm.sm_chan until
 * rconfc->rc_sm.sm_state is RCS_FINAL.
 *
 * @note User is not allowed to call @ref m0_rconfc_start again on stopped
 * rconfc instance as well as other API. The only calls allowed with stopped
 * instance are @ref m0_rconfc_ver_max_read and @ref m0_rconfc_fini.
 */
M0_INTERNAL void m0_rconfc_stop(struct m0_rconfc *rconfc);

/**
 * Synchronous version of @ref m0_rconfc_stop.
 */
M0_INTERNAL void m0_rconfc_stop_sync(struct m0_rconfc *rconfc);

/**
 * Finalises rconfc instance.
 */
M0_INTERNAL void m0_rconfc_fini(struct m0_rconfc *rconfc);

M0_INTERNAL void m0_rconfc_lock(struct m0_rconfc *rconfc);
M0_INTERNAL void m0_rconfc_unlock(struct m0_rconfc *rconfc);

/**
 * Maximum version number the herd confc elements gathered from their confd
 * peers.
 *
 * @note Supposed to be called internally, e.g. by spiel during transaction
 * opening.
 */
M0_INTERNAL uint64_t m0_rconfc_ver_max_read(struct m0_rconfc *rconfc);

/**
 * Set expiration callback.
 *
 * @note The alternative to providing expiration callback is detecting rconfc
 * RCS_CONDUCTOR_DRAIN state. Once the consumer observes the rconfc instance in
 * this state, the consumer must close all opened configuration objects and
 * invalidate local copies of configuration objects.
 *
 * @pre rconfc is locked.
 */
M0_INTERNAL void m0_rconfc_exp_cb_set(struct m0_rconfc   *rconfc,
				      m0_rconfc_exp_cb_t  cb);

/**
 * Set drain callback.
 *
 * @pre rconfc is locked.
 */
M0_INTERNAL void m0_rconfc_drained_cb_set(struct m0_rconfc       *rconfc,
					  m0_rconfc_drained_cb_t  cb);

/**
 * Allocates and fills eps with confd endpoints from m0_rconfc::rc_herd list.
 * Returns number of endpoints or -ENOMEM if memory allocation was failed during
 * duplication of an endpoint.
 */
M0_INTERNAL int m0_rconfc_confd_endpoints(struct m0_rconfc   *rconfc,
					  const char       ***eps);

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
