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

#include "lib/types.h"
#include "lib/thread.h"
#include "sm/sm.h"

#include "be/ut/helper.h"	/* XXX */

struct m0_be_domain;
struct m0_be_domain_cfg;

/**
   @addtogroup db Data-base interfaces.

   @{
 */

enum {
	DB_TXN_NOWAIT = 0,
};

/** be part of generic m0_dbenv. */
struct m0_dbenv_impl {
        /** Underlying domain on which db implementation is being built */
        struct m0_be_domain *d_dom;
	struct m0_be_seg    *d_seg;
	struct m0_be_ut_backend	d_ut_be;
	struct m0_be_ut_seg	d_ut_seg;
};

struct m0_table_impl {
        struct m0_be_btree *i_tree;

};

struct m0_db_buf_impl {
        struct m0_buf db_dbt;

};

struct m0_db_tx_impl {
        struct m0_be_tx *dt_txn;
	struct m0_be_tx dt_tx;
	struct m0_sm_ast dt_ast;
	struct m0_semaphore dt_commit_sem;
	struct m0_be_ut_backend *dt_ut_be;
};

struct m0_db_tx_waiter_impl {
};

struct m0_db_cursor_impl {
        struct m0_be_btree_cursor *c_i;
	bool c_after_delete;
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
