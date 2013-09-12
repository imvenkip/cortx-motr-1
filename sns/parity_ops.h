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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 10/19/2010
 */

#pragma once

#ifndef __MERO_SNS_PARITY_OPS_H__
#define __MERO_SNS_PARITY_OPS_H__

#include "galois/galois.h"
#include "lib/assert.h"

#define M0_PARITY_ZERO (0)
#define M0_PARITY_GALOIS_W (8)
typedef int m0_parity_elem_t;

M0_INTERNAL int m0_parity_init(void);
M0_INTERNAL void m0_parity_fini(void);

static inline m0_parity_elem_t m0_parity_add(m0_parity_elem_t x, m0_parity_elem_t y)
{
	return x ^ y;
}

static inline m0_parity_elem_t m0_parity_sub(m0_parity_elem_t x, m0_parity_elem_t y)
{
	return x ^ y;
}

static inline m0_parity_elem_t m0_parity_mul(m0_parity_elem_t x, m0_parity_elem_t y)
{
	/* return galois_single_multiply(x, y, M0_PARITY_GALOIS_W); */
	return galois_multtable_multiply(x, y, M0_PARITY_GALOIS_W);
}

static inline m0_parity_elem_t m0_parity_div(m0_parity_elem_t x, m0_parity_elem_t y)
{
	/* return galois_single_divide(x, y, M0_PARITY_GALOIS_W); */
	return galois_multtable_divide(x, y, M0_PARITY_GALOIS_W);
}

static inline m0_parity_elem_t m0_parity_lt(m0_parity_elem_t x, m0_parity_elem_t y)
{
	return x < y;
}

static inline m0_parity_elem_t m0_parity_gt(m0_parity_elem_t x, m0_parity_elem_t y)
{
	return x > y;
}

/* __MERO_SNS_PARITY_OPS_H__ */
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
