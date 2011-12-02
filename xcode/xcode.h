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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 05/13/2010
 */

#ifndef __COLIBRI_XCODE_XCODE_H__
#define __COLIBRI_XCODE_XCODE_H__

#include "lib/vec.h"                /* c2_bufvec_cursor */
#include "lib/types.h"              /* c2_bcount_t */

/* import */
struct c2_bufvec_cursor;

/* export */
struct c2_xcode;
struct c2_xcode_type;
struct c2_xcode_type_ops;
struct c2_xcode_ctx;

struct c2_xcode {
	struct c2_xcode_type *xc_type;
};

struct c2_xcode_type {
	const char                     *xct_name;
	const struct c2_xcode_type_ops *xct_ops;
};

struct c2_xcode_type_ops {
	int (*xto_length)(struct c2_xcode_ctx *ctx, const struct c2_xcode *xc);
	int (*xto_encode)(struct c2_xcode_ctx *ctx, const struct c2_xcode *xc);
	int (*xto_decode)(struct c2_xcode_ctx *ctx, struct c2_xcode *xc);
};

enum c2_xcode_endianness {
	C2_XCODE_END_LE,
	C2_XCODE_END_BE
};

struct c2_xcode_ctx {
	enum c2_xcode_endianness xcx_end;
	struct c2_bufvec_cursor  xcx_it;
	c2_bcount_t              xcx_share_threshold;
};

/** @} end of xcode group */

/* __COLIBRI_XCODE_XCODE_H__ */
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
