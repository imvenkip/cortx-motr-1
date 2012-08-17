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
 * Original creation date: 08/18/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/** @todo create lib/limits.h */
#ifndef __KERNEL__
#include <limits.h>		/* ULONG_MAX */
#else
#include <linux/kernel.h>	/* ULONG_MAX */
#endif

#include "lib/assert.h"		/* C2_PRE */
#include "lib/cdefs.h"		/* NULL */

#include "net/test/uint256.h"

/**
   @addtogroup NetTestInt256Internals

   @see
   @ref net-test

   @{
 */

double c2_net_test_uint256_double_get(const struct c2_net_test_uint256 *a)
{
	const double base   = 1. + ULONG_MAX;
	double	     pow    = 1.;
	double	     result = .0;
	int	     i;

	C2_PRE(a != NULL);

	for (i = 0; i < C2_NET_TEST_UINT256_QWORDS_NR; ++i) {
		result += pow * c2_net_test_uint256_qword_get(a, i);
		pow *= base;
	}
	return result;
}

/**
   @} end of NetTestInt256Internals group
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
