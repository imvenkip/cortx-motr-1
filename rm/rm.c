/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
#include "rm/rm_internal.h"

/**
   @addtogroup rm
   @{
 */
uint64_t node_gencount;
struct owner_invariant_state;
extern void c2_cookie_copy(struct c2_cookie *dst, const struct c2_cookie *src);
extern int c2_cookie_remote_build(void *obj_ptr, struct c2_cookie *out);
extern int c2_cookie_dereference(const struct c2_cookie *cookie, void **out);

static void resource_get           (struct c2_rm_resource *res);
static void resource_put           (struct c2_rm_resource *res);
static bool resource_list_check    (const struct c2_rm_resource *res,
				    void *datum);
static bool resource_type_invariant(const struct c2_rm_resource_type *rt);

static void owner_balance          (struct c2_rm_owner *o);
static bool owner_invariant        (struct c2_rm_owner *owner);
static void owner_init_internal    (struct c2_rm_owner *owner,
				    struct c2_rm_resource *res);

static void pin_del                (struct c2_rm_pin *pin);
static bool owner_invariant_state  (const struct c2_rm_owner *owner,
				    struct owner_invariant_state *is);
static void incoming_check         (struct c2_rm_incoming *in);
static int  incoming_check_with    (struct c2_rm_incoming *in,
				    struct c2_rm_right *right);
static void incoming_complete      (struct c2_rm_incoming *in, int32_t rc);
static void incoming_policy_apply  (struct c2_rm_incoming *in);
static void incoming_policy_none   (struct c2_rm_incoming *in);
static bool incoming_invariant     (const struct c2_rm_incoming *in);
static int  incoming_pin_nr        (const struct c2_rm_incoming *in,
				    uint32_t flags);
static void incoming_release       (struct c2_rm_incoming *in);

static int  right_pin_nr           (const struct c2_rm_right *right,
				    uint32_t flags);
static int  service_locate         (struct c2_rm_resource_type *rtype,
				    struct c2_rm_remote *rem);
static int  resource_locate        (struct c2_rm_resource_type *rtype,
				    struct c2_rm_remote *rem);

static int  outgoing_check         (struct c2_rm_incoming *in,
				    enum c2_rm_outgoing_type,
				    struct c2_rm_right *right,
				    struct c2_rm_remote *other);
static int  revoke_send            (struct c2_rm_incoming *in,
				    struct c2_rm_loan *loan,
				    struct c2_rm_right *right);
static int  borrow_send            (struct c2_rm_incoming *in,
				    struct c2_rm_right *right);

static int right_copy              (struct c2_rm_right *dest,
				    const struct c2_rm_right *src);
static bool right_eq               (const struct c2_rm_right *r0,
				    const struct c2_rm_right *r1);
static bool right_is_empty         (const struct c2_rm_right *right);
static bool right_intersects       (const struct c2_rm_right *A,
				    const struct c2_rm_right *B);
static bool right_conflicts        (const struct c2_rm_right *A,
				    const struct c2_rm_right *B);
static int  right_diff             (struct c2_rm_right *r0,
				    const struct c2_rm_right *r1);

static struct c2_rm_resource *resource_find(const struct c2_rm_resource_type *rt,
					    const struct c2_rm_resource *res);
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
static struct c2_rm_resource *resource_find(const struct c2_rm_resource_type *rt,
					    const struct c2_rm_resource *res)
{
	struct c2_rm_resource *scan;

	c2_tlist_for(&res_tl, (struct c2_tl *)&rt->rt_resources, scan) {
		if (rt->rt_ops->rto_eq(res, scan))
			break;
	} c2_tlist_endfor;
	return scan;
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
	C2_PRE(resource_type_invariant(rtype));

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
	c2_mutex_lock(&rtype->rt_lock);
	C2_PRE(resource_type_invariant(rtype));
	C2_PRE(res->r_ref == 0);
	C2_PRE(resource_find(rtype, res) == NULL);
	res->r_type = rtype;
	res_tlink_init_at(res, &rtype->rt_resources);
	C2_CNT_INC(rtype->rt_nr_resources);
	C2_POST(res_tlist_contains(&rtype->rt_resources, res));
	C2_POST(resource_type_invariant(rtype));
	c2_mutex_unlock(&rtype->rt_lock);
	C2_POST(res->r_type == rtype);
}
C2_EXPORTED(c2_rm_resource_add);

void c2_rm_resource_del(struct c2_rm_resource *res)
{
	struct c2_rm_resource_type *rtype = res->r_type;

	c2_mutex_lock(&rtype->rt_lock);
	C2_PRE(res_tlist_contains(&rtype->rt_resources, res));
	C2_PRE(resource_type_invariant(rtype));

	res_tlink_del_fini(res);
	C2_CNT_DEC(rtype->rt_nr_resources);

	C2_POST(resource_type_invariant(rtype));
	C2_POST(!res_tlist_contains(&rtype->rt_resources, res));
	c2_mutex_unlock(&rtype->rt_lock);
}
C2_EXPORTED(c2_rm_resource_del);

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

	ur_tlist_init(&owner->ro_borrowed);
	ur_tlist_init(&owner->ro_sublet);
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
	C2_PRE(ergo(creditor != NULL,
		    creditor->rem_state >= REM_SERVICE_LOCATED));

	owner_init_internal(owner, res);
	owner->ro_creditor = creditor;

	C2_POST((owner->ro_state == ROS_INITIALISING ||
		 owner->ro_state == ROS_ACTIVE) && owner->ro_resource == res);

}
C2_EXPORTED(c2_rm_owner_init);

int c2_rm_owner_selfadd(struct c2_rm_owner *owner, struct c2_rm_right *r)
{
	struct c2_rm_loan *nominal_capital;
	int                rc;

	C2_PRE(owner->ro_state == ROS_INITIALISING ||
	       owner->ro_state == ROS_ACTIVE);
	C2_PRE(r->ri_owner == owner);
	/* owner must be "top-most" */
	C2_PRE(owner->ro_creditor == NULL);

	C2_ALLOC_PTR(nominal_capital);
	if (nominal_capital != NULL) {
		rc = c2_rm_loan_init(nominal_capital, r);
		if (rc == 0) {
			ur_tlist_add(&owner->ro_owned[OWOS_CACHED], r);
			/* Add self-loan to the borrowed list. */
			ur_tlist_add(&owner->ro_borrowed,
				     &nominal_capital->rl_right);
		} else
			c2_free(nominal_capital);
	} else
		rc = -ENOMEM;

	C2_POST(ergo(rc == 0,
		     (owner->ro_state == ROS_INITIALISING ||
		      owner->ro_state == ROS_ACTIVE) &&
	     	      ur_tlist_contains(&owner->ro_owned[OWOS_CACHED], r) &&
		     owner_invariant(owner)));
	return rc;
}
C2_EXPORTED(c2_rm_owner_selfadd);

static bool owner_busy(struct c2_rm_owner *owner)
{
	bool rc = false;
	int  i;
	int  j;

	for (i = 0; i < C2_RM_REQUEST_PRIORITY_NR; i++)
		for (j = 0; j < OQS_NR; j++)
			if (!ur_tlist_is_empty(&owner->ro_incoming[i][j])) {
				rc = true;
				break;
			}
	return rc;
}

int c2_rm_owner_retire(struct c2_rm_owner *owner)
{
	struct c2_rm_right    *right;
	struct c2_rm_loan     *loan;
	struct c2_rm_incoming  in;
	int		       rc;
	int		       i;

	C2_PRE(owner->ro_state == ROS_ACTIVE ||
	       owner->ro_state == ROS_FINALISING);
	/*
	 * Put the owner in ROS_FINALISING. This will prevent any new
	 * incoming requests on it. If it's already in FINALISING state
	 * don't bother to check state variable; just set the state.
	 * Check if there are any earlier pending incoming requests.
	 */
	c2_mutex_lock(&owner->ro_lock);
	owner->ro_state = ROS_FINALISING;
	if (owner_busy(owner))
		return -EBUSY;
	c2_mutex_unlock(&owner->ro_lock);

	/*
	 * Note that it's possible to asynchronously send multiple requests.
	 * To do that we will have allocate 'c2_rm_incoming' in a loop and
	 * wait on multiple channels.
	 * If memory allocation fails, handling error may become difficult.
	 * You have to wait for responses for all the revokes. Additionally
	 * 'revoke_send()' may return error. Handling errors, releasing memory
	 * and waiting on partial or all requests can be error prone.
	 * Hence, there is inefficient revoke-wait per right.
	 * Revisit during inspection.
	 */
	c2_rm_incoming_init(&in, owner, RIT_REVOKE, RIP_NONE, RIF_MAY_REVOKE);
	c2_tlist_for(&ur_tl, &owner->ro_sublet, right) {
		/*
		 * This is convoluted. Now that user incoming requests have
		 * drained, we add our incoming requests for REVOKE and CANCEL
		 * processing to the incoming queue.
		 *
		 * Question : Should we ignore error and continue
		 * finalising owner?? Accepting force flag as argument
		 * to this function might be a good idea.
		 */
		rc = right_copy(&in.rin_want, right);
		if (rc != 0)
			return rc;
		ur_tlist_add(&owner->ro_incoming[C2_RM_REQUEST_PRIORITY_NR - 1]\
						[OQS_EXCITED],
			     &in.rin_want);
		owner_balance(owner);
		c2_rm_right_timedwait(&in, 0);
		if (in.rin_state == RI_FAILURE)
			return in.rin_rc;
	} c2_tlist_endfor;

	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		c2_tlist_for(&ur_tl, &owner->ro_owned[i], right) {
			ur_tlink_del_fini(right);
		} c2_tlist_endfor;
	}

	c2_tlist_for(&ur_tl, &owner->ro_borrowed, right) {
		loan = container_of(right, struct c2_rm_loan, rl_right);
		if (loan->rl_id == C2_RM_LOAN_SELF_ID) {
			ur_tlink_del_fini(right);
			c2_free(loan);
		} else {
			/* @todo - pending cancel implementation */
			/* cancel_send(in, loan, right) */
		}
	} c2_tlist_endfor;

	c2_mutex_unlock(&owner->ro_lock);
	owner->ro_state = ROS_FINAL;
	C2_POST(ergo(rc == 0,
		     owner->ro_state == ROS_FINAL && owner_invariant(owner)));
	c2_mutex_unlock(&owner->ro_lock);
	return rc;
}

void c2_rm_owner_fini(struct c2_rm_owner *owner)
{
	struct c2_rm_resource *res = owner->ro_resource;

	C2_PRE(owner->ro_state == ROS_FINAL);
	C2_PRE(owner_invariant(owner));
	C2_PRE((ur_tlist_length(&owner->ro_borrowed) > 0) ==
	       (owner->ro_creditor == NULL));

	RM_OWNER_LISTS_FOR(owner, ur_tlist_fini);
	owner->ro_resource = NULL;
	c2_mutex_fini(&owner->ro_lock);

	resource_put(res);
}
C2_EXPORTED(c2_rm_owner_fini);

void c2_rm_right_init(struct c2_rm_right *right, struct c2_rm_owner *owner)
{
	C2_PRE(right != NULL);

	right->ri_datum = 0;
	ur_tlink_init(right);
	pr_tlist_init(&right->ri_pins);
	right->ri_owner = owner;
	owner->ro_resource->r_ops->rop_right_init(owner->ro_resource, right);
	C2_PRE(right->ri_ops != NULL);
}
C2_EXPORTED(c2_rm_right_init);

void c2_rm_right_fini(struct c2_rm_right *right)
{
	ur_tlink_fini(right);
	pr_tlist_fini(&right->ri_pins);
	right->ri_ops->rro_free(right);
}
C2_EXPORTED(c2_rm_right_fini);

void c2_rm_incoming_init(struct c2_rm_incoming *in, struct c2_rm_owner *owner,
			 enum c2_rm_incoming_type type,
			 enum c2_rm_incoming_policy policy, uint64_t flags)
{
	C2_PRE(in != NULL);

	C2_SET0(in);
	in->rin_state  = RI_INITIALISED;
	in->rin_type   = type;
	in->rin_policy = policy;
	in->rin_flags  = flags;
	pi_tlist_init(&in->rin_pins);
	c2_chan_init(&in->rin_signal);
	c2_rm_right_init(&in->rin_want, owner);
	C2_POST(incoming_invariant(in));
}
C2_EXPORTED(c2_rm_incoming_init);

void c2_rm_incoming_fini(struct c2_rm_incoming *in)
{
	C2_PRE(in->rin_state == RI_SUCCESS || in->rin_state == RI_FAILURE);
	C2_PRE(incoming_invariant(in));

	in->rin_rc = 0;
	in->rin_state = 0;
	c2_rm_right_fini(&in->rin_want);
	c2_chan_fini(&in->rin_signal);
	pi_tlist_fini(&in->rin_pins);
}
C2_EXPORTED(c2_rm_incoming_fini);

void c2_rm_outgoing_init(struct c2_rm_outgoing *out,
			 enum c2_rm_outgoing_type req_type,
			 struct c2_rm_owner *owner)
{
	C2_PRE(out != NULL);
	C2_PRE(owner != NULL);

	out->rog_rc = 0;
	out->rog_type = req_type;
	out->rog_owner = owner;
}
C2_EXPORTED(c2_rm_outgoing_init);

int c2_rm_loan_init(struct c2_rm_loan *loan,
		     struct c2_rm_right *right)
{
	C2_PRE(loan != NULL);
	C2_PRE(right != NULL);

	loan->rl_other = right->ri_owner->ro_creditor;
	loan->rl_id = C2_RM_LOAN_SELF_ID;
	c2_cookie_remote_build(loan, &loan->rl_cookie);
	c2_rm_right_init(&loan->rl_right, right->ri_owner);

	return right_copy(&loan->rl_right, right);
}
C2_EXPORTED(c2_rm_loan_init);

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
	resource_put(rem->rem_resource);
}
C2_EXPORTED(c2_rm_remote_fini);

static int rights_integrate(struct c2_rm_incoming *in,
			    struct c2_rm_right *intg_right)
{
	struct c2_rm_pin   *pin;
	struct c2_rm_right *right;
	struct c2_rm_owner *owner  = in->rin_want.ri_owner;
	int		    rc = 0;

	C2_PRE(c2_mutex_is_locked(&owner->ro_lock));

	/* Constructs a single cumulative right */
	c2_tlist_for(&pi_tl, &in->rin_pins, pin) {
		C2_ASSERT(pin->rp_flags == RPF_PROTECT);
		right = pin->rp_right;
		rc = right->ri_ops->rro_join(intg_right, right);
		if (rc != 0)
			break;
	} c2_tlist_endfor;

	if (rc == 0) {
		c2_tlist_for(&pi_tl, &in->rin_pins, pin) {
			right = pin->rp_right;
			pin_del(pin);
			ur_tlink_del_fini(right);
			c2_rm_right_fini(right);
			c2_free(right);
		} c2_tlist_endfor;
	}

	return rc;
}

int c2_rm_borrow_commit(struct c2_rm_remote_incoming *bor)
{
	struct c2_rm_incoming *in     = &bor->ri_incoming;
	struct c2_rm_loan     *loan   = bor->ri_loan;
	struct c2_rm_owner    *owner  = in->rin_want.ri_owner;
	int                    rc;

	C2_PRE(c2_mutex_is_locked(&owner->ro_lock));
	C2_PRE(in->rin_state == RI_SUCCESS);
	C2_PRE(in->rin_type == RIT_BORROW);
	C2_PRE(loan->rl_right.ri_owner == owner);

	rc = rights_integrate(in, &loan->rl_right);
	if (rc == 0) {
		C2_ALLOC_PTR(loan->rl_other);
		if (loan->rl_other != NULL) {
			c2_rm_remote_init(loan->rl_other, owner->ro_resource);
			/*
			 * TODO - Do we need service id in a FOP?
			 */
			loan->rl_other->rem_state = REM_OWNER_LOCATED;
			c2_cookie_copy(&loan->rl_other->rem_cookie,
				       &bor->ri_owner_cookie);
			ur_tlist_add(&owner->ro_sublet, &loan->rl_right);
			bor->ri_loan = NULL;
		} else
			rc = -ENOMEM;
	}

	owner_invariant(owner);
 	return rc;
}
C2_EXPORTED(c2_rm_borrow_commit);

int c2_rm_revoke_commit(struct c2_rm_remote_incoming *rvk)
{
	struct c2_rm_incoming *in     = &rvk->ri_incoming;
	struct c2_rm_loan     *loan   = rvk->ri_loan;
	struct c2_rm_loan     *old_loan;
	struct c2_rm_owner    *owner  = in->rin_want.ri_owner;
	int                    rc = 0;
	struct c2_rm_right     brwd_right;

	C2_PRE(c2_mutex_is_locked(&owner->ro_lock));
	C2_PRE(in->rin_state == RI_SUCCESS);
	C2_PRE(in->rin_type == RIT_REVOKE);

	c2_rm_right_init(&brwd_right, in->rin_want.ri_owner);
	/*
	 * Incoming revoke right may be a subset of the borrowed right.
	 * The borrowed right might have been split on the local node.
	 * Go through all the rights and contruct a cumulative right.
	 */
	rc = rights_integrate(in, &brwd_right);

	/*
	 * Calculate the diff. If incoming right is subset of the borrowed
	 * right, add the remaining (borrowed - incoming) back to the CACHED and
	 * borrowed lists.
	 * Delete the old loan. Add a new loan (with same loan id) for the
	 * remainder of the rights, if any.
	 */
	rc = rc ?: right_diff(&brwd_right, &loan->rl_right);
	if (rc == 0) {
		/*
		 * Earlier, FOM verifies stale cookie. Hence we expect
		 * cookie to be valid here.
		 */
		old_loan = c2_rm_loan_find(&rvk->ri_loan_cookie);
		C2_ASSERT(rc == 0);

		if (!right_is_empty(&brwd_right)) {
			rc = right_copy(&loan->rl_right, &brwd_right);
			if (rc == 0) {
				loan->rl_other = old_loan->rl_other;
				loan->rl_id = old_loan->rl_id;
				c2_cookie_copy(&loan->rl_cookie,
					       &old_loan->rl_cookie);
				ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					     &loan->rl_right);
				ur_tlist_add(&owner->ro_borrowed,
					     &loan->rl_right);
				rvk->ri_loan = NULL;

			}
		} else {
			c2_rm_remote_fini(old_loan->rl_other);
			c2_free(old_loan->rl_other);
		}
		/* Release the old loan */
		ur_tlist_del(&old_loan->rl_right);
		c2_rm_right_fini(&old_loan->rl_right);
		c2_free(old_loan);
	}

	c2_rm_right_fini(&brwd_right);
	owner_invariant(owner);
 	return rc;
}
C2_EXPORTED(c2_rm_revoke_commit);

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

 */
/** @{ */

/**
   External resource manager entry point: request a right from the resource
   owner.
 */
void c2_rm_right_get(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_want.ri_owner;

	C2_PRE(IS_IN_ARRAY(in->rin_priority, owner->ro_incoming));
	C2_PRE(in->rin_state == RI_INITIALISED);
	C2_PRE(in->rin_rc == 0);
	C2_PRE(pi_tlist_is_empty(&in->rin_pins));
	C2_PRE(owner->ro_state == ROS_ACTIVE);

	c2_mutex_lock(&owner->ro_lock);
	/*
	 * This check will make sure that new requests are added
	 * while owner is in ACTIVE state. This will take care
	 * of races between owner state transition and right requests.
	 */
	if (owner->ro_state == ROS_ACTIVE) {
		/*
		 * Mark incoming request "excited". owner_balance() will
		 * process it.
		 */
		ur_tlist_add(&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			     &in->rin_want);
		owner_balance(owner);
	} else {
		in->rin_rc = -ENODEV;
		in->rin_state = RI_FAILURE;
	}
	c2_mutex_unlock(&owner->ro_lock);
}

void c2_rm_right_put(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_want.ri_owner;

	C2_PRE(in->rin_state == RI_SUCCESS);
	C2_PRE(owner->ro_state == ROS_ACTIVE);

	c2_mutex_lock(&owner->ro_lock);
	incoming_release(in);
	ur_tlist_del(&in->rin_want);
	C2_POST(pi_tlist_is_empty(&in->rin_pins));
	c2_mutex_unlock(&owner->ro_lock);
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
	struct c2_rm_loan     *loan;
	struct c2_rm_outgoing *out;
	struct c2_rm_incoming *in;
	bool                   todo;
	int                    prio;

	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	do {
		todo = false;
		c2_tlist_for(&ur_tl, &o->ro_outgoing[OQS_EXCITED], right) {
			todo = true;
			loan = container_of(right, struct c2_rm_loan, rl_right);
			out = container_of(loan, struct c2_rm_outgoing,
					   rog_want);
			/*
			 * Outgoing request completes: remove all pins stuck in
			 * and finalise it. Also pass the processing error, if
			 * any, to the corresponding incoming structure(s).
			 *
			 * Removing of pins might excite incoming requests
			 * waiting for outgoing request completion.
			 */
			c2_tlist_for(&pr_tl, &right->ri_pins, pin) {
				C2_ASSERT(pin->rp_flags == RPF_TRACK);
				pin->rp_incoming->rin_rc = out->rog_rc;
				pin_del(pin);
			} c2_tlist_endfor;
			ur_tlink_del_fini(right);
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
   Takes an incoming request in RI_CHECK state and attempt to perform a
   non-blocking state transition.

   This function leaves the request either in RI_WAIT, RI_SUCCESS or RI_FAILURE
   state.
 */
static void incoming_check(struct c2_rm_incoming *in)
{
	struct c2_rm_right rest;
	int		   rc;

	/*
	 * This function is reentrant. An outgoing request might set
	 * the processing error for the incoming structure. Check for the
	 * error. If there is an error, there is no need to continue the
	 * processing.
	 */
	if (in->rin_rc == 0) {
		c2_rm_right_init(&rest, in->rin_want.ri_owner);
		rc = right_copy(&rest, &in->rin_want);
		rc = rc ?: incoming_check_with(in, &rest);
		c2_rm_right_fini(&rest);
	} else
		rc = in->rin_rc;

	if (rc > 0) {
		C2_ASSERT(incoming_pin_nr(in, RPF_PROTECT) == 0);
		in->rin_state = RI_WAIT;
	} else {
		if (rc == 0) {
			C2_ASSERT(incoming_pin_nr(in, RPF_TRACK) == 0);
			incoming_policy_apply(in);
		}
		incoming_complete(in, rc);
	}
}

/**
   Main helper function to incoming_check(), which starts with "rest" set to the
   wanted right and goes though the sequence of checks, reducing "rest".

   CHECK logic can be described by means of "want conditions". A wait condition
   is something that prevents immediate fulfillment of the request.

       - A request with RIF_LOCAL_WAIT bit set can be fulfilled iff the rights
         on ->ro_owned[OWOS_CACHED] list together imply the wanted right;

       - a request without RIF_LOCAL_WAIT bit can be fulfilled iff the rights on
         all ->ro_owned[] lists together imply the wanted right.

   If there is not enough rights on ->ro_owned[] lists, an incoming request has
   to wait until some additional rights are borrowed from the upward creditor or
   revoked from downward debtors.

   A RIF_LOCAL_WAIT request, in addition, can wait until a right moves from
   ->ro_owned[OWOS_HELD] to ->ro_owned[OWOS_CACHED].

   This function performs no state transitions by itself. Instead its return
   value indicates the target state:

       - 0: the request is fulfilled, the target state is RI_SUCCESS,
       - +ve: more waiting is needed, the target state is RI_WAIT,
       - -ve: error, the target state is RI_FAILURE.
 */
static int incoming_check_with(struct c2_rm_incoming *in,
			       struct c2_rm_right *rest)
{
	struct c2_rm_right *want = &in->rin_want;
	struct c2_rm_owner *o    = want->ri_owner;
	struct c2_rm_right *r;
	struct c2_rm_loan  *loan;
	int                 i;
	int                 wait = 0;
	int		    rc = 0;

	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(ur_tlist_contains(&o->ro_incoming[in->rin_priority][OQS_GROUND],
				 want));
	C2_PRE(in->rin_state == RI_CHECK);
	C2_PRE(pi_tlist_is_empty(&in->rin_pins));

	/*
	 * Check for "local" wait conditions.
	 */
	for (i = 0; i < ARRAY_SIZE(o->ro_owned); ++i) {
		c2_tlist_for(&ur_tl, &o->ro_owned[i], r) {
			if (!right_intersects(r, want))
				continue;
			if (i == OWOS_HELD && (in->rin_flags & RIF_LOCAL_WAIT) &&
			    right_conflicts(r, want)) {
				rc = pin_add(in, r, RPF_TRACK);
				wait++;
			} else if (wait == 0)
				rc = pin_add(in, r, RPF_PROTECT);
			rc = rc ?: right_diff(rest, r);
			if (rc != 0)
				return rc;
		} c2_tlist_endfor;
	}

	if (!right_is_empty(rest)) {
		c2_tlist_for(&ur_tl, &o->ro_sublet, r) {
			if (!right_intersects(r, rest))
				continue;
			if (!(in->rin_flags & RIF_MAY_REVOKE))
				return -EREMOTE;
			loan = container_of(r, struct c2_rm_loan, rl_right);
			/*
			 * It is possible that this loop emits multiple
			 * outgoing requests toward the same remote
			 * owner. Don't bother to coalesce them here. The rpc
			 * layer would do this more efficiently.
			 *
			 * XXX use rpc grouping here.
			 */
			wait++;
			rc = revoke_send(in, loan, rest);
			rc = rc ?: right_diff(rest, r);
			if (rc != 0)
				return rc;
		} c2_tlist_endfor;
	}

	if (!right_is_empty(rest)) {
		if (o->ro_creditor != NULL) {
			if (!(in->rin_flags & RIF_MAY_BORROW))
				return -EREMOTE;
			wait++;
			rc = borrow_send(in, rest);
		} else
			rc = -ESRCH;
	}

	return rc ?: wait;
}

/**
   Called when an outgoing request completes (possibly with an error, like a
   timeout).
 */
void c2_rm_outgoing_complete(struct c2_rm_outgoing *og)
{
	struct c2_rm_owner *owner;

	C2_PRE(og != NULL);

	owner = og->rog_owner;
	ur_tlist_move(&owner->ro_outgoing[OQS_EXCITED],
		      &og->rog_want->rl_right);
	owner_balance(owner);
}

/**
   Helper function called when an incoming request processing completes.

   Sets c2_rm_incoming::rin_rc, updates request state, invokes completion
   call-back, broadcasts request channel and releases request pins.
 */
static void incoming_complete(struct c2_rm_incoming *in, int32_t rc)
{
	C2_PRE(c2_mutex_is_locked(&in->rin_want.ri_owner->ro_lock));
	C2_PRE(in->rin_state == RI_INITIALISED || in->rin_state == RI_CHECK);
	C2_PRE(in->rin_rc == 0);
	C2_PRE(rc <= 0);

	in->rin_rc = rc;
	in->rin_state = rc == 0 ? RI_SUCCESS : RI_FAILURE;
	if (rc != 0) {
		incoming_release(in);
		ur_tlist_del(&in->rin_want);
		C2_POST(pi_tlist_is_empty(&in->rin_pins));
	} else {
		/*
		 * incoming_release() might have moved the request into excited
		 * state when the last tracking pin was removed, shun it back
		 * into obscurity.
		 */
		/*
		 * Question : Who moves the right to OWOS_HELD?
		 */
		ur_tlist_move(&in->rin_want.ri_owner->ro_incoming[in->rin_priority][OQS_GROUND],
		      &in->rin_want);
		in->rin_ops->rio_complete(in, rc);
	}
	owner_invariant(in->rin_want.ri_owner);
	c2_chan_broadcast(&in->rin_signal);
}

static void incoming_policy_none(struct c2_rm_incoming *in)
{
}

static void incoming_policy_apply(struct c2_rm_incoming *in)
{
	static void (*generic[RIP_NR])(struct c2_rm_incoming *) = {
		[RIP_NONE]    = &incoming_policy_none,
		[RIP_INPLACE] = &incoming_policy_none,
		[RIP_STRICT]  = &incoming_policy_none,
		[RIP_JOIN]    = &incoming_policy_none,
		[RIP_MAX]     = &incoming_policy_none
	};

	if (IS_IN_ARRAY(in->rin_policy, generic))
		generic[in->rin_policy](in);
	else {
		struct c2_rm_resource *resource;

		resource = in->rin_want.ri_owner->ro_resource;
		resource->r_ops->rop_policy(resource, in);
	}
}

/**

 */
static int outgoing_check(struct c2_rm_incoming *in,
			  enum c2_rm_outgoing_type otype,
			  struct c2_rm_right *right,
			  struct c2_rm_remote *other)
{
	int		       i;
	int		       rc = 0;
	struct c2_rm_owner    *owner = in->rin_want.ri_owner;
	struct c2_rm_right    *scan;
	struct c2_rm_loan     *loan;
	struct c2_rm_outgoing *out;

	for (i = 0; i < ARRAY_SIZE(owner->ro_outgoing); i++) {
		c2_tlist_for(&ur_tl, &owner->ro_outgoing[i], scan) {
			loan = container_of(scan, struct c2_rm_loan, rl_right);
			out = container_of(loan, struct c2_rm_outgoing,
					   rog_want);
			if (out->rog_type == otype && right_intersects(scan,
								       right)) {
				C2_ASSERT(out->rog_want->rl_other == other);
				/* @todo adjust outgoing requests priority
				 * (priority inheritance) */
				rc = pin_add(in, scan, RPF_TRACK);
				rc = rc ?: right_diff(right, scan);
				if (rc != 0)
					break;
			}
		} c2_tlist_endfor;
		if (rc != 0)
			break;
	}
	return rc;
}

/**
   Sends an outgoing revoke request to remote owner specified by the "loan". The
   request will revoke the right "right", which might be a part of original
   loan.
 */
static int revoke_send(struct c2_rm_incoming *in,
		       struct c2_rm_loan *loan, struct c2_rm_right *right)
{
	int rc;

	rc = outgoing_check(in, ROT_REVOKE, right, loan->rl_other);
	if (!right_is_empty(right) && rc == 0)
		rc = c2_rm_revoke_out(in, loan, right);
	return rc;
}

/**
   Sends an outgoing borrow request to the upward creditor. The request will
   borrow the right "right".
 */
static int borrow_send(struct c2_rm_incoming *in, struct c2_rm_right *right)
{
	int rc;

	C2_PRE(in->rin_want.ri_owner->ro_creditor != NULL);

	rc = outgoing_check(in, ROT_BORROW, right,
				in->rin_want.ri_owner->ro_creditor);
	if (!right_is_empty(right) && rc == 0)
		rc = c2_rm_borrow_out(in, right);
	return rc;
}

/**
   Helper function to get right with timed wait (deadline).
 */
int c2_rm_right_timedwait(struct c2_rm_incoming *in, const c2_time_t deadline)
{
	struct c2_clink clink;
	int             rc;

	C2_PRE(in->rin_state > RI_CHECK);

	c2_clink_init(&clink, NULL);
	c2_clink_add(&in->rin_signal, &clink);
	rc = 0;

	while (rc == 0 && in->rin_state == RI_WAIT)
		rc = c2_chan_timedwait(&clink, deadline) ? 0 : -ETIMEDOUT;

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	C2_ASSERT(ergo(rc == 0, in->rin_state == RI_SUCCESS ||
		       in->rin_state == RI_FAILURE));

	return rc ?: in->rin_rc;
}

/**
   Helper function to get right with infinite wait time.
 */
int c2_rm_right_get_wait(struct c2_rm_incoming *in)
{
	c2_rm_right_get(in);
	return c2_rm_right_timedwait(in, C2_TIME_NEVER);
}

/** @} end of Owner state machine group */

/**
   @name invariant Invariants group

   Resource manager maintains a number of interrelated data-structures in
   memory. Invariant checking functions, defined in this section assert internal
   consistency of these structures.

  @{
 */

/** Helper function used by resource_type_invariant() to check all elements of
    c2_rm_resource_type::rt_resources. */
static bool resource_list_check(const struct c2_rm_resource *res, void *datum)
{
	const struct c2_rm_resource_type *rt = datum;

	return resource_find(rt, res) == res && res->r_type == rt;
}

static bool resource_type_invariant(const struct c2_rm_resource_type *rt)
{
	struct c2_rm_domain   *dom   = rt->rt_dom;
	const struct c2_tl    *rlist = &rt->rt_resources;

	return
		res_tlist_invariant_ext(rlist, resource_list_check, (void *)rt) &&
		rt->rt_nr_resources == res_tlist_length(rlist) &&
		dom != NULL && IS_IN_ARRAY(rt->rt_id, dom->rd_types) &&
		dom->rd_types[rt->rt_id] == rt;
}

/**
   Invariant for c2_rm_incoming.
 */
static bool incoming_invariant(const struct c2_rm_incoming *in)
{
	return
		(in->rin_rc != 0) == (in->rin_state == RI_FAILURE) &&
		!(in->rin_flags & ~(RIF_MAY_REVOKE|RIF_MAY_BORROW|RIF_LOCAL_WAIT|
				    RIF_LOCAL_TRY)) &&
		IS_IN_ARRAY(in->rin_priority,
			    in->rin_want.ri_owner->ro_incoming) &&
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
		     incoming_pin_nr(in, ~0) == 0) &&
		pr_tlist_is_empty(&in->rin_want.ri_pins);
}

enum right_queue {
	OIS_BORROWED = 0,
	OIS_SUBLET,
	OIS_OUTGOING,
	OIS_OWNED,
	OIS_INCOMING,
	OIS_NR
};

struct owner_invariant_state {
	enum right_queue    is_phase;
	int                 is_owned_idx;
	struct c2_rm_right  is_debit;
	struct c2_rm_right  is_credit;
	struct c2_rm_owner *is_owner;
};

static bool right_invariant(const struct c2_rm_right *right, void *data)
{
	struct owner_invariant_state *is =
		(struct owner_invariant_state *) data;
	return
		/* only held rights have PROTECT pins */
		(is->is_phase == OIS_OWNED /* && owos == OWOS_HELD*/) ==
		(right_pin_nr(right, RPF_PROTECT) > 0) &&
		ergo(is->is_phase == OIS_INCOMING,
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
	struct c2_rm_right *right;
	int		    rc;

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
	if (!ur_tlist_invariant_ext(&owner->ro_borrowed, &right_invariant, (void *)is))
		return false;
	is->is_phase = OIS_SUBLET;
	if (!ur_tlist_invariant_ext(&owner->ro_sublet,   &right_invariant, (void *)is))
		return false;
	is->is_phase = OIS_OUTGOING;
	if (!ur_tlist_invariant_ext(&owner->ro_outgoing[0], &right_invariant, (void *)is))
		return false;

	is->is_phase = OIS_OWNED;
	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		is->is_owned_idx = i;
		if (ur_tlist_invariant_ext(&owner->ro_owned[i],
					   &right_invariant, (void *)is))
		    return false;
	}
	is->is_phase = OIS_INCOMING;
	for (i = 0; i < ARRAY_SIZE(owner->ro_incoming); ++i) {
		for (j = 0; j < ARRAY_SIZE(owner->ro_incoming[i]); ++j) {
			if (ur_tlist_invariant(&owner->ro_incoming[i][j]))
				return false;
		}
	}

	/* Calculate debit */
	c2_tlist_for(&ur_tl, &owner->ro_borrowed, right) {
		rc = right->ri_ops->rro_join(&is->is_debit, right);
		if (rc != 0)
			return false;
	} c2_tlist_endfor;

	/* Calculate credit */
	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		is->is_owned_idx = i;
		c2_tlist_for(&ur_tl, &owner->ro_owned[i], right) {
			rc = right->ri_ops->rro_join(&is->is_credit, right);
			if (rc != 0)
				return false;
		} c2_tlist_endfor;
	}
	c2_tlist_for(&ur_tl, &owner->ro_sublet, right) {
		rc = right->ri_ops->rro_join(&is->is_credit, right);
		if (rc != 0)
			return false;
	} c2_tlist_endfor;

	return true;
}

/**
   Checks internal consistency of a resource owner.
 */
static bool owner_invariant(struct c2_rm_owner *owner)
{
	bool                         rc;
	struct owner_invariant_state is;

	C2_SET0(&is);

	c2_rm_right_init(&is.is_debit, owner);
	c2_rm_right_init(&is.is_credit, owner);

	rc = owner_invariant_state(owner, &is) &&
		 right_eq(&is.is_debit, &is.is_credit);

	c2_rm_right_fini(&is.is_debit);
	c2_rm_right_fini(&is.is_credit);
	return rc;
}

/** @} end of invariant group */

/**
   @name pin Pin helpers

  @{
 */

/**
   Number of pins with a given flag combination, stuck in a given right.
 */
static int right_pin_nr(const struct c2_rm_right *right, uint32_t flags)
{
	int		  nr = 0;
	struct c2_rm_pin *pin;

	c2_tlist_for(&pr_tl, &right->ri_pins, pin) {
		if (pin->rp_flags & flags)
			++nr;
	} c2_tlist_endfor;
	return nr;
}

/**
   Number of pins with a given flag combination, issued by a given incoming
   request.
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

/**
   Releases rights pinned by an incoming request, waking up other pending
   incoming requests if necessary.
 */
static void incoming_release(struct c2_rm_incoming *in)
{
	struct c2_rm_pin   *kingpin;
	struct c2_rm_pin   *pin;
	struct c2_rm_right *right;

	c2_tlist_for(&pi_tl, &in->rin_pins, kingpin) {
		if (kingpin->rp_flags & RPF_PROTECT) {
			right = kingpin->rp_right;
			/*
			 * If this was the last protecting pin, wake up incoming
			 * requests waiting on this right release.
			 */
			if (right_pin_nr(right, RPF_PROTECT) == 1) {
				/*
				 * I think we are introducing "thundering herd"
				 * problem here.
				 */
				c2_tlist_for(&pr_tl, &right->ri_pins, pin) {
					if (pin->rp_flags & RPF_TRACK)
						pin_del(pin);
				} c2_tlist_endfor;
			}
		}
		pin_del(kingpin);
	} c2_tlist_endfor;
}

/**
   Removes a pin on a resource usage right.

   If this was a last tracking pin issued by the request---excite the latter.
 */
static void pin_del(struct c2_rm_pin *pin)
{
	struct c2_rm_incoming *in;
	struct c2_rm_owner    *owner;

	C2_ASSERT(pin != NULL);

	in = pin->rp_incoming;
	owner = in->rin_want.ri_owner;
	pi_tlink_del_fini(pin);
	pr_tlink_del_fini(pin);
	if (incoming_pin_nr(in, RPF_TRACK) == 0 && (pin->rp_flags & RPF_TRACK))
		/*
		 * Last tracking pin removed, excite the request.
		 */
		ur_tlist_move(&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			      &in->rin_want);
	c2_free(pin);
}

/**
   Sticks a tracking pin on @right. When @right is released, the all incoming
   requests that stuck pins into it are notified.
 */
int pin_add(struct c2_rm_incoming *in,
	    struct c2_rm_right *right,
	    uint32_t flags)
{
	struct c2_rm_pin *pin;

	C2_ALLOC_PTR(pin);
	if (pin != NULL) {
		pin->rp_flags = flags;
		pin->rp_right = right;
		pin->rp_incoming = in;
		pr_tlink_init(pin);
		pi_tlink_init(pin);
		pr_tlist_add(&right->ri_pins, pin);
		pi_tlist_add(&in->rin_pins, pin);
		return 0;
	} else
		return -ENOMEM;
}

/** @} end of pin group */

/**
   @name right Right helpers

  @{
 */

static bool right_intersects(const struct c2_rm_right *A,
			     const struct c2_rm_right *B)
{
	return A->ri_ops->rro_intersects(A, B);
}

static bool right_conflicts(const struct c2_rm_right *A,
			    const struct c2_rm_right *B)
{
	return A->ri_ops->rro_conflicts(A, B);
}


static int right_diff(struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	return r0->ri_ops->rro_diff(r0, r1);
}

static bool right_eq(const struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	int  rc;
	bool res;
	struct c2_rm_right right;

	/* no apples and oranges comparison. */
	C2_PRE(r0->ri_owner == r1->ri_owner);
	c2_rm_right_init(&right, r0->ri_owner);
	rc = right_copy(&right, r0);
	rc = rc ?: right_diff(&right, r1);

	res = rc ? false : right_is_empty(&right);
	c2_rm_right_fini(&right);

	return res;
}

/**
   Makes another copy of right struct.
 */
static int right_copy(struct c2_rm_right *dst, const struct c2_rm_right *src)
{
	C2_PRE(src != NULL);
	C2_PRE(dst->ri_datum == 0);

	return src->ri_ops->rro_copy(dst, src);
}

/**
   Returns true when ri_datum is 0, else returns false.
 */
static bool right_is_empty(const struct c2_rm_right *right)
{
	return right->ri_datum == 0;
}

/** @} end of right group */

/**
   @name remote Code to deal with remote owners

  @{
 */

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
	struct c2_rm_resource_type *rtype = right->ri_owner->ro_resource->r_type;
	struct c2_rm_resource *res;
	int		       rc;

	C2_PRE(other->rem_state == REM_INITIALIZED);

	other->rem_state = REM_SERVICE_LOCATING;
	rc = service_locate(rtype, other);
	if (rc != 0)
		goto error;

	other->rem_state = REM_OWNER_LOCATING;
	rc = resource_locate(rtype, other);
	if (rc != 0)
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
	return rc;
}
C2_EXPORTED(c2_rm_net_locate);

/*
 * Get the owner address from a cookie.
 * 
 * @ret NULL - if cookie is stale
 *      pointer - if cookie is valid
 */
struct c2_rm_owner *c2_rm_owner_find(const struct c2_cookie *cookie)
{
	struct c2_rm_owner *owner;
	int		    rc;

	C2_PRE(cookie != NULL);

	rc = c2_cookie_dereference(cookie, (void **)&owner);
	return rc ? NULL : owner;
}
C2_EXPORTED(c2_rm_owner_find);

/*
 * Get the loan address from a cookie.
 * 
 * @ret NULL - if cookie is stale
 *      pointer - if cookie is valid
 */
struct c2_rm_loan *c2_rm_loan_find(const struct c2_cookie *cookie)
{
	struct c2_rm_loan *loan;
	int		   rc;

	C2_PRE(cookie != NULL);

	rc = c2_cookie_dereference(cookie, (void **)&loan);
	return rc ? NULL : loan;
}
C2_EXPORTED(c2_rm_loan_find);

void c2_rm_owner_cookie_get(const struct c2_rm_owner *owner,
			    struct c2_cookie *cookie)
{
	c2_cookie_remote_build ((void *)owner, cookie);
}
C2_EXPORTED(c2_rm_owner_cookie);

void c2_rm_loan_cookie_get(const struct c2_rm_loan *loan,
		           struct c2_cookie *cookie)
{
	c2_cookie_remote_build ((void *)loan, cookie);
}
C2_EXPORTED(c2_rm_loan_cookie);

int c2_rm_rdatum2buf(struct c2_rm_right *right,
		     void **buf, c2_bcount_t *bytesnr)
{
	struct c2_bufvec	datum_buf = C2_BUFVEC_INIT_BUF(buf, bytesnr);
	struct c2_bufvec_cursor cursor;

	C2_PRE(buf != NULL);
	C2_PRE(bytesnr != NULL);

	*bytesnr = right->ri_ops->rro_len(right);
	*buf = c2_alloc(*bytesnr);
	if (*buf == NULL)
		return -ENOMEM;

	c2_bufvec_cursor_init(&cursor, &datum_buf);
	return right->ri_ops->rro_encode(right, &cursor);
}
C2_EXPORTED(c2_rm_rdatum2buf);

int c2_rm_buf2rdatum(struct c2_rm_right *right, void *buf, c2_bcount_t bytesnr)
{
	struct c2_bufvec	datum_buf = C2_BUFVEC_INIT_BUF(&buf, &bytesnr);
	struct c2_bufvec_cursor cursor;

	c2_bufvec_cursor_init(&cursor, &datum_buf);
	return right->ri_ops->rro_decode(right, &cursor);
}
C2_EXPORTED(c2_rm_buf2rdatum);

/** @} end of remote group */

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
