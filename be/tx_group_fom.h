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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>,
 *                  Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 17-Jun-2013
 */

#pragma once
#ifndef __MERO_BE_TX_GROUP_FOM_H__
#define __MERO_BE_TX_GROUP_FOM_H__

#include "lib/types.h"		/* bool */
#include "lib/refs.h"		/* m0_ref */

#include "fop/fom.h"		/* m0_fom */

#include "be/op.h"

struct m0_be_engine;
struct m0_be_tx_group;
struct m0_reqh;

/**
 * @defgroup be
 * @{
 */

struct m0_be_tx_group_fom {
	/** generic fom */
	struct m0_fom	       tgf_gen;
	struct m0_reqh	      *tgf_reqh;
	/** group to handle */
	struct m0_be_tx_group *tgf_group;
	struct m0_fom_timeout  tgf_to;
	m0_time_t              tgf_close_abs_timeout;
	/** m0_be_op for I/O operations */
	struct m0_be_op	       tgf_op;
	/**
	 * The number of transactions that have been added to the tx_group
	 * but have not switched to M0_BTS_GROUPED state yet.
	 */
	struct m0_ref          tgf_nr_ungrouped;
	/**
	 * True iff all transactions of the group have reached M0_BTS_DONE
	 * state.
	 */
	bool                   tgf_stable;
	bool		       tgf_stopping;
	struct m0_sm_ast       tgf_ast_handle;
	struct m0_sm_ast       tgf_ast_stable;
	struct m0_sm_ast       tgf_ast_stop;
	struct m0_sm_ast       tgf_ast_timeout;
	struct m0_semaphore    tgf_start_sem;
	struct m0_semaphore    tgf_finish_sem;
};

/** @todo XXX TODO s/gf/m/ in function parameters */
M0_INTERNAL void m0_be_tx_group_fom_init(struct m0_be_tx_group_fom *m,
					 struct m0_be_tx_group *gr,
					 struct m0_reqh *reqh);
M0_INTERNAL void m0_be_tx_group_fom_fini(struct m0_be_tx_group_fom *m);
M0_INTERNAL void m0_be_tx_group_fom_reset(struct m0_be_tx_group_fom *m);

M0_INTERNAL int m0_be_tx_group_fom_start(struct m0_be_tx_group_fom *gf);
M0_INTERNAL void m0_be_tx_group_fom_stop(struct m0_be_tx_group_fom *gf);

M0_INTERNAL void m0_be_tx_group_fom_handle(struct m0_be_tx_group_fom *m,
					   m0_time_t abs_timeout);
M0_INTERNAL void m0_be_tx_group_fom_stable(struct m0_be_tx_group_fom *gf);

M0_INTERNAL void m0_be_tx_group_fom_mod_init(void);
M0_INTERNAL void m0_be_tx_group_fom_mod_fini(void);

/** @} end of be group */
#endif /* __MERO_BE_TX_GROUP_FOM_H__ */

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
