/* -*- C -*- */

#include "lib/mutex.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"

#include "rm/rm.h"

static void rings_policy(struct c2_rm_resource *resource,
			 struct c2_rm_incoming *in)
{
}

struct c2_rm_resource_ops rings_ops = {
	.rto_encode = NULL,
	.rop_right_decode = NULL,
	.rop_policy = rings_policy
};

struct c2_rm_resource_type_ops rings_rtype_ops = {
	.rto_eq = NULL,
	.rto_decode = NULL
};

static int shift_count(uint64_t value)
{
        int shifts = 0;

        do {
                value = value >> 1;
                shifts++;
        } while (value != 0x1);

        return shifts;
}

static void right_meet(struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	if(r1->ri_datum <= r0->ri_datum)
                r0->ri_datum = r1->ri_datum >> 1;
        else
                r0->ri_datum = r0->ri_datum >> 1;
}

static void right_join(struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	if(r1->ri_datum >= r0->ri_datum)
                r0->ri_datum = r1->ri_datum << 1;
        else
                r0->ri_datum = r0->ri_datum << 1;
}

static void right_diff(struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	int r0_value;
        int r1_value;

        if (r0->ri_ops->rro_implies(r0, r1)) {
                r0_value = shift_count(r0->ri_datum);
                r1_value = shift_count(r1->ri_datum);
                if (r1_value == r0_value)
                        r0->ri_datum = 0;
                else
                        r0->ri_datum = 1 << (r1_value - r0_value);
        }
}

static bool right_implies(const struct c2_rm_right *r0,
			  const struct c2_rm_right *r1)
{
	if (r1->ri_datum <= r0->ri_datum)
                return true;
        else
                return false;
}

struct c2_rm_right_ops rings_right_ops = {
	.rro_free = NULL,
	.rro_encode = NULL,
	.rro_meet = right_meet,
	.rro_join = right_join,
	.rro_diff = right_diff,
	.rro_implies = right_implies
};

static void incoming_complete(struct c2_rm_incoming *in, int32_t rc)
{
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

