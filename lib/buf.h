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
 * Original creation date: 12/02/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_BUF_H__
#define __COLIBRI_LIB_BUF_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
   @defgroup buf Basic buffer type
   @{
*/

struct c2_buf {
	c2_bcount_t b_nob;
	void       *b_addr;
};

/*
 * Initialisers for struct c2_buf.
 */
#define C2_BUF_INIT(size, data) { .b_nob = (size), .b_addr = (data) }
#define C2_BUF_INITS(str)       C2_BUF_INIT(strlen(str), (str))
#define C2_BUF_INIT0            C2_BUF_INIT(0, NULL)

/** Returns true iff two buffers are equal. */
bool c2_buf_eq(const struct c2_buf *x, const struct c2_buf *y);

/**
 * Copies a buffer.
 *
 * @pre   dest->cb_size == 0 && dest->cb_data == NULL
 * @post  ergo(result == 0, c2_buf_eq(dest, src))
 */
int c2_buf_copy(struct c2_buf *dest, const struct c2_buf *src);

/** Initialises struct c2_buf */
void c2_buf_init(struct c2_buf *buf, void *data, uint32_t nob);

/** @} end of buf group */


/* __COLIBRI_LIB_BUF_H__ */
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
