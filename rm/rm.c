/* -*- C -*- */

#include "lib/mutex.h"
#include "lib/list.h"

#include "rm/rm.h"

/**
   @addtogroup rm
   @{
 */

void c2_rm_domain_init(struct c2_rm_domain *dom)
{
        C2_PRE(dom != NULL);
        c2_mutex_init(&dom->rd_lock);
}

void c2_rm_domain_fini(struct c2_rm_domain *dom)
{
        C2_PRE(dom != NULL);
        c2_mutex_fini(&dom->rd_lock);
}

void c2_rm_type_register(struct c2_rm_domain *dom,
                         struct c2_rm_resource_type *rt)
{
        C2_PRE(dom != NULL);
        C2_PRE(rt->rt_dom == NULL);
        C2_PRE(rt->rt_id != C2_RM_RESOURCE_TYPE_ID_INVALID);

        c2_mutex_lock(&dom->rd_lock);

        dom->rd_types[rt->rt_id] = rt;
        rt->rt_dom = dom;

        rt->rt_ref = 0;
        c2_mutex_init(&rt->rt_lock);
        c2_list_init(&rt->rt_resources);

        c2_mutex_unlock(&dom->rd_lock);
}

void c2_rm_type_deregister(struct c2_rm_resource_type *rtype)
{
        struct c2_rm_domain *dom = rtype->rt_dom;

        C2_PRE(dom != NULL);
        C2_PRE(rtype->rt_id != C2_RM_RESOURCE_TYPE_ID_INVALID);
        IS_IN_ARRAY(rtype->rt_id, dom->rd_types);

        c2_mutex_lock(&dom->rd_lock);

        dom->rd_types[rtype->rt_id] = NULL;
        rtype->rt_dom = NULL;
        rtype->rt_id = C2_RM_RESOURCE_TYPE_ID_INVALID;

        if (c2_list_is_empty(&rtype->rt_resources)) {
        }

        c2_list_fini(&rtype->rt_resources);
        c2_mutex_fini(&rtype->rd_lock);
        rtype->rt_ref = 0;

        c2_mutex_unlock(&dom->rd_lock);
}

int  c2_rm_resource_add(struct c2_rm_resource_type *rtype,
                        struct c2_rm_resource *res)
{
        int     result = 0;

        C2_PRE(rtype != NULL);
        C2_PRE(res != NULL);

        c2_mutes_lock(&rtype->rt_lock);
        c2_list_add(rtype->rt_resources, res->r_linkage);
        c2_mutes_unlock(&rtype->rt_lock);

        return result;
}

void c2_rm_resource_del(struct c2_rm_resource *res)
{
        C2_PRE(res != NULL);

        c2_mutes_lock(&rtype->rt_lock);
        c2_list_del(res->r_linkage);
        c2_mutes_unlock(&rtype->rt_lock);
}

static int c2_rm_owner_init_internal(struct c2_rm_owner **o, 
				     struct c2_rm_resource **r)
{
        struct c2_rm_resource_type      *rtype = res->r_type;
	struct c2_rm_owner 		*owner = *o;
	struct c2_rm_resource		*res = *r;
	int 				result = 0;


        owner->ro_resource = res;
        res->r_ref = 0;

        owner->ro_state = ROS_FINAL;
        owner->ro_group = NULL;

        c2_list_init(&owner->ro_borrowed);
        c2_list_init(&owner->ro_sublet);

        for (int i = 0; i < ARRAY_SIZE(owner->ro_owned), i++)
                c2_list_init(&owner->ro_owned[i]);

        for (int j = 0; j < OQS_NR; j++) {
                for (int i = 0; i < C2_RM_REQUEST_PRIORITY_NR, i++)
                        c2_list_init(&owner->ro_incoming[i][j]);

                c2_list_init(&owner->ro_outgoing[j]);
        }

        c2_mutex_init(&owner->ro_lock);

        return result;
}


int c2_rm_owner_init(struct c2_rm_owner *owner, struct c2_rm_resource *res)
{
        C2_PRE(owner != NULL);
        C2_PRE(res != NULL);
	
	return c2_rm_owner_init_internal(&owner, &res);
}

int c2_rm_owner_init_with(struct c2_rm_owner *owner,
                          struct c2_rm_resource *res, struct c2_rm_right *r)
{
        int	result = 0;

        C2_PRE(owner != NULL);
        C2_PRE(res != NULL);

	result = c2_rm_owner_init_internal(&owner, &res);
	C2_ASSERT(result == 0);

	/** 
	* Add The right to the woner in held list.
	*/
        c2_list_add(&owner->ro_owned[OWOS_HELD], r->ri_linkage);
        res->r_ref++;

        return result;
}

void c2_rm_owner_fini(struct c2_rm_owner *owner)
{
        struct c2_rm_resource_type      *rtype = res->r_type;
        int                             result = 0;

        C2_PRE(owner != NULL);
        owner->ro_resource = NULL;
        res->r_ref = 0;
        owner->ro_state = ROS_FINAL;
        owner->ro_group = NULL;
        c2_mutex_lock(&owner->ro_lock);

        if (!c2_list_is_empty(&owner->ro_borrowed)) {
        }

        if (!c2_list_is_empty(&owner->ro_sublet)) {
        }

        for (int i = 0; i < ARRAY_SIZE(owner->ro_owned), i++) {
                if(!c2_list_is_empty(&owner->ro_owned[i])) {
                }
        }

        for (int j = 0; j < OQS_NR; j++) {
                for (int i = 0; i < C2_RM_REQUEST_PRIORITY_NR, i++) {
                        if (!c2_list_is_empty(&owner->ro_incoming[i][j])) {
                        }
                }
                if (!c2_list_is_empty(&owner->ro_outgoing[j])) {
                }
        }

        res->r_ref = 0;
        c2_mutex_unlock(&owner->ro_lock);
        c2_mutex_fini(&owner->ro_lock);
}

void c2_rm_right_init(struct c2_rm_right *right)
{
        C2_PRE(right != NULL);
        c2_list_init(&right->ri_pins);
}

void c2_rm_right_fini(struct c2_rm_right *right)
{
        C2_PRE(right != NULL);
        if (c2_list_is_empty(right->ri_pins)){
        }
        c2_list_fini(&right->ri_pins);
}

void c2_rm_incoming_init(struct c2_rm_incoming *in)
{
        C2_PRE(in != NULL);
	c2_list_init(&in->rin_pins);
}

void c2_rm_incoming_fini(struct c2_rm_incoming *in)
{
        C2_PRE(in != NULL);

	if (!c2_list_is_empty(&in->rin_pins)) {
	}
	c2_list_fini(&in->rin_pins);
}


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
int c2_rm_right_get(struct c2_rm_owner *owner, struct c2_rm_incoming *in)
{
	int result = 0;

	C2_PRE(IS_IN_ARRAY(in->rin_priority, owner->ro_incoming));
	C2_PRE(in->rin_state == RI_INITIALISED);
	C2_PRE(c2_list_is_empty(&in->rin_want.ri_linkage));

	c2_mutex_lock(&owner->ro_lock);
	c2_list_add(&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
		    &in->rin_want.ri_linkage);
	c2_rm_owner_balance(owner);
	c2_mutex_lock(&owner->ro_lock);

	return result;
}

/**
   External resource manager entry point that releases a right.
 */
void c2_rm_right_put(struct c2_rm_incoming *in)
{
	struct c2_rm_pin	*in_pin;
	struct c2_rm_pin	*ri_pin;
	struct c2_rm_right 	*right;

	C2_PRE(in != NULL);

	c2_mutex_lock(&in->rin_owner->ro_lock);
	c2_list_for_each_entry(&in->rin_pins, pin,
			       struct c2_rm_pin, rp_incoming_linkage) {
		right = in_pin->rp_right;
		c2_list_for_each_entry(&right->ri_pins, ri_pin,
				       struct c2_rm_pin, rp_right_linkage) {
			if (pin2->rp_flags & RPF_TRACK) {
				c2_rm_pin_remove(ri_pin);
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
void c2_rm_outgoing_complete(struct c2_rm_outgoing *og, int rc)
{
	struct c2_rm_owner *owner;

	C2_PRE(og != NULL);

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
static void c2_rm_pin_remove(struct c2_rm_pin *pin)
{
	struct c2_rm_incoming 	*in;

	C2_ASSERT(pin != NULL);

	in = pin->rp_incoming;
	c2_list_del(&pin->rp_incoming_linkage);
	if (c2_list_is_empty(&in->rin_pins)) {
		/*
		 * Last pin removed, excite the request.
		 */
		c2_list_move(&o->ro_incoming[in->rin_priority][OQS_EXCITED],
			     &in->rin_want.rl_linkage);
	}
}

/**
   Main owner state machine function.

   Goes through the lists of excited incoming and outgoing requests until all
   the excitement is gone.
 */
static void c2_rm_owner_balance(struct c2_rm_owner *o)
{
	struct c2_rm_pin	*pin;
	struct c2_rm_loan	*loan;
	struct c2_rm_right	*right;
	struct c2_rm_right	*tmp;
	struct c2_list_link	*link;
	struct c2_rm_outgoing	*out;
	struct c2_rm_incoming	*in;
	bool 			todo;
	int 			prio;


	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	do {
		todo = false;
		c2_list_for_each_entry_safe(&o->ro_outgoing[OQS_EXCITED], right, 
				tmp, struct c2_rm_right, ri_linkage) {
			todo = true;

			loan = container_of(right, struct c2_rm_loan, rl_right);
			C2_ASSERT(loan != NULL);

			out = container_of(loan, struct c2_rm_outgoing, rog_want);
			C2_ASSERT(out != NULL);
			/*
			 * Outgoing request completes.
			 */
			c2_list_for_each_entry(out->rog_want.rl_right.ri_pins, 
					   pin, struct c2_rm_right, ri_linkage)
				c2_rm_pin_remove(pin);
			c2_rm_outgoing_delete(out);
		}
		for (prio = C2_RM_REQUEST_PRIORITY_MAX; prio >= 0; prio--) {
			c2_list_for_each(&o->ro_incoming[prio][OQS_EXCITED], 
								link) {
				todo = true;
				right = c2_list_entry(link, struct c2_rm_right, 
								ri_linkage);
				C2_ASSERT(right != NULL);

				in = container_of(right, struct c2_rm_incoming, 
								rin_want);
				C2_ASSERT(in != NULL);
				C2_ASSERT(in->rin_state == RI_WAIT || 
					  in->rin_state == RI_INITIALISED);
				C2_ASSERT(c2_list_is_empty(&in->rin_pins));
				/*
				 * All waits completed, go to CHECK
				 * state.
				 */
				c2_list_move(o->ro_incoming[prio][OQS_GROUND],
					     &in->rin_want.rl_right.ri_linkage);
				in->rin_state = RI_CHECK;
				c2_rm_incoming_check(in);
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
static void c2_rm_incoming_check(struct c2_rm_incoming *in)
{
	struct c2_rm_right rest;
	struct c2_rm_right join;
	struct c2_rm_owner *owner;
	bool               track_local;
	bool               wait_local;
	bool               need_local;

	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(in->rin_state == RI_CHECK);
	C2_PRE(c2_list_is_empty(&in->rin_pins));

	/*
	 * This function goes through owner rights lists checking for "wait"
	 * conditions that should be satisfied before the request could be
	 * fulfilled.
	 *
	 * If there is nothing to wait for, the request is either fulfilled
	 * immediately or fails.
	 */

	owner = in->rin_owner;
	rest = c2_rm_right_copy(&in->rin_want);

	/*
	 * Check for "local" wait conditions.
	 */
	c2_rm_incoming_check_local(in, &rest);

	if (!rest->ri_datum) {
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
			c2_rm_apply_policy(in);
			switch (in->rin_type) {
			case RIT_LOAN:
				c2_rm_move_to_sublet(in);
				c2_rm_reply_to_loan_request(in);
			case RIT_LOCAL:
				break;
			case RIT_REVOKE:
				c2_rm_remove_rights(in);
				c2_rm_go_out(in, ROT_CANCEL, &in->rin_want,
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
			c2_rm_sublet_revoke(in, rest);
		if (in->rin_flags & RIF_MAY_BORROW) {
			/* borrow more */
			while (rest->ri_datum) {
				struct c2_rm_loan borrow;

				c2_rm_net_locate(rest, &borrow);
				c2_rm_go_out(in, ROT_BORROW,
				       &borrow, borrow.rl_right);
			}
		}
		if (!rest->ri_datum) {
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
static void c2_rm_incoming_check_local(struct c2_rm_incoming *in,
					struct c2_rm_right *rest)
{
	struct c2_rm_right *scan;
	struct c2_rm_owner *o = in->rin_owner;
	bool track_local = false;
	bool coverage = false;

	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(in->rin_state == RI_CHECK);

	/*
	 * If track_local is true, a check must be made for locally possessed
	 * rights conflicting with the wanted right.
	 *
	 * Typically, track_local is true for a remote request (loan or
	 * revoke), because the local rights have to be released before the
	 * request can be fulfilled. For a local request track_local can also
	 * be true, depending on the policy.
	 */
	if (in->rin_type != RIT_LOCAL)
		track_local = true;
	/*
	 * If coverage is true, the loop below pins some collection of locally
	 * possessed rights which together imply (i.e., cover) the wanted
	 * right. Otherwise, all locally possessed rights, intersecting with
	 * the wanted right are pinned.
	 *
	 * Typically, revoke and loan requests have coverage set to true, and
	 * local requests have coverage set to false.
	 */
	if (in->rin_type != RIT_LOCAL)
		coverage = true;

	if (!track_local)
		return;

	for (int i = 0; i < OWOS_NR; ++i) {
		c2_list_for_each_entry(&o->ro_owned[i],
					scan, struct c2_rm_right, ri_linkage) {
			if (scan intersects rest) {
				if (!coverage) {
					rest = c2_rm_right_diff(rest, scan);
					if (!rest->ri_datum)
						return;
				}
				c2_rm_pin_add(in, scan);
			}
		}
	}
}

/**
   Revokes @right (or parts thereof) sub-let to downward owners.
 */
static void c2_rm_sublet_revoke(struct c2_rm_incoming *in,
				struct c2_rm_right *right)
{
	struct c2_rm_right *scan;
	struct c2_rm_owner *o = in->rin_owner;

	c2_list_for_each_entry(&o->ro_sublet, scan,
				struct c2_rm_right, ri_linkage) {
		if (scan intersects rest) {
			rest = c2_rm_right_diff(rest, scan);
			loan = container_of(scan, struct c2_rm_loan, rl_right);
			/*
			 * It is possible that this loop emits multiple
			 * outgoing requests toward the same remote
			 * owner. Don't bother to coalesce them here. The rpc
			 * layer would do this more efficiently.
			 */
			c2_rm_go_out(in, ROT_REVOKE, loan, right_meet(rest, scan));
		}
	}
}

/**
   Sticks a tracking pin on @right. When @right is released, the all incoming
   requests that stuck pins into it are notified.
 */
int c2_rm_pin_add(struct c2_rm_incoming *in, struct c2_rm_right *right)
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
int c2_rm_go_out(struct c2_rm_incoming *in, enum c2_rm_outgoing_type otype,
	   struct c2_rm_loan *loan, struct c2_rm_right *right)
{
	struct c2_rm_outgoint 	*out;
	int 			result = 0;

	/* first check for existing outgoing requests */
	for_each_right(scan, in->rin_owner->ro_outgoing[*]) {
		if (scan->rog_type == otype && scan intersects right) {
			/* @todo adjust outgoing requests priority (priority
			   inheritance) */
			result = c2_rm_pin_add(in, scan);
			C2_ASSERT(result == 0);
			rest = c2_rm_right_diff(rest, scan);
			break;
		}
	}
	C2_ALLOC_PTR(out);
	if (out == NULL)
		return -ENOMEM;
	out->rog_type = otype;
	out->rog_want.rl_other = loan->rl_other;
	out->rog_want.rl_id    = loan->rl_id;
	out->rog_want.rl_right = c2_rm_right_copy(right);
	c2_list_add(&in->rin_owner->ro_outgoing[OQS_GROUND],
		    &out->rog_want.rl_right.ri_linkage);
	result = c2_rm_pin_add(in, &out->rog_want.rl_right);
	C2_ASSERT(result == 0);

	return result;
}

int c2_rm_right_timedwait(struct c2_rm_incoming *in,
                          const struct c2_time *deadline)
{
}

int c2_rm_right_get_wait(struct c2_rm_owner *owner,
			 struct c2_rm_incoming *in)
{
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
