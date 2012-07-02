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

/** Operation type. @see c2_net_test_xcode(). */
enum c2_net_test_xcode_op {
	C2_NET_TEST_ENCODE, /**< Encode operation. */
	C2_NET_TEST_DECODE, /**< Decode operation. */
};

/** Field description. @see c2_net_test_xcode(). */
struct c2_net_test_descr {
	size_t ntd_offset;	/**< Data offset in structure */
	size_t ntd_length;	/**< Data length in structure */
	bool   ntd_plain_data;	/**< Do not care about endiannes */
};

#define TYPE_DESCR(type_name) \
	static const struct c2_net_test_descr type_name ## _descr[]

#define USE_TYPE_DESCR(type_name) \
	type_name ## _descr, ARRAY_SIZE(type_name ## _descr)

#define FIELD_SIZE(type, field) (sizeof ((type *) 0)->field)

#define FIELD_DESCR(type, field) {					\
	.ntd_offset	= offsetof(type, field),			\
	.ntd_length	= FIELD_SIZE(type, field),			\
	.ntd_plain_data	= false,					\
}

#define FIELD_DESCR_PLAIN(type, field) {				\
	.ntd_offset	= offsetof(type, field),			\
	.ntd_length	= FIELD_SIZE(type, field),			\
	.ntd_plain_data	= true,						\
}

/**
   Encode or decode data.
   @pre plain_data || data_len <= 8
   @see c2_net_test_xcode().
 */
c2_bcount_t c2_net_test_xcode_data(enum c2_net_test_xcode_op op,
				   void *data,
				   c2_bcount_t data_len,
				   bool plain_data,
				   struct c2_bufvec *bv,
				   c2_bcount_t bv_offset);

/**
   Encode or decode data structure with the given description.
   @param op Operation. Can be NET_TEST_ENCODE or NET_TEST_DECODE.
   @param obj Pointer to data structure.
   @param descr Array of data field descriptions.
   @param descr_nr Described fields number in descr.
   @param bv c2_bufvec. Can be NULL - in this case bv_offset is ignored.
   @param bv_offset Offset in bv.
   @return 0 No space in buffer or descr_nr == 0.
   @return >0 Number of bytes read/written/will be written to/from buffer.
   @pre op == C2_NET_TEST_ENCODE || op == C2_NET_TEST_DECODE
   @pre obj != NULL
   @pre descr != NULL
   @note c2_net_test_descr.ntd_length can't be > 8 if
   c2_net_test_descr.ntd_plain_data is true.
 */
c2_bcount_t c2_net_test_xcode(enum c2_net_test_xcode_op op,
			      void *obj,
			      const struct c2_net_test_descr descr[],
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
