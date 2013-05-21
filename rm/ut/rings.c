/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajesh Bhalerao <rajesh_bhalerao@xyratex.com>
 * Original creation date: 05/12/2011
 */
#include "lib/finject.h"
#include "lib/mutex.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/chan.h"
#include "lib/vec.h"

#include "rm/rm.h"
#include "rm/ut/rings.h"

static void rings_policy(struct m0_rm_resource *resource,
			 struct m0_rm_incoming *in)
{
}

static void rings_credit_init(struct m0_rm_resource *resource,
			      struct m0_rm_credit   *credit)
{
	credit->cr_ops = &rings_credit_ops;
}

void rings_resource_free(struct m0_rm_resource *resource)
{
	struct m0_rings *rings;

	rings = container_of(resource, struct m0_rings, rs_resource);
	m0_free(rings);
}

void rings_initial_credit(const struct m0_rm_resource *resource,
			  struct m0_rm_credit         *credit)
{
	credit->cr_datum = ALLRINGS;
}

const struct m0_rm_resource_ops rings_ops = {
	.rop_credit_decode  = NULL,
	.rop_policy	    = rings_policy,
	.rop_credit_init    = rings_credit_init,
	.rop_resource_free  = rings_resource_free,
	.rop_initial_credit = rings_initial_credit,
};

static bool rings_resources_are_equal(const struct m0_rm_resource *c0,
				      const struct m0_rm_resource *c1)
{
	return c0 == c1;
}

static bool rings_resource_is(const struct m0_rm_resource *res, uint64_t res_id)
{
	struct m0_rings *ring;

	ring = container_of(res, struct m0_rings, rs_resource);
	M0_ASSERT(ring != NULL);
	return res_id == ring->rs_id;
}

static m0_bcount_t rings_resource_len(const struct m0_rm_resource *resource)
{
	/* Resource type id + resource id */
	return (m0_bcount_t) sizeof(uint64_t) + sizeof(uint64_t);
}

static int rings_resource_encode(struct m0_bufvec_cursor  *cur,
				 struct m0_rm_resource    *resource)
{
	struct m0_rings *rings;

	rings = container_of(resource, struct m0_rings, rs_resource);
	M0_ASSERT(rings != NULL);

	m0_bufvec_cursor_copyto(cur, (void *)&resource->r_type->rt_id,
				sizeof resource->r_type->rt_id);

	m0_bufvec_cursor_copyto(cur, (void *)&rings->rs_id,
				sizeof rings->rs_id);
	return 0;
}

static int rings_resource_decode(struct m0_bufvec_cursor  *cur,
				 struct m0_rm_resource   **resource)
{
	static uint64_t  res_id;
	struct m0_rings *rings;

	m0_bufvec_cursor_copyfrom(cur, &res_id, sizeof res_id);

	M0_ALLOC_PTR(rings);
	if (rings == NULL)
		return -ENOMEM;

	rings->rs_id              = res_id;
	rings->rs_resource.r_ops  = &rings_ops;

	*resource = &rings->rs_resource;
	return 0;
}

const struct m0_rm_resource_type_ops rings_rtype_ops = {
	.rto_eq     = rings_resources_are_equal,
	.rto_is     = rings_resource_is,
	.rto_len    = rings_resource_len,
	.rto_encode = rings_resource_encode,
	.rto_decode = rings_resource_decode
};

static bool rings_credit_intersects(const struct m0_rm_credit *c0,
				    const struct m0_rm_credit *c1)
{
      return (c0->cr_datum & c1->cr_datum) != 0;
}

static int rings_credit_join(struct m0_rm_credit       *c0,
			     const struct m0_rm_credit *c1)
{
	c0->cr_datum |= c1->cr_datum;
	return 0;
}

static int rings_credit_diff(struct m0_rm_credit       *c0,
			     const struct m0_rm_credit *c1)
{
	c0->cr_datum &= ~c1->cr_datum;
	return 0;
}

static void rings_credit_free(struct m0_rm_credit *credit)
{
	credit->cr_datum = 0;
}

static int rings_credit_encode(const struct m0_rm_credit *credit,
			       struct m0_bufvec_cursor   *cur)
{
	m0_bufvec_cursor_copyto(cur, (void *)&credit->cr_datum,
				sizeof credit->cr_datum);

	return 0;
}

static int rings_credit_decode(struct m0_rm_credit     *credit,
			       struct m0_bufvec_cursor *cur)
{
	m0_bufvec_cursor_copyfrom(cur, &credit->cr_datum,
				  sizeof credit->cr_datum);
	return 0;
}

static int rings_credit_copy(struct m0_rm_credit       *dest,
			     const struct m0_rm_credit *src)
{
	if (M0_FI_ENABLED("fail_copy"))
		return -ENOMEM;

	dest->cr_datum = src->cr_datum;
	dest->cr_owner = src->cr_owner;
	dest->cr_ops = src->cr_ops;
	return 0;
}

static m0_bcount_t rings_credit_len(const struct m0_rm_credit *credit)
{
	return (m0_bcount_t) sizeof(uint64_t);
}

static bool rings_is_subset(const struct m0_rm_credit *src,
			    const struct m0_rm_credit *dest)
{
	return (dest->cr_datum & src->cr_datum) == src->cr_datum;
}

static int rings_disjoin(struct m0_rm_credit       *src,
			 const struct m0_rm_credit *dest,
			 struct m0_rm_credit       *intersection)
{
	intersection->cr_datum = src->cr_datum & dest->cr_datum;
	src->cr_datum &= ~intersection->cr_datum;
	return 0;
}

static bool rings_conflicts(const struct m0_rm_credit *c0,
			    const struct m0_rm_credit *c1)
{
	return c0->cr_datum & c1->cr_datum;
}

const struct m0_rm_credit_ops rings_credit_ops = {
	.cro_intersects = rings_credit_intersects,
	.cro_join	= rings_credit_join,
	.cro_diff	= rings_credit_diff,
	.cro_copy	= rings_credit_copy,
	.cro_free	= rings_credit_free,
	.cro_encode	= rings_credit_encode,
	.cro_decode	= rings_credit_decode,
	.cro_len	= rings_credit_len,
	.cro_is_subset	= rings_is_subset,
	.cro_disjoin	= rings_disjoin,
	.cro_conflicts	= rings_conflicts,
};

static void rings_incoming_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_PRE(in != NULL);
}

static void rings_incoming_conflict(struct m0_rm_incoming *in)
{
}

const struct m0_rm_incoming_ops rings_incoming_ops = {
	.rio_complete = rings_incoming_complete,
	.rio_conflict = rings_incoming_conflict
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
