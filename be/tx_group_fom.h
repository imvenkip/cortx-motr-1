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

struct m0_be_tx_engine;
struct m0_be_tx_group;
struct m0_reqh;

/**
 * @defgroup be
 * @{
 */

struct m0_be_tx_group_fom {
	/* this field has to be first in structure */
	struct m0_fom           tgf_gen;
	struct m0_be_tx_engine *tgf_engine;
	bool                    tgf_full;
	bool                    tgf_expired;
	bool                    tgf_stopping;
	struct m0_fom_timeout   tgf_to;
	struct m0_be_op         tgf_op;
	/**
	 * The number of transactions that have been added to the tx_group
	 * but have not switched to M0_BTS_GROUPED state yet.
	 */
	struct m0_ref           tgf_nr_ungrouped;
	struct m0_semaphore     tgf_started;
};

M0_INTERNAL int m0_be_tx_group_fom_init(struct m0_be_tx_group_fom *gf);
M0_INTERNAL void m0_be_tx_group_fom_fini(struct m0_be_tx_group_fom *gf);
M0_INTERNAL void m0_be_tx_group_fom_reset(struct m0_be_tx_group_fom *gf);

M0_INTERNAL void m0_be_tx_group_fom_run(struct m0_be_tx_group_fom *gf,
					struct m0_be_tx_group *gr);

M0_INTERNAL int m0_be_tx_engine_start(struct m0_be_tx_engine *engine,
				      struct m0_reqh *reqh);
M0_INTERNAL void m0_be_tx_engine_stop(struct m0_be_tx_engine *engine);

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
