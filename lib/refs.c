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
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 04/08/2010
 */

#include "lib/refs.h"

void c2_ref_init(struct c2_ref *ref, int init_num,
		void (*release) (struct c2_ref *ref))
{
	c2_atomic64_set(&ref->ref_cnt, init_num);
	ref->release = release;
}

C2_INTERNAL void c2_ref_get(struct c2_ref *ref)
{
	c2_atomic64_inc(&ref->ref_cnt);
}

C2_INTERNAL void c2_ref_put(struct c2_ref *ref)
{
	if (c2_atomic64_dec_and_test(&ref->ref_cnt)) {
		ref->release(ref);
	}
}

C2_INTERNAL int64_t c2_ref_read(const struct c2_ref *ref)
{
	return c2_atomic64_get(&ref->ref_cnt);
}
