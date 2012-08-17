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
 * Original creation date: 07/09/2012
 */

#ifndef __NET_TEST_UINT256_H__
#define __NET_TEST_UINT256_H__

#include "lib/types.h"		/* bool */
#include "lib/assert.h"		/* C2_BASSERT */

#include "net/test/serialize.h"	/* c2_net_test_serialize_op */

#ifndef __KERNEL__
#include "net/test/user_space/uint256_u.h" /* c2_net_test_uint256_double_get */
#endif

/**
   @defgroup NetTestInt256DFS uint256
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

enum {
	/** Number of qwords in struct c2_net_test_uint256 */
	C2_NET_TEST_UINT256_QWORDS_NR = 4,
};

/** Make sure that c2_net_test_uint256 will be 256-bit wide */
C2_BASSERT(sizeof (unsigned long) == 8);

/**
   256-bit wide integer.
   Least significant qword is ntsi_qword[0].
   Most significant qword is ntsi_qword[3].
   If result of operation doesn't fit in in this type, then most significant
   bits of the result will be truncated to fit this type.
 */
struct c2_net_test_uint256 {
	unsigned long ntsi_qword[C2_NET_TEST_UINT256_QWORDS_NR];
};

/**
   a = value
   @pre a != NULL
 */
void c2_net_test_uint256_set(struct c2_net_test_uint256 *a,
			     unsigned long value);

/**
   a += value
   @pre a != NULL
 */
void c2_net_test_uint256_add(struct c2_net_test_uint256 *a,
			     unsigned long value);

/**
   a += value * value
   @pre a != NULL
 */
void c2_net_test_uint256_add_sqr(struct c2_net_test_uint256 *a,
			         unsigned long value);

/**
   a += value256
   @pre a != NULL
   @pre value256 != NULL
 */
void
c2_net_test_uint256_add_uint256(struct c2_net_test_uint256 *a,
				const struct c2_net_test_uint256 *value256);

/**
   Get a specific qword from a.
   @pre a != NULL
   @pre index < C2_NET_TEST_UINT256_QWORDS_NR
 */
unsigned long c2_net_test_uint256_qword_get(const struct c2_net_test_uint256 *a,
					    unsigned index);

/**
   Set a specific qword in a
   @pre a != NULL
   @pre index < C2_NET_TEST_UINT256_QWORDS_NR
 */
void c2_net_test_uint256_qword_set(struct c2_net_test_uint256 *a,
				   unsigned index,
				   unsigned long value);

/**
   Is a == value?
   Useful in UT.
   @pre a != NULL
 */
bool c2_net_test_uint256_is_eq(const struct c2_net_test_uint256 *a,
			       unsigned long value);

/**
   Get double representation of c2_net_test_uint256.
   @note This functions isn't defined for kernel mode.
   @pre a != NULL
 */
double c2_net_test_uint256_double_get(const struct c2_net_test_uint256 *a);

/**
   Serialize or deserialize c2_net_test_uinb256.
   @pre ergo(op == C2_NET_TEST_DESERIALIZE, a != NULL)
   @see c2_net_test_serialize()
 */
c2_bcount_t c2_net_test_uint256_serialize(enum c2_net_test_serialize_op op,
					  struct c2_net_test_uint256 *a,
					  struct c2_bufvec *bv,
					  c2_bcount_t bv_offset);


/**
   @} end of NetTestInt256DFS group
 */

#endif /*  __NET_TEST_UINT256_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
