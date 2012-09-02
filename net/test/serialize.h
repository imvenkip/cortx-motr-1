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

#ifndef __NET_TEST_SERIALIZE_H__
#define __NET_TEST_SERIALIZE_H__

#include "lib/types.h"	/* c2_bcount_t */
#include "lib/vec.h"	/* c2_buvfec */

/**
   @defgroup NetTestSerializeDFS Serialization
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

/** Operation type. @see c2_net_test_serialize(). */
enum c2_net_test_serialize_op {
	C2_NET_TEST_SERIALIZE,  /**< Serialize operation. */
	C2_NET_TEST_DESERIALIZE, /**< Deserialize operation. */
};

/** Field description. @see c2_net_test_serialize(). */
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
   Serialize or deserialize data.
   @pre data_len > 0
   @pre plain_data || data_len == 1 || data_len == 2 ||
		      data_len == 4 || data_len == 8
   @see c2_net_test_serialize().
 */
c2_bcount_t c2_net_test_serialize_data(enum c2_net_test_serialize_op op,
				       void *data,
				       c2_bcount_t data_len,
				       bool plain_data,
				       struct c2_bufvec *bv,
				       c2_bcount_t bv_offset);

/**
   Serialize or deserialize data structure with the given description.
   @param op Operation. Can be C2_NET_TEST_SERIALIZE or C2_NET_TEST_DESERIALIZE.
   @param obj Pointer to data structure.
   @param descr Array of data field descriptions.
   @param descr_nr Described fields number in descr.
   @param bv c2_bufvec. Can be NULL - in this case bv_offset is ignored.
   @param bv_offset Offset in bv.
   @return 0 No space in buffer or descr_nr == 0.
   @return >0 Number of bytes read/written/will be written to/from buffer.
   @pre op == C2_NET_TEST_SERIALIZE || op == C2_NET_TEST_DESERIALIZE
   @pre obj != NULL
   @pre descr != NULL
   @note c2_net_test_descr.ntd_length can't be > 8 if
   c2_net_test_descr.ntd_plain_data is true.
 */
c2_bcount_t c2_net_test_serialize(enum c2_net_test_serialize_op op,
				  void *obj,
				  const struct c2_net_test_descr descr[],
				  size_t descr_nr,
				  struct c2_bufvec *bv,
				  c2_bcount_t bv_offset);

/**
   @} end of NetTestSerializeDFS group
 */

/**
   @defgroup NetTestPCharDFS Serialization of ASCIIZ string
   @ingroup NetTestDFS

   @see
   @ref net-test
   @todo move to net/test/str.h

   @{
 */

enum {
	C2_NET_TEST_STR_MAGIC = 0x474e49525453544e,	/**< NTSTRING */
};

/**
   Serialize or deserialize ASCIIZ string.
   @pre op == C2_NET_TEST_SERIALIZE || op == C2_NET_TEST_DESERIALIZE
   @pre str != NULL
   @note str should be finalized after deserializing using
   c2_net_test_str_fini() to prevent memory leak.
 */
c2_bcount_t c2_net_test_str_serialize(enum c2_net_test_serialize_op op,
				      char **str,
				      struct c2_bufvec *bv,
				      c2_bcount_t bv_offset);

/**
   Finalize c2_net_test_str.
   @see c2_net_test_str_serialize().
 */
void c2_net_test_str_fini(char **str);

/**
   @} end of NetTestPCharDFS group
 */

#endif /*  __NET_TEST_SERIALIZE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
