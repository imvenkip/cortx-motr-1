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
 * Original creation date: 18-Jul-2013
 */

#include "be/domain.h"

#include "ut/ut.h"

void m0_be_ut_domain(void)
{
	struct m0_be_domain_cfg cfg;
	struct m0_be_domain     dom;
	int		     rc;

	cfg = (struct m0_be_domain_cfg) {
		.bc_engine = {
			.bec_group_nr = 1,
			.bec_log_size = 1 << 24,
			.bec_group_size_max =
				M0_BE_TX_CREDIT(200000, 1 << 22),
			.bec_group_tx_max = 20,
			.bec_group_fom_reqh = NULL,	/* XXX */
		},
	};
	rc = m0_be_domain_init(&dom, &cfg);
	M0_UT_ASSERT(rc == 0);
	m0_be_domain_fini(&dom);
}

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
