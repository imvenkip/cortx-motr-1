/* -*- C -*- */

#include "lib/mutex.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/chan.h"

#include "rm/rm.h"

static void rings_policy(struct c2_rm_resource *resource,
			 struct c2_rm_incoming *in)
{
}

struct c2_rm_resource_ops rings_ops = {
	.rop_right_decode = NULL,
	.rop_policy 	  = rings_policy
};

struct c2_rm_resource_type_ops rings_rtype_ops = {
	.rto_eq     = NULL,
	.rto_decode = NULL
};

static void right_meet(struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	r0->ri_datum = r0->ri_datum & r1->ri_datum ;
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
	return ((r0->ri_datum & r1->ri_datum) == r0->ri_datum);
}

struct c2_rm_right_ops rings_right_ops = {
	.rro_free    = NULL,
	.rro_encode  = NULL,
	.rro_meet    = right_meet,
	.rro_join    = right_join,
	.rro_diff    = right_diff,
	.rro_implies = right_implies
};

static void incoming_complete(struct c2_rm_incoming *in, int32_t rc)
{
	struct c2_rm_owner *owner;

        C2_PRE(in != NULL);

        owner = in->rin_owner;
	c2_list_del(&in->rin_want.ri_linkage);
	c2_chan_broadcast(&in->rin_signal);
}

static void incoming_conflict(struct c2_rm_incoming *in)
{
}

struct c2_rm_incoming_ops rings_incoming_ops = {
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

