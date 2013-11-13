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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 17-Jul-2013
 */


#pragma once

#ifndef __MERO_BE_ENGINE_H__
#define __MERO_BE_ENGINE_H__

#include "lib/types.h"		/* bool */
#include "lib/mutex.h"		/* m0_mutex */
#include "lib/tlist.h"		/* m0_tl */

#include "be/log.h"		/* m0_be_log */
#include "be/tx.h"		/* m0_be_tx */
#include "be/tx_credit.h"	/* m0_be_tx_credit */

struct m0_be_tx_group;
struct m0_reqh;
struct m0_stob;

/**
 * @defgroup be
 *
 * @{
 */

/* m0_be_engine group state */
enum {
	M0_BEG_OPEN,
	M0_BEG_CLOSED,
	M0_BEG_NR,
};

struct m0_be_engine_cfg {
	size_t		       bec_group_nr;
	size_t		       bec_log_size;
	struct m0_be_tx_credit bec_tx_size_max;
	struct m0_be_tx_credit bec_group_size_max;
	size_t		       bec_group_tx_max;
	struct m0_reqh	      *bec_group_fom_reqh;
	bool		       bec_log_replay;
	struct m0_stob	      *bec_log_stob;
};

struct m0_be_engine {
	struct m0_be_engine_cfg	  *eng_cfg;
	/** Protects all fields of this struct. */
	struct m0_mutex		   eng_lock;
	/**
	 * Per-state lists of transaction. Each non-failed transaction is in one
	 * of these lists.
	 */
	struct m0_tl		   eng_txs[M0_BTS_NR + 1];
	struct m0_tl		   eng_groups[M0_BEG_NR];
	/** Transactional log. */
	struct m0_be_log	   eng_log;
	/** Transactional group. */
	struct m0_be_tx_group	  *eng_group;
	size_t			   eng_group_nr;
	/**
	 * XXX Indicator for one group.
	 * Remove it when add multiple groups support.
	 */
	bool			   eng_group_closed;
};

M0_INTERNAL bool m0_be_engine__invariant(struct m0_be_engine *en);

M0_INTERNAL int m0_be_engine_init(struct m0_be_engine *en,
				  struct m0_be_engine_cfg *en_cfg);
M0_INTERNAL void m0_be_engine_fini(struct m0_be_engine *en);

M0_INTERNAL void m0_be_engine_start(struct m0_be_engine *en);
M0_INTERNAL void m0_be_engine_stop(struct m0_be_engine *en);

/* next functions should be called from m0_be_tx implementation */
M0_INTERNAL void m0_be_engine__tx_init(struct m0_be_engine *en,
				       struct m0_be_tx *tx,
				       enum m0_be_tx_state state);

M0_INTERNAL void m0_be_engine__tx_fini(struct m0_be_engine *en,
				       struct m0_be_tx *tx);

M0_INTERNAL void m0_be_engine__tx_state_set(struct m0_be_engine *en,
					    struct m0_be_tx *tx,
					    enum m0_be_tx_state state);

M0_INTERNAL void m0_be_engine__tx_group_open(struct m0_be_engine *en,
					     struct m0_be_tx_group *gr);

/** @} end of be group */
#endif /* __MERO_BE_ENGINE_H__ */

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
