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

#pragma once

#ifndef __MERO_NET_TEST_STR_H__
#define __MERO_NET_TEST_STR_H__

#include "net/test/serialize.h"


/**
   @defgroup NetTestStrDFS Serialization of ASCIIZ string
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

enum {
	/** NTSTRING @todo move to lib/magic.h */
	M0_NET_TEST_STR_MAGIC = 0x474e49525453544e,
};

/**
   Serialize or deserialize ASCIIZ string.
   @pre op == M0_NET_TEST_SERIALIZE || op == M0_NET_TEST_DESERIALIZE
   @pre str != NULL
   @note str should be finalized after deserializing using
   m0_net_test_str_fini() to prevent memory leak.
 */
m0_bcount_t m0_net_test_str_serialize(enum m0_net_test_serialize_op op,
				      char **str,
				      struct m0_bufvec *bv,
				      m0_bcount_t bv_offset);

/**
   Finalize m0_net_test_str.
   @see m0_net_test_str_serialize().
 */
void m0_net_test_str_fini(char **str);

/**
   @} end of NetTestStrDFS group
 */

#endif /*  __MERO_NET_TEST_STR_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
