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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 05/12/2011
 */
#include "lib/mutex.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/chan.h"

#include "rm/rm.h"
#include "rm/ut/rings.h"

static void rings_policy(struct c2_rm_resource *resource,
			 struct c2_rm_incoming *in)
{
}

const struct c2_rm_resource_ops rings_ops = {
	.rop_right_decode = NULL,
	.rop_policy	  = rings_policy
};

static bool resources_are_equal(const struct c2_rm_resource *r0,
				const struct c2_rm_resource *r1)
{
	return false;
}

static bool resource_is_valid(uint64_t res_id,
			       const struct c2_rm_resource *res)
{
	struct c2_rings *ring;

	ring = container_of(res, struct c2_rings, rs_resource);
	C2_ASSERT(ring != NULL);
	return res_id == ring->rs_id;
}

const struct c2_rm_resource_type_ops rings_rtype_ops = {
	.rto_eq		  = resources_are_equal,
	.rto_res_is_valid = resource_is_valid
};

static void right_meet(struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	r0->ri_datum = r0->ri_datum & r1->ri_datum ;
}

static bool right_intersects(const struct c2_rm_right *r0,
                      const struct c2_rm_right *r1)
{
	return (r0->ri_datum & r1->ri_datum) != 0;
}

static void right_join(struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	r0->ri_datum = r0->ri_datum | r1->ri_datum ;
}

static void right_diff(struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	r0->ri_datum = r0->ri_datum & ~r1->ri_datum ;
}

static bool right_implies(const struct c2_rm_right *r0,
			  const struct c2_rm_right *r1)
{
	return (r0->ri_datum & r1->ri_datum) == r1->ri_datum;
}

static void rings_right_copy(struct c2_rm_right *dest,
                       const struct c2_rm_right *src)
{
	dest->ri_datum = src->ri_datum;
}

const struct c2_rm_right_ops rings_right_ops = {
	.rro_free	= NULL,
	.rro_encode	= NULL,
	.rro_meet	= right_meet,
	.rro_intersects = right_intersects,
	.rro_join	= right_join,
	.rro_diff	= right_diff,
	.rro_implies	= right_implies,
	.rro_copy	= rings_right_copy
};

static void incoming_complete(struct c2_rm_incoming *in, int32_t rc)
{
	C2_PRE(in != NULL);

	c2_chan_broadcast(&in->rin_signal);
}

static void incoming_conflict(struct c2_rm_incoming *in)
{
}

const struct c2_rm_incoming_ops rings_incoming_ops = {
	.rio_complete = incoming_complete,
	.rio_conflict = incoming_conflict
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

