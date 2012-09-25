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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 09/03/2012
 */

#ifndef __KERNEL__
#include <string.h>		/* strlen */
#else
#include <linux/string.h>	/* strlen */
#endif

#include "lib/memory.h"		/* c2_alloc */

#include "net/test/str.h"

/**
   @defgroup NetTestStrInternals Serialization of ASCIIZ string
   @ingroup NetTestInternals

   @todo move to net/test/str.h

   @see
   @ref net-test

   @{
 */

struct net_test_str_len {
	size_t   ntsl_len;
	uint64_t ntsl_magic;
};

TYPE_DESCR(net_test_str_len) = {
	FIELD_DESCR(struct net_test_str_len, ntsl_len),
	FIELD_DESCR(struct net_test_str_len, ntsl_magic),
};

c2_bcount_t c2_net_test_str_serialize(enum c2_net_test_serialize_op op,
				      char **str,
				      struct c2_bufvec *bv,
				      c2_bcount_t bv_offset)
{
	struct net_test_str_len str_len;
	c2_bcount_t		len = 0;
	c2_bcount_t		len_total;

	C2_PRE(op == C2_NET_TEST_SERIALIZE || op == C2_NET_TEST_DESERIALIZE);
	C2_PRE(str != NULL);

	if (op == C2_NET_TEST_SERIALIZE) {
		str_len.ntsl_len = strlen(*str) + 1;
		str_len.ntsl_magic = C2_NET_TEST_STR_MAGIC;
	}
	len_total = c2_net_test_serialize(op, &str_len,
					  USE_TYPE_DESCR(net_test_str_len),
					  bv, bv_offset);
	if (len_total != 0) {
		if (op == C2_NET_TEST_DESERIALIZE) {
			if (str_len.ntsl_magic != C2_NET_TEST_STR_MAGIC)
				return 0;
			*str = c2_alloc(str_len.ntsl_len);
			if (*str == NULL)
				return 0;
		}
		len = c2_net_test_serialize_data(op, *str, str_len.ntsl_len,
						 true,
						 bv, bv_offset + len_total);
		len_total += len;
	};

	return len == 0 ? 0 : len_total;
}

void c2_net_test_str_fini(char **str)
{
	c2_free(*str);
	*str = NULL;
}

/**
   @} end of NetTestStrInternals group
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
