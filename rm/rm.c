/* -*- C -*- */

#include "lib/mutex.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/cdefs.h"
#include "fop/fop.h"

#include "rm/rm.h"
#include "rm/rm_u.h"
#include "rm/rm_fop.h"

/**
   @addtogroup rm
   @{
 */

static void owner_balance(struct c2_rm_owner *o);
static void pin_remove(struct c2_rm_pin *pin);
static int pin_add(struct c2_rm_incoming *in, struct c2_rm_right *right);
static int go_out(struct c2_rm_incoming *in, enum c2_rm_outgoing_type otype,
		  struct c2_rm_loan *loan, struct c2_rm_right *right);
static void incoming_check(struct c2_rm_incoming *in);
static void incoming_check_local(struct c2_rm_incoming *in,
				 struct c2_rm_right *rest);
static void sublet_revoke(struct c2_rm_incoming *in,
			  struct c2_rm_right *right);

void c2_rm_domain_init(struct c2_rm_domain *dom)
{
	C2_PRE(dom != NULL);
	C2_SET_ARR0(dom->rd_types);
	c2_mutex_init(&dom->rd_lock);
}

void c2_rm_domain_fini(struct c2_rm_domain *dom)
{
	bool found = false;
	int  i;

	for (i = 0; i < ARRAY_SIZE(dom->rd_types); i++) {
		if (dom->rd_types[i] != NULL) {
			found = true;
			break;
		}
	}
	C2_PRE(!found);
	c2_mutex_fini(&dom->rd_lock);
}

void c2_rm_type_register(struct c2_rm_domain *dom,
			 struct c2_rm_resource_type *rt)
{
	C2_PRE(rt->rt_dom == NULL);
	C2_PRE(dom->rd_types[rt->rt_id] == NULL);

	c2_mutex_lock(&dom->rd_lock);
	dom->rd_types[rt->rt_id] = rt;
	rt->rt_dom = dom;
	c2_mutex_init(&rt->rt_lock);
	c2_list_init(&rt->rt_resources);
	c2_mutex_unlock(&dom->rd_lock);
	rt->rt_nr_resources = 0;

	C2_POST(IS_IN_ARRAY(rt->rt_id, dom->rd_types));
	C2_POST(rt->rt_dom == dom);
}

void c2_rm_type_deregister(struct c2_rm_resource_type *rtype)
{
	struct c2_rm_domain *dom = rtype->rt_dom;

	C2_PRE(rtype->rt_dom != NULL);
	C2_PRE(IS_IN_ARRAY(rtype->rt_id, dom->rd_types));
	C2_PRE(dom->rd_types[rtype->rt_id] == rtype);

	c2_mutex_lock(&dom->rd_lock);
	dom->rd_types[rtype->rt_id] = NULL;
	rtype->rt_dom = NULL;
	rtype->rt_id = C2_RM_RESOURCE_TYPE_ID_INVALID;
	c2_list_fini(&rtype->rt_resources);
	c2_mutex_fini(&rtype->rt_lock);
	rtype->rt_nr_resources = 0;
	c2_mutex_unlock(&dom->rd_lock);

	C2_POST(rtype->rt_id == C2_RM_RESOURCE_TYPE_ID_INVALID);
	C2_POST(rtype->rt_dom == NULL);
}

void c2_rm_resource_add(struct c2_rm_resource_type *rtype,
			struct c2_rm_resource *res)
{
	struct c2_rm_resource *r;
	bool		       found = false;

	C2_PRE(res->r_ref == 0);

	c2_mutex_lock(&rtype->rt_lock);
	/* rtype->rt_resources does not contain a resource equal to res */
	c2_list_for_each_entry(&rtype->rt_resources, r,
			       struct c2_rm_resource, r_linkage) {
		if (rtype->rt_ops->rto_eq(r, res)) {
			found = true;
			break;
		}
	}
	if (!found) {
		res->r_type = rtype;
		c2_list_add(&rtype->rt_resources, &res->r_linkage);
		rtype->rt_nr_resources++;
	}
	c2_mutex_unlock(&rtype->rt_lock);

	C2_POST(rtype->rt_nr_resources > 0);
	C2_POST(res->r_type == rtype);
	C2_POST(c2_list_contains(&rtype->rt_resources, &res->r_linkage));
}

void c2_rm_resource_del(struct c2_rm_resource *res)
{
	struct c2_rm_resource_type *rtype = res->r_type;

	C2_PRE(rtype->rt_nr_resources > 0);
	C2_PRE(c2_list_contains(&rtype->rt_resources, &res->r_linkage));

	c2_mutex_lock(&rtype->rt_lock);
	c2_list_del(&res->r_linkage);
	rtype->rt_nr_resources--;
	c2_mutex_unlock(&rtype->rt_lock);

	C2_POST(!c2_list_contains(&rtype->rt_resources, &res->r_linkage));
}

static void owner_init_internal(struct c2_rm_owner *owner,
			        struct c2_rm_resource *res)
{
	struct c2_rm_resource_type *rtype = res->r_type;
	int			    i;
	int			    j;

	owner->ro_resource = res;
	owner->ro_state = ROS_INITIALISING;
	c2_mutex_lock(&rtype->rt_lock);
	res->r_ref++;
	c2_mutex_unlock(&rtype->rt_lock);

	c2_list_init(&owner->ro_borrowed);
	c2_list_init(&owner->ro_sublet);

	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); i++)
		c2_list_init(&owner->ro_owned[i]);

	for (i = 0; i < ARRAY_SIZE(owner->ro_incoming); i++) {
		for(j = 0; j < ARRAY_SIZE(owner->ro_incoming[i]); j++)
		c2_list_init(&owner->ro_incoming[i][j]);
	}

	for (i = 0; i < ARRAY_SIZE(owner->ro_outgoing); i++)
		c2_list_init(&owner->ro_outgoing[i]);

	c2_mutex_init(&owner->ro_lock);

}


void c2_rm_owner_init(struct c2_rm_owner *owner, struct c2_rm_resource *res)
{
	C2_PRE(owner->ro_state == ROS_FINAL);

	owner_init_internal(owner, res);

	C2_POST((owner->ro_state == ROS_INITIALISING ||
		 owner->ro_state == ROS_ACTIVE) &&
		 (owner->ro_resource == res));
}

void c2_rm_owner_init_with(struct c2_rm_owner *owner,
			   struct c2_rm_resource *res, struct c2_rm_right *r)
{

	C2_PRE(owner->ro_state == ROS_FINAL);

	/* Add The right to the woner's cached list.*/
	owner_init_internal(owner, res);
	owner->ro_state = ROS_ACTIVE;
	c2_list_add(&owner->ro_owned[OWOS_CACHED], &r->ri_linkage);

	C2_POST((owner->ro_state == ROS_INITIALISING ||
		 owner->ro_state == ROS_ACTIVE) &&
		 (owner->ro_resource == res));
	C2_POST(c2_list_contains(&owner->ro_owned[OWOS_CACHED],
				 &r->ri_linkage));
}

void c2_rm_owner_fini(struct c2_rm_owner *owner)
{
	struct c2_rm_resource	   *res = owner->ro_resource;
	struct c2_rm_resource_type *rtype = res->r_type;
	int			    i;
	int			    j;

	C2_PRE(owner->ro_state == ROS_FINAL);
	C2_PRE(c2_list_is_empty(&owner->ro_borrowed));
	C2_PRE(c2_list_is_empty(&owner->ro_sublet));

	c2_list_fini(&owner->ro_borrowed);
	c2_list_fini(&owner->ro_sublet);

	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); i++) {
		C2_PRE(c2_list_is_empty(&owner->ro_owned[i]));
		c2_list_fini(&owner->ro_owned[i]);
	}

	for (i = 0; i < ARRAY_SIZE(owner->ro_incoming); i++) {
		for(j = 0; j < ARRAY_SIZE(owner->ro_incoming[i]); j++)
			C2_PRE(c2_list_is_empty(&owner->ro_incoming[i][j]));
			c2_list_fini(&owner->ro_incoming[i][j]);
	}

	for (i = 0; i < ARRAY_SIZE(owner->ro_outgoing); i++) {
		C2_PRE(c2_list_is_empty(&owner->ro_outgoing[i]));
		c2_list_fini(&owner->ro_outgoing[i]);
	}

	owner->ro_resource = NULL;
	c2_mutex_lock(&rtype->rt_lock);
	res->r_ref--;
	c2_mutex_unlock(&rtype->rt_lock);
	c2_mutex_fini(&owner->ro_lock);
}

void c2_rm_right_init(struct c2_rm_right *right)
{
	C2_PRE(right != NULL);
	c2_list_init(&right->ri_pins);
}

void c2_rm_right_fini(struct c2_rm_right *right)
{
	C2_PRE(c2_list_is_empty(&right->ri_pins));
	c2_list_fini(&right->ri_pins);
}

void c2_rm_incoming_init(struct c2_rm_incoming *in)
{
	C2_PRE(in != NULL);
	c2_list_init(&in->rin_pins);
}

void c2_rm_incoming_fini(struct c2_rm_incoming *in)
{
	C2_PRE(c2_list_is_empty(&in->rin_pins));
	c2_list_fini(&in->rin_pins);
}

/**
 * Revoke or cacelation of rights will require this.
 */
static void remove_rights(struct c2_rm_incoming *in)
{
	struct c2_rm_pin   *in_pin;
	struct c2_rm_pin   *ri_pin;
	struct c2_rm_pin   *in_tmp;
	struct c2_rm_pin   *ri_tmp;
	struct c2_rm_right *right;

	c2_list_for_each_entry_safe(&in->rin_pins, in_pin, in_tmp,
				    struct c2_rm_pin, rp_incoming_linkage) {
		right = in_pin->rp_right;
		c2_list_for_each_entry_safe(&right->ri_pins, ri_pin, ri_tmp,
					    struct c2_rm_pin, rp_right_linkage) {
			if (ri_pin->rp_flags & RPF_TRACK) {
				pin_remove(ri_pin);
			}
		}
	}
}

/**
 * Frees the outgoing request after completion.
 */
static void outgoing_delete(struct c2_rm_outgoing *out)
{
	C2_PRE(out != NULL);

	c2_list_del(&out->rog_want.rl_right.ri_linkage);
	c2_free(out);
}

/**
 * Just make another copy of right struct.
 */
static void right_copy(struct c2_rm_right *dest,
		       const struct c2_rm_right *src)
{
	C2_PRE(src != NULL);

	dest->ri_resource = src->ri_resource;
	dest->ri_ops = src->ri_ops;
	c2_list_init(&dest->ri_pins);
	src->ri_ops->rro_copy(dest, src);

	/* Two rights are equal when both rights implies to each other */
	C2_POST(src->ri_ops->rro_implies(dest, src) &&
		src->ri_ops->rro_implies(src, dest));
}

/**
 * Apply universal policies
 */
static int apply_policy(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
	struct c2_rm_right *right;
	struct c2_rm_right *pin_right;
	struct c2_rm_pin   *pin;
	struct c2_rm_pin   *pin_tmp;
	bool 		    first;

	switch (in->rin_policy) {
		case RIP_NONE:
		case RIP_RESOURCE_TYPE_BASE:
			break;
		case RIP_INPLACE:
			c2_list_for_each_entry(&in->rin_pins, pin,
					       struct c2_rm_pin,
					       rp_incoming_linkage) {
				c2_list_move(&in->rin_want.ri_pins,
					     &pin->rp_right_linkage);
				pin->rp_right = &in->rin_want;

			}
			break;
		case RIP_STRICT:
			/* 
			 * Remove all pined rights and insert right equivalent
			 * to requested right.
			 */
			remove_rights(in);
			C2_ALLOC_PTR(right);
			if (right == NULL)
				return -ENOMEM;
			right_copy(right, &in->rin_want);
			pin_add(in, right);
			c2_list_add(&owner->ro_owned[OWOS_CACHED],
				    &right->ri_linkage);

			break;
		case RIP_JOIN:
			/* 
			 * New right equivalent to joined rights of granted
			 * rights is inserted into owned rights list.
			 */
			if (c2_list_length(&in->rin_pins) <= 1)
				break;

			first = true;
			C2_ALLOC_PTR(right);
			if (right == NULL)
				return -ENOMEM;
			c2_list_for_each_entry_safe(&in->rin_pins, pin, pin_tmp,
						    struct c2_rm_pin,
						    rp_incoming_linkage) {
				pin_right = pin->rp_right;
				if (first) {
					pin_right->ri_ops->rro_copy(right,
								    pin_right);
					first = false;
					pin_remove(pin);
					continue;
				}
				right->ri_ops->rro_join(right, pin_right);
				pin_remove(pin);
			}
			pin_add(in, right);
			c2_list_add(&owner->ro_owned[OWOS_CACHED],
				    &right->ri_linkage);
			break;
		case RIP_MAX:
			/* 
			 * Maxinum rights which implies to requested right
			 * are granted.
			 */
			c2_list_for_each_entry(&owner->ro_owned[OWOS_CACHED],
					       right, struct c2_rm_right,
					       ri_linkage) {
				c2_list_for_each_entry(&in->rin_pins, pin,
						       struct c2_rm_pin,
						       rp_incoming_linkage) {
					if (right->ri_ops->rro_implies(right,
								       &in->
								       rin_want)
								       &&
					    !c2_list_contains(&right->ri_pins,
					    		      &pin->
							      rp_right_linkage))
						pin_add(in, right);
				}
			}
	}

	return 0;
}

/**
 * Info about remote domain is filled.
 */
static void remote_copy(const struct c2_rm_incoming *in,
			struct c2_rm_remote *rem)
{
	/*@todo locate remote domain of request
	 * and init c2_rm_remote members */
	//rem->rem_resource->r_ref++;
}

/**
 * The rights(as loan) are moved to sublet list of owner
 * as the request came for loan.
 */
static int move_to_sublet(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
	struct c2_rm_pin   *pin;
	struct c2_rm_pin   *pin_tmp;
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;

	C2_PRE(c2_mutex_is_locked(&owner->ro_lock));

	c2_list_for_each_entry_safe(&in->rin_pins, pin, pin_tmp,
				    struct c2_rm_pin, rp_incoming_linkage) {
		right = pin->rp_right;
		C2_ALLOC_PTR(loan);
		if (loan == NULL)
			return -ENOMEM;
		right_copy(&loan->rl_right, right);
		remote_copy(in, &loan->rl_other);
		c2_list_move(&owner->ro_sublet, &right->ri_linkage);
	}

	return 0;
}

#if 0
static int netcall(struct c2_net_conn *conn, struct c2_fop *arg,
		   struct c2_fop *ret)
{
	struct c2_net_call call = {
		.ac_arg = arg,
		.ac_ret = ret
	};
	return c2_net_cli_call(conn, &call);
}

/**
 * Reply to the incoming loan request when wanted rights granted
 */
static int reply_to_loan_request(struct c2_rm_incoming *in)
{
	struct c2_rm_right	*right;
	struct c2_rm_pin	*pin;
	struct c2_rm_loan	*loan;
	struct c2_fop		*f;
	struct c2_fop		*r;
	struct c2_rm_loan_reply *fop;
	struct c2_rm_loan_reply *rep;
	struct c2_net_conn	*conn;
	struct c2_service_id	 sid;
	int			 result = 0;

	c2_list_for_each_entry(&in->rin_pins, pin, struct c2_rm_pin,
			       rp_right_linkage) {
		right = pin->rp_right;
		loan = container_of(right, struct c2_rm_loan, rl_right);
		/*@todo Form fop for each loan and reply or
		 * we can apply rpc grouping and send reply
		 */
		sid = loan->rl_other.rem_service;
		conn = c2_net_conn_find(&sid);
		C2_ASSERT(conn != NULL);

		f = c2_fop_alloc(&c2_rm_loan_reply_fopt, NULL);
		fop = c2_fop_data(f);
		r = c2_fop_alloc(&c2_rm_loan_reply_fopt, NULL);
		rep = c2_fop_data(r);

		fop->loan_id = loan->rl_id;
		fop->rem_id = loan->rl_other.rem_id;
		/*May be right_copy used later*/
		fop->ri_datum = loan->rl_right.ri_datum;

		result = netcall(conn, f, r);
	}
	return result;
}
/**
 * It sends out outgoing excited requests
 */
static int send_out_request(struct c2_rm_outgoing *out)
{
	struct c2_fop	      *f;
	struct c2_fop	      *r;
	struct c2_rm_send_out *fop;
	struct c2_rm_send_out *rep;
	struct c2_net_conn    *conn;
	struct c2_service_id   sid;
	int 		       result = 0;

	sid = out->rog_want.rl_other.rem_service;
	conn = c2_net_conn_find(&sid);
	C2_ASSERT(conn != NULL);

	f = c2_fop_alloc(&c2_rm_send_out_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_rm_send_out_fopt, NULL);
	rep = c2_fop_data(r);

	fop->req_type = out->rog_type;
	fop->loan_id = out->rog_want.rl_id;
	fop->rem_id = out->rog_want.rl_other.rem_id;
	/*May be right_copy used later*/
	fop->ri_datum = out->rog_want.rl_right.ri_datum;
	fop->rem_state = out->rog_want.rl_other.rem_state;

	result = netcall(conn, f, r);
	return result;
}

#endif

/**
 * Check for conflict.
 */
static bool local_rights_held(const struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
	struct c2_rm_pin   *pin;
	struct c2_rm_right *right;

	if (in->rin_type != RIT_LOCAL) {
		c2_list_for_each_entry(&in->rin_pins, pin, struct c2_rm_pin,
				       rp_incoming_linkage) {
			right = pin->rp_right;
			if (c2_list_contains(&owner->ro_owned[OWOS_HELD],
					     &right->ri_linkage))
				return true;
		}
	}
	return false;
}



/**
 * resource type private field. By convention, 0 means "empty"
 * right.
 */
static bool right_is_empty(const struct c2_rm_right *right)
{
	return right->ri_datum == 0;
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

   Event handling is serialised by the owner lock. It is not legal to wait for
   networking or IO events under this lock.

   Pseudo-code below omits error checking and some details too prolix to
   narrate in a design specification.
 */
/** @{ */

/**
   External resource manager entry point: request a right from the resource
   owner.
 */
void c2_rm_right_get(struct c2_rm_owner *owner, struct c2_rm_incoming *in)
{
	C2_PRE(IS_IN_ARRAY(in->rin_priority, owner->ro_incoming));
	C2_PRE(in->rin_state == RI_INITIALISED);
	C2_PRE(c2_list_is_empty(&in->rin_pins));

	c2_mutex_lock(&owner->ro_lock);
	/*
	 * Mark incoming request "excited". owner_balance() will process it.
	 */
	c2_list_add(&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
		    &in->rin_want.ri_linkage);
	owner_balance(owner);
	c2_mutex_unlock(&owner->ro_lock);
}

/**
   External resource manager entry point that releases a right.
 */
void c2_rm_right_put(struct c2_rm_incoming *in)
{
	C2_PRE(in->rin_state == RI_SUCCESS);

	c2_mutex_lock(&in->rin_owner->ro_lock);
	remove_rights(in);
	c2_mutex_unlock(&in->rin_owner->ro_lock);
	C2_POST(c2_list_is_empty(&in->rin_pins));
}

/**
   Called when an outgoing request completes (possibly with an error, like a
   timeout).
 */
static void outgoing_complete(struct c2_rm_outgoing *og, int rc)
{
	struct c2_rm_owner *owner;

	C2_PRE(og != NULL);

	/*@todo rc return related things */
	owner = og->rog_owner;
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
	struct c2_rm_incoming 	*in;
	struct c2_rm_owner	*owner;

	C2_ASSERT(pin != NULL);

	in = pin->rp_incoming;
	owner = in->rin_owner;
	c2_list_del(&pin->rp_incoming_linkage);
	c2_list_del(&pin->rp_right_linkage);
	c2_free(pin);
	if (c2_list_is_empty(&in->rin_pins)) {
		/*
		 * Last pin removed, excite the request.
		 */
		c2_list_move(&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			     &in->rin_want.ri_linkage);
	}
}

/**
   Main owner state machine function.

   Goes through the lists of excited incoming and outgoing requests until all
   the excitement is gone.
 */
static void owner_balance(struct c2_rm_owner *o)
{
	struct c2_rm_pin	*pin;
	struct c2_rm_pin	*pin_tmp;
	struct c2_rm_loan	*loan;
	struct c2_rm_right	*right;
	struct c2_rm_right	*ri_tmp;
	struct c2_rm_outgoing	*out;
	struct c2_rm_incoming	*in;
	bool			 todo;
	int			 prio;


	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	do {
		todo = false;
		c2_list_for_each_entry_safe(&o->ro_outgoing[OQS_EXCITED],
					    right, ri_tmp, struct c2_rm_right,
					    ri_linkage) {

			todo = true;
			loan = container_of(right, struct c2_rm_loan, rl_right);
			C2_ASSERT(loan != NULL);
			out = container_of(loan,
					   struct c2_rm_outgoing, rog_want);
			C2_ASSERT(out != NULL);
			/*
			 * Outgoing request completes.
			 */
			c2_list_for_each_entry_safe(&out->rog_want.rl_right.ri_pins,
						    pin, pin_tmp, struct c2_rm_pin,
						    rp_incoming_linkage)
				pin_remove(pin);

			outgoing_delete(out);
		}
		for (prio = ARRAY_SIZE(o->ro_incoming); prio >= 0; prio--) {
			c2_list_for_each_entry_safe(&o->ro_incoming[prio]
						    [OQS_EXCITED],right, ri_tmp,
						    struct c2_rm_right,
						    ri_linkage) {

				todo = true;
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
				c2_list_move(&o->ro_incoming[prio][OQS_GROUND],
					     &in->rin_want.ri_linkage);
				in->rin_state = RI_CHECK;
				incoming_check(in);

				if (in->rin_state == RI_SUCCESS ||
				    in->rin_state == RI_FAILURE) {
				    in->rin_ops->rio_complete(in,
							      in->rin_state);
				}
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
	struct c2_rm_owner *o = in->rin_owner;
	struct c2_rm_loan  *loan;
	struct c2_rm_right  rest;
	int		    result;

	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(c2_list_contains(&o->ro_incoming[in->rin_priority][OQS_GROUND],
				&in->rin_want.ri_linkage));
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
	right_copy(&rest, &in->rin_want);

	/*
	 * Check for "local" wait conditions.
	 */
	incoming_check_local(in, &rest);

	if (right_is_empty(&rest)) {
		/*
		 * The wanted right is completely covered by the local
		 * rights. There are no remote conditions to wait for.
		 */
		if (local_rights_held(in)) {
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
			result = apply_policy(in);
			C2_ASSERT(result == 0);
			switch (in->rin_type) {
			case RIT_LOAN:
				move_to_sublet(in);
				/*@todo uncomment when producer consumer is ready*/
				//reply_to_loan_request(in);
			case RIT_LOCAL:
				break;
			case RIT_REVOKE:
				remove_rights(in);
				/* Incoming request got rigths which is means
				 * right should be part of loan/borrowed list.
				 */
				loan = container_of(&in->rin_want,
						    struct c2_rm_loan,
						    rl_right);
				go_out(in, ROT_CANCEL, loan, &in->rin_want);
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
			sublet_revoke(in, &rest);
		if (in->rin_flags & RIF_MAY_BORROW) {
			/* borrow more */
			while (!right_is_empty(&rest)) {
				struct c2_rm_loan borrow;
				/** @todo implement net_locate */
				//c2_rm_net_locate(rest, &borrow);
				go_out(in, ROT_BORROW,
				       &borrow, &borrow.rl_right);
			}
		}
		if (right_is_empty(&rest)) {
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
static void incoming_check_local(struct c2_rm_incoming *in,
			  	 struct c2_rm_right *rest)
{
	struct c2_rm_right *right;
	struct c2_rm_owner *o = in->rin_owner;
	bool 		    track_local = false;
	bool 		    coverage = false;
	int		    i;

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

	for (i = 0; i < ARRAY_SIZE(o->ro_owned); ++i) {
		c2_list_for_each_entry(&o->ro_owned[i], right,
				       struct c2_rm_right, ri_linkage) {
			if (rest->ri_ops->rro_implies(right, rest)) {
				pin_add(in, right);
				if (coverage) {
					rest->ri_ops->rro_diff(rest, right);
					if (right_is_empty(rest))
						return;
				}
			}
		}
	}
}

/**
   Revokes @right (or parts thereof) sub-let to downward owners.
 */
static void sublet_revoke(struct c2_rm_incoming *in,
			  struct c2_rm_right *rest)
{
	struct c2_rm_owner *o = in->rin_owner;
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;

	c2_list_for_each_entry(&o->ro_sublet, right,
			       struct c2_rm_right, ri_linkage) {
		if (rest->ri_ops->rro_implies(right, rest)) {
			rest->ri_ops->rro_diff(rest, right);
			loan = container_of(right, struct c2_rm_loan, rl_right);
			/*
			 * It is possible that this loop emits multiple
			 * outgoing requests toward the same remote
			 * owner. Don't bother to coalesce them here. The rpc
			 * layer would do this more efficiently.
			 */
			rest->ri_ops->rro_meet(rest, right);
			go_out(in, ROT_REVOKE, loan, rest);
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

	/*
	* Check whether pin can be added to given right
	*/
	c2_list_for_each_entry(&right->ri_pins, pin, struct c2_rm_pin,
			       rp_right_linkage) {
		if (pin->rp_flags & RPF_BARRIER)
			return -EPERM;
	}

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
	struct c2_rm_outgoing	*out;
	struct c2_rm_right 	*out_right;
	struct c2_rm_right 	*tmp;
	struct c2_rm_loan	*out_loan;
	int 			 result = 0;
	int			 i;
	bool			 found = false;

	/* first check for existing outgoing requests */
	for (i = 0; i < ARRAY_SIZE(in->rin_owner->ro_outgoing); i++) {
		c2_list_for_each_entry_safe(&in->rin_owner->ro_outgoing[i],
				       out_right, tmp, struct c2_rm_right,
				       ri_linkage) {
			out_loan = container_of(out_right,
						struct c2_rm_loan, rl_right);
			out = container_of(out_loan,
					      struct c2_rm_outgoing, rog_want);
			if (out->rog_type == otype &&
				right->ri_ops->rro_implies(out_right, right)) {
				/* @todo adjust outgoing requests priority
				 * (priority inheritance) */
				result = pin_add(in, out_right);
				if (result)
					return result;
				right->ri_ops->rro_diff(right, out_right);
				found = true;
				break;
			}
		}
		if (found)
			break;
	}

	if (!found) {
		C2_ALLOC_PTR(out);
		if (out == NULL)
			return -ENOMEM;
		out->rog_type = otype;
		out->rog_want.rl_other = loan->rl_other;
		out->rog_want.rl_id    = loan->rl_id;
		right_copy(&out->rog_want.rl_right, right);
		c2_list_add(&in->rin_owner->ro_outgoing[OQS_GROUND],
		    	&out->rog_want.rl_right.ri_linkage);
		result = pin_add(in, &out->rog_want.rl_right);
	}
	/*@todo uncomment when producer consumer is redy*/
	//result = send_out_request(out);

	outgoing_complete(out, result);

	return result;
}

int c2_rm_right_timedwait(struct c2_rm_incoming *in,
			  const c2_time_t deadline)
{
	struct c2_rm_owner *owner = in->rin_owner;
	struct c2_clink     clink;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&in->rin_signal, &clink);
	c2_rm_right_get(owner,in);
	if (in->rin_state != RI_SUCCESS ||
	    in->rin_state != RI_FAILURE) {
		c2_chan_timedwait(&clink, deadline);
	}
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	if (in->rin_state != RI_SUCCESS ||
	    in->rin_state != RI_FAILURE)
		return -ETIMEDOUT;

	return in->rin_state != RI_SUCCESS;
}

int c2_rm_right_get_wait(struct c2_rm_owner *owner,
			 struct c2_rm_incoming *in)
{
	struct c2_clink clink;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&in->rin_signal, &clink);
	c2_rm_right_get(owner,in);
	if (in->rin_state != RI_SUCCESS ||
	    in->rin_state != RI_FAILURE) {
		c2_chan_wait(&clink);
	}
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return in->rin_state != RI_SUCCESS;
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
