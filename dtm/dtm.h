/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHgNOLOGY
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
 * Original creation date: 04/01/2010
 */

#pragma once

#ifndef __MERO_DTM_DTM_H__
#define __MERO_DTM_DTM_H__

#include "lib/types.h"           /* m0_uint128 */

#include "db/db.h"
#include "be/be.h"
#include "fol/fol.h"
#include "dtm/history.h"
#include "dtm/nucleus.h"
#include "dtm/catalogue.h"
#include "dtm/fol.h"

/**
 * @defgroup dtm Distributed transaction manager
 *
 * @par Overview
 *
 * Distributed transaction manager (DTM) is a major Mero
 * component, responsible for dealing with certain types of transient failures.
 *
 * Specifically, DTM maintains, as far as possible, consistency of system state
 * in the face of transient network failures (message reordering and
 * duplication) and transient node failures (node failure followed by restart).
 *
 * Consistency is maintained by grouping state updates, represented by fops
 * (@ref m0_fop), in collections called "distributed transactions". DTM
 * guarantees that transactions are atomic, in the sense that either all or none
 * of the fops are executed, even should a failure happen in the middle of
 * transaction execution.
 *
 * Atomicity is achieved by redoing or undoing some fops after a failure. This
 * is done transparently by DTM, relieving its users from the burden of failure
 * handling.
 *
 * Because different transactions can update the same entities, undo and redo of
 * one transaction might require undo or redo of other dependent
 * transactions. To fulfil this requirement, DTM tracks transaction
 * dependencies.
 *
 * It might so happen, that too much state was lost in a failure and consistency
 * can not be restored. In this case, DTM determines the subset of system
 * history that is no longer consistent and notifies HA that affected nodes
 * should be anathematised.
 *
 * @par High level design
 *
 * <a href="https://docs.google.com/a/xyratex.com/document/d/1RacseZooNnfbKiaD-s5rmTpLlpw_QlPomAX9iH4PlfI">HLD</a>
 *
 * @par Abstractions
 *
 * DTM introduces the following major abstractions:
 *
 *     - dtm instance (m0_dtm, dtm/dtm.c, dtm/dtm.h),
 *     - history (m0_dtm_history, dtm/history.c, dtm/history.h),
 *     - update (m0_dtm_update, dtm/update.c, dtm/update.h),
 *     - operation (m0_dtm_operation, dtm/operation.c, dtm/operation.h),
 *     - distributed transaction (m0_dtm_dtx, dtm/dtx.c, dtm/dtx.h),
 *     - domain (m0_dtm_domain, dtm/domain.c, dtm/domain.h).
 *
 * See the HLD for more details.
 *
 * @par Nucleus
 *
 * Nucleus is a sub-module within DTM, which contains core versioning and
 * ordering logic for histories, operations and updates. Nucleus introduces its
 * own types:
 *
 *     - nucleus m0_dtm_nu,
 *     - nucleus operation m0_dtm_op,
 *     - nucleus history m0_dtm_hi and
 *     - nucleus update m0_dtm_up
 *
 * Nuclear types are embedded in the appropriate DTM types. The rest of DTM
 * hides nucleus types and functions from DTM users.
 *
 * See dtm/nucleus.c, dtm/nucleus.h.
 *
 * @par Coding conventions
 *
 * Throughout the code, names of nuclear types, functions and variables use 2
 * letter abbreviations: @t nu (nucleus), @t hi (history), @t op (operation),
 * @t up (update).
 *
 * In the rest of DTM, longer names "history", "oper" and "update" are
 * used. "rem" is used as an abbreviation for "remote dtm instance"
 * (m0_dtm_remote).
 *
 * dtm_internal.h contains definitions and declarations that are used internally
 * by the DTM code.
 *
 * @todo Internal declarations leak in the global name-space. This should be
 * fixed by making them static and including dtm .c files in a single dtm/dtm.c
 * file.
 *
 * @par Outline of DTM usage patterns
 *
 * A typical interaction with DTM consists of the following steps:
 *
 *     - create a transaction (m0_dtm_dtx_init());
 *
 *     - create an operation (m0_dtm_oper_init());
 *
 *     - for each entity, modified by the operation, locate the history
 *       (m0_dtm_history), representing the entity;
 *
 *     - create an update and add it to the history and the operation
 *       (m0_dtm_update_init());
 *
 *     - when all updates are added to the operation, add the operation to the
 *       transaction (m0_dtm_dtx_add());
 *
 *     - create a fop, representing the update and associate it with the update;
 *
 *     - close the operation (m0_dtm_oper_close());
 *
 *     - repeat for other operations in the transaction;
 *
 *     - close the transaction (m0_dtm_dtx_close());
 *
 * @todo pack, unpack, fop.
 *
 * @par Theory of operation
 *
 * First, read the HLD.
 *
 * A history (m0_dtm_history) is a piece of system state to which updates are
 * applied sequentially. Some histories correspond to user-visible entities,
 * such as files, keys in meta-data tables, pages with file data; other
 * histories correspond to internal DTM entities, used to express grouping and
 * ordering of state updates. Examples of such internal histories are: fol
 * (m0_dtm_fol), distributed transaction (m0_dtm_dtx), local transaction
 * (m0_dtm_ltx), epoch, domain (m0_dtm_domain).
 *
 * An update to a history is m0_dtm_update. The list of updates is hanging off
 * of a history (@ref m0_dtm_history::h_hi::hi_ups), the latest update at the
 * head. An update is linked in its history list through
 * m0_dtm_update::upd_up::up_hi_linkage.
 *
 * Updates are grouped in operations (m0_dtm_oper). Updates of an operation hang
 * off of m0_dtm_oper::oprt_op::op_ups, linked through
 * m0_dtm_update::upd_up::up_op_linkage.
 *
 * Thus, updates, histories and operations form a certain gridiron pattern:
 *
 * @verbatim
 *
 *        H0        H1        H2        H3        H4        H5        H6  NOW
 *        |         |         |         |         |         |         |    |
 *        |   O0----U---------U         |         |         |         |    |
 *        |         |         |         |         |         |         |    |
 *        |         |   O1----U---------U---------U---------U---------U    |
 *        |         |         |         |         |         |         |    |
 *        |         |         |         |   O2----U         |         |    |
 *        |         |         |         |         |         |         |    |
 *  O3----U---------U---------U---------U---------U---------U---------U    |
 *        |         |         |         |         |         |         |    |
 *  O4----U---------U         |   O5----U---------U---------U---------U    |
 *        |         |         |         |         |         |         |    |
 *        |   O6----U---------U---------U         |         |         |    V
 *        |         |         |         |         |         |         |   PAST
 *
 * @endverbatim
 *
 * Note that operation groups updates for logical purposes, it doesn't imply any
 * kind of atomicity (more on operations below).
 *
 * Each update has 2 version numbers: m0_dtm_update::upd_up::up_ver and
 * m0_dtm_update::upd_up::up_orig_ver.
 *
 * Version number specifies a position in history. Version numbers are used to
 * order updates and to determine when an update is
 * applicable. m0_dtm_update::upd_up::up_orig_ver specifies the version the
 * history has before the update was applied and m0_dtm_update::upd_up::up_ver
 * specifies the version the history has after the update is applied. These 2
 * version numbers can coincide, when update doesn't change the entity,
 * represented by the history.
 *
 * Version 0 is "unknown version", see below. Version 1 is the version of the
 * history after the first (earliest) update in the history has been executed.
 *
 * The following 3 complementary sub-sections describe organization of DTM
 * structures from the point of view of update, history and operation
 * respectively.
 *
 * @par Update
 *
 * Each update has a state: m0_dtm_update::upd_up::up_state.
 *
 * When going through updates of a history pastward (that is, starting from the
 * head of the history update list, downward in the diagram above), version
 * numbers are non-increasing and states are non-decreasing.
 *
 * Update state determines the execution status of the update. Update state
 * increases throughout update life. Hence, update states describe update's life
 * stages. Updates states are the following (m0_dtm_state):
 *
 *     - LIMBO: a new update is initialised by m0_dtm_update_init(). From the
 *       very beginning the update is associated with an operation and a
 *       history. m0_dtm_init() places the update on the operation's list
 *       updates, but not on the history's list of updates. The update starts in
 *       LIMBO state and remains in this stats until the operation, to which the
 *       update belongs is closed. In other words, LIMBO is the state of updates
 *       belonging to still not closed operations.
 *
 *       LIMBO is the only state, in which the update is not on its history
 *       update list. When an operation is closed, m0_dtm_op_close() places all
 *       its updates on the corresponding history lists.
 *
 *     - FUTURE: this is the state of updates from closed operations that are
 *	 not yet executed. Operation can be in FUTURE state for several reasons:
 *
 *           - it is too early, and some previous operations, as determined by
 *             version numbers, are not yet added. This is possible when network
 *             reorders operations arriving to a server;
 *
 *           - histories of some of operation's updates are busy (see PREPARE
 *             state below);
 *
 *     - PREPARE: when all updates of an operation are ready (versionwise) to be
 *       executed, they are moved from FUTURE to PREPARE state and
 *       m0_dtm_op_ops::doo_ready() call-back is called by the DTM.
 *
 *       For a given history, at most one update of the history can be in
 *       PREPARE state. When an update is moved in PREPARE state, its history is
 *       marked busy (M0_DHF_BUSY) preventing other otherwise ready updates of
 *       this history to advance past FUTURE state.
 *
 *       The purpose of PREPARE state is to allow DTM user to serialise
 *       operation execution as necessary, e.g., take locks. When all the locks
 *       necessary for the operation execution are taken, the user calls
 *       m0_dtm_op_prepared(), which moves all operation updates in INPROGRESS
 *       state.
 *
 *     - INPROGRESS: an update in INPROGRESS state is being executed. When an
 *       update transitions from PREPARED to INPROGRESS state, history's version
 *       (m0_dtm_hi::hi_ver) is set to update's version, implying that as far as
 *       DTM is concerned, the state of the entity represented by the history
 *       contains the update. Because update execution is never truly
 *       instantaneous, the actual entity state is changed gradually, while the
 *       update is in INPROGRESS state, but thanks to the locks taken by the
 *       user in PREPARE state, this doesn't matter.
 *
 *     - VOLATILE: an update moves to VOLATILE state when its execution
 *       completes and modified entity state is present only in volatile store
 *       and can be lost in an allowed failure.
 *
 *     - PERSISTENT: an update moves to PERSISTENT state, when modified entity
 *       state makes it to persistent store, which is guaranteed to survive any
 *       allowed failure.
 *
 *     - STABLE: finally, an update moves to STABLE state, when DTM guarantees
 *       that it will survive any further allowed failure. STABLE state is
 *       different from PERSISTENT, because even if a particular update U is
 *       persistent, some earlier updates on which U depends can be not yet
 *       persistent. If such earlier updates are lost in a failure, DTM must
 *       undo U to preserve consistency.
 *
 * @par Update
 *
 * Each update has a a state (m0_dtm_update::upd_up::up_state) and 2 version
 * numbers: m0_dtm_update::upd_up::up_ver and
 * m0_dtm_update::upd_up::up_orig_ver.
 *
 * Version number specifies a position in history. Version numbers are used to
 * order updates and to determine when an update is
 * applicable. m0_dtm_update::upd_up::up_orig_ver specifies the version the
 * history has before the update was applied and m0_dtm_update::upd_up::up_ver
 * specifies the version the history has after the update is applied. These 2
 * version numbers can coincide, when update doesn't change the entity,
 * represented by the history.
 *
 * When going through updates of a history pastward (that is, starting from the
 * head of the history update list, downward in the diagram above), version
 * numbers are non-increasing and states are non-decreasing.
 *
 * Update state determines the execution status of the update. Update state
 * increases throughout update life. Hence, update states describe update's life
 * stages. Updates states are the following (m0_dtm_state):
 *
 *     - LIMBO: a new update is initialised by m0_dtm_update_init(). From the
 *       very beginning the update is associated with an operation and a
 *       history. m0_dtm_init() places the update on the operation's list
 *       updates, but not on the history's list of updates. The update starts in
 *       LIMBO state and remains in this stats until the operation, to which the
 *       update belongs is closed. In other words, LIMBO is the state of updates
 *       belonging to still not closed operations.
 *
 *       LIMBO is the only state, in which the update is not on its history
 *       update list. When an operation is closed, m0_dtm_op_close() places all
 *       its updates on the corresponding history lists.
 *
 *     - FUTURE: this is the state of updates from closed operations that are
 *	 not yet executed. Operation can be in FUTURE state for several reasons:
 *
 *           - it is too early, and some previous operations, as determined by
 *             version numbers, are not yet added. This is possible when network
 *             reorders operations arriving to a server;
 *
 *           - histories of some of operation's updates are busy (see PREPARE
 *             state below);
 *
 *     - PREPARE: when all updates of an operation are ready (versionwise) to be
 *       executed, they are moved from FUTURE to PREPARE state and
 *       m0_dtm_op_ops::doo_ready() call-back is called by the DTM.
 *
 *       For a given history, at most one update of the history can be in
 *       PREPARE state. When an update is moved in PREPARE state, its history is
 *       marked busy (M0_DHF_BUSY) preventing other otherwise ready updates of
 *       this history to advance past FUTURE state.
 *
 *       The purpose of PREPARE state is to allow DTM user to serialise
 *       operation execution as necessary, e.g., take locks. When all the locks
 *       necessary for the operation execution are taken, the user calls
 *       m0_dtm_op_prepared(), which moves all operation updates in INPROGRESS
 *       state.
 *
 *     - INPROGRESS: an update in INPROGRESS state is being executed. When an
 *       update transitions from PREPARED to INPROGRESS state, history's version
 *       (m0_dtm_hi::hi_ver) is set to update's version, implying that as far as
 *       DTM is concerned, the state of the entity represented by the history
 *       contains the update. Because update execution is never truly
 *       instantaneous, the actual entity state is changed gradually, while the
 *       update is in INPROGRESS state, but thanks to the locks taken by the
 *       user in PREPARE state, this doesn't matter.
 *
 *     - VOLATILE: an update moves to VOLATILE state when its execution
 *       completes, but modified entity state is present only in volatile store
 *       and can be lost in an allowed failure.
 *
 *     - PERSISTENT: an update moves to PERSISTENT state, when modified entity
 *       state makes it to persistent store, which is guaranteed to survive any
 *       allowed failure.
 *
 *     - STABLE: finally, an update moves to STABLE state, when DTM guarantees
 *       that it will survive any further allowed failure. STABLE state is
 *       different from PERSISTENT, because even if a particular update U is
 *       persistent, some earlier updates on which U depends can be not yet
 *       persistent. If such earlier updates are lost in a failure, DTM must
 *       undo U to preserve consistency.
 *
 * @par History
 *
 * History represent evolution of a storage entity. Storage entity changes as
 * result of update executions. Version number unambiguously identifies a point
 * in the history.
 *
 * History lists both already executed updates and still not executed
 * updates. m0_dtm_hi::hi_ver identifies the point in history corresponding to
 * the current state of the entity. All updates with version numbers less than
 * m0_dtm_hi::hi_ver have been executed and none of the updates with version
 * numbers greater than m0_dtm_hi::hi_ver executed. There can be multiple
 * updates with version numbers equal to m0_dtm_hi::hi_ver, all but one of them
 * must be read-only (because they do not change history version, see above), at
 * least one of such updates has been executed.
 *
 * History is "owned" (M0_DHF_OWNED) when the local DTM instance has the right
 * to assign version numbers in the history. This is possible when, for example,
 * local Mero instance owns an exclusive lock on the entity represented by the
 * history.
 *
 * Originally, when an update is added to an owned history, its version can be
 * either unknown (0) or known (non 0). When an update is added to a non-owned
 * history, its version must be known, its original version can be either known
 * or unknown.
 *
 * By the time update reaches VOLATILE state, both its version and original
 * versions are known. This is achieved as follows:
 *
 *     - if original version is initially unknown, the original version is
 *       assigned (up_ready()) to the current version history when the update is
 *       moved from FUTURE to PREPARE state;
 *
 *     - if update version is initially unknown,
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 * @par Concurrency
 *
 * @par Ownership
 *
 * @{
 */

/* import */
struct m0_dtm_history_type;

/* export */
struct m0_dtm;
struct m0_dtx;

enum {
	/**
	 * Maximal number of history types (m0_dtm_history_type).
	 */
	M0_DTM_HISTORY_TYPE_NR = 256
};

/**
 * DTM instance is a container for histories, operations and updates.
 *
 * To use DTM, one needs a "local" DTM instance. This instance communicates with
 * other DTM instances, which are "remote". A remote instance is represented by
 * m0_dtm_remote.
 *
 * Each DTM instance has a globally unique identifier m0_dtm::d_id. This
 * identifier is also used as the identifier of instance's fol.
 */
struct m0_dtm {
	/**
	 * Nucleus of this DTM instance.
	 */
	struct m0_dtm_nu                  d_nu;
	/**
	 * Identifier of this DTM instance.
	 *
	 * This is globally unique. The identifier is assigned to the instance,
	 * when DTM is initialised.
	 */
	struct m0_uint128                 d_id;
	struct m0_dtm_catalogue           d_cat[M0_DTM_HISTORY_TYPE_NR];
	struct m0_dtm_fol                 d_fol;
	struct m0_tl                      d_excited;
	const struct m0_dtm_history_type *d_htype[M0_DTM_HISTORY_TYPE_NR];
};

struct m0_dtx {
	/**
	   @todo placeholder for now.
	 */
	enum m0_dtx_state      tx_state;
	struct m0_db_tx        tx_dbtx;
	struct m0_be_tx        tx_betx;
	struct m0_be_tx_credit tx_betx_cred;
	struct m0_fol_rec      tx_fol_rec;
};

M0_INTERNAL void m0_dtx_init(struct m0_dtx *tx,
			     struct m0_be_domain *be_domain,
			     struct m0_sm_group  *sm_group);
M0_INTERNAL void m0_dtx_open(struct m0_dtx *tx);
M0_INTERNAL void m0_dtx_done(struct m0_dtx *tx);
M0_INTERNAL int m0_dtx_open_sync(struct m0_dtx *tx);
M0_INTERNAL int m0_dtx_done_sync(struct m0_dtx *tx);
M0_INTERNAL void m0_dtx_fini(struct m0_dtx *tx);

M0_INTERNAL void m0_dtm_init(struct m0_dtm *dtm, struct m0_uint128 *id);
M0_INTERNAL void m0_dtm_fini(struct m0_dtm *dtm);

M0_INTERNAL int  m0_dtm_global_init(void);
M0_INTERNAL void m0_dtm_global_fini(void);

/** @} end of dtm group */
#endif /* __MERO_DTM_DTM_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
