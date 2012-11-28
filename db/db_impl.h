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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/13/2010
 */

#pragma once

#ifndef __MERO_DB_DB_IMPL_H__
#define __MERO_DB_DB_IMPL_H__

#include <db.h>

#include "lib/types.h"
#include "lib/thread.h"
#include "lib/tlist.h"
#include "lib/mutex.h"
#include "lib/cond.h"

/**
   @addtogroup db Data-base interfaces.

   @{
 */

/**
   db5-specific part of generic m0_dbenv.

   Most fields are only updated when the environment is set up. Others (as noted
   below) are protected by m0_dbenv_impl::d_lock.
 */
struct m0_dbenv_impl {
	/** db5 private handle */
	DB_ENV            *d_env;
	/** File stream where error messages for this dbenv are sent to. */
	FILE              *d_errlog;
	/** File stream where informational messages for this dbenv are sent
	    to. */
	FILE              *d_msglog;
	/** Log cursor used to determine the current LSN. */
	DB_LOGC           *d_logc;
	/** Lock protecting waiters list. */
	struct m0_mutex    d_lock;
	/** A list of waiters (m0_db_tx_waiter). Protected by
	    m0_dbenv_impl::d_lock.  */
	struct m0_tl       d_waiters;
	/** Thread for asynchronous environment related work. */
	struct m0_thread   d_thread;
	/** True iff the environment is being shut down. Protected by
	    m0_dbenv_impl::d_lock.*/
	bool               d_shutdown;
	/** Condition variable signalled on shutdown. Signalled under
	    m0_dbenv_impl::d_lock.*/
	struct m0_cond     d_shutdown_cond;
};

struct m0_table_impl {
	/** db5 private table handle. */
	DB              *t_db;
	struct m0_mutex  t_lock;
};

struct m0_db_buf_impl {
	DBT db_dbt;
};

struct m0_db_tx_impl {
	/** A db5 private transaction handle. */
	DB_TXN *dt_txn;
};

struct m0_db_tx_waiter_impl {
	/** An lsn from the transaction this wait is for. */
	DB_LSN tw_lsn;
};

struct m0_db_cursor_impl {
	DBC *c_dbc;
};

/** @} end of db group */

/* __MERO_DB_REC_H__ */
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
