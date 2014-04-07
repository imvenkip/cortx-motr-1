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

#ifndef __MERO_LIB_BUF_H__
#define __MERO_LIB_BUF_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"

/**
   @defgroup buf Basic buffer type
   @{
*/

/** Memory buffer. */
struct m0_buf {
	m0_bcount_t b_nob;
	void       *b_addr;
} M0_XCA_SEQUENCE;

/**
 * Initialisers for struct m0_buf.
 *
 * @note
 *
 *   1. #include "lib/misc.h" for M0_BUF_INITS().
 *
 *   2. M0_BUF_INITS() cannot be used with `static' variables.
 * @code
 *         // static const struct m0_buf bad = M0_BUF_INITS("foo");
 *         //  ==> warning: initializer element is not constant
 *
 *         static char str[] = "foo";
 *         static const struct m0_buf good = M0_BUF_INIT(sizeof str, str);
 * @endcode
 */
#define M0_BUF_INIT(size, data)		\
	((struct m0_buf) {		\
		.b_nob = (size),	\
		.b_addr = (data)	\
	})

#define M0_BUF_INIT_PTR(p)      M0_BUF_INIT(sizeof *(p), (p))
#define M0_BUF_INITS(str)       M0_BUF_INIT(strlen(str), (str))
#define M0_BUF_INIT0            M0_BUF_INIT(0, NULL)

/** Initialises struct m0_buf. */
M0_INTERNAL void m0_buf_init(struct m0_buf *buf, void *data, uint32_t nob);

/** Frees memory pointed to by buf->b_addr and zeroes buffer's fields. */
M0_INTERNAL void m0_buf_free(struct m0_buf *buf);

/** Returns true iff two buffers are equal. */
M0_INTERNAL bool m0_buf_eq(const struct m0_buf *x, const struct m0_buf *y);

/**
 * Copies a buffer.
 *
 * User is responsible for m0_buf_free()ing `dest'.
 *
 * @pre   dest->cb_size == 0 && dest->cb_data == NULL
 * @post  ergo(result == 0, m0_buf_eq(dest, src))
 */
M0_INTERNAL int m0_buf_copy(struct m0_buf *dest, const struct m0_buf *src);

/** Does the buffer point at anything? */
M0_INTERNAL bool m0_buf_is_set(const struct m0_buf *buf);

/**
 * Do `buf' and `str' contain equal sequences of non-'\0' characters?
 *
 * @pre  m0_buf_is_set(buf) && str != NULL
 */
M0_INTERNAL bool m0_buf_streq(const struct m0_buf *buf, const char *str);

/**
 * Duplicates a string pointed to by buf->b_addr.
 *
 * Maximum length of the resulting string, including null character,
 * is buf->b_nob.
 *
 * @pre  m0_buf_is_set(buf)
 */
M0_INTERNAL char *m0_buf_strdup(const struct m0_buf *buf);

/** @} end of buf group */
#endif /* __MERO_LIB_BUF_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
