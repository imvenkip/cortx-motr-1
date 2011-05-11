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

   Pseudo-code below omits error checking and some details too prolix to
   narrate in a design specification.
 */
/** @{ */

/**
   External resource manager entry point: request a right from the resource
   owner.
 */
void right_get(struct c2_rm_owner *owner, struct c2_rm_incoming *in)
{
	C2_PRE(IS_IN_ARRAY(in->rin_priority, owner->ro_incoming));
	C2_PRE(in->rin_state == RI_INITIALISED);
	C2_PRE(c2_list_is_empty(&in->rin_want.ri_linkage));

	c2_mutex_lock(&owner->ro_lock);
	c2_list_add(&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
		    &in->rin_want.ri_linkage);
	owner_balance();
	c2_mutex_lock(&owner->ro_lock);
	return 0;
}

/**
   External resource manager entry point that releases a right.
 */
void right_put(struct c2_rm_incoming *in)
{
	c2_mutex_lock(&in->rin_owner->ro_lock);
	c2_list_for_each(pin, &in->rin_pins) {
		right = pin->rp_right;
		c2_list_for_each(pin2, &right->ri_pins) {
			if (pin2->rp_flags & RPF_TRACK) {
				pin_remove(pin2);
			}
		}
	}
	c2_mutex_unlock(&in->rin_owner->ro_lock);
}

/**
   Called when an outgoing request completes (possibly with an error, like a
   timeout).

   Errors are not handled in the pseudo-code.
 */
void outgoing_complete(struct c2_rm_outgoing *og, int rc)
{
	owner = &og->rog_owner;
	c2_mutex_lock(&owner->ro_lock);
	c2_list_move(&owner->ro_outgoing[OQS_EXCITED],
		     &og->rog_want.rl_right.ri_linkage);
	c2_mutex_unlock(&owner->ro_lock);
}

/**
   Removes a tracking pin on a resource usage right.

   If this was a last pin issued by an incoming request, excite the request.
 */
static void pin_remove(struct c2_rm_pin *pin)
{
	in = pin->rp_incoming;
	c2_list_del(&pin->rp_incoming_linkage);
	if (c2_list_empty(&in->rin_pins))
		/*
		 * Last pin removed, excite the request.
		 */
		c2_list_move(&o->ro_incoming[in->rin_priority][OQS_EXCITED],
			     &in->rin_want.rl_linkage);
}

/**
   Main owner state machine function.

   Goes through the lists of excited incoming and outgoing requests until all
   the excitement is gone.
 */
static void owner_balance(struct c2_rm_owner *o)
{
	int prio;

	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	do {
		todo = false;
		c2_list_for_each_safe(out, &o->ro_outgoing[OQS_EXCITED]) {
			todo = true;
			/*
			 * Outgoing request completes.
			 */
			c2_list_for_each(pin, &o->rog_want.rl_right.ri_pins)
				pin_remove(pin);
			outgoing_delete(out);
		}
		for (prio = C2_RM_REQUEST_PRIORITY_MAX; prio >= 0; prio--) {
			c2_list_for_each(in,
					 &o->ro_incoming[prio][OQS_EXCITED]) {
				todo = true;
				C2_ASSERT(in->rin_state == RI_WAIT ||
					  in->rin_state == RI_INITIALISED);
				C2_ASSERT(c2_list_empty(&in->rin_pins));
				/*
				 * All waits completed, go to CHECK
				 * state.
				 */
				c2_list_move(o->ro_incoming[prio][OQS_GROUND],
					     &in->rin_want.ri_linkage);
				in->rin_state = RI_CHECK;
				incoming_check(in);
			}
		}
	} while (todo);
}

/**
   Takes an incoming request in RI_CHECK state and perform a non-blocking state
   transition.

   This function leaves the request either in RI_WAIT, RI_SUCCESS or RI_FAILURE
   state.
 */
static void incoming_check(struct c2_rm_incoming *in)
{
	struct c2_rm_right rest;
	struct c2_rm_right join;
	bool               track_local;
	bool               wait_local;
	bool               need_local;

	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(in->rin_state == RI_CHECK);
	C2_PRE(c2_list_empty(&in->rin_pins));

	/*
	 * This function goes through owner rights lists checking for "wait"
	 * conditions that should be satisfied before the request could be
	 * fulfilled.
	 *
	 * If there is nothing to wait for, the request is either fulfilled
	 * immediately or fails.
	 */

	o = in->rin_owner;
	rest = right_copy(&in->rin_want);

	/*
	 * Check for "local" wait conditions.
	 */
	incoming_check_local(in, &rest);

	if (rest is empty) {
		/*
		 * The wanted right is completely covered by the local
		 * rights. There are no remote conditions to wait for.
		 */
		if (some local rights on &in->rin_pins are held) {
			/*
			 * conflicting held rights were found, has to
			 * wait until local rights are released.
			 */
			in->rin_state = RI_WAIT;
		} else {
			/*
			 * all conflicting rights are cached (not
			 * held).
			 *
			 * Apply the policy.
			 */
			apply_policy(in);
			switch (in->rin_type) {
			case RIT_LOAN:
				move_to_sublet(in);
				reply_to_loan_request(in);
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
		/*
		 * "rest" is not empty. Communication with remote owners is
		 * necessary to fulfill the request.
		 */
		/* @todo employ rpc grouping here. */
		/*
		 * revoke sub-let rights.
		 *
		 * The actual implementation should be somewhat different: if
		 * some right, conflicting with the wanted one is sub-let, but
		 * RIF_MAY_REVOKE is cleared, the request should fail instead of
		 * borrowing more rights.
		 */
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

/**
   "Local" part of incoming request CHECK phase.

   Goes through the locally possessed rights intersecting with the request and
   pins them if necessary.
 */
static void incoming_check_local(struct c2_rm_incoming *in)
{
	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(in->rin_state == RI_CHECK);

	struct c2_rm_owner *o = in->rin_owner;
	/*
	 * If track_local is true, a check must be made for locally possessed
	 * rights conflicting with the wanted right.
	 *
	 * Typically, track_local is true for a remote request (loan or
	 * revoke), because the local rights have to be released before the
	 * request can be fulfilled. For a local request track_local can also
	 * be true, depending on the policy.
	 */
	bool track_local = ...;
	/*
	 * If coverage is true, the loop below pins some collection of locally
	 * possessed rights which together imply (i.e., cover) the wanted
	 * right. Otherwise, all locally possessed rights, intersecting with
	 * the wanted right are pinned.
	 *
	 * Typically, revoke and loan requests have coverage set to true, and
	 * local requests have coverage set to false.
	 */
	bool coverage = ...;

	if (!track_local)
		return;

	for (i = 0; i < OWOS_NR; ++i) {
		for_each_right(scan, &o->ro_owned[i]) {
			if (scan intersects rest) {
				if (!coverage) {
					rest = right_diff(rest, scan);
					if (rest is empty)
						return;
				}
				pin_add(in, scan);
			}
		}
	}
}

/**
   Revokes @right (or parts thereof) sub-let to downward owners.
 */
static void sublet_revoke(struct c2_rm_incoming *in, struct c2_rm_right *right)
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

/**
   Sticks a tracking pin on @right. When @right is released, the all incoming
   requests that stuck pins into it are notified.
 */
int pin_add(struct c2_rm_incoming *in, struct c2_rm_right *right)
{
	struct c2_rm_pin *pin;

	C2_ALLOC_PTR(pin);
	if (pin == NULL)
		return -ENOMEM;
	pin->rp_flags = RPF_TRACK;
	pin->rp_right = right;
	pin->rp_incoming = in;
	c2_list_add(&right->ri_pins, &pin->rp_right_linkage);
	c2_list_add(&in->rin_pins, &pin->rp_incoming_linkage);
	return 0;
}

/**
   Sends an outgoing request of type @otype to a remote owner specified by
   @loan and with requested (or cancelled) right @right.
 */
int go_out(struct c2_rm_incoming *in, enum c2_rm_outgoing_type otype,
	   struct c2_rm_loan *loan, struct c2_rm_right *right)
{
	struct c2_rm_outgoint *out;

	/* first check for existing outgoing requests */
	for_each_right(scan, in->rin_owner->ro_outgoing[*]) {
		if (scan->rog_type == otype && scan intersects right) {
			/* @todo adjust outgoing requests priority (priority
			   inheritance) */
			pin_add(in, scan);
			rest = right_diff(rest, scan);
			break;
		}
	}
	C2_ALLOC_PTR(out);
	out->rog_type = otype;
	out->rog_want.rl_other = loan->rl_other;
	out->rog_want.rl_id    = loan->rl_id;
	out->rog_want.rl_right = right_copy(right);
	c2_list_add(&in->rin_owner->ro_outgoing[OQS_GROUND],
		    &out->rog_want.rl_right.ri_linkage);
	pin_add(in, &out->rog_want.rl_right);
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
