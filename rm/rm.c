/* -*- C -*- */

#include "lib/mutex.h"
#include "lib/list.h"

#include "rm/rm.h"

/**
   @addtogroup rm
   @{
 */

/**
   @name Owner state machine

   c2_rm_owner and c2_rm_incoming together form a state machine where basic
   resource management functionality is implemented.

   This state machine reacts to the following external events:

       - an incoming request from a local user;

       - an incoming loan request from another domain;

       - an incoming revocation request from another domain;

       - local user releases a pin on a right (as a by-product of destroying an
         incoming request);

       - completion of an outgoing request to another domain (including a
         timeout or a failure).

   Any event is processed in a uniform manner:

       - c2_rm_owner::ro_lock is taken;

       - c2_rm_owner lists are updated to reflect the event, see details
         below. This temporarily violates the c2_rm_owner_invariant();

       - owner_balance() is called to restore the invariant, this might create
         new imbalances and go through several iterations;

       - c2_rm_owner::ro_lock is released.

   Event handling is serialised by the owner lock. It not legal to wait for
   networking on IO events under this lock.
 */
/** @{ */

incoming_check(struct c2_rm_incoming *in)
{
	struct c2_rm_right rest;
	struct c2_rm_right join;
	bool               track_local;
	bool               wait_local;
	bool               need_local;

	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(in->rin_state == RI_CHECK);

	o = in->rin_owner;
	rest = right_copy(&in->rin_want);
	need_local = (in->rin_flags & RIF_WAIT_LOCAL) ||
		in->rin_type != RIT_LOCAL;
	track_local = need_local || in->rin_policy == RIP_INPLACE;
	wait_local = false;

	for (i = 0; i < OWOS_NR; ++i) {
		for_each_right(scan, &o->ro_owned[i]) {
			if (scan intersects rest) {
				rest = right_diff(rest, scan);
				if (track_local) {
					pin_add(in, scan, RPF_TRACK);
					wait_local |= (i == OWOS_HELD);
				}
			}
			if (rest is empty)
				break;
		}
		if (rest is empty)
			break;
	}
	if (rest is empty) {
		if (need_local && wait_local) {
			/*
			 * conflicting held rights were found, has to
			 * wait.
			 */
			in->rin_state = RI_WAIT;
		} else {
			/*
			 * all conflicting rights are cached (not
			 * held).
			 */
			apply_policy(in);
			switch (in->rin_type) {
			case RIT_LOAN:
				move_to_sublet(in);
			case RIT_LOCAL:
				break;
			case RIT_REVOKE:
				remove_rights(in);
				go_out(in, ROT_CANCEL, &in->rin_want,
				       &in->rin_want.rl_right);
			}
			in->rin_state = RI_SUCCESS;
		}
	} else {
		/* @todo employ rpc grouping here. */
		/* revoke sub-let rights */
		if (in->rin_flags & RIF_MAY_REVOKE)
			sublet_revoke(in, rest);
		if (in->rin_flags & RIF_MAY_BORROW) {
			/* borrow more */
			while (rest is not empty) {
				struct c2_rm_loan borrow;

				c2_rm_net_locate(rest, &borrow);
				go_out(in, ROT_BORROW,
				       &borrow, borrow.rl_right);
			}
		}
		if (rest is empty) {
			in->rin_state = RI_WAIT;
		} else {
			/* cannot fulfill the request. */
			in->rin_state = RI_FAILURE;
		}
	}
}


sublet_revoke(struct c2_rm_incoming *in, struct c2_rm_right *right)
{
	for_each_right(scan, &o->ro_sublet[]) {
		if (scan intersects rest) {
			rest = right_diff(rest, scan);
			loan = container_of(scan, ...);
			/*
			 * It is possible that this loop emits multiple
			 * outgoing requests toward the same remote
			 * owner. Don't bother to coalesce them here. The rpc
			 * layer would do this more efficiently.
			 */
			go_out(in, ROT_REVOKE, loan, right_meet(rest, scan));
		}
	}
}

int pin_add(struct c2_rm_incoming *in, struct c2_rm_right *right, bool move)
{
	struct c2_rm_pin *pin;

	C2_ALLOC_PTR(pin);
	if (pin == NULL)
		return -ENOMEM;
	pin->rp_right = right;
	pin->rp_incoming = in;
	if (move && c2_list_empty(&right->ri_pins)) {
		c2_list_move(&in->rin_owner->ro_owned[OWOS_HELD],
			     &right->ri_linkage);
	}
	c2_list_add(&right->ri_pins, &pin->rp_right_linkage);
	c2_list_add(&in->rin_pins, &pin->rp_incoming_linkage);
	return 0;
}

int go_out(struct c2_rm_incoming *in, enum c2_rm_outgoing_type otype,
	   struct c2_rm_loan *loan, struct c2_rm_right *right)
{
	struct c2_rm_outgoint *out;

	/* first check for existing outgoing requests */
	for_each_right(scan, in->rin_owner->ro_outgoing) {
		if (scan->rog_type == otype && scan intersects right) {
			/* @todo adjust outgoing requests priority (priority
			   inheritance) */
			pin_add(in, scan, false);
			rest = right_diff(rest, scan);
			break;
		}
	}
	C2_ALLOC_PTR(out);
	out->rog_type = otype;
	out->rog_want.rl_other = loan->rl_other;
	out->rog_want.rl_id    = loan->rl_id;
	out->rog_want.rl_right = right_copy(right);
	c2_list_add(&in->rin_owner->ro_outgoing,
		    &out->rog_want.rl_right.ri_linkage);
	pin_add(in, &out->rog_want.rl_right, false);
}

/** @} end of Owner state machine group */

/** @} end of rm group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
