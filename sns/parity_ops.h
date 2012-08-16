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

#ifndef __COLIBRI_SNS_PARITY_OPS_H__
#define __COLIBRI_SNS_PARITY_OPS_H__

#include "galois/galois.h"
#include "lib/assert.h"

#define C2_PARITY_ZERO (0)
#define C2_PARITY_GALOIS_W (8)
typedef int c2_parity_elem_t;

void c2_parity_fini(void);
void c2_parity_init(void);

static inline c2_parity_elem_t c2_parity_add(c2_parity_elem_t x, c2_parity_elem_t y)
{
	return x ^ y;
}

static inline c2_parity_elem_t c2_parity_sub(c2_parity_elem_t x, c2_parity_elem_t y)
{
	return x ^ y;
}

static inline c2_parity_elem_t c2_parity_mul(c2_parity_elem_t x, c2_parity_elem_t y)
{
	/* return galois_single_multiply(x, y, C2_PARITY_GALOIS_W); */
	return galois_multtable_multiply(x, y, C2_PARITY_GALOIS_W);
}

static inline c2_parity_elem_t c2_parity_div(c2_parity_elem_t x, c2_parity_elem_t y)
{
	/* return galois_single_divide(x, y, C2_PARITY_GALOIS_W); */
	return galois_multtable_divide(x, y, C2_PARITY_GALOIS_W);
}

static inline c2_parity_elem_t c2_parity_lt(c2_parity_elem_t x, c2_parity_elem_t y)
{
	return x < y;
}

static inline c2_parity_elem_t c2_parity_gt(c2_parity_elem_t x, c2_parity_elem_t y)
{
	return x > y;
}

/* __COLIBRI_SNS_PARITY_OPS_H__ */
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
