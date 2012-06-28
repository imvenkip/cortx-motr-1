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
 * Original author Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 06/28/2012
 */

#ifndef __NET_TEST_NTXCODE_H__
#define __NET_TEST_NTXCODE_H__

#include "lib/types.h"	/* c2_bcount_t */
#include "lib/vec.h"	/* c2_buvfec */

/**
   @defgroup NetTestXCODE Colibri Network Benchmark Xcode.

   @see
   @ref net-test

   @{
 */

enum c2_net_test_xcode_op {
	C2_NET_TEST_ENCODE,
	C2_NET_TEST_DECODE
};

struct c2_net_test_descr {
	size_t ntd_offset;
	size_t ntd_length;
};

#define TYPE_DESCR(type_name) \
	static const struct c2_net_test_descr type_name ## _descr[]

#define FIELD_SIZE(type, field) (sizeof ((type *) 0)->field)

#define FIELD_DESCR(type, field) {		\
	.ntd_offset		= offsetof(type, field),	\
	.ntd_length		= FIELD_SIZE(type, field),	\
}

c2_bcount_t c2_net_test_xcode(enum c2_net_test_xcode_op op,
			      void *obj,
			      struct c2_net_test_descr *descr,
			      size_t descr_nr,
			      struct c2_bufvec *bv,
			      c2_bcount_t bv_offset);

#endif /*  __NET_TEST_NTXCODE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
