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

/**
 * @addtogroup be
 *
 * @{
 */

M0_INTERNAL int m0_be_domain_init(struct m0_be_domain *dom,
				  struct m0_be_domain_cfg *cfg)
{
	struct m0_be_engine *en = &dom->bd_engine;
	int		     rc;

	dom->bd_cfg = *cfg;
	rc = m0_be_engine_init(en, &dom->bd_cfg.bc_engine);
	if (rc == 0) {
		rc = m0_be_engine_start(en);
		if (rc != 0)
			m0_be_engine_fini(en);
	}
	return rc;
}

M0_INTERNAL void m0_be_domain_fini(struct m0_be_domain *dom)
{
	//m0_be_engine_stop(&dom->bd_engine);
	m0_be_engine_fini(&dom->bd_engine);
}

M0_INTERNAL struct m0_be_engine *m0_be_domain_engine(struct m0_be_domain *dom)
{
	return &dom->bd_engine;
}

/** @} end of be group */

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
