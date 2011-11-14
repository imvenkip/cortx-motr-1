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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 04/28/2011
 */
#include "lib/memory.h" /* C2_ALLOC_PTR */
#include "lib/misc.h"   /* C2_SET_ARR0 */
#include "lib/errno.h"  /* ETIMEDOUT */
#include "lib/arith.h"  /* C2_CNT_{INC,DEC} */

#include "rm/rm.h"

/*@todo remove after RM:Fops is developed */
#include "rm/ut/rmproto.h" /* PRO_LOAN_REPLY,PRO_OUT_REQUEST */
/**
   @addtogroup rm
   @{
 */

static void owner_balance(struct c2_rm_owner *o);
static void pin_remove(struct c2_rm_pin *pin, bool move);
static int  go_out(struct c2_rm_incoming *in, enum c2_rm_outgoing_type otype,
		   struct c2_rm_loan *loan, struct c2_rm_right *right);
static void incoming_check(struct c2_rm_incoming *in);
static void incoming_check_local(struct c2_rm_incoming *in,
				 struct c2_rm_right *rest);
static int  sublet_revoke(struct c2_rm_incoming *in,
			  struct c2_rm_right *right);
int pin_add(struct c2_rm_incoming *in, struct c2_rm_right *right);

C2_TL_DESCR_DEFINE(res, "resources", , struct c2_rm_resource,
		   r_linkage, r_magix,
		   0xb1f01d5add1eb111 /* bifold saddlebill */,
		   0xc1a551ca15eedbed /* classical seedbed */);
C2_TL_DEFINE(res, , struct c2_rm_resource);

C2_TL_DESCR_DEFINE(ur, "usage rights", , struct c2_rm_right,
		   ri_linkage, ri_magix,
		   0xc0a1faceba5111ca /* coalface basilica */,
		   0xca11ab1e5111c1de /* callable silicide */);
C2_TL_DEFINE(ur, , struct c2_rm_right);

enum {
	PIN_MAGIX = 0xD15CA1CED0551C1E /* discalced ossicle */
};

C2_TL_DESCR_DEFINE(pr, "pins-of-right", , struct c2_rm_pin,
		   rp_right_linkage, rp_magix,
		   PIN_MAGIX,
		   0x11d1e55c010ca51a /* lidless colocasia */);
C2_TL_DEFINE(pr, , struct c2_rm_pin);

C2_TL_DESCR_DEFINE(pi, "pins-of-incoming", , struct c2_rm_pin,
		   rp_incoming_linkage, rp_magix,
		   PIN_MAGIX,
		   0x11fe512e51da1cea /* lifesize sidalcea */);
C2_TL_DEFINE(pi, , struct c2_rm_pin);

void c2_rm_domain_init(struct c2_rm_domain *dom)
{
	C2_PRE(dom != NULL);

	C2_SET_ARR0(dom->rd_types);
	c2_mutex_init(&dom->rd_lock);
}
C2_EXPORTED(c2_rm_domain_init);

void c2_rm_domain_fini(struct c2_rm_domain *dom)
{
	int  i;

	for (i = 0; i < ARRAY_SIZE(dom->rd_types); i++)
		C2_ASSERT(dom->rd_types[i] == NULL);

	c2_mutex_fini(&dom->rd_lock);
}
C2_EXPORTED(c2_rm_domain_fini);

/**
   Returns a resource equal to a given one from a resource type's resource list
   or NULL if none.
 */
static struct c2_rm_resource *res_find(const struct c2_rm_resource_type *rt,
				       const struct c2_rm_resource *res)
{
	struct c2_rm_resource *scan;

	c2_tlist_for(&res_tl, (struct c2_tl *)&rt->rt_resources, scan) {
		if (rt->rt_ops->rto_eq(res, scan))
			break;
	} c2_tlist_endfor;
	return scan;
}

/** Helper function used by resource_type_invariant() to check all elements of
    c2_rm_resource_type::rt_resources. */
static bool resource_list_check(const struct c2_rm_resource *res, void *datum)
{
	const struct c2_rm_resource_type *rt = datum;

	return res_find(datum, res) == res && res->r_type == rt;
}

static bool resource_type_invariant(const struct c2_rm_resource_type *rt)
{
	struct c2_rm_domain   *dom   = rt->rt_dom;
	struct c2_tl          *rlist = &rt->rt_resources;

	return
		res_tlist_invariant_ext(rlist, resource_list_check) &&
		rt->rt_nr_resources == res_tlist_length(rlist) &&
		dom != NULL && IS_IN_ARRAY(rt->rt_id, dom->rd_types) &&
		dom->rd_types[rt->rt_id] == rt;
}

void c2_rm_type_register(struct c2_rm_domain *dom,
			 struct c2_rm_resource_type *rt)
{
	C2_PRE(rt->rt_dom == NULL);
	C2_PRE(IS_IN_ARRAY(rt->rt_id, dom->rd_types));
	C2_PRE(dom->rd_types[rt->rt_id] == NULL);

	c2_mutex_init(&rt->rt_lock);
	res_tlist_init(&rt->rt_resources);
	rt->rt_nr_resources = 0;

	c2_mutex_lock(&dom->rd_lock);
	dom->rd_types[rt->rt_id] = rt;
	rt->rt_dom = dom;
	C2_POST(resource_type_invariant(rt));
	c2_mutex_unlock(&dom->rd_lock);

	C2_POST(dom->rd_types[rt->rt_id] == rt);
	C2_POST(rt->rt_dom == dom);
}
C2_EXPORTED(c2_rm_type_register);

void c2_rm_type_deregister(struct c2_rm_resource_type *rtype)
{
	struct c2_rm_domain *dom = rtype->rt_dom;

	C2_PRE(dom != NULL);
	C2_PRE(IS_IN_ARRAY(rtype->rt_id, dom->rd_types));
	C2_PRE(dom->rd_types[rtype->rt_id] == rtype);
	C2_PRE(res_tlist_is_empty(&rtype->rt_resources));
	C2_PRE(rtype->rt_nr_resources == 0);

	c2_mutex_lock(&dom->rd_lock);
	C2_PRE(resource_type_invariant(rt));

	dom->rd_types[rtype->rt_id] = NULL;
	rtype->rt_dom = NULL;
	rtype->rt_id = C2_RM_RESOURCE_TYPE_ID_INVALID;
	res_tlist_fini(&rtype->rt_resources);
	c2_mutex_fini(&rtype->rt_lock);
	c2_mutex_unlock(&dom->rd_lock);

	C2_POST(rtype->rt_id == C2_RM_RESOURCE_TYPE_ID_INVALID);
	C2_POST(rtype->rt_dom == NULL);
}
C2_EXPORTED(c2_rm_type_deregister);

void c2_rm_resource_add(struct c2_rm_resource_type *rtype,
			struct c2_rm_resource *res)
{
	struct c2_rm_resource *r;

	c2_mutex_lock(&rtype->rt_lock);
	C2_PRE(resource_type_invariant(rt));
	C2_PRE(res->r_ref == 0);
	C2_PRE(res_find(rtype, res) == NULL);
	res->r_type = rtype;
	res_tlink_init_at(res, &rtype->rt_resources);
	C2_CNT_INC(rtype->rt_nr_resources);
	C2_POST(res_tlist_contains(&rtype->rt_resources, res));
	C2_POST(resource_type_invariant(rt));
	c2_mutex_unlock(&rtype->rt_lock);
	C2_POST(res->r_type == rtype);
}
C2_EXPORTED(c2_rm_resource_add);

void c2_rm_resource_del(struct c2_rm_resource *res)
{
	struct c2_rm_resource_type *rtype = res->r_type;

	c2_mutex_lock(&rtype->rt_lock);
	C2_PRE(res_tlist_contains(&rtype->rt_resources, res));
	C2_PRE(resource_type_invariant(rt));

	res_tlink_del_fini(res);
	C2_CNT_DEC(rtype->rt_nr_resources);

	C2_POST(resource_type_invariant(rt));
	C2_POST(!res_tlist_contains(&rtype->rt_resources, res));
	c2_mutex_unlock(&rtype->rt_lock);
}
C2_EXPORTED(c2_rm_resource_del);

bool right_eq(const struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	/* no apples and oranges comparison. */
	C2_PRE(r0->ri_resource == r1->ri_resource);
	/* Two rights are equal if each implies the other */
	return
		r0->ri_ops->rro_implies(r0, r1) &&
		r1->ri_ops->rro_implies(r1, r0)));
}

static bool incoming_invariant(const struct c2_rm_incoming *in)
{
	return
		(in->rin_rc != 0) == (in->rin_state == RI_FAILURE) &&
		/* a request can be in "check" state only during owner_balance()
		   execution. */
		in->rin_state != RI_CHECK &&
		pi_tlist_invariant(&in->rin_pins) &&
		/* a request in the WAIT state... */
		ergo(in->rin_state == RI_WAIT,
		     /* waits on something... */
		     incoming_pin_nr(in, RPF_TRACK) > 0 &&
		     /* and doesn't hold anything. */
		     incoming_pin_nr(in, RPF_PROTECT) == 0) &&
		/* a fulfilled request... */
		ergo(in->rin_state == RI_SUCCESS,
		     /* holds something... */
		     incoming_pin_nr(in, RPF_PROTECT) > 0 &&
		     /* and waits on nothing. */
		     incoming_pin_nr(in, RPF_TRACK) == 0) &&
		ergo(in->rin_state == RI_FAILURE ||
		     in->rin_state == RI_INITIALISED,
		     incoming_pin_nr(in, ~0) == 0);
}

/**
   Number of pins with a given flag combination, stuck in a given right.
 */
static int right_pin_nr(const struct c2_rm_right *right, uint32_t flags)
{
	int nr;
	struct c2_rm_pin *pin;

	nr = 0;
	c2_tlist_for(&pr_tl, &right->ri_pins, pin) {
		if (pin->rp_flags & flags)
			++nr;
	} c2_tlist_endfor;
	return nr;
}

/**
   Number of pins with a given flag combination, stuck in a given right.
 */
static int incoming_pin_nr(const struct c2_rm_incoming *in, uint32_t flags)
{
	int nr;
	struct c2_rm_pin *pin;

	nr = 0;
	c2_tlist_for(&pi_tl, &in->rin_pins, pin) {
		if (pin->rp_flags & flags)
			++nr;
	} c2_tlist_endfor;
	return nr;
}

struct owner_invariant_state {
	enum {
		OIS_BORROWED = 0,
		OIS_SUBLET,
		OIS_OUTGOING,
		OIS_OWNED,
		OIS_INCOMING,
		OIS_NR
	}                   is_phase;
	int                 is_owned_idx;
	struct c2_rm_right  is_debit;
	struct c2_rm_right  is_credit;
	struct c2_rm_owner *is_owner;
};

static bool right_check(const struct c2_rm_right *right, void *datum)
{
	struct owner_invariant_state *s = datum;

	if (s->is_phase == OIS_BORROWED)
		s->is_credit->ri_ops->rro_join(s->is_credit, right);
	if (s->is_phase == OIS_SUBLET || s->is_phase == OIS_OWNED)
		s->is_debit->ri_ops->rro_join(s->is_credit, right);

	return
		(s->is_owner->ro_resource != right->ri_resource) &&
		ergo(s->is_phase == OIS_OWNED,
		     ((s->is_owned_idx == OWOS_HELD) ==
		      right_pin_nr(right, RPF_PROTECT))) &&
		ergo(s->is_phase == OIS_INCOMING,
		     incoming_invariant(container_of(right,
						     struct c2_rm_incoming,
						     rin_want)));
}

/**
   Checks internal consistency of a resource owner.
 */
static bool owner_invariant_state(const struct c2_rm_owner *owner,
				  struct owner_invariant_state *is)
{
	int i;
	int j;

	if (owner->ro_state < ROS_FINAL || owner->ro_state > ROS_FINALISING)
		return false;
	/*
	  Iterate over all rights lists:

	      - checking their consistency as double-linked lists
                (ur_tlist_invariant_ext());

	      - making additional consistency checks:

	            - that a right is for the same resource as the owner,

		    - that a right on c2_rm_owner::ro_owned[X] is pinned iff X
                      == OWOS_HELD.

	      - accumulating total credit and debit.
	*/
	is->is_phase = OIS_BORROWED;
	if (!ur_tlist_invariant_ext(owner->ro_borrowed, &right_check, is))
		return false;
	is->is_phase = OIS_SUBLET;
	if (!ur_tlist_invariant_ext(owner->ro_sublet,   &right_check, is))
		return false;
	is->is_phase = OIS_OUTGOING;
	if (!ur_tlist_invariant_ext(owner->ro_outgoing, &right_check, is))
		return false;

	is->is_phase = OIS_OWNED;
	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		is->is_owned_idx = i;
		if (ur_tlist_invariant_ext(owner->ro_owned[i],
					   &right_check, is))
		    return false;
	}
	is->is_phase = OIS_INCOMING;
	for (i = 0; i < ARRAY_SIZE(owner->ro_incoming); ++i) {
		for (j = 0; j < ARRAY_SIZE(owner->ro_incoming[i]); ++j) {
			if (ur_tlist_invariant(owner->ro_incoming[i][j],
					       &right_check, is))
				return false;
		}
	}
	return true;
}

/**
   Checks internal consistency of a resource owner.
 */
static bool owner_invariant(const struct c2_rm_owner *owner)
{
	bool                         result;
	struct owner_invariant_state is;

	C2_SET0(&is);

	c2_rm_right_init(&is.is_debit);
	c2_rm_right_init(&is.is_credit);

	result = owner_invariant_state(owner, &is) &&
		/* books must be balanced. */
		right_eq(&is.is_debit, &is.is_credit);

	c2_rm_right_fini(&is.is_debit);
	c2_rm_right_fini(&is.is_credit);
	return result;
}

static void resource_get(struct c2_rm_resource *res)
{
	struct c2_rm_resource_type *rtype = res->r_type;

	c2_mutex_lock(&rtype->rt_lock);
	C2_CNT_INC(res->r_ref);
	c2_mutex_unlock(&rtype->rt_lock);
}

static void resource_put(struct c2_rm_resource *res)
{
	struct c2_rm_resource_type *rtype = res->r_type;

	c2_mutex_lock(&rtype->rt_lock);
	C2_CNT_DEC(res->r_ref);
	c2_mutex_unlock(&rtype->rt_lock);
}

static void owner_init_internal(struct c2_rm_owner *owner,
			        struct c2_rm_resource *res)
{
	owner->ro_resource = res;
	owner->ro_state = ROS_INITIALISING;
	owner->ro_group = NULL;

	RM_OWNER_LISTS_FOR(owner, ur_tlist_init);
	c2_mutex_init(&owner->ro_lock);

	resource_get(res);
	owner->ro_state = ROS_ACTIVE;
	C2_POST(owner_invariant(owner));
}


void c2_rm_owner_init(struct c2_rm_owner *owner, struct c2_rm_resource *res,
		      struct c2_rm_remote *creditor)
{
	C2_PRE(owner->ro_state == ROS_FINAL);
	C2_PRE(creditor->rem_state >= REM_SERVICE_LOCATED);

	owner_init_internal(owner, res);
	owner->ro_creditor = creditor;

	C2_POST((owner->ro_state == ROS_INITIALISING ||
		 owner->ro_state == ROS_ACTIVE) && owner->ro_resource == res);
}
C2_EXPORTED(c2_rm_owner_init);

int c2_rm_owner_init_with(struct c2_rm_owner *owner,
			  struct c2_rm_resource *res, struct c2_rm_right *r)
{
	struct c2_rm_loan *nominal_capital;
	int                result;

	C2_PRE(owner->ro_state == ROS_FINAL);

	C2_ALLOC_PTR(nominal_capital);
	if (nominal_capital != NULL) {
		result = right_copy(&nominal_capital->rl_right, r);
		if (result == 0) {
			nominal_capital->rl_id = C2_RM_LOAN_SELF_ID;
			owner_init_internal(owner, res);
			owner->ro_creditor = NULL;
			/* Add the right to the owner's cached list. */
			ur_tlist_add(&owner->ro_owned[OWOS_CACHED], r);
			/* Add self-loan to the borrowed list. */
			ur_tlist_add(&owner->ro_borrowed,
				     &nominal_capital->rl_right);
		} else
			c2_free(nominal_capital);
	} else
		result = -ENOMEM;

	C2_POST(ergo(result == 0,
		     (owner->ro_state == ROS_INITIALISING ||
		      owner->ro_state == ROS_ACTIVE) &&
		     owner->ro_resource == res &&
		     ur_tlist_contains(&owner->ro_owned[OWOS_CACHED], r) &&
		     owner_invariant(owner)));
	return result;
}
C2_EXPORTED(c2_rm_owner_init_with);

void c2_rm_owner_fini(struct c2_rm_owner *owner)
{
	struct c2_rm_resource *res = owner->ro_resource;
	struct c2_rm_right    *self_right;

	C2_PRE(owner->ro_state == ROS_FINAL);
	C2_PRE(owner_invariant(owner));
	C2_PRE(ur_tlist_length(&owner->ro_borrowed) ==
	       (owner->ro_creditor == NULL));

	/* Destroy self-loan if it is here. */
	self_right = ur_tlist_head(&owner->ro_borrowed);
	if (self_right != NULL) {
		struct c2_rm_loan *loan;

		loan = container_of(self_right, struct c2_rm_loan, rl_right);
		C2_ASSERT(loan->rl_id == C2_RM_LOAN_SELF_ID);
		ur_tlink_del_fini(self_right);
		c2_free(loan);
	}

	RM_OWNER_LISTS_FOR(owner, ur_tlist_fini);
	owner->ro_resource = NULL;
	c2_mutex_fini(&owner->ro_lock);

	resource_put(res);
}
C2_EXPORTED(c2_rm_owner_fini);

void c2_rm_right_init(struct c2_rm_right *right)
{
	C2_PRE(right != NULL);

	right->ri_datum = 0;
	ur_tlink_init(right);
	pr_tlist_init(&right->ri_pins);
}
C2_EXPORTED(c2_rm_right_init);

void c2_rm_right_fini(struct c2_rm_right *right)
{
	ur_tlink_fini(right);
	pr_tlist_fini(&right->ri_pins);
	right->ri_ops->rro_free(right);
}
C2_EXPORTED(c2_rm_right_fini);

void c2_rm_incoming_init(struct c2_rm_incoming *in)
{
	C2_PRE(in != NULL);

	in->rin_rc = 0;
	in->rin_state = RI_INITIALISED;
	pi_tlist_init(&in->rin_pins);
	c2_chan_init(&in->rin_signal);
	c2_rm_right_init(&in->rin_want);
}
C2_EXPORTED(c2_rm_incoming_init);

void c2_rm_incoming_fini(struct c2_rm_incoming *in)
{
	C2_PRE(in->rin_state == RI_SUCCESS || in->rin_state == RI_FAILURE);

	in->rin_rc = 0;
	in->rin_state = 0;
	c2_rm_right_fini(&in->rin_want);
	c2_chan_fini(&in->rin_signal);
	pi_tlist_fini(&in->rin_pins);
}
C2_EXPORTED(c2_rm_incoming_fini);

void c2_rm_remote_init(struct c2_rm_remote *rem, struct c2_rm_resource *res)
{
	C2_PRE(rem->rem_state == REM_FREED);

	rem->rem_state = REM_INITIALIZED;
	rem->rem_resource = res;
	c2_chan_init(&rem->rem_signal);
	resource_get(res);
}
C2_EXPORTED(c2_rm_remote_init);

void c2_rm_remote_fini(struct c2_rm_remote *rem)
{
	C2_PRE(rem->rem_state == REM_INITIALIZED ||
	       rem->rem_state == REM_SERVICE_LOCATED ||
	       rem->rem_state == REM_OWNER_LOCATED);
	rem->rem_state = REM_FREED;
	c2_chan_fini(&rem->rem_signal);
	resource_put(res);
}
C2_EXPORTED(c2_rm_remote_fini);

/**
   Releases rights by removing pins associated with incoming request.

   Revocation or cancellation of rights requires this.
 */
static void incoming_release(struct c2_rm_incoming *in)
{
	struct c2_rm_pin *pin;

	c2_tlist_for(&pi_tl, &in->rin_pins, pin) {
		if (pin->rp_flags & RPF_TRACK)
			pin_remove(pin, false);
	} c2_tlist_endfor;
}

/**
   Makes another copy of right struct.
 */
int right_copy(struct c2_rm_right *dest, const struct c2_rm_right *src)
{
	int result;

	C2_PRE(src != NULL);
	C2_PRE(dest->ri_datum == 0);

	/* Resource specific join may fail */
	result = src->ri_ops->rro_join(dest, src);
	/* Two rights are equal when both rights implies to each other */
	C2_POST(ergo(result == 0, right_eq(src, dst)));
	return result;
}

/**
   Check for existing pins for same right and then add pin to
   incoming request for corresponding right.
 */
static int pin_check_and_add(struct c2_rm_incoming *in,
			     struct c2_rm_right *right)
{
	struct c2_rm_pin *pin;
	int		  result;

	c2_tlist_for(&pi_tl, &in->rin_pins, pin) {
		if (!pi_tlist_contains(&right->ri_pins, pin)) {
			result = pin_add(in, right);
			if (result != 0)
				return result;
		}
	} c2_tlist_endfor;
	return 0;
}

/**
   Applies universal policies
 */
static int apply_policy(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;
	struct c2_rm_right *pin_right;
	struct c2_rm_pin   *pin;
	bool		    first;
	int		    result;

	switch (in->rin_policy) {
	case RIP_NONE:
	case RIP_RESOURCE_TYPE_BASE:
		break;
	case RIP_INPLACE:
		/*
		 * If possible, don't insert a new right into the list of
		 * possessed rights. Instead, pin possessed rights overlapping
		 * with the requested right.
		 */
		c2_tlist_for(&pi_tl, &in->rin_pins, pin) {
			pin_right = pin->rp_right;
			pin_right->ri_ops->rro_diff(pin->rp_right,
						    &in->rin_want);
			pin->rp_right = &in->rin_want;
			pr_tlist_move(&in->rin_want.ri_pins, pin);
		} c2_tlist_endfor;
		break;
	case RIP_STRICT:
		/*
		 * Remove all pinned rights and insert right equivalent
		 * to requested right.
		 */
		C2_ALLOC_PTR(loan);
		if (loan == NULL)
			return -ENOMEM;
		incoming_release(in);
		c2_rm_right_init(&loan->rl_right);
		result = right_copy(&loan->rl_right, &in->rin_want);
		if (result != 0) {
			c2_free(loan);
			return result;
		}
		result = pin_add(in, &loan->rl_right);
		if (result != 0) {
			c2_free(loan);
			return result;
		}
		ur_tlist_add(&owner->ro_owned[OWOS_CACHED], &loan->rl_right);
		break;
	case RIP_JOIN:
		/*
		 * New right equivalent to joined rights of granted
		 * rights is inserted into owned rights list.
		 */
		if (pi_tlist_is_empty(&in->rin_pins))
			break;

		first = true;
		c2_tlist_for (&pi_tl, &in->rin_pins, pin) {
			if (first) {
				right = pin->rp_right;
				first = false;
				continue;
			}
			pin_right = pin->rp_right;
			right->ri_ops->rro_join(right, pin_right);
			ur_tlink_del_fini(pin_right);
			loan = container_of(pin_right, struct c2_rm_loan,
					    rl_right);
			pin_remove(pin, false);
			c2_free(loan);
		} c2_tlist_endfor;
		ur_tlist_move(&owner->ro_owned[OWOS_CACHED], right);
		break;
	case RIP_MAX:
		/*
		 * Maximum rights which intersects to requested right
		 * are granted from cached right list.
		 */
		c2_tlist_for (&ur_tl, &owner->ro_owned[OWOS_CACHED], right) {
			if (right->ri_ops->rro_intersects(right,
							  &in->rin_want)) {
				result = pin_check_and_add(in, right);
				if (result != 0)
					return result;
			}
		} c2_tlist_endfor;
	}

	return 0;
}

int c2_rm_db_service_query(const char *name, struct c2_rm_remote *rem)
{
        /* Create search query for DB using name as key and
         * find record  and assign service ID */
        rem->rem_state = REM_SERVICE_LOCATED;
        return 0;
}

int c2_rm_remote_resource_locate(struct c2_rm_remote *rem)
{
         /* Send resource management fop to locate resource */
         rem->rem_state = REM_OWNER_LOCATED;
         return 0;
}

/**
   A distributed resource location data-base is consulted to locate the service.
 */
static int service_locate(struct c2_rm_resource_type *rtype,
			  struct c2_rm_remote *rem)
{
	struct c2_clink clink;
	int		rc;

	C2_PRE(c2_mutex_is_locked(&rtype->rt_lock));
	C2_PRE(rem->rem_state == REM_SERVICE_LOCATING);

	c2_clink_init(&clink, NULL);
	c2_clink_add(&rem->rem_signal, &clink);
	/*
	 * DB callback should assign value to rem_service and
	 * rem_state should be changed to REM_SERVICE_LOCATED.
	 */
	rc = c2_rm_db_service_query(rtype->rt_name, rem);
	if (rc != 0) {
		fprintf(stderr, "c2_rm_db_service_query failed!\n");
		goto error;
	}
	if (rem->rem_state != REM_SERVICE_LOCATED)
		c2_chan_wait(&clink);
	if (rem->rem_state != REM_SERVICE_LOCATED)
		rc = -EINVAL;

error:
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return rc;
}

/**
   Sends a resource management fop to the service. The service responds
   with the remote owner identifier (c2_rm_remote::rem_id) used for
   further communications.
 */
static int resource_locate(struct c2_rm_resource_type *rtype,
			   struct c2_rm_remote *rem)
{
	struct c2_clink clink;
	int		rc;

	C2_PRE(c2_mutex_is_locked(&rtype->rt_lock));
	C2_PRE(rem->rem_state == REM_OWNER_LOCATING);

	c2_clink_init(&clink, NULL);
	c2_clink_add(&rem->rem_signal, &clink);
	/*
	 * RPC callback should assign value to rem_id and
	 * rem_state should be set to REM_OWNER_LOCATED.
	 */
	rc = c2_rm_remote_resource_locate(rem);
	if (rc != 0) {
		fprintf(stderr, "c2_rm_remote_resource_find failed!\n");
		goto error;
	}
	if (rem->rem_state != REM_OWNER_LOCATED)
		c2_chan_wait(&clink);
	if (rem->rem_state != REM_OWNER_LOCATED)
		rc = -EINVAL;

error:
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return rc;
}

int c2_rm_net_locate(struct c2_rm_right *right, struct c2_rm_remote *other)
{
	struct c2_rm_resource_type *rtype = right->ri_resource->r_type;
	struct c2_rm_resource	   *res;
	int			    result;

	C2_PRE(other->rem_state == REM_INITIALIZED);

	other->rem_state = REM_SERVICE_LOCATING;
	result = service_locate(rtype, other);
	if (result != 0)
		goto error;

	other->rem_state = REM_OWNER_LOCATING;
	result = resource_locate(rtype, other);
	if (result != 0)
		goto error;

	/* Search for resource having resource id equal to remote id */
	c2_mutex_lock(&rtype->rt_lock);
	c2_tlist_for(&res_tl, &rtype->rt_resources, res) {
		if (rtype->rt_ops->rto_resource_is(res, other->rem_id)) {
			other->rem_resource = res;
			break;
		}
	} c2_tlist_endfor;
	c2_mutex_unlock(&rtype->rt_lock);

error:
	C2_POST(ergo(result == 0, other->rem_resource == right->ri_resource));
	return result;
}
C2_EXPORTED(c2_rm_net_locate);

/**
   Locates c2_rm_remote for every right in the incoming request (for loan)
   and moves them to sublet list of owner.
 */
static int move_to_sublet(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
	struct c2_rm_pin   *pin;
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;

	C2_PRE(c2_mutex_is_locked(&owner->ro_lock));

	C2_ALLOC_PTR(loan);
	if (loan == NULL)
		return -ENOMEM;
	c2_rm_right_init(&loan->rl_right);

	/* Constructs a single cumulative right */
	c2_tlist_for (&pi_tl, &in->rin_pins, pin) {
		right = pin->rp_right;
		loan->rl_right.ri_ops = right->ri_ops;
		right->ri_ops->rro_join(&loan->rl_right, right);
		pin_remove(pin, false);
		/* Loaned right should not be on cached list */
		ur_tlink_del_fini(right);
		c2_free(right);
	} c2_tlist_endfor;
	ur_tlist_add(&owner->ro_sublet, &loan->rl_right);
	return 0;
}

/**
   Builds the request.
 */
static void request_build(struct c2_rm_req_reply *req,
			  struct c2_rm_loan *loan,
			  enum c2_rm_request_type type)
{
        struct c2_rm_remote *rem = &loan->rl_other;

        req->type = type;
        req->sig_id = loan->rl_id;
        req->reply_id = rem->rem_id;

	c2_rm_incoming_init(&req->in);
        c2_rm_right_init(&req->in.rin_want);
        req->in.rin_state = RI_INITIALISED;
        req->in.rin_priority = 0;
        req->in.rin_type = RIT_LOCAL;
        req->in.rin_policy = RIP_INPLACE;
        req->in.rin_flags = RIF_LOCAL_WAIT;
	c2_queue_link_init(&req->rq_link);
}

/**
   Reply to the incoming revoke request
 */
static int revoke_request_reply(struct c2_rm_incoming *in)
{
	struct c2_rm_right     *right;
	struct c2_rm_right     *bo_right;
	struct c2_rm_pin       *pin;
	struct c2_rm_loan      *loan;
	struct c2_rm_req_reply *request;
	struct c2_rm_owner     *owner = in->rin_owner;
	int			result;

	c2_tlist_for (&pi_tl, &in->rin_pins, pin) {
		right = pin->rp_right;
		loan = container_of(right, struct c2_rm_loan, rl_right);
		if (c2_rm_net_locate(right, &loan->rl_other) == 0) {
			C2_ALLOC_PTR(request);
			if (request == NULL)
				return -ENOMEM;
			request_build(request, loan, PRO_LOAN_REPLY);
			result = right_copy(&request->in.rin_want,
					    &loan->rl_right);
			if (result != 0)
				return result;
			/*@todo will be replaced by rpc call and fop will be
			 * using rpc layer API's */
			c2_mutex_lock(&rpc_lock);
			c2_queue_put(&rpc_queue, &request->rq_link);
			c2_mutex_unlock(&rpc_lock);

			/* Revoke(cancel) request will remove rights from borrow
			 * and owned lists
			 */
			c2_tlist_for (&ur_tl, &owner->ro_borrowed, bo_right) {
				if (right_eq(right, bo_right)) {
					ur_tlink_del_fini(bo_right);
					c2_rm_right_fini(bo_right);
					c2_free(bo_right);
				}
			} c2_tlist_endfor;
			pin_remove(pin, false);
			ur_tlink_del_fini(right);
			c2_free(right);
		}
	} c2_tlist_endfor;
	return 0;
}

/**
   Reply to the incoming loan request when wanted rights granted
 */
static int loan_request_reply(struct c2_rm_incoming *in)
{
	struct c2_rm_right     *right;
	struct c2_rm_pin       *pin;
	struct c2_rm_loan      *loan;
	struct c2_rm_req_reply *request;
	int			result;

	c2_tlist_for (&pi_tl, &in->rin_pins, pin) {
		right = pin->rp_right;
		loan = container_of(right, struct c2_rm_loan, rl_right);
		/*@todo Form fop for each loan and reply or
		 * we can apply rpc grouping and send reply
		 */
		if (!c2_rm_net_locate(right, &loan->rl_other)) {
			C2_ALLOC_PTR(request);
			if (request == NULL)
				return -ENOMEM;
			request_build(request, loan, PRO_LOAN_REPLY);
			result = right_copy(&request->in.rin_want,
					    &loan->rl_right);
			if (result != 0)
				return result;
			c2_mutex_lock(&rpc_lock);
			c2_queue_put(&rpc_queue, &request->rq_link);
			c2_mutex_unlock(&rpc_lock);
		}
	} c2_tlist_endfor;

	return 0;
}

/**
   It sends out outgoing excited requests
 */
static int out_request_send(struct c2_rm_outgoing *out)
{
	struct c2_rm_req_reply *request;
	int			result;

	C2_ALLOC_PTR(request);
	if (request == NULL)
		return -ENOMEM;
	request_build(request, &out->rog_want, PRO_OUT_REQUEST);
	result = right_copy(&request->in.rin_want, &out->rog_want.rl_right);
	if (result != 0)
		return result;
	/*@todo following code will be replaced by rpc call */
	c2_mutex_lock(&rpc_lock);
	c2_queue_put(&rpc_queue, &request->rq_link);
	c2_mutex_unlock(&rpc_lock);

	return 0;
}

/**
   Check for held conflict rights.
 */
static bool conflicts_exist(struct c2_rm_incoming *in)
{
	struct c2_rm_pin *pin;

	if (in->rin_type == RIT_LOCAL)
		return false;

	c2_tlist_for(&pi_tl, &in->rin_pins, pin) {
		if (ur_tlist_contains(&in->rin_owner->ro_owned[OWOS_HELD],
				      pin->rp_right))
			return true;
	} c2_tlist_endfor;

	return false;
}


/**
   Returns true when ri_datum is 0, else returns false.
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
         below. This temporarily violates the owner_invariant();

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
void c2_rm_right_get(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
	C2_PRE(IS_IN_ARRAY(in->rin_priority, owner->ro_incoming));
	C2_PRE(in->rin_state == RI_INITIALISED);
	C2_PRE(pi_tlist_is_empty(&in->rin_pins));

	c2_mutex_lock(&owner->ro_lock);
	/*
	 * Mark incoming request "excited". owner_balance() will process it.
	 */
	ur_tlist_add(&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
		     &in->rin_want);
	owner_balance(owner);
	c2_mutex_unlock(&owner->ro_lock);
}

void c2_rm_right_put(struct c2_rm_incoming *in)
{
	C2_PRE(in->rin_state == RI_SUCCESS);

	c2_mutex_lock(&in->rin_owner->ro_lock);
	incoming_release(in);
	c2_mutex_unlock(&in->rin_owner->ro_lock);
	C2_POST(pi_tlist_is_empty(&in->rin_pins));
}

/**
   Called when an outgoing request completes (possibly with an error, like a
   timeout).
 */
void c2_rm_outgoing_complete(struct c2_rm_outgoing *og, int32_t rc)
{
	struct c2_rm_owner *owner;

	C2_PRE(og != NULL);

	owner = og->rog_owner;
	ur_tlist_move(&owner->ro_outgoing[OQS_EXCITED], &og->rog_want.rl_right);
	owner_balance(owner);
}

/**
   Removes a tracking pin on a resource usage right.

   If this was a last pin issued by an incoming request, excite the request.
 */
static void pin_remove(struct c2_rm_pin *pin, bool move)
{
	struct c2_rm_incoming *in;
	struct c2_rm_owner    *owner;

	C2_ASSERT(pin != NULL);

	in = pin->rp_incoming;
	owner = in->rin_owner;
	pi_tlink_del_fini(pin);
	pr_tlink_del_fini(pin);
	c2_free(pin);
	if (pi_tlist_is_empty(&in->rin_pins) && move) {
		/*
		 * Last pin removed, excite the request.
		 */
		ur_tlist_move(&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			      &in->rin_want);
	}
}

static int right_borrow(struct c2_rm_incoming *in, struct c2_rm_right *right)
{
	struct c2_rm_loan *borrow;
	int		   result;

	C2_PRE(right != NULL);

	C2_ALLOC_PTR(borrow);
	if (borrow == NULL)
		return -ENOMEM;

	c2_rm_right_init(&borrow->rl_right);
	result = right_copy(&borrow->rl_right, right);
	if (result != 0)
		goto end;

	result = c2_rm_net_locate(right, &borrow->rl_other);
	if (result != 0)
		goto end;

	result = go_out(in, ROT_BORROW, borrow, &borrow->rl_right);
	if (result == 0)
		result = right_copy(right, &borrow->rl_right);

end:
	c2_free(borrow);
	return result;
}

static void incoming_complete(struct c2_rm_incoming *in, int32_t rc)
{
	C2_PRE(c2_mutex_is_locked(&in->rin_owner->ro_lock));
	C2_PRE(in->rin_state == RI_INITIALISED || in->rin_state == RI_CHECK);
	C2_PRE(in->rin_rc == 0);
	C2_PRE(rc <= 0);

	in->rin_rc = rc;
	in->rin_state = rc == 0 ? RI_SUCCESS : RI_FAILURE;
	c2_chan_broadcast(&in->rin_signal);
	in->rin_ops->rio_complete(in, rc);
}

/**
   Main owner state machine function.

   Goes through the lists of excited incoming and outgoing requests until all
   the excitement is gone.
 */
static void owner_balance(struct c2_rm_owner *o)
{
	struct c2_rm_pin      *pin;
	struct c2_rm_right    *right;
	struct c2_rm_outgoing *out;
	struct c2_rm_incoming *in;
	bool                   todo;
	int                    prio;


	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	do {
		todo = false;
		c2_tlist_for(&ur_tl, &o->ro_outgoing[OQS_EXCITED], right) {
			todo = true;
			out = container_of(right, struct c2_rm_outgoing,
					   rog_want.rl_right);
			/*
			 * Outgoing request completes: remove all pins stuck in
			 * and finalise it.
			 *
			 * Removing of pins might excite incoming requests
			 * waiting for outgoing request completion.
			 */
			c2_tlist_for(&pr_tl, &right->ri_pins, pin) {
				pin_remove(pin, true);
			} c2_tlist_endfor;

			ur_tlink_del_fini(right);
			c2_free(out);
		} c2_tlist_endfor;
		for (prio = ARRAY_SIZE(o->ro_incoming) - 1; prio >= 0; prio--) {
			c2_tlist_for(&ur_tl,
				     &o->ro_incoming[prio][OQS_EXCITED], right) {
				todo = true;
				in = container_of(right, struct c2_rm_incoming,
						  rin_want);
				C2_ASSERT(in->rin_state == RI_WAIT ||
					  in->rin_state == RI_INITIALISED);
				/*
				 * All waits completed, go to CHECK
				 * state.
				 */
				ur_tlist_move(&o->ro_incoming[prio][OQS_GROUND],
					      &in->rin_want);
				in->rin_state = RI_CHECK;
				incoming_check(in);
			} c2_tlist_endfor;
		}
	} while (todo);
}

/**
   Helper function to incoming_check(), which starts with "rest" set to the
   right the incoming request wants to obtain and goes though the sequence of
   checks.

   This function performs no state transitions by itself. Instead its return
   value indicates the target state:

       - 0: the request is fulfilled, the target state is RI_SUCCESS,
       - +ve: more waiting is needed, the target state is RI_WAIT,
       - -ve: error, the target state is RI_FAILURE.
 */
static int incoming_check_with(struct c2_rm_incoming *in,
			       struct c2_rm_right *rest)
{
	struct c2_rm_owner *o = in->rin_owner;
	struct c2_rm_right  rest;
	int		    result;
	bool		    held;


	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(ur_tlist_contains(&o->ro_incoming[in->rin_priority][OQS_GROUND],
				 &in->rin_want));
	C2_PRE(in->rin_state == RI_CHECK);

	/*
	 * This function goes through owner rights lists checking for "wait"
	 * conditions that should be satisfied before the request could be
	 * fulfilled.
	 *
	 * If there is nothing to wait for, the request is either fulfilled
	 * immediately or fails.
	 */

	/*
	 * Check for "local" wait conditions.
	 */
	incoming_check_local(in, rest);
	held = conflicts_exist(in);
	/* Try Lock */
	if (held && (in->rin_flags & RIF_LOCAL_TRY)) {
		incoming_release(in);
		return -EWOULDBLOCK;
	}

	if (right_is_empty(rest) ||
	    (in->rin_type == RIT_LOCAL && !pi_tlist_is_empty(&in->rin_pins))) {
		/*
		 * The wanted right is completely covered by the local
		 * rights. There are no remote conditions to wait for.
		 */
		if (held && (in->rin_flags & RIF_LOCAL_WAIT)) {
			/*
			 * conflicting held rights were found, has to
			 * wait until local rights are released.
			 */
			return +1;
		} else {
			/*
			 * all conflicting rights are cached (not
			 * held).
			 * Apply the policy.
			 */
			result = apply_policy(in);
			if (result != 0)
				return result;
			switch (in->rin_type) {
			case RIT_LOAN:
				result = move_to_sublet(in);
				if (result == 0)
					result = loan_request_reply(in);
				if (result != 0)
					return result;
			case RIT_LOCAL:
				break;
			case RIT_REVOKE:
				result = revoke_request_reply(in);
				if (result != 0)
					return result;
			}
			return 0;
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
		if (in->rin_flags & RIF_MAY_REVOKE) {
			result = sublet_revoke(in, rest);
			if (result != 0)
				return result;
		}
		if (in->rin_flags & RIF_MAY_BORROW) {
			/* borrow more */
			while (!right_is_empty(rest)) {
				result = right_borrow(in, rest);
				if (result != 0)
					return result;
			}
		}
		if (right_is_empty(rest))
			return +1;
		else
			/* cannot fulfill the request. */
			return -EBUSY;
	}
}

/**
   Takes an incoming request in RI_CHECK state and attempt to perform a
   non-blocking state transition.

   This function leaves the request either in RI_WAIT, RI_SUCCESS or RI_FAILURE
   state.
 */
static void incoming_check(struct c2_rm_incoming *in)
{
	struct c2_rm_right  rest;
	int		    result;

	c2_rm_right_init(&rest);
	result = right_copy(&rest, &in->rin_want);
	if (result == 0)
		result = incoming_check_with(in, &rest);
	c2_rm_right_fini(&rest);
	if (result > 0)
		in->rin_state = RI_WAIT;
	else
		incoming_complete(in, result);
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
	bool		    coverage;
	int		    i;
	int		    result;

	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(in->rin_state == RI_CHECK);

	/*
	 * Only continue this function, when a check must be made for locally
	 * possessed rights conflicting with the wanted right.
	 *
	 * Typically, this check is needed for a remote request (loan or
	 * revoke), because the local rights have to be released before the
	 * request can be fulfilled. For a local request, this check can also
	 * be true, depending on the policy.
	 */
	if (in->rin_type == RIT_LOCAL || in->rin_policy != RIP_INPLACE ||
	    !(in->rin_flags & RIF_LOCAL_WAIT))
		return;
	/*
	 * If coverage is true, the loop below pins some collection of locally
	 * possessed rights which together imply (i.e., cover) the wanted
	 * right. Otherwise, all locally possessed rights, intersecting with
	 * the wanted right are pinned.
	 *
	 * Typically, revoke and loan requests have coverage set to true, and
	 * local requests have coverage set to false.
	 */
	coverage = in->rin_type != RIT_LOCAL;

	for (i = 0; i < ARRAY_SIZE(o->ro_owned); ++i) {
		c2_tlist_for(&ur_tl, &o->ro_owned[i], right) {
			if (rest->ri_ops->rro_intersects(right, rest)) {
				result = pin_add(in, right);
				if (result != 0) {
					in->rin_rc = result;
					return;
				}
				if (coverage) {
					rest->ri_ops->rro_diff(rest, right);
					if (right_is_empty(rest))
						return;
				}
			}
		} c2_tlist_endfor;
	}
}

/**
   Revokes @right (or parts thereof) sub-let to downward owners.
 */
static int sublet_revoke(struct c2_rm_incoming *in,
			  struct c2_rm_right *rest)
{
	struct c2_rm_owner *o = in->rin_owner;
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;
	int		    result;

	c2_tlist_for(&ur_tl, &o->ro_sublet, right) {
		if (rest->ri_ops->rro_intersects(right, rest)) {
			rest->ri_ops->rro_diff(rest, right);
			loan = container_of(right, struct c2_rm_loan, rl_right);
			/*
			 * It is possible that this loop emits multiple
			 * outgoing requests toward the same remote
			 * owner. Don't bother to coalesce them here. The rpc
			 * layer would do this more efficiently.
			 */
			result = go_out(in, ROT_REVOKE, loan, right);
			if (result != 0)
				return result;
		}
	} c2_tlist_endfor;

	return 0;
}

/**
   Sticks a tracking pin on @right. When @right is released, the all incoming
   requests that stuck pins into it are notified.
 */
int pin_add(struct c2_rm_incoming *in, struct c2_rm_right *right)
{
	struct c2_rm_pin *pin;

	C2_ALLOC_PTR(pin);
	if (pin != NULL) {
		pin->rp_flags = RPF_TRACK;
		pin->rp_right = right;
		pin->rp_incoming = in;
		pr_tlist_add(&right->ri_pins, pin);
		pi_tlist_add(&in->rin_pins, pin);
		return 0;
	} else
		return -ENOMEM;
}

/**
   Sends an outgoing request of type @otype to a remote owner specified by
   @loan and with requested (or cancelled) right @right.
 */
int go_out(struct c2_rm_incoming *in, enum c2_rm_outgoing_type otype,
	   struct c2_rm_loan *loan, struct c2_rm_right *right)
{
	struct c2_rm_outgoing	*out;
	struct c2_rm_right	*out_right;
	int			 result = 0;
	int			 i;
	bool			 found = false;

	/* first check for existing outgoing requests */
	for (i = 0; i < ARRAY_SIZE(in->rin_owner->ro_outgoing); i++) {
		c2_tlist_for(&ur_tl, &in->rin_owner->ro_outgoing[i], out_right) {
			out = container_of(out_right, struct c2_rm_outgoing,
					   rog_want.rl_right);
			if (out->rog_type == otype &&
				right->ri_ops->rro_intersects(out_right, right)){
				/* @todo adjust outgoing requests priority
				 * (priority inheritance) */
				result = pin_add(in, out_right);
				if (result != 0)
					return result;
				right->ri_ops->rro_diff(right, out_right);
				found = true;
				break;
			}
		} c2_tlist_endfor;
		if (found)
			break;
	}

	if (!found) {
		C2_ALLOC_PTR(out);
		if (out == NULL)
			return -ENOMEM;
		out->rog_type = otype;
		out->rog_owner = in->rin_owner;
		out->rog_want.rl_other = loan->rl_other;
		out->rog_want.rl_id = loan->rl_id;
		c2_rm_right_init(&out->rog_want.rl_right);
		result = right_copy(&out->rog_want.rl_right, right);
		if (result != 0)
			return result;
		ur_tlist_add(&in->rin_owner->ro_outgoing[OQS_GROUND],
			     &out->rog_want.rl_right);
		result = pin_add(in, right);
		if (result != 0)
			return result;
		right->ri_ops->rro_diff(right, right);
	}

	return out_request_send(out);
}

/**
   Helper function to get right with timed wait(deadline).
 */
int c2_rm_right_timedwait(struct c2_rm_incoming *in, const c2_time_t deadline)
{
	struct c2_clink clink;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&in->rin_signal, &clink);
	c2_rm_right_get(in);
	if (in->rin_state == RI_WAIT)
		c2_chan_timedwait(&clink, deadline);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	switch (in->rin_state) {
	case RI_WAIT:
		return -ETIMEDOUT;
	case RI_SUCCESS:
		return 0;
	case RI_FAILURE:
	default:
		return in->rin_rc;
	}
}

/**
   Helper function to get right with infinite wait time.
 */
int c2_rm_right_get_wait(struct c2_rm_incoming *in)
{
	struct c2_clink clink;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&in->rin_signal, &clink);
	c2_rm_right_get(in);
	if (in->rin_state == RI_WAIT)
		c2_chan_wait(&clink);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	switch (in->rin_state) {
	case RI_SUCCESS:
		return 0;
	case RI_WAIT:
	case RI_FAILURE:
	default:
		return in->rin_rc;
	}
}

struct c2_rm_cookie *c2_rm_gen_rem_id(uint64_t addr)
{
	struct c2_rm_cookie *cookie;
	static uint64_t	     rem_id = 0;

	C2_ALLOC_PTR(cookie);
	if (cookie == NULL)
		return NULL;

	cookie->rc_counter = ++rem_id;
	cookie->rc_address = addr;

	return cookie;
}

uint64_t c2_rm_gen_loan_id(void)
{
	static uint64_t loan_id = 0;
	return ++loan_id;
}

struct c2_rm_owner *c2_rm_search_owner(struct c2_rm_resource *res)
{
	struct c2_rm_owner *owner;

	owner = container_of(res, struct c2_rm_owner, ro_resource);
	return owner;
}

void c2_rm_cookie_free(struct c2_rm_cookie *cookie)
{
	C2_PRE(cookie != NULL);
	c2_free(cookie);
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
