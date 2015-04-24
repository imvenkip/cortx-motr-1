/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 10/19/2010
 */

#include "sns/parity_ops.h"

#include "gf/gf_complete.h"
static gf_t m0_parity_ops_gf;

M0_INTERNAL int m0_parity_init(void)
{
	int ret;
	ret = gf_init_easy(&m0_parity_ops_gf, M0_PARITY_GALOIS_W);
	/* XXX: crazy error conventions inside library */
	return ret == 1 ? 0 : -1;
}

M0_INTERNAL void m0_parity_fini(void)
{
	gf_free(&m0_parity_ops_gf, 0);
}

M0_INTERNAL m0_parity_elem_t m0_parity_mul(m0_parity_elem_t x,
					   m0_parity_elem_t y)
{
	return m0_parity_ops_gf.multiply.w32(&m0_parity_ops_gf, x, y);
}

M0_INTERNAL m0_parity_elem_t m0_parity_div(m0_parity_elem_t x,
					   m0_parity_elem_t y)
{
	return m0_parity_ops_gf.divide.w32(&m0_parity_ops_gf, x, y);
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
