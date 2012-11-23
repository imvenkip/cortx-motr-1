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
 * Original author: Rajesh Bhalerao <rajesh_bhalerao@xyratex.com>
 * Original creation date: 04/28/2011
 */
#include "lib/memory.h" /* C2_ALLOC_PTR */
#include "lib/misc.h"   /* C2_SET_ARR0 */
#include "lib/errno.h"  /* ETIMEDOUT */
#include "lib/arith.h"  /* C2_CNT_{INC,DEC} */
#include "lib/trace.h"
#include "lib/bob.h"
#include "colibri/magic.h"
#include "sm/sm.h"

#include "rm/rm.h"
#include "rm/rm_internal.h"

/**
   @addtogroup rm
   @{
 */
struct owner_invariant_state;

static void resource_get           (struct c2_rm_resource *res);
static void resource_put           (struct c2_rm_resource *res);
static bool resource_list_check    (const struct c2_rm_resource *res,
				    void *datum);
static bool resource_type_invariant(const struct c2_rm_resource_type *rt);

static void owner_balance          (struct c2_rm_owner *o);
static bool owner_invariant        (struct c2_rm_owner *owner);

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
static void retire_incoming_complete(struct c2_rm_incoming *in,
				     int32_t rc);
static void retire_incoming_conflict(struct c2_rm_incoming *in);
static int cached_rights_hold       (struct c2_rm_incoming *in);
static void cached_rights_flush     (struct c2_rm_owner *owner);
static bool owner_is_idle	    (struct c2_rm_owner *o);
static bool incoming_is_complete    (struct c2_rm_incoming *in);
static int remnant_right_get	    (const struct c2_rm_right *src,
				     const struct c2_rm_right *diff,
				     struct c2_rm_right **remnant_right);
static int remnant_loan_get	    (const struct c2_rm_loan *loan,
				     const struct c2_rm_right *right,
				     struct c2_rm_loan **remnant_loan);
static int loan_dup		    (const struct c2_rm_loan *src_loan,
				     struct c2_rm_loan **dest_loan);
static void loans_flush		    (struct c2_rm_owner *src_owner);
static struct c2_rm_resource *
resource_find(const struct c2_rm_resource_type *rt,
	      const struct c2_rm_resource *res);

C2_TL_DESCR_DEFINE(res, "resources", , struct c2_rm_resource,
		   r_linkage, r_magix,
		   C2_RM_RESOURCE_MAGIC, C2_RM_RESOURCE_HEAD_MAGIC);
C2_TL_DEFINE(res, C2_INTERNAL, struct c2_rm_resource);

static struct c2_bob_type resource_bob;
C2_BOB_DEFINE(C2_INTERNAL, &resource_bob, c2_rm_resource);

C2_TL_DESCR_DEFINE(c2_rm_ur, "usage rights", , struct c2_rm_right,
		   ri_linkage, ri_magix,
		   C2_RM_RIGHT_MAGIC, C2_RM_USAGE_RIGHT_HEAD_MAGIC);
C2_TL_DEFINE(c2_rm_ur, C2_INTERNAL, struct c2_rm_right);

C2_TL_DESCR_DEFINE(remotes, "remote owners", , struct c2_rm_remote,
		   rem_linkage, rem_magix,
		   C2_RM_REMOTE_MAGIC, C2_RM_REMOTE_OWNER_HEAD_MAGIC);
C2_TL_DEFINE(remotes, C2_INTERNAL, struct c2_rm_remote);

static const struct c2_bob_type right_bob = {
        .bt_name         = "right",
        .bt_magix_offset = offsetof(struct c2_rm_right, ri_magix),
        .bt_magix        = C2_RM_RIGHT_MAGIC,
        .bt_check        = NULL
};
C2_BOB_DEFINE(C2_INTERNAL, &right_bob, c2_rm_right);

C2_TL_DESCR_DEFINE(pr, "pins-of-right", , struct c2_rm_pin,
		   rp_right_linkage, rp_magix,
		   C2_RM_PIN_MAGIC, C2_RM_RIGHT_PIN_HEAD_MAGIC);
C2_TL_DEFINE(pr, C2_INTERNAL, struct c2_rm_pin);

C2_TL_DESCR_DEFINE(pi, "pins-of-incoming", , struct c2_rm_pin,
		   rp_incoming_linkage, rp_magix,
		   C2_RM_PIN_MAGIC, C2_RM_INCOMING_PIN_HEAD_MAGIC);
C2_TL_DEFINE(pi, C2_INTERNAL, struct c2_rm_pin);

static const struct c2_bob_type pin_bob = {
        .bt_name         = "pin",
        .bt_magix_offset = offsetof(struct c2_rm_pin, rp_magix),
        .bt_magix        = C2_RM_PIN_MAGIC,
        .bt_check        = NULL
};
C2_BOB_DEFINE(static, &pin_bob, c2_rm_pin);

const struct c2_bob_type loan_bob = {
	.bt_name         = "loan",
	.bt_magix_offset = offsetof(struct c2_rm_loan, rl_magix),
	.bt_magix        = C2_RM_LOAN_MAGIC,
	.bt_check        = NULL
};
C2_BOB_DEFINE(, &loan_bob, c2_rm_loan);

static const struct c2_bob_type incoming_bob = {
	.bt_name         = "incoming request",
	.bt_magix_offset = offsetof(struct c2_rm_incoming, rin_magix),
	.bt_magix        = C2_RM_INCOMING_MAGIC,
	.bt_check        = NULL
};
C2_BOB_DEFINE(static, &incoming_bob, c2_rm_incoming);

static const struct c2_bob_type outgoing_bob = {
	.bt_name         = "outgoing request ",
	.bt_magix_offset = offsetof(struct c2_rm_outgoing, rog_magix),
	.bt_magix        = C2_RM_OUTGOING_MAGIC,
	.bt_check        = NULL
};
C2_BOB_DEFINE(C2_INTERNAL, &outgoing_bob, c2_rm_outgoing);

C2_INTERNAL void c2_rm_domain_init(struct c2_rm_domain *dom)
{
	C2_PRE(dom != NULL);

	C2_SET_ARR0(dom->rd_types);
	c2_mutex_init(&dom->rd_lock);
	c2_bob_type_tlist_init(&resource_bob, &res_tl);
}
C2_EXPORTED(c2_rm_domain_init);

C2_INTERNAL void c2_rm_domain_fini(struct c2_rm_domain *dom)
{
	C2_PRE(c2_forall(i, ARRAY_SIZE(dom->rd_types),
			 dom->rd_types[i] == NULL));
	c2_mutex_fini(&dom->rd_lock);
}
C2_EXPORTED(c2_rm_domain_fini);

static const struct c2_rm_incoming_ops retire_incoming_ops = {
	.rio_complete = retire_incoming_complete,
	.rio_conflict = retire_incoming_conflict,
};

/**
 * Returns a resource equal to a given one from a resource type's resource list
 * or NULL if none.
 */
static struct c2_rm_resource *
resource_find(const struct c2_rm_resource_type *rt,
	      const struct c2_rm_resource      *res)
{
	struct c2_rm_resource *scan;

	C2_PRE(rt->rt_ops->rto_eq != NULL);

	c2_tl_for(res, (struct c2_tl *)&rt->rt_resources, scan) {
		C2_ASSERT(c2_rm_resource_bob_check(scan));
		if (rt->rt_ops->rto_eq(res, scan))
			break;
	} c2_tl_endfor;
	return scan;
}

C2_INTERNAL void c2_rm_type_register(struct c2_rm_domain *dom,
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

C2_INTERNAL void c2_rm_type_deregister(struct c2_rm_resource_type *rtype)
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
	res_tlist_fini(&rtype->rt_resources);
	c2_mutex_fini(&rtype->rt_lock);
	c2_mutex_unlock(&dom->rd_lock);

	C2_POST(rtype->rt_dom == NULL);
}
C2_EXPORTED(c2_rm_type_deregister);

C2_INTERNAL void c2_rm_resource_add(struct c2_rm_resource_type *rtype,
				    struct c2_rm_resource *res)
{
	c2_mutex_lock(&rtype->rt_lock);
	C2_PRE(resource_type_invariant(rtype));
	C2_PRE(res->r_ref == 0);
	C2_PRE(resource_find(rtype, res) == NULL);
	res->r_type = rtype;
	res_tlink_init_at(res, &rtype->rt_resources);
	remotes_tlist_init(&res->r_remote);
	c2_rm_resource_bob_init(res);
	C2_CNT_INC(rtype->rt_nr_resources);
	C2_POST(res_tlist_contains(&rtype->rt_resources, res));
	C2_POST(resource_type_invariant(rtype));
	c2_mutex_unlock(&rtype->rt_lock);
	C2_POST(res->r_type == rtype);
}
C2_EXPORTED(c2_rm_resource_add);

C2_INTERNAL void c2_rm_resource_del(struct c2_rm_resource *res)
{
	struct c2_rm_resource_type *rtype = res->r_type;

	c2_mutex_lock(&rtype->rt_lock);
	C2_PRE(res_tlist_contains(&rtype->rt_resources, res));
	C2_PRE(remotes_tlist_is_empty(&res->r_remote));
	C2_PRE(resource_type_invariant(rtype));

	res_tlink_del_fini(res);
	C2_CNT_DEC(rtype->rt_nr_resources);

	C2_POST(resource_type_invariant(rtype));
	C2_POST(!res_tlist_contains(&rtype->rt_resources, res));
	c2_rm_resource_bob_fini(res);
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

static const struct c2_sm_state_descr owner_states[] = {
	[ROS_INITIAL] = {
		.sd_flags     = C2_SDF_INITIAL,
		.sd_name      = "Init",
		.sd_allowed   = C2_BITS(ROS_INITIALISING, ROS_FINAL)
	},
	[ROS_INITIALISING] = {
		.sd_name      = "Initialising",
		.sd_allowed   = C2_BITS(ROS_ACTIVE, ROS_FINAL)
	},
	[ROS_ACTIVE] = {
		.sd_name      = "Active",
		.sd_allowed   = C2_BITS(ROS_QUIESCE)
	},
	[ROS_QUIESCE] = {
		.sd_name      = "Quiesce",
		.sd_allowed   = C2_BITS(ROS_FINALISING, ROS_FINAL)
	},
	[ROS_FINALISING] = {
		.sd_name      = "Finalising",
		.sd_allowed   = C2_BITS(ROS_DEFUNCT, ROS_FINAL)
	},
	[ROS_DEFUNCT] = {
		.sd_flags     = C2_SDF_TERMINAL,
		.sd_name      = "Defunct"
	},
	[ROS_FINAL] = {
		.sd_flags     = C2_SDF_TERMINAL,
		.sd_name      = "Fini"
	}
};

static const struct c2_sm_conf owner_conf = {
	.scf_name      = "Resource Owner",
	.scf_nr_states = ARRAY_SIZE(owner_states),
	.scf_state     = owner_states
};

/*
 * Group lock is held
 */
static inline void owner_state_set(struct c2_rm_owner *owner,
				   enum c2_rm_owner_state state)
{
	C2_LOG(C2_INFO, "Owner: %p, Owner state: %d, Owner new state: %d\n",
	       owner, owner->ro_sm.sm_state, state);
	c2_sm_state_set(&owner->ro_sm, state);
}

static bool owner_has_loans(struct c2_rm_owner *owner)
{
	return !c2_rm_ur_tlist_is_empty(&owner->ro_sublet) ||
	       !c2_rm_ur_tlist_is_empty(&owner->ro_borrowed);
}

static void owner_finalisation_check(struct c2_rm_owner *owner)
{
	switch (owner_state(owner)) {
	case ROS_QUIESCE:
		if (owner_is_idle(owner)) {
			/*
			 * No more user-right requests are pending.
			 * Flush the loans and cached rights.
			 */
			if (owner_has_loans(owner)) {
				owner_state_set(owner, ROS_FINALISING);
				cached_rights_flush(owner);
				loans_flush(owner);
			} else {
				owner_state_set(owner, ROS_FINAL);
				C2_POST(owner_invariant(owner));
			}
		}
		break;
	case ROS_FINALISING:
		/*
		 * loans_flush() creates requests. Make sure that all those
		 * requests are processed. Once the owner is idle, if there
		 * are no pending loans, finalise owner. Otherwise put it
		 * in DEFUNCT state. Currently there is no recovery from
		 * DEFUNCT state.
		 */
		if (owner_is_idle(owner))
			owner_state_set(owner, owner_has_loans(owner) ?
					       ROS_DEFUNCT : ROS_FINAL);
		break;
	case ROS_DEFUNCT:
	case ROS_FINAL:
		break;
	default:
		break;
	}
	/**
	 * @todo Optionally send notification to
	 * objects waiting for finalising the owner.
	 */
}

C2_INTERNAL void c2_rm_owner_lock(struct c2_rm_owner *owner)
{
	c2_sm_group_lock(&owner->ro_sm_grp);
}
C2_EXPORTED(c2_rm_owner_lock);

C2_INTERNAL void c2_rm_owner_unlock(struct c2_rm_owner *owner)
{
	c2_sm_group_unlock(&owner->ro_sm_grp);
}
C2_EXPORTED(c2_rm_owner_unlock);

C2_INTERNAL void c2_rm_owner_init(struct c2_rm_owner *owner,
				  struct c2_rm_resource *res,
				  struct c2_rm_remote *creditor)
{
	C2_PRE(ergo(creditor != NULL,
		    creditor->rem_state >= REM_SERVICE_LOCATED));

	c2_sm_group_init(&owner->ro_sm_grp);
	c2_sm_init(&owner->ro_sm, &owner_conf, ROS_INITIAL,
		   &owner->ro_sm_grp, NULL);

	owner->ro_resource = res;
	c2_rm_owner_lock(owner);
	owner_state_set(owner, ROS_INITIALISING);
	c2_rm_owner_unlock(owner);
	owner->ro_group = NULL;

	c2_rm_ur_tlist_init(&owner->ro_borrowed);
	c2_rm_ur_tlist_init(&owner->ro_sublet);
	RM_OWNER_LISTS_FOR(owner, c2_rm_ur_tlist_init);

	resource_get(res);
	c2_rm_owner_lock(owner);
	owner_state_set(owner, ROS_ACTIVE);
	c2_rm_owner_unlock(owner);
	owner->ro_creditor = creditor;
	c2_cookie_new(&owner->ro_id);

	C2_POST(owner_invariant(owner));
	C2_POST(owner->ro_resource == res);

}
C2_EXPORTED(c2_rm_owner_init);

C2_INTERNAL int c2_rm_owner_selfadd(struct c2_rm_owner *owner,
				    struct c2_rm_right *r)
{
	struct c2_rm_right *right_transfer;
	struct c2_rm_loan  *nominal_capital;
	int                 rc;

	C2_PRE(r != NULL);
	C2_PRE(r->ri_owner == owner);
	/* Owner must be "top-most" */
	C2_PRE(owner->ro_creditor == NULL);

	C2_ALLOC_PTR(nominal_capital);
	if (nominal_capital != NULL) {
		/*
		 * Immediately transfer the rights. Otherwise owner will not
		 * be balanced.
		 */
		C2_ALLOC_PTR(right_transfer);
		if (right_transfer == NULL) {
			c2_free(nominal_capital);
			return -ENOMEM;
		}
		c2_rm_right_init(right_transfer, owner);
		rc = right_copy(right_transfer, r) ?:
		     c2_rm_loan_init(nominal_capital, r, NULL);
		if (rc == 0) {
			nominal_capital->rl_other = owner->ro_creditor;
			nominal_capital->rl_id = C2_RM_LOAN_SELF_ID;
			/* Add capital to the borrowed list. */
			c2_rm_ur_tlist_add(&owner->ro_borrowed,
					   &nominal_capital->rl_right);
			/* Add right transfer to the CACHED list. */
			c2_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   right_transfer);
		} else {
			c2_free(nominal_capital);
			c2_free(right_transfer);
		}
	} else
		rc = -ENOMEM;

	C2_POST(ergo(rc == 0, owner_invariant(owner)));
	return rc;
}
C2_EXPORTED(c2_rm_owner_selfadd);

/*
 * @todo Stub. Mainline code does not call this callback yet.
 */
static void retire_incoming_conflict(struct c2_rm_incoming *in)
{
	C2_IMPOSSIBLE("Conflict not possible during retirement");
}

static void retire_incoming_complete(struct c2_rm_incoming *in, int32_t rc)
{
	c2_free(in);
}

static bool owner_is_idle (struct c2_rm_owner *o)
{
	return c2_forall(i, ARRAY_SIZE(o->ro_incoming),
			 c2_forall(j, ARRAY_SIZE(o->ro_incoming[i]),
			   c2_rm_ur_tlist_is_empty(&o->ro_incoming[i][j])));
}

static void loans_flush(struct c2_rm_owner *owner)
{
	struct c2_rm_right    *right;
	struct c2_rm_loan     *loan;
	struct c2_rm_incoming *in;
	int		       rc = 0;

	/*
	 * While processing the queues, if -ENOMEM or other error occurs
	 * then the owner will be in a limbo. A force cleanup remains one of
	 * the options.
	 */
	c2_tl_for(c2_rm_ur, &owner->ro_sublet, right) {
		C2_ALLOC_PTR(in);
		if (in == NULL)
			break;
		c2_rm_incoming_init(in, owner, C2_RIT_REVOKE,
				    RIP_NONE, RIF_MAY_REVOKE);
		in->rin_priority = 0;
		in->rin_ops = &retire_incoming_ops;
		/**
		 * This is convoluted. Now that user incoming requests have
		 * drained, we add our incoming requests for REVOKE and CANCEL
		 * processing to the incoming queue.
		 *
		 * If there are any errors then loans (sublets, borrows) will
		 * remain in the list. Eventually owner will enter DEFUNCT
		 * state.
		 */
		C2_ASSERT(c2_rm_right_bob_check(right));
		rc = right_copy(&in->rin_want, right);
		if (rc == 0) {
			c2_rm_ur_tlist_add(
			    &owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			    &in->rin_want);
		} else
			break;
	} c2_tl_endfor;

	c2_tl_for(c2_rm_ur, &owner->ro_borrowed, right) {
		C2_ASSERT(c2_rm_right_bob_check(right));
		loan = bob_of(right, struct c2_rm_loan, rl_right, &loan_bob);
		if (loan->rl_id == C2_RM_LOAN_SELF_ID) {
			c2_rm_ur_tlink_del_fini(right);
			c2_free(loan);
		} else {
			/* @todo - pending cancel implementation */
			/* cancel_send(in, loan) */
		}
	} c2_tl_endfor;
	owner_balance(owner);
}

C2_INTERNAL void c2_rm_owner_retire(struct c2_rm_owner *owner)
{
	/*
	 * Put the owner in ROS_QUIESCE. This will prevent any new
	 * incoming requests on it.
	 */
	c2_rm_owner_lock(owner);
	if (owner_state(owner) != ROS_QUIESCE) {
		owner_state_set(owner, ROS_QUIESCE);
		owner_balance(owner);
	}
	c2_sm_group_unlock(&owner->ro_sm_grp);
}

C2_INTERNAL void c2_rm_owner_fini(struct c2_rm_owner *owner)
{
	struct c2_rm_resource *res = owner->ro_resource;

	C2_PRE(owner_invariant(owner));
	C2_PRE(owner->ro_creditor == NULL);

	RM_OWNER_LISTS_FOR(owner, c2_rm_ur_tlist_fini);

	c2_sm_fini(&owner->ro_sm);

	owner->ro_resource = NULL;
	c2_sm_group_fini(&owner->ro_sm_grp);

	resource_put(res);
}
C2_EXPORTED(c2_rm_owner_fini);

C2_INTERNAL void c2_rm_right_init(struct c2_rm_right *right,
				  struct c2_rm_owner *owner)
{
	C2_PRE(right != NULL);
	C2_PRE(owner->ro_resource->r_ops != NULL);
	C2_PRE(owner->ro_resource->r_ops->rop_right_init != NULL);

	right->ri_datum = 0;
	c2_rm_ur_tlink_init(right);
	pr_tlist_init(&right->ri_pins);
	c2_rm_right_bob_init(right);
	right->ri_owner = owner;
	owner->ro_resource->r_ops->rop_right_init(owner->ro_resource, right);

	C2_POST(right->ri_ops != NULL);
}
C2_EXPORTED(c2_rm_right_init);

C2_INTERNAL void c2_rm_right_fini(struct c2_rm_right *right)
{
	C2_PRE(right != NULL);

	c2_rm_ur_tlink_fini(right);
	pr_tlist_fini(&right->ri_pins);
	c2_rm_right_bob_fini(right);
	right->ri_ops->rro_free(right);
}
C2_EXPORTED(c2_rm_right_fini);

static const struct c2_sm_state_descr inc_states[] = {
	[RI_INITIALISED] = {
		.sd_flags     = C2_SDF_INITIAL,
		.sd_name      = "Initialised",
		.sd_allowed   = C2_BITS(RI_CHECK, RI_FAILURE, RI_FINAL)
	},
	[RI_CHECK] = {
		.sd_name      = "Check",
		.sd_allowed   = C2_BITS(RI_SUCCESS, RI_FAILURE, RI_WAIT)
	},
	[RI_SUCCESS] = {
		.sd_name      = "Success",
		.sd_allowed   = C2_BITS(RI_RELEASED)
	},
	[RI_FAILURE] = {
		.sd_flags     = C2_SDF_FAILURE,
		.sd_name      = "Failure",
		.sd_allowed   = C2_BITS(RI_FINAL)
	},
	[RI_WAIT] = {
		.sd_name      = "Wait",
		.sd_allowed   = C2_BITS(RI_WAIT, RI_FAILURE, RI_CHECK)
	},
	[RI_RELEASED] = {
		.sd_name      = "Released",
		.sd_allowed   = C2_BITS(RI_FINAL)
	},
	[RI_FINAL] = {
		.sd_flags     = C2_SDF_TERMINAL,
		.sd_name      = "Final",
	}
};

static const struct c2_sm_conf inc_conf = {
	.scf_name      = "Incoming Request",
	.scf_nr_states = ARRAY_SIZE(inc_states),
	.scf_state     = inc_states
};

static inline void incoming_state_set(struct c2_rm_incoming *in,
				      enum c2_rm_incoming_state state)
{
	C2_PRE(c2_mutex_is_locked(&in->rin_want.ri_owner->ro_sm_grp.s_lock));
	C2_LOG(C2_INFO, "Incoming req: %p, incoming state: %d, new state: %d\n",
	       in, in->rin_sm.sm_state, state);
	c2_sm_state_set(&in->rin_sm, state);
}

C2_INTERNAL void c2_rm_incoming_init(struct c2_rm_incoming *in,
				     struct c2_rm_owner *owner,
				     enum c2_rm_incoming_type type,
				     enum c2_rm_incoming_policy policy,
				     uint64_t flags)
{
	C2_PRE(in != NULL);

	C2_SET0(in);
	c2_sm_init(&in->rin_sm, &inc_conf, RI_INITIALISED,
		   &owner->ro_sm_grp, NULL);
	in->rin_type   = type;
	in->rin_policy = policy;
	in->rin_flags  = flags;
	pi_tlist_init(&in->rin_pins);
	c2_rm_right_init(&in->rin_want, owner);
	c2_rm_incoming_bob_init(in);
	C2_POST(incoming_invariant(in));
}
C2_EXPORTED(c2_rm_incoming_init);

C2_INTERNAL void c2_rm_incoming_fini(struct c2_rm_incoming *in)
{
	C2_PRE(incoming_invariant(in));
	c2_rm_owner_lock(in->rin_want.ri_owner);
	C2_PRE(C2_IN(incoming_state(in),
	       (RI_INITIALISED, RI_FAILURE, RI_RELEASED)));
	incoming_state_set(in, RI_FINAL);
	c2_rm_owner_unlock(in->rin_want.ri_owner);
	c2_sm_fini(&in->rin_sm);
	c2_rm_incoming_bob_fini(in);
	c2_rm_right_fini(&in->rin_want);
	pi_tlist_fini(&in->rin_pins);
}
C2_EXPORTED(c2_rm_incoming_fini);

C2_INTERNAL void c2_rm_outgoing_init(struct c2_rm_outgoing *out,
				     enum c2_rm_outgoing_type req_type)
{
	C2_PRE(out != NULL);

	out->rog_rc = 0;
	out->rog_type = req_type;
	c2_rm_outgoing_bob_init(out);
}
C2_EXPORTED(c2_rm_outgoing_init);

C2_INTERNAL void c2_rm_outgoing_fini(struct c2_rm_outgoing *out)
{
	C2_PRE(out != NULL);
	c2_rm_outgoing_bob_fini(out);
}
C2_EXPORTED(c2_rm_outgoing_fini);

static int loan_dup(const struct c2_rm_loan *src_loan,
		    struct c2_rm_loan **dest_loan)
{
	return c2_rm_loan_alloc(dest_loan, &src_loan->rl_right,
				src_loan->rl_other);
}

C2_INTERNAL int c2_rm_loan_alloc(struct c2_rm_loan **loan,
				 const struct c2_rm_right *right,
				 struct c2_rm_remote *creditor)
{
	struct c2_rm_loan *new_loan;
	int		   rc = -ENOMEM;

	C2_PRE(loan != NULL);
	C2_PRE(right != NULL);

	C2_ALLOC_PTR(new_loan);
	if (new_loan != NULL) {
		rc = c2_rm_loan_init(new_loan, right, creditor);
		if (rc != 0) {
			c2_free(new_loan);
			new_loan = NULL;
		}
	}

	*loan = new_loan;
	return rc;
}
C2_EXPORTED(c2_rm_loan_alloc);

/*
 * Allocates a new loan and calculates the difference between
 * loan->rl_right and right.
 */
static int remnant_loan_get(const struct c2_rm_loan *loan,
			    const struct c2_rm_right *right,
			    struct c2_rm_loan **remnant_loan)
{
	struct c2_rm_loan *new_loan;
	int		   rc;

	C2_PRE(remnant_loan != NULL);
	C2_PRE(loan != NULL);

	rc = loan_dup(loan, &new_loan) ?:
		right_diff(&new_loan->rl_right, right);
	if (rc != 0 && new_loan != NULL) {
		c2_rm_loan_fini(new_loan);
		c2_free(new_loan);
		new_loan = NULL;
	}
	*remnant_loan = new_loan;
	return rc;
}

C2_INTERNAL int c2_rm_loan_init(struct c2_rm_loan *loan,
				const struct c2_rm_right *right,
				struct c2_rm_remote *creditor)
{
	C2_PRE(loan != NULL);
	C2_PRE(right != NULL);

	loan->rl_id = 0;
	c2_cookie_new(&loan->rl_id);
	c2_rm_right_init(&loan->rl_right, right->ri_owner);
	c2_rm_loan_bob_init(loan);
	loan->rl_other = creditor;
	if (loan->rl_other != NULL)
		resource_get(loan->rl_other->rem_resource);

	return right_copy(&loan->rl_right, right);
}
C2_EXPORTED(c2_rm_loan_init);

C2_INTERNAL void c2_rm_loan_fini(struct c2_rm_loan *loan)
{
	C2_PRE(loan != NULL);

	c2_rm_right_fini(&loan->rl_right);
	if (loan->rl_other != NULL)
		resource_put(loan->rl_other->rem_resource);
	loan->rl_other = NULL;
	loan->rl_id = 0;
	c2_rm_loan_bob_fini(loan);
}
C2_EXPORTED(c2_rm_loan_fini);

static int remote_find(struct c2_rm_remote **rem,
		       struct c2_rpc_session *session,
		       struct c2_rm_resource *res,
		       struct c2_cookie *cookie)
{
	struct c2_rm_remote *other;
	int		     rc = 0;

	C2_PRE(rem != NULL);
	C2_PRE(res != NULL);
	C2_PRE(cookie != NULL);

	c2_tl_for(remotes, &res->r_remote, other) {
		C2_ASSERT(other->rem_resource == res);
		if (other->rem_cookie.co_addr == cookie->co_addr &&
		    other->rem_cookie.co_generation == cookie->co_generation)
			break;
	} c2_tl_endfor;

	if (other != NULL)
		*rem = other;
	else {
		C2_ALLOC_PTR(other);
		if (other != NULL) {
			c2_rm_remote_init(other, res);
			other->rem_session = session;
			other->rem_state = REM_SERVICE_LOCATED;
			other->rem_cookie = *cookie;
			/* @todo - Figure this out */
			/* other->rem_id = 0; */
			remotes_tlist_add(&res->r_remote, other);
		} else
			rc = -ENOMEM;
	}
	return rc;
}

C2_INTERNAL void c2_rm_remote_init(struct c2_rm_remote *rem,
				   struct c2_rm_resource *res)
{
	C2_PRE(rem->rem_state == REM_FREED);

	rem->rem_state = REM_INITIALISED;
	rem->rem_resource = res;
	c2_chan_init(&rem->rem_signal);
	remotes_tlink_init(rem);
	resource_get(res);
}
C2_EXPORTED(c2_rm_remote_init);

C2_INTERNAL void c2_rm_remote_fini(struct c2_rm_remote *rem)
{
	C2_PRE(rem != NULL);
	C2_PRE(C2_IN(rem->rem_state, (REM_INITIALISED,
				      REM_SERVICE_LOCATED,
				      REM_OWNER_LOCATED)));
	rem->rem_state = REM_FREED;
	c2_chan_fini(&rem->rem_signal);
	remotes_tlink_fini(rem);
	resource_put(rem->rem_resource);
}
C2_EXPORTED(c2_rm_remote_fini);

static void cached_rights_flush(struct c2_rm_owner *owner)
{
	struct c2_rm_right *right;
	int		    i;

	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		c2_tl_for(c2_rm_ur, &owner->ro_owned[i], right) {
			c2_rm_ur_tlink_del_fini(right);
			c2_free(right);
		} c2_tl_endfor;
	}
}

/*
 * Remove the OWOS_CACHED rights that match incoming rights. If the right(s)
 * completely intersects the incoming right, remove it(them) from the cache.
 * If the CACHED right partly intersects with the incoming right, retain the
 * difference in the CACHE.
 */
static int cached_rights_remove(struct c2_rm_incoming *in)
{
	struct c2_rm_pin   *pin;
	struct c2_rm_right *right;
	struct c2_rm_right *remnant_right;
	struct c2_rm_owner *owner = in->rin_want.ri_owner;
	struct c2_tl	    diff_list;
	struct c2_tl	    remove_list;
	int		    rc = 0;

	c2_rm_ur_tlist_init(&diff_list);
	c2_rm_ur_tlist_init(&remove_list);
	c2_tl_for(pi, &in->rin_pins, pin) {
		C2_ASSERT(c2_rm_pin_bob_check(pin));
		C2_ASSERT(pin->rp_flags == C2_RPF_PROTECT);
		right = pin->rp_right;

		pin_del(pin);
		c2_rm_ur_tlist_move(&remove_list, right);
		if (!right->ri_ops->rro_is_subset(right, &in->rin_want)) {
			/*
			 * The cached right does not completely intersect
			 * incoming right.
			 *
			 * Make a copy of the cached right and calculate
			 * the difference with incoming right. Store
			 * the difference in the remnant right.
			 */
			rc = remnant_right_get(right, &in->rin_want,
					       &remnant_right);
			if (rc == 0)
				c2_rm_ur_tlist_add(&diff_list, remnant_right);
		}

	} c2_tl_endfor;

	/*
	 * On successful completion, remove the rights from the "remove-list"
	 * and move the remnant rights to the OWOS_CACHED. Do the opposite
	 * on failure.
	 */
	c2_tl_for(c2_rm_ur, rc ? &diff_list : &remove_list, right) {
		     c2_rm_ur_tlist_del(right);
		     c2_rm_right_fini(right);
		     c2_free(right);

	} c2_tl_endfor;

	c2_tl_for(c2_rm_ur, rc ? &remove_list : &diff_list, right) {
	     c2_rm_ur_tlist_move(&owner->ro_owned[OWOS_CACHED], right);
	} c2_tl_endfor;

	c2_rm_ur_tlist_fini(&diff_list);
	c2_rm_ur_tlist_fini(&remove_list);
	return rc;
}

C2_INTERNAL int c2_rm_borrow_commit(struct c2_rm_remote_incoming *rem_in)
{
	struct c2_rm_incoming *in    = &rem_in->ri_incoming;
	struct c2_rm_owner    *owner = in->rin_want.ri_owner;
	struct c2_rm_loan     *loan = NULL;
	struct c2_rm_remote   *debtor = NULL;
	int                    rc;

	C2_PRE(in->rin_type == C2_RIT_BORROW);

	/*
	 * Allocate loan and copy the right (to be borrowed).
	 * Flush the rights cache and remove incoming rights from the cache.
	 * If everything succeeds add loan to the sublet list.
	 * @todo Find the remote object for this loan.
	 */
	rc = remote_find(&debtor, rem_in->ri_rem_session,
			 owner->ro_resource, &rem_in->ri_rem_owner_cookie) ?:
	     c2_rm_loan_alloc(&loan, &in->rin_want, debtor);
	rc = rc ?: cached_rights_remove(in);
	if (rc == 0) {
		/*
		 * Store the loan in the sublet list.
		 */
		c2_rm_ur_tlist_add(&owner->ro_sublet, &loan->rl_right);
		/*
		 * Store loan cookie llocally. Copy it into
		 * rem_in->ri_loan_cookie.
		 */
		c2_cookie_init(&loan->rl_cookie, &loan->rl_id);
		rem_in->ri_loan_cookie = loan->rl_cookie;
	} else {
		if (loan != NULL) {
			c2_rm_loan_fini(loan);
			c2_free(loan);
		}
	}
	C2_POST(owner_invariant(owner));
	return rc;
}
C2_EXPORTED(c2_rm_borrow_commit);

C2_INTERNAL int c2_rm_revoke_commit(struct c2_rm_remote_incoming *rem_in)
{
	struct c2_rm_incoming *in    = &rem_in->ri_incoming;
	struct c2_rm_owner    *owner = in->rin_want.ri_owner;
	struct c2_rm_loan     *rvk_loan;
	struct c2_rm_loan     *remnant_loan;
	struct c2_rm_loan     *add_loan;
	struct c2_rm_loan     *remove_loan;
	int                    rc = 0;
	bool		       is_remnant = false;

	C2_PRE(in->rin_type == C2_RIT_REVOKE);
	/*
	 * Check if the loan cookie is stale. If the cookie is stale
	 * don't proceed with the reovke processing.
	 */
	rvk_loan = c2_cookie_of(&rem_in->ri_loan_cookie,
				struct c2_rm_loan, rl_id);
	rc = rvk_loan ? 0: -EPROTO;
	if (rc != 0)
		goto out;

	/*
	 * Flush the rights cache and remove incoming rights from the cache.
	 *
	 * Check the difference between the borrowed rights and the revoke
	 * rights. If the revoke fully intersects the previously borrowed right,
	 * remove it from the list and FOM will take care of releasing the
	 * memory.
	 *
	 * If it's a partial revoke, right_diff() will retain the remnant
	 * borrowed right. In such case make, rem_in->ri_loan NULL so that
	 * the loan memory is not released. cached_rights_remove() will leave
	 * remnant right in the CACHE.
	 */
	/*
	 * Remove the loan from borrowed list.
	 */
	c2_rm_ur_tlist_del(&rvk_loan->rl_right);
	/*
	 * Check if there is partial revoke.
	 */
	if (!rvk_loan->rl_right.ri_ops->rro_is_subset(&rvk_loan->rl_right,
	    &in->rin_want)) {
		rc = remnant_loan_get(rvk_loan, &in->rin_want, &remnant_loan);
		is_remnant = true;
	}
	/*
	 * Now remove the corresponding right from the OWOS_CACHED list.
	 */
	rc = rc ?: cached_rights_remove(in);
	/*
	 * If there is a failure add the original loan back to borrowed list.
	 * On success, if there is remnant right, add that to the borrowed list
	 * (this will happen only in partial revoke).
	 */
	add_loan = rc ? rvk_loan : is_remnant ? remnant_loan : NULL;
	/*
	 * Check if there is loan to free.
	 * If there is error & partial revoke remove remnant_loan
	 * If there is success, remove the original loan.
	 */
	remove_loan = rc ? is_remnant ? remnant_loan : NULL : rvk_loan;

	if (add_loan != NULL)
		c2_rm_ur_tlist_add(&owner->ro_borrowed, &add_loan->rl_right);

	if (remove_loan != NULL) {
		c2_rm_loan_fini(remove_loan);
		c2_free(remove_loan);
	}

	C2_POST(owner_invariant(owner));
out:
	return rc;
}
C2_EXPORTED(c2_rm_revoke_commit);

/**
 * @name Owner state machine
 *
 * c2_rm_owner and c2_rm_incoming together form a state machine where basic
 * resource management functionality is implemented.
 *
 * This state machine reacts to the following external events:
 *
 *     - an incoming request from a local user;
 *
 *     - an incoming loan request from another domain;
 *
 *     - an incoming revocation request from another domain;
 *
 *     - local user releases a pin on a right (as a by-product of destroying an
 *       incoming request);
 *
 *     - completion of an outgoing request to another domain (including a
 *       timeout or a failure).
 *
 * Any event is processed in a uniform manner:
 *
 *     - c2_rm_owner::ro_sm_grp Group lock is taken;
 *
 *     - c2_rm_owner lists are updated to reflect the event, see details
 *       below. This temporarily violates the owner_invariant();
 *
 *     - owner_balance() is called to restore the invariant, this might create
 *       new imbalances and go through several iterations;
 *
 *     - c2_rm_owner::ro_sm_grp Group lock is released.
 *
 * Event handling is serialised by the owner lock. It is not legal to wait for
 * networking or IO events under this lock.
 *
 */
/** @{ */

/**
   External resource manager entry point: request a right from the resource
   owner.
 */
C2_INTERNAL void c2_rm_right_get(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_want.ri_owner;

	C2_PRE(IS_IN_ARRAY(in->rin_priority, owner->ro_incoming));
	C2_PRE(in->rin_sm.sm_rc == 0);
	C2_PRE(in->rin_rc == 0);
	C2_PRE(pi_tlist_is_empty(&in->rin_pins));

	c2_rm_owner_lock(owner);
	/*
	 * This check will make sure that new requests are added
	 * while owner is in ACTIVE state. This will take care
	 * of races between owner state transition and right requests.
	 */
	if (owner_state(owner) == ROS_ACTIVE) {
		/*
		 * Mark incoming request "excited". owner_balance() will
		 * process it.
		 */
		c2_rm_ur_tlist_add(
			&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			&in->rin_want);
		owner_balance(owner);
	} else
		c2_sm_move(&in->rin_sm, -ENODEV, RI_FAILURE);

	c2_rm_owner_unlock(owner);
}

C2_INTERNAL void c2_rm_right_put(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_want.ri_owner;

	incoming_release(in);
	c2_rm_owner_lock(owner);
	incoming_state_set(in, RI_RELEASED);
	c2_rm_ur_tlist_del(&in->rin_want);
	C2_ASSERT(pi_tlist_is_empty(&in->rin_pins));

	/*
	 * Release of this right may excite other waiting incoming-requests.
	 * Hence, call owner_balance() to process them.
	 */
	owner_balance(owner);
	c2_rm_owner_unlock(owner);
}

/*
 * After successful completion of incoming request, move OWOS_CACHED rights
 * to OWOS_HELD rights.
 */
static int cached_rights_hold(struct c2_rm_incoming *in)
{
	enum c2_rm_owner_owned_state  ltype;
	struct c2_rm_pin	     *pin;
	struct c2_rm_owner	     *owner = in->rin_want.ri_owner;
	struct c2_rm_right	     *right;
	struct c2_rm_right	     *held_right;
	struct c2_rm_right	      rest;
	struct c2_tl		      transfers;
	int			      rc;

	c2_rm_right_init(&rest, in->rin_want.ri_owner);
	rc = right_copy(&rest, &in->rin_want);
	if (rc != 0)
		goto out;

	c2_rm_ur_tlist_init(&transfers);
	c2_tl_for(pi, &in->rin_pins, pin) {
		C2_ASSERT(pin->rp_flags == C2_RPF_PROTECT);
		right = pin->rp_right;
		C2_ASSERT(right_intersects(&rest, right));
		C2_ASSERT(right->ri_ops != NULL);
		C2_ASSERT(right->ri_ops->rro_is_subset != NULL);

		/* If the right is already part of HELD list, skip it */
		if (right_pin_nr(right, C2_RPF_PROTECT) > 1) {
			rc = right_diff(&rest, right);
			if (rc != 0)
				break;
			else
				continue;
		}

		/*
		 * Check if the cached right is a subset (including a
		 * proper subset) of incoming right (request).
		 */
		if (right->ri_ops->rro_is_subset(right, &rest)) {
			/* Move the subset from CACHED list to HELD list */
			c2_rm_ur_tlist_move(&transfers, right);
			rc = right_diff(&rest, right);
			if (rc != 0)
				break;
		} else {
			C2_ALLOC_PTR(held_right);
			if (held_right == NULL) {
				rc = -ENOMEM;
				break;
			}

			c2_rm_right_init(held_right, owner);
			/*
			 * If incoming right partly intersects, then move
			 * intersection to the HELD list. Retain the difference
			 * in the CACHED list. This may lead to fragmentation of
			 * rights.
			 */
			rc = right->ri_ops->rro_disjoin(right, &rest,
							held_right);
			if (rc != 0) {
				c2_rm_right_fini(held_right);
				c2_free(held_right);
				break;
			}
			c2_rm_ur_tlist_add(&transfers, held_right);
			rc = right_diff(&rest, held_right);
			if (rc != 0)
				break;
			pin_add(in, held_right, C2_RPF_PROTECT);
			pin_del(pin);
		}

	} c2_tl_endfor;

	C2_POST(ergo(rc == 0, right_is_empty(&rest)));
	/*
	 * Only cached rights are part of transfer list.
	 * On success, move the rights to OWOS_HELD list. Otherwise move
	 * them back OWOS_CACHED list.
	 */
	ltype = rc ? OWOS_CACHED : OWOS_HELD;
	c2_tl_for(c2_rm_ur, &transfers, right) {
	     c2_rm_ur_tlist_move(&owner->ro_owned[ltype], right);
	} c2_tl_endfor;

	c2_rm_ur_tlist_fini(&transfers);

out:
	c2_rm_right_fini(&rest);
	return rc;
}

/**
 * Main owner state machine function.
 *
 * Goes through the lists of excited incoming and outgoing requests until all
 * the excitement is gone.
 */
static void owner_balance(struct c2_rm_owner *o)
{
	struct c2_rm_pin      *pin;
	struct c2_rm_right    *right;
	struct c2_rm_outgoing *out;
	struct c2_rm_incoming *in;
	bool                   todo;
	int                    prio;

	do {
		todo = false;
		c2_tl_for(c2_rm_ur, &o->ro_outgoing[OQS_EXCITED], right) {
			C2_ASSERT(c2_rm_right_bob_check(right));
			todo = true;
			out = bob_of(right, struct c2_rm_outgoing,
				     rog_want.rl_right, &outgoing_bob);
			/*
			 * Outgoing request completes: remove all pins stuck in
			 * and finalise it. Also pass the processing error, if
			 * any, to the corresponding incoming structure(s).
			 *
			 * Removing of pins might excite incoming requests
			 * waiting for outgoing request completion.
			 */
			c2_tl_for(pr, &right->ri_pins, pin) {
				C2_ASSERT(c2_rm_pin_bob_check(pin));
				C2_ASSERT(pin->rp_flags == C2_RPF_TRACK);
				/*
				 * If one outgoing request has set an error,
				 * then don't overwrite the error code. It's
				 * possible that an error code could be
				 * reset to 0 as other requests succeed.
				 */
				pin->rp_incoming->rin_rc =
					pin->rp_incoming->rin_rc ?: out->rog_rc;
				pin_del(pin);
			} c2_tl_endfor;
			c2_rm_ur_tlink_del_fini(right);
		} c2_tl_endfor;
		for (prio = ARRAY_SIZE(o->ro_incoming) - 1; prio >= 0; --prio) {
			c2_tl_for(c2_rm_ur,
				  &o->ro_incoming[prio][OQS_EXCITED], right) {
				todo = true;
				in = bob_of(right, struct c2_rm_incoming,
					    rin_want, &incoming_bob);
				/*
				 * All waits completed, go to CHECK
				 * state.
				 */
				c2_rm_ur_tlist_move(
					&o->ro_incoming[prio][OQS_GROUND],
					&in->rin_want);
				incoming_state_set(in, RI_CHECK);
				incoming_check(in);
			} c2_tl_endfor;
		}
	} while (todo);
	/*
	 * Check if owner needs to be finalised.
	 */
	owner_finalisation_check(o);
}

/**
 * Takes an incoming request in RI_CHECK state and attempt to perform a
 * non-blocking state transition.
 *
 * This function leaves the request either in RI_WAIT, RI_SUCCESS or RI_FAILURE
 * state.
 */
static void incoming_check(struct c2_rm_incoming *in)
{
	struct c2_rm_right rest;
	int		   rc;

	C2_PRE(c2_rm_incoming_bob_check(in));
	/*
	 * This function is reentrant. An outgoing request might set
	 * the processing error for the incoming structure. Check for the
	 * error. If there is an error, there is no need to continue the
	 * processing.
	 */
	if (in->rin_rc == 0) {
		c2_rm_right_init(&rest, in->rin_want.ri_owner);
		rc = right_copy(&rest, &in->rin_want) ?:
		     incoming_check_with(in, &rest);
		c2_rm_right_fini(&rest);
	} else
		rc = in->rin_rc;

	if (rc > 0) {
		C2_ASSERT(incoming_pin_nr(in, C2_RPF_PROTECT) == 0);
		incoming_state_set(in, RI_WAIT);
	} else {
		if (rc == 0) {
			C2_ASSERT(incoming_pin_nr(in, C2_RPF_TRACK) == 0);
			incoming_policy_apply(in);
			/*
			 * Transfer the CACHED rights to HELD list. Later
			 * it may be subsumed by policy functions (or
			 * vice versa).
			 */
			rc = cached_rights_hold(in);
		}
		/*
		 * Check if incoming request is complete. When there is
		 * partial failure (with part of the request failing)
		 * of incoming request, it's necesary to check that it's
		 * complete (and there are no outstanding outgoing requests
		 * pending againt it).
		 */
		if (incoming_is_complete(in))
			incoming_complete(in, rc);
	}
}

/*
 * Checks if there are outstanding "outgoing requests" for this incoming
 * requests.
 */
static bool incoming_is_complete(struct c2_rm_incoming *in)
{
	return incoming_pin_nr(in, C2_RPF_TRACK) == 0;
}

/**
 * Main helper function to incoming_check(), which starts with "rest" set to the
 * wanted right and goes though the sequence of checks, reducing "rest".
 *
 * CHECK logic can be described by means of "wait conditions". A wait condition
 * is something that prevents immediate fulfillment of the request.
 *
 *     - A request with RIF_LOCAL_WAIT bit set can be fulfilled iff the rights
 *       on ->ro_owned[OWOS_CACHED] list together imply the wanted right;
 *
 *     - a request without RIF_LOCAL_WAIT bit can be fulfilled iff the rights on
 *       all ->ro_owned[] lists together imply the wanted right.
 *
 * If there is not enough rights on ->ro_owned[] lists, an incoming request has
 * to wait until some additional rights are borrowed from the upward creditor or
 * revoked from downward debtors.
 *
 * A RIF_LOCAL_WAIT request, in addition, can wait until a right moves from
 * ->ro_owned[OWOS_HELD] to ->ro_owned[OWOS_CACHED].
 *
 * This function performs no state transitions by itself. Instead its return
 * value indicates the target state:
 *
 *     - 0: the request is fulfilled, the target state is RI_SUCCESS,
 *     - +ve: more waiting is needed, the target state is RI_WAIT,
 *     - -ve: error, the target state is RI_FAILURE.
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

	C2_PRE(c2_rm_ur_tlist_contains(
		       &o->ro_incoming[in->rin_priority][OQS_GROUND], want));
	C2_PRE(pi_tlist_is_empty(&in->rin_pins));

	/*
	 * 1. Scan owned lists first. Check for "local" wait/try conditions.
	 */
	for (i = 0; i < ARRAY_SIZE(o->ro_owned); ++i) {
		c2_tl_for(c2_rm_ur, &o->ro_owned[i], r) {
			C2_ASSERT(c2_rm_right_bob_check(r));
			if (!right_intersects(r, want))
				continue;
			if (i == OWOS_HELD && right_conflicts(r, want)) {
				if (in->rin_flags & RIF_LOCAL_WAIT) {
					rc = pin_add(in, r, C2_RPF_TRACK);
					if (rc == 0)
						in->rin_ops->rio_conflict(in);
					wait++;
				} else if (in->rin_flags & RIF_LOCAL_TRY)
					return -EBUSY;
			} else if (wait == 0)
				rc = pin_add(in, r, C2_RPF_PROTECT);
			rc = rc ?: right_diff(rest, r);
			if (rc != 0)
				return rc;
		} c2_tl_endfor;
	}

	/*
	 * 2. If the right request cannot still be satisfied, check against the
	 *    sublet list.
	 */
	if (!right_is_empty(rest)) {
		c2_tl_for(c2_rm_ur, &o->ro_sublet, r) {
			C2_ASSERT(c2_rm_right_bob_check(r));
			if (!right_intersects(r, rest))
				continue;
			if (!(in->rin_flags & RIF_MAY_REVOKE))
				return -EREMOTE;
			loan = bob_of(r, struct c2_rm_loan, rl_right,
				      &loan_bob);
			/*
			 * It is possible that this loop emits multiple
			 * outgoing requests toward the same remote
			 * owner. Don't bother to coalesce them here. The rpc
			 * layer would do this more efficiently.
			 *
			 * @todo use rpc grouping here.
			 */
			wait++;
			rc = revoke_send(in, loan, rest) ?: right_diff(rest, r);
			if (rc != 0)
				return rc;
		} c2_tl_endfor;
	}

	/*
	 * 3. If the right request still cannot be satisfied, check
	 *    if it's possible borrow remaining right from the creditor.
	 */
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
 * Called when an outgoing request completes (possibly with an error, like a
 * timeout).
 */
C2_INTERNAL void c2_rm_outgoing_complete(struct c2_rm_outgoing *og)
{
	struct c2_rm_owner *owner;

	C2_PRE(og != NULL);

	owner = og->rog_want.rl_right.ri_owner;
	c2_rm_ur_tlist_move(&owner->ro_outgoing[OQS_EXCITED],
			    &og->rog_want.rl_right);
	owner_balance(owner);
}

/**
 * Helper function called when an incoming request processing completes.
 *
 * Sets c2_rm_incoming::rin_sm.sm_rc, updates request state, invokes completion
 * call-back, broadcasts request channel and releases request pins.
 */
static void incoming_complete(struct c2_rm_incoming *in, int32_t rc)
{
	struct c2_rm_owner *owner = in->rin_want.ri_owner;

	C2_PRE(in->rin_ops != NULL);
	C2_PRE(in->rin_ops->rio_complete != NULL);
	C2_PRE(rc <= 0);

	in->rin_rc = rc;
	c2_sm_move(&in->rin_sm, rc, rc == 0 ? RI_SUCCESS : RI_FAILURE);
	/*
	 * incoming_release() might have moved the request into excited
	 * state when the last tracking pin was removed, shun it back
	 * into obscurity.
	 */
	c2_rm_ur_tlist_move(&owner->ro_incoming[in->rin_priority][OQS_GROUND],
			    &in->rin_want);
	if (rc != 0) {
		incoming_release(in);
		c2_rm_ur_tlist_del(&in->rin_want);
		C2_ASSERT(pi_tlist_is_empty(&in->rin_pins));
	}
	in->rin_ops->rio_complete(in, rc);
	C2_POST(owner_invariant(owner));
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
 * Check if the outgoing request for requested right is already pending.
 * If yes, attach a tracking pin.
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
	struct c2_rm_outgoing *out;

	for (i = 0; i < ARRAY_SIZE(owner->ro_outgoing); ++i) {
		c2_tl_for(c2_rm_ur, &owner->ro_outgoing[i], scan) {
			C2_ASSERT(c2_rm_right_bob_check(scan));
			out = bob_of(scan, struct c2_rm_outgoing,
				     rog_want.rl_right, &outgoing_bob);
			if (out->rog_type == otype &&
			    right_intersects(scan, right)) {
				C2_ASSERT(out->rog_want.rl_other == other);
				/**
				 * @todo adjust outgoing requests priority
				 * (priority inheritance)
				 */
				rc = pin_add(in, scan, C2_RPF_TRACK) ?:
				     right_diff(right, scan);
				if (rc != 0)
					break;
			}
		} c2_tl_endfor;

		if (rc != 0)
			break;
	}
	return rc;
}

/**
 * Sends an outgoing revoke request to remote owner specified by the "loan". The
 * request will revoke the right "right", which might be a part of original
 * loan.
 */
static int revoke_send(struct c2_rm_incoming *in,
		       struct c2_rm_loan *loan, struct c2_rm_right *right)
{
	int rc;

	rc = outgoing_check(in, C2_ROT_REVOKE, right, loan->rl_other);
	if (!right_is_empty(right) && rc == 0)
		rc = c2_rm_request_out(in, loan, right);
	return rc;
}

/**
 * Sends an outgoing borrow request to the upward creditor. The request will
 * borrow the right "right".
 */
static int borrow_send(struct c2_rm_incoming *in, struct c2_rm_right *right)
{
	int rc;

	C2_PRE(in->rin_want.ri_owner->ro_creditor != NULL);

	rc = outgoing_check(in, C2_ROT_BORROW, right,
				in->rin_want.ri_owner->ro_creditor);
	if (!right_is_empty(right) && rc == 0)
		rc = c2_rm_request_out(in, NULL, right);
	return rc;
}

C2_INTERNAL int c2_rm_sublet_remove(struct c2_rm_right *right)
{
	struct c2_rm_owner *owner = right->ri_owner;
	struct c2_rm_right *sublet;
	struct c2_rm_loan  *loan;
	struct c2_rm_loan  *remnant_loan;
	struct c2_tl	    diff_list;
	struct c2_tl	    remove_list;
	int		    rc = 0;

	C2_PRE(right != NULL);

	c2_rm_ur_tlist_init(&diff_list);
	c2_rm_ur_tlist_init(&remove_list);
	c2_tl_for(c2_rm_ur, &owner->ro_sublet, sublet) {
		c2_rm_ur_tlist_move(&remove_list, sublet);
		if (!right->ri_ops->rro_is_subset(sublet, right)) {
			loan = bob_of(sublet, struct c2_rm_loan, rl_right,
				      &loan_bob);
			/* Get diff(loan->rl_right, right) */
			rc = remnant_loan_get(loan, right, &remnant_loan);
			if (rc == 0)
				c2_rm_ur_tlist_add(&diff_list,
						   &remnant_loan->rl_right);
		}
	} c2_tl_endfor;
	/*
	 * On successful completion, remove the rights from the "remove-list"
	 * and move the remnant rights to the OWOS_CACHED. Do the opposite
	 * on failure.
	 */
	c2_tl_for(c2_rm_ur, rc ? &diff_list : &remove_list, right) {
		c2_rm_ur_tlist_del(right);
		loan = bob_of(right, struct c2_rm_loan, rl_right, &loan_bob);
		c2_rm_loan_fini(loan);
		c2_free(loan);

	} c2_tl_endfor;

	c2_tl_for(c2_rm_ur, rc ? &remove_list : &diff_list, right) {
	     c2_rm_ur_tlist_move(&owner->ro_sublet, right);
	} c2_tl_endfor;

	c2_rm_ur_tlist_fini(&diff_list);
	c2_rm_ur_tlist_fini(&remove_list);
	return rc;
}

/** @} end of Owner state machine group */

/**
 * @name invariant Invariants group
 *
 * Resource manager maintains a number of interrelated data-structures in
 * memory. Invariant checking functions, defined in this section assert internal
 * consistency of these structures.
 *
  @{
 */

/**
 * Helper function used by resource_type_invariant() to check all elements of
 *  c2_rm_resource_type::rt_resources.
 */
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
		res_tlist_invariant_ext(rlist, resource_list_check,
					(void *)rt) &&
		rt->rt_nr_resources == res_tlist_length(rlist) &&
		dom != NULL && IS_IN_ARRAY(rt->rt_id, dom->rd_types) &&
		dom->rd_types[rt->rt_id] == rt;
}

/**
 * Invariant for c2_rm_incoming.
 */
static bool incoming_invariant(const struct c2_rm_incoming *in)
{
	return
		(in->rin_rc != 0) == (incoming_state(in) == RI_FAILURE) &&
		!(in->rin_flags & ~(RIF_MAY_REVOKE|RIF_MAY_BORROW|
				    RIF_LOCAL_WAIT|RIF_LOCAL_TRY)) &&
		IS_IN_ARRAY(in->rin_priority,
			    in->rin_want.ri_owner->ro_incoming) &&
		/* a request can be in "check" state only during owner_balance()
		   execution. */
		incoming_state(in) != RI_CHECK &&
		pi_tlist_invariant(&in->rin_pins) &&
		/* a request in the WAIT state... */
		ergo(incoming_state(in) == RI_WAIT,
		     /* waits on something... */
		     incoming_pin_nr(in, C2_RPF_TRACK) > 0 &&
		     /* and doesn't hold anything. */
		     incoming_pin_nr(in, C2_RPF_PROTECT) == 0) &&
		/* a fulfilled request... */
		ergo(incoming_state(in) == RI_SUCCESS,
		     /* holds something... */
		     incoming_pin_nr(in, C2_RPF_PROTECT) > 0 &&
		     /* and waits on nothing. */
		     incoming_pin_nr(in, C2_RPF_TRACK) == 0) &&
		ergo(incoming_state(in) == RI_FAILURE ||
		     incoming_state(in) == RI_INITIALISED,
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
		ergo((is->is_phase == OIS_OWNED &&
		     is->is_owned_idx == OWOS_HELD),
		     right_pin_nr(right, C2_RPF_PROTECT) > 0) &&
		ergo(is->is_phase == OIS_INCOMING,
		     incoming_invariant(container_of(right,
						     struct c2_rm_incoming,
						     rin_want)));
}

/**
 * Checks internal consistency of a resource owner.
 */
static bool owner_invariant_state(const struct c2_rm_owner *owner,
				  struct owner_invariant_state *is)
{
	struct c2_rm_right *right;
	int		    i;
	int		    j;

	/*
	 * Iterate over all rights lists:
	 *
	 *    - checking their consistency as double-linked lists
         *      (c2_rm_ur_tlist_invariant_ext());
	 *
	 *    - making additional consistency checks:
	 *
	 *    - that a right is for the same resource as the owner,
	 *
	 *    - that a right on c2_rm_owner::ro_owned[X] is pinned iff X
         *            == OWOS_HELD.
	 *
	 *    - accumulating total credit and debit.
	 */
	is->is_phase = OIS_BORROWED;
	if (!c2_rm_ur_tlist_invariant_ext(&owner->ro_borrowed,
					  &right_invariant, (void *)is))
		return false;
	is->is_phase = OIS_SUBLET;
	if (!c2_rm_ur_tlist_invariant_ext(&owner->ro_sublet,
					  &right_invariant, (void *)is))
		return false;
	is->is_phase = OIS_OUTGOING;
	if (!c2_rm_ur_tlist_invariant_ext(&owner->ro_outgoing[0],
					  &right_invariant, (void *)is))
		return false;

	is->is_phase = OIS_OWNED;
	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		is->is_owned_idx = i;
		if (!c2_rm_ur_tlist_invariant_ext(&owner->ro_owned[i],
					   &right_invariant, (void *)is))
		    return false;
	}
	is->is_phase = OIS_INCOMING;
	for (i = 0; i < ARRAY_SIZE(owner->ro_incoming); ++i) {
		for (j = 0; j < ARRAY_SIZE(owner->ro_incoming[i]); ++j) {
			if (!c2_rm_ur_tlist_invariant(
				    &owner->ro_incoming[i][j]))
				return false;
		}
	}

	/*
	 * @todo Revisit during inspection. It may not be possible to join
	 *       all the rights. This will make this invariant very complicated.
	 */
	/* Calculate credit */
	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		c2_tl_for(c2_rm_ur, &owner->ro_owned[i], right) {
			if(right->ri_ops->rro_join(&is->is_credit, right))
				return false;
		} c2_tl_endfor;
	}
	c2_tl_for(c2_rm_ur, &owner->ro_sublet, right) {
		if(right->ri_ops->rro_join(&is->is_credit, right))
			return false;
	} c2_tl_endfor;

	/* Calculate debit */
	c2_tl_for(c2_rm_ur, &owner->ro_borrowed, right) {
		if(right->ri_ops->rro_join(&is->is_debit, right))
			return false;
	} c2_tl_endfor;

	return true;
}

/**
 * Checks internal consistency of a resource owner.
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
 * Number of pins with a given flag combination, stuck in a given right.
 */
static int right_pin_nr(const struct c2_rm_right *right, uint32_t flags)
{
	int		  nr = 0;
	struct c2_rm_pin *pin;

	c2_tl_for(pr, &right->ri_pins, pin) {
		C2_ASSERT(c2_rm_pin_bob_check(pin));
		if (pin->rp_flags & flags)
			++nr;
	} c2_tl_endfor;
	return nr;
}

/**
 * Number of pins with a given flag combination, issued by a given incoming
 * request.
 */
static int incoming_pin_nr(const struct c2_rm_incoming *in, uint32_t flags)
{
	int nr;
	struct c2_rm_pin *pin;

	nr = 0;
	c2_tl_for(pi, &in->rin_pins, pin) {
		C2_ASSERT(c2_rm_pin_bob_check(pin));
		if (pin->rp_flags & flags)
			++nr;
	} c2_tl_endfor;
	return nr;
}

/**
 * Releases rights pinned by an incoming request, waking up other pending
 * incoming requests if necessary.
 */
static void incoming_release(struct c2_rm_incoming *in)
{
	struct c2_rm_pin   *kingpin;
	struct c2_rm_pin   *pin;
	struct c2_rm_right *right;
	struct c2_rm_owner *o = in->rin_want.ri_owner;

	c2_tl_for(pi, &in->rin_pins, kingpin) {
		C2_ASSERT(c2_rm_pin_bob_check(kingpin));
		if (kingpin->rp_flags & C2_RPF_PROTECT) {
			right = kingpin->rp_right;
			/*
			 * If this was the last protecting pin, wake up incoming
			 * requests waiting on this right release.
			 */
			if (right_pin_nr(right, C2_RPF_PROTECT) == 1) {
				/*
				 * Move the right back to the CACHED list.
				 */
				c2_rm_ur_tlist_move(&o->ro_owned[OWOS_CACHED],
						    right);
				/*
				 * I think we are introducing "thundering herd"
				 * problem here.
				 */
				c2_tl_for(pr, &right->ri_pins, pin) {
					C2_ASSERT(c2_rm_pin_bob_check(pin));
					if (pin->rp_flags & C2_RPF_TRACK)
						pin_del(pin);
				} c2_tl_endfor;
			}
		}
		pin_del(kingpin);
	} c2_tl_endfor;
}

/**
 * Removes a pin on a resource usage right.
 *
 * If this was a last tracking pin issued by the request---excite the latter.
 * The function returns true if it excited an incoming request.
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
	c2_rm_pin_bob_fini(pin);
	if (incoming_pin_nr(in, C2_RPF_TRACK) == 0 &&
	    pin->rp_flags & C2_RPF_TRACK) {
		/*
		 * Last tracking pin removed, excite the request.
		 */
		c2_rm_ur_tlist_move(
			&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			&in->rin_want);
	}
	c2_free(pin);
}

/**
 * Sticks a tracking pin on right. When right is released, the all incoming
 * requests that stuck pins into it are notified.
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
		c2_rm_pin_bob_init(pin);
		return 0;
	} else
		return -ENOMEM;
}

/** @} end of pin group */

/**
 *  @name right Right helpers
 *
 * @{
 */

static bool right_intersects(const struct c2_rm_right *A,
			     const struct c2_rm_right *B)
{
	C2_PRE(A->ri_ops != NULL);
	C2_PRE(A->ri_ops->rro_intersects != NULL);

	return A->ri_ops->rro_intersects(A, B);
}

static bool right_conflicts(const struct c2_rm_right *A,
			    const struct c2_rm_right *B)
{
	C2_PRE(A->ri_ops != NULL);
	C2_PRE(A->ri_ops->rro_conflicts != NULL);

	return A->ri_ops->rro_conflicts(A, B);
}


static int right_diff(struct c2_rm_right *r0, const struct c2_rm_right *r1)
{
	C2_PRE(r0->ri_ops != NULL);
	C2_PRE(r0->ri_ops->rro_diff != NULL);

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

/*
 * Allocates a new right and calculates the difference between src and diff.
 * Stores the diff(src, diff) in the newly allocated right.
 */
static int remnant_right_get(const struct c2_rm_right *src,
			     const struct c2_rm_right *diff,
			     struct c2_rm_right **remnant_right)
{
	struct c2_rm_right *new_right;
	int		    rc;

	C2_PRE(remnant_right != NULL);
	C2_PRE(src != NULL);
	C2_PRE(diff != NULL);

	rc = c2_rm_right_dup(src, &new_right) ?: right_diff(new_right, diff);
	if (rc != 0 && new_right != NULL) {
		c2_rm_right_fini(new_right);
		c2_free(new_right);
		new_right = NULL;
	}
	*remnant_right = new_right;
	return rc;
}

/**
 * Allocates memory and makes another copy of right struct.
 */
C2_INTERNAL int c2_rm_right_dup(const struct c2_rm_right *src_right,
				struct c2_rm_right **dest_right)
{
	struct c2_rm_right *right;
	int		    rc = -ENOMEM;

	C2_PRE(src_right != NULL);

	C2_ALLOC_PTR(right);
	if (right != NULL) {
		c2_rm_right_init(right, src_right->ri_owner);
		right->ri_ops = src_right->ri_ops;
		rc = right_copy(right, src_right);
		if (rc != 0) {
			c2_rm_right_fini(right);
			c2_free(right);
			right = NULL;
		}
	}
	*dest_right = right;
	return rc;
}
C2_EXPORTED(c2_rm_right_dup);

/**
 * Makes another copy of right struct.
 */
static int right_copy(struct c2_rm_right *dst, const struct c2_rm_right *src)
{
	C2_PRE(src != NULL);
	C2_PRE(dst->ri_datum == 0);

	return src->ri_ops->rro_copy(dst, src);
}

/**
 * Returns true when ri_datum is 0, else returns false.
 */
static bool right_is_empty(const struct c2_rm_right *right)
{
	return right->ri_datum == 0;
}

/** @} end of right group */

/**
 * @name remote Code to deal with remote owners
 *
 * @{
 */

C2_INTERNAL int c2_rm_db_service_query(const char *name,
				       struct c2_rm_remote *rem)
{
        /* Create search query for DB using name as key and
         * find record  and assign service ID */
        rem->rem_state = REM_SERVICE_LOCATED;
        return 0;
}

C2_INTERNAL int c2_rm_remote_resource_locate(struct c2_rm_remote *rem)
{
         /* Send resource management fop to locate resource */
         rem->rem_state = REM_OWNER_LOCATED;
         return 0;
}

/**
 * A distributed resource location data-base is consulted to locate the service.
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
		C2_LOG(C2_ERROR, "c2_rm_db_service_query failed!\n");
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
 * Sends a resource management fop to the service. The service responds
 * with the remote owner identifier (c2_rm_remote::rem_id) used for
 * further communications.
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
		C2_LOG(C2_ERROR, "c2_rm_remote_resource_find failed!\n");
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

C2_INTERNAL int c2_rm_net_locate(struct c2_rm_right *right,
				 struct c2_rm_remote *other)
{
	struct c2_rm_resource_type *rtype;
	struct c2_rm_resource	   *res;
	int			    rc;

	C2_PRE(other->rem_state == REM_INITIALISED);

	rtype = right->ri_owner->ro_resource->r_type;
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
	c2_tl_for(res, &rtype->rt_resources, res) {
		if (rtype->rt_ops->rto_is(res, other->rem_id)) {
			other->rem_resource = res;
			break;
		}
	} c2_tl_endfor;
	c2_mutex_unlock(&rtype->rt_lock);

error:
	return rc;
}
C2_EXPORTED(c2_rm_net_locate);

C2_INTERNAL int c2_rm_right_encode(const struct c2_rm_right *right,
				   struct c2_buf *buf)
{
	struct c2_bufvec	datum_buf;
	struct c2_bufvec_cursor cursor;

	C2_PRE(buf != NULL);
	C2_PRE(right->ri_ops != NULL);
	C2_PRE(right->ri_ops->rro_len != NULL);
	C2_PRE(right->ri_ops->rro_encode != NULL);

	buf->b_nob = right->ri_ops->rro_len(right);
	buf->b_addr = c2_alloc(buf->b_nob);
	if (buf->b_addr == NULL)
		return -ENOMEM;

	datum_buf.ov_buf = &buf->b_addr;
	datum_buf.ov_vec.v_nr = 1;
	datum_buf.ov_vec.v_count = &buf->b_nob;

	c2_bufvec_cursor_init(&cursor, &datum_buf);
	return right->ri_ops->rro_encode(right, &cursor);
}
C2_EXPORTED(c2_rm_right_encode);

C2_INTERNAL int c2_rm_right_decode(struct c2_rm_right *right,
				   struct c2_buf *buf)
{
	struct c2_bufvec	datum_buf = C2_BUFVEC_INIT_BUF(&buf->b_addr,
							       &buf->b_nob);
	struct c2_bufvec_cursor cursor;

	C2_PRE(right->ri_ops != NULL);
	C2_PRE(right->ri_ops->rro_decode != NULL);

	c2_bufvec_cursor_init(&cursor, &datum_buf);
	return right->ri_ops->rro_decode(right, &cursor);
}
C2_EXPORTED(c2_rm_right_decode);

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
