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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 04/28/2010
 */

#include "stob/stob_id.h"
C2_INTERNAL bool c2_stob_id_eq(const struct c2_stob_id *id0,
			       const struct c2_stob_id *id1)
{
	return c2_uint128_eq(&id0->si_bits, &id1->si_bits);
}

C2_INTERNAL int c2_stob_id_cmp(const struct c2_stob_id *id0,
			       const struct c2_stob_id *id1)
{
	return c2_uint128_cmp(&id0->si_bits, &id1->si_bits);
}

C2_INTERNAL bool c2_stob_id_is_set(const struct c2_stob_id *id)
{
	static const struct c2_stob_id zero = {
		.si_bits = {
			.u_hi = 0,
			.u_lo = 0
		}
	};
	return !c2_stob_id_eq(id, &zero);
}
