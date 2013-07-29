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
   @defgroup dtm Distributed transaction manager

   @par Overview

   Distributed transaction manager (DTM) is a major Mero
   component, responsible for dealing with certain types of transient failures.

   Specifically, DTM maintains, as far as possible, consistency of system state
   in the face of transient network failures (message reordering and
   duplication) and transient node failures (node failure followed by restart).

   Consistency is maintained by grouping state updates, represented by fops
   (@ref m0_fop), in collections called "distributed transactions". DTM
   guarantees that transactions are atomic, in the sense that either all or none
   of the fops are executed, even should a failure happen in the middle of
   transaction execution.

   Atomicity is achieved by redoing or undoing some fops after a failure. This
   is done transparently by DTM, relieving its users from the burden of failure
   handling.

   Because different transactions can update the same objects, undo and redo of
   one transaction might require undo or redo of other dependent
   transactions. To fulfil this requirement, DTM tracks transaction
   dependencies.

   It might so happen, that too much state was lost in a failure and consistency
   can not be restored. In this case, DTM determines the subset of system
   history that is no longer consistent and notifies HA that affected nodes
   should be anathematised.

   @par High level design

   <a href="https://docs.google.com/a/xyratex.com/document/d/1RacseZooNnfbKiaD-s5rmTpLlpw_QlPomAX9iH4PlfI">HLD</a>

   @par Abstractions

   DTM introduces the following major abstractions:

       - dtm instance (m0_dtm, dtm.c, dtm.h),
       - history (m0_dtm_history, history.c, history.h),
       - update (m0_dtm_update, update.c, update.h),
       - operation (m0_dtm_operation, operation.c, operation.h),
       - distributed transaction (m0_dtm_dtx, dtx.c, dtx.h),
       - domain (m0_dtm_domain, domain.c, domain.h).

   See the HLD for more details.

   @par Nucleus

   Nucleus is a sub-module within DTM, which contains core versioning and
   ordering logic for histories, operations and updates. Nucleus introduces its
   own types:

       - nucleus m0_dtm_nu,
       - nucleus operation m0_dtm_op,
       - nucleus history m0_dtm_hi and
       - nucleus update m0_dtm_up

   Nuclear types are embedded in the appropriate DTM types. The rest of DTM
   hides nucleus types and functions from DTM users.

   See nucleus.c, nucleus.h.

   @par Coding conventions

   Throughout the code, names of nuclear types, functions and variables use 2
   letter abbreviations: @t nu (nucleus), @t hi (history), @t op (operation),
   @t up (update).

   In the rest of DTM, longer names "history", "oper" and "update" are
   used. "rem" is used as an abbreviation for "remote dtm instance".

   dtm_internal.h contains definitions and declarations that are used internally
   by the DTM code.

   @todo Internal declarations leak in the global name-space. This should be
   fixed by making them static and including dtm .c files in a single dtm/dtm.c
   file.

   @par Outline of DTM usage patterns

   A typical interaction with DTM consists of the following steps:

       - creates a transaction (m0_dtm_dtx_init());

       - create an operation (m0_dtm_oper_init());

       - for each object, modified by the operation, locate the history
         (m0_dtm_history), representing the object;

       - create an update and add it to the history and the operation
         (m0_dtm_update_init());

       - when all updates are added to the operation, add the operation to the
         transaction (m0_dtm_dtx_add());

       - create a fop, representing the update and associate it with the update;

       - close the operation (m0_dtm_oper_close());

       - repeat for other operations in the transaction;

       - close the transaction (m0_dtm_dtx_close());

   @todo pack, unpack, fop.

   @par Theory of operation

   First, read the HLD.



   @par Concurrency

   @par Ownership

   @{
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
