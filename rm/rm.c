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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original author: Rajesh Bhalerao <rajesh_bhalerao@xyratex.com>
 * Original creation date: 04/28/2011
 */

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RM

#include "lib/memory.h" /* M0_ALLOC_PTR */
#include "lib/misc.h"   /* M0_SET_ARR0 */
#include "lib/errno.h"  /* ETIMEDOUT */
#include "lib/arith.h"  /* M0_CNT_{INC,DEC} */
#include "lib/trace.h"
#include "lib/bob.h"
#include "mero/magic.h"
#include "sm/sm.h"

#include "rm/rm.h"
#include "rm/rm_internal.h"

/**
   @addtogroup rm
   @{
 */
struct owner_invariant_state;

static void resource_get           (struct m0_rm_resource *res);
static void resource_put           (struct m0_rm_resource *res);
static bool resource_list_check    (const struct m0_rm_resource *res,
				    void *datum);
static bool resource_type_invariant(const struct m0_rm_resource_type *rt);

static void owner_balance          (struct m0_rm_owner *o);
static bool owner_invariant        (struct m0_rm_owner *owner);

static void pin_del                (struct m0_rm_pin *pin);
static bool owner_invariant_state  (const struct m0_rm_owner *owner,
				    struct owner_invariant_state *is);
static void incoming_check         (struct m0_rm_incoming *in);
static int  incoming_check_with    (struct m0_rm_incoming *in,
				    struct m0_rm_credit *credit);
static void incoming_complete      (struct m0_rm_incoming *in, int32_t rc);
static void incoming_policy_apply  (struct m0_rm_incoming *in);
static void incoming_policy_none   (struct m0_rm_incoming *in);
static bool incoming_invariant     (const struct m0_rm_incoming *in);
static int  incoming_pin_nr        (const struct m0_rm_incoming *in,
				    uint32_t flags);
static void incoming_release       (struct m0_rm_incoming *in);

static int  credit_pin_nr           (const struct m0_rm_credit *credit,
				    uint32_t flags);
static int  service_locate         (struct m0_rm_resource_type *rtype,
				    struct m0_rm_remote *rem);
static int  resource_locate        (struct m0_rm_resource_type *rtype,
				    struct m0_rm_remote *rem);
static int  outgoing_check         (struct m0_rm_incoming *in,
				    enum m0_rm_outgoing_type,
				    struct m0_rm_credit *credit,
				    struct m0_rm_remote *other);
static int  revoke_send            (struct m0_rm_incoming *in,
				    struct m0_rm_loan *loan,
				    struct m0_rm_credit *credit);
static int  borrow_send            (struct m0_rm_incoming *in,
				    struct m0_rm_credit *credit);

static int credit_copy              (struct m0_rm_credit *dest,
				     const struct m0_rm_credit *src);
static bool credit_eq               (const struct m0_rm_credit *c0,
				     const struct m0_rm_credit *c1);
static bool credit_is_empty         (const struct m0_rm_credit *credit);
static bool credit_intersects       (const struct m0_rm_credit *A,
				     const struct m0_rm_credit *B);
static bool credit_conflicts        (const struct m0_rm_credit *A,
				     const struct m0_rm_credit *B);
static int  credit_diff             (struct m0_rm_credit *c0,
				     const struct m0_rm_credit *c1);
static void windup_incoming_complete(struct m0_rm_incoming *in,
				     int32_t rc);
static void windup_incoming_conflict(struct m0_rm_incoming *in);
static int cached_credits_hold       (struct m0_rm_incoming *in);
static void cached_credits_clear     (struct m0_rm_owner *owner);
static bool owner_is_idle	    (struct m0_rm_owner *o);
static bool incoming_is_complete    (struct m0_rm_incoming *in);
static int remnant_credit_get	    (const struct m0_rm_credit *src,
				     const struct m0_rm_credit *diff,
				     struct m0_rm_credit **remnant_credit);
static int remnant_loan_get	    (const struct m0_rm_loan *loan,
				     const struct m0_rm_credit *credit,
				     struct m0_rm_loan **remnant_loan);
static int loan_dup		    (const struct m0_rm_loan *src_loan,
				     struct m0_rm_loan **dest_loan);
static void owner_liquidate	    (struct m0_rm_owner *src_owner);
static void credit_processor        (struct m0_rm_resource_type *rt);

static struct m0_rm_resource *
resource_find(const struct m0_rm_resource_type *rt,
	      const struct m0_rm_resource *res);

M0_TL_DESCR_DEFINE(res, "resources", , struct m0_rm_resource,
		   r_linkage, r_magix,
		   M0_RM_RESOURCE_MAGIC, M0_RM_RESOURCE_HEAD_MAGIC);
M0_TL_DEFINE(res, M0_INTERNAL, struct m0_rm_resource);

static struct m0_bob_type resource_bob;
M0_BOB_DEFINE(M0_INTERNAL, &resource_bob, m0_rm_resource);

M0_TL_DESCR_DEFINE(m0_rm_ur, "usage credits", , struct m0_rm_credit,
		   cr_linkage, cr_magix,
		   M0_RM_CREDIT_MAGIC, M0_RM_USAGE_CREDIT_HEAD_MAGIC);
M0_TL_DEFINE(m0_rm_ur, M0_INTERNAL, struct m0_rm_credit);

M0_TL_DESCR_DEFINE(remotes, "remote owners", , struct m0_rm_remote,
		   rem_linkage, rem_magix,
		   M0_RM_REMOTE_MAGIC, M0_RM_REMOTE_OWNER_HEAD_MAGIC);
M0_TL_DEFINE(remotes, M0_INTERNAL, struct m0_rm_remote);

static const struct m0_bob_type credit_bob = {
        .bt_name         = "credit",
        .bt_magix_offset = offsetof(struct m0_rm_credit, cr_magix),
        .bt_magix        = M0_RM_CREDIT_MAGIC,
        .bt_check        = NULL
};
M0_BOB_DEFINE(M0_INTERNAL, &credit_bob, m0_rm_credit);

M0_TL_DESCR_DEFINE(pr, "pins-of-credit", , struct m0_rm_pin,
		   rp_credit_linkage, rp_magix,
		   M0_RM_PIN_MAGIC, M0_RM_CREDIT_PIN_HEAD_MAGIC);
M0_TL_DEFINE(pr, M0_INTERNAL, struct m0_rm_pin);

M0_TL_DESCR_DEFINE(pi, "pins-of-incoming", , struct m0_rm_pin,
		   rp_incoming_linkage, rp_magix,
		   M0_RM_PIN_MAGIC, M0_RM_INCOMING_PIN_HEAD_MAGIC);
M0_TL_DEFINE(pi, M0_INTERNAL, struct m0_rm_pin);

static const struct m0_bob_type pin_bob = {
        .bt_name         = "pin",
        .bt_magix_offset = offsetof(struct m0_rm_pin, rp_magix),
        .bt_magix        = M0_RM_PIN_MAGIC,
        .bt_check        = NULL
};
M0_BOB_DEFINE(static, &pin_bob, m0_rm_pin);

const struct m0_bob_type loan_bob = {
	.bt_name         = "loan",
	.bt_magix_offset = offsetof(struct m0_rm_loan, rl_magix),
	.bt_magix        = M0_RM_LOAN_MAGIC,
	.bt_check        = NULL
};
M0_BOB_DEFINE(, &loan_bob, m0_rm_loan);

static const struct m0_bob_type incoming_bob = {
	.bt_name         = "incoming request",
	.bt_magix_offset = offsetof(struct m0_rm_incoming, rin_magix),
	.bt_magix        = M0_RM_INCOMING_MAGIC,
	.bt_check        = NULL
};
M0_BOB_DEFINE(static, &incoming_bob, m0_rm_incoming);

static const struct m0_bob_type outgoing_bob = {
	.bt_name         = "outgoing request ",
	.bt_magix_offset = offsetof(struct m0_rm_outgoing, rog_magix),
	.bt_magix        = M0_RM_OUTGOING_MAGIC,
	.bt_check        = NULL
};
M0_BOB_DEFINE(M0_INTERNAL, &outgoing_bob, m0_rm_outgoing);

static const struct m0_bob_type rem_bob = {
	.bt_name         = "proxy for remote owner ",
	.bt_magix_offset = offsetof(struct m0_rm_remote, rem_magix),
	.bt_magix        = M0_RM_REMOTE_MAGIC,
	.bt_check        = NULL
};
M0_BOB_DEFINE(M0_INTERNAL, &rem_bob, m0_rm_remote);

M0_INTERNAL void m0_rm_domain_init(struct m0_rm_domain *dom)
{
	M0_ENTRY();
	M0_PRE(dom != NULL);

	M0_SET_ARR0(dom->rd_types);
	m0_mutex_init(&dom->rd_lock);
	m0_bob_type_tlist_init(&resource_bob, &res_tl);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_domain_init);

M0_INTERNAL void m0_rm_domain_fini(struct m0_rm_domain *dom)
{
	M0_ENTRY();
	M0_PRE(m0_forall(i, ARRAY_SIZE(dom->rd_types),
			 dom->rd_types[i] == NULL));
	m0_mutex_fini(&dom->rd_lock);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_domain_fini);

static const struct m0_rm_incoming_ops windup_incoming_ops = {
	.rio_complete = windup_incoming_complete,
	.rio_conflict = windup_incoming_conflict,
};

/**
 * Returns a resource equal to a given one from a resource type's resource list
 * or NULL if none.
 */
static struct m0_rm_resource *
resource_find(const struct m0_rm_resource_type *rt,
	      const struct m0_rm_resource      *res)
{
	struct m0_rm_resource *scan;

	M0_PRE(rt->rt_ops->rto_eq != NULL);

	m0_tl_for(res, (struct m0_tl *)&rt->rt_resources, scan) {
		M0_ASSERT(m0_rm_resource_bob_check(scan));
		if (rt->rt_ops->rto_eq(res, scan))
			break;
	} m0_tl_endfor;
	return scan;
}

M0_INTERNAL int m0_rm_type_register(struct m0_rm_domain *dom,
				    struct m0_rm_resource_type *rt)
{
	int rc;

	M0_ENTRY("resource type: %s", rt->rt_name);
	M0_PRE(rt->rt_dom == NULL);
	M0_PRE(IS_IN_ARRAY(rt->rt_id, dom->rd_types));
	M0_PRE(dom->rd_types[rt->rt_id] == NULL);

	m0_mutex_init(&rt->rt_lock);
	res_tlist_init(&rt->rt_resources);
	rt->rt_nr_resources = 0;
	m0_sm_group_init(&rt->rt_sm_grp);

	rt->rt_stop_worker = false;
	rc = M0_THREAD_INIT(&rt->rt_worker, struct m0_rm_resource_type *, NULL,
			    &credit_processor, rt, "RM RT agent");
	if (rc != 0)
		M0_RETURN(rc);

	m0_mutex_lock(&dom->rd_lock);
	dom->rd_types[rt->rt_id] = rt;
	rt->rt_dom = dom;
	M0_POST(resource_type_invariant(rt));
	m0_mutex_unlock(&dom->rd_lock);

	M0_POST(dom->rd_types[rt->rt_id] == rt);
	M0_POST(rt->rt_dom == dom);

	M0_RETURN(rc);
}
M0_EXPORTED(m0_rm_type_register);

M0_INTERNAL void m0_rm_type_deregister(struct m0_rm_resource_type *rt)
{
	struct m0_rm_domain *dom = rt->rt_dom;

	M0_ENTRY("resource type: %s", rt->rt_name);
	M0_PRE(dom != NULL);
	M0_PRE(res_tlist_is_empty(&rt->rt_resources));
	M0_PRE(rt->rt_nr_resources == 0);

	m0_mutex_lock(&dom->rd_lock);
	M0_PRE(IS_IN_ARRAY(rt->rt_id, dom->rd_types));
	M0_PRE(dom->rd_types[rt->rt_id] == rt);
	M0_PRE(resource_type_invariant(rt));

	dom->rd_types[rt->rt_id] = NULL;
	m0_mutex_unlock(&dom->rd_lock);

	m0_sm_group_lock(&rt->rt_sm_grp);
	rt->rt_stop_worker = true;
	m0_clink_signal(&rt->rt_sm_grp.s_clink);
	m0_sm_group_unlock(&rt->rt_sm_grp);

	M0_LOG(M0_INFO, "Waiting for RM RT agent to join");
	m0_thread_join(&rt->rt_worker);
	m0_thread_fini(&rt->rt_worker);
	m0_sm_group_fini(&rt->rt_sm_grp);

	rt->rt_dom = NULL;
	res_tlist_fini(&rt->rt_resources);
	m0_mutex_fini(&rt->rt_lock);

	M0_POST(rt->rt_dom == NULL);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_type_deregister);

M0_INTERNAL void m0_rm_resource_add(struct m0_rm_resource_type *rtype,
				    struct m0_rm_resource *res)
{
	M0_ENTRY("res-type: %p resource : %p", rtype, res);
	m0_mutex_lock(&rtype->rt_lock);
	M0_PRE(resource_type_invariant(rtype));
	M0_PRE(res->r_ref == 0);
	M0_PRE(resource_find(rtype, res) == NULL);
	res->r_type = rtype;
	res_tlink_init_at(res, &rtype->rt_resources);
	remotes_tlist_init(&res->r_remote);
	m0_rm_resource_bob_init(res);
	M0_CNT_INC(rtype->rt_nr_resources);
	M0_POST(res_tlist_contains(&rtype->rt_resources, res));
	M0_POST(resource_type_invariant(rtype));
	m0_mutex_unlock(&rtype->rt_lock);
	M0_POST(res->r_type == rtype);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_resource_add);

M0_INTERNAL void m0_rm_resource_del(struct m0_rm_resource *res)
{
	struct m0_rm_resource_type *rtype = res->r_type;

	M0_ENTRY("resource : %p", res);
	m0_mutex_lock(&rtype->rt_lock);
	M0_PRE(res_tlist_contains(&rtype->rt_resources, res));
	M0_PRE(remotes_tlist_is_empty(&res->r_remote));
	M0_PRE(resource_type_invariant(rtype));

	res_tlink_del_fini(res);
	M0_CNT_DEC(rtype->rt_nr_resources);

	M0_POST(resource_type_invariant(rtype));
	M0_POST(!res_tlist_contains(&rtype->rt_resources, res));
	m0_rm_resource_bob_fini(res);
	m0_mutex_unlock(&rtype->rt_lock);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_resource_del);

static void resource_get(struct m0_rm_resource *res)
{
	struct m0_rm_resource_type *rtype = res->r_type;
	uint32_t		    count;

	M0_ENTRY("resource : %p", res);
	m0_mutex_lock(&rtype->rt_lock);
	count = res->r_ref;
	M0_CNT_INC(res->r_ref);
	m0_mutex_unlock(&rtype->rt_lock);
	M0_LOG(M0_DEBUG, "ref[%u -> %u]", count, count + 1);
	M0_LEAVE();
}

static void resource_put(struct m0_rm_resource *res)
{
	struct m0_rm_resource_type *rtype = res->r_type;
	uint32_t		    count = res->r_ref;

	M0_ENTRY("resource : %p", res);
	m0_mutex_lock(&rtype->rt_lock);
	count = res->r_ref;
	M0_CNT_DEC(res->r_ref);
	m0_mutex_unlock(&rtype->rt_lock);
	M0_LOG(M0_DEBUG, "ref[%u -> %u]", count, count - 1);
	M0_LEAVE();
}

static const struct m0_sm_state_descr owner_states[] = {
	[ROS_INITIAL] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Init",
		.sd_allowed   = M0_BITS(ROS_INITIALISING, ROS_FINAL)
	},
	[ROS_INITIALISING] = {
		.sd_name      = "Initialising",
		.sd_allowed   = M0_BITS(ROS_ACTIVE, ROS_FINAL)
	},
	[ROS_ACTIVE] = {
		.sd_name      = "Active",
		.sd_allowed   = M0_BITS(ROS_QUIESCE)
	},
	[ROS_QUIESCE] = {
		.sd_name      = "Quiesce",
		.sd_allowed   = M0_BITS(ROS_FINALISING, ROS_FINAL)
	},
	[ROS_FINALISING] = {
		.sd_name      = "Finalising",
		.sd_allowed   = M0_BITS(ROS_INSOLVENT, ROS_FINAL)
	},
	[ROS_INSOLVENT] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "Insolvent"
	},
	[ROS_FINAL] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "Fini"
	}
};

static const struct m0_sm_conf owner_conf = {
	.scf_name      = "Resource Owner",
	.scf_nr_states = ARRAY_SIZE(owner_states),
	.scf_state     = owner_states
};

/*
 * Group lock is held
 */
static inline void owner_state_set(struct m0_rm_owner *owner,
				   enum m0_rm_owner_state state)
{
	M0_LOG(M0_INFO, "Owner: %p, state change:[%d -> %d]\n",
	       owner, owner->ro_sm.sm_state, state);
	m0_sm_state_set(&owner->ro_sm, state);
}

static bool owner_has_loans(struct m0_rm_owner *owner)
{
	return !m0_rm_ur_tlist_is_empty(&owner->ro_sublet) ||
	       !m0_rm_ur_tlist_is_empty(&owner->ro_borrowed);
}

static void owner_finalisation_check(struct m0_rm_owner *owner)
{
	switch (owner_state(owner)) {
	case ROS_QUIESCE:
		if (owner_is_idle(owner)) {
			/*
			 * No more user-credit requests are pending.
			 * Flush the loans and cached credits.
			 */
			if (owner_has_loans(owner)) {
				owner_state_set(owner, ROS_FINALISING);
				cached_credits_clear(owner);
				owner_liquidate(owner);
			} else {
				owner_state_set(owner, ROS_FINAL);
				M0_POST(owner_invariant(owner));
			}
		}
		break;
	case ROS_FINALISING:
		/*
		 * owner_liquidate() creates requests. Make sure that all those
		 * requests are processed. Once the owner is idle, if there
		 * are no pending loans, finalise owner. Otherwise put it
		 * in INSOLVENT state. Currently there is no recovery from
		 * INSOLVENT state.
		 */
		if (owner_is_idle(owner)) {
			cached_credits_clear(owner);
			owner_state_set(owner, owner_has_loans(owner) ?
					       ROS_INSOLVENT : ROS_FINAL);
		}
		break;
	case ROS_INSOLVENT:
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

static bool owner_smgrp_is_locked(const struct m0_rm_owner *owner)
{
	struct m0_sm_group *smgrp;

	M0_PRE(owner != NULL);
	smgrp = owner_grp(owner);
	return m0_mutex_is_locked(&smgrp->s_lock);
}

M0_INTERNAL void m0_rm_owner_lock(struct m0_rm_owner *owner)
{
	m0_sm_group_lock(owner_grp(owner));
}
M0_EXPORTED(m0_rm_rt_lock);

M0_INTERNAL void m0_rm_owner_unlock(struct m0_rm_owner *owner)
{
	m0_sm_group_unlock(owner_grp(owner));
}
M0_EXPORTED(m0_rm_rt_unlock);

M0_INTERNAL void m0_rm_owner_init(struct m0_rm_owner *owner,
				  struct m0_rm_resource *res,
				  struct m0_rm_remote *creditor)
{
	M0_PRE(ergo(creditor != NULL,
		    creditor->rem_state >= REM_SERVICE_LOCATED));

	M0_ENTRY("owner: %p resource: %p creditor: %p",
		 owner, res, creditor);
	owner->ro_resource = res;
	m0_sm_init(&owner->ro_sm, &owner_conf, ROS_INITIAL,
		   owner_grp(owner), NULL);
	m0_rm_owner_lock(owner);
	owner_state_set(owner, ROS_INITIALISING);
	m0_rm_owner_unlock(owner);
	owner->ro_group = NULL;

	m0_rm_ur_tlist_init(&owner->ro_borrowed);
	m0_rm_ur_tlist_init(&owner->ro_sublet);
	RM_OWNER_LISTS_FOR(owner, m0_rm_ur_tlist_init);
	resource_get(res);
	m0_rm_owner_lock(owner);
	owner_state_set(owner, ROS_ACTIVE);
	m0_rm_owner_unlock(owner);
	owner->ro_creditor = creditor;
	m0_cookie_new(&owner->ro_id);

	M0_POST(owner_invariant(owner));
	M0_POST(owner->ro_resource == res);

	M0_LEAVE();
}
M0_EXPORTED(m0_rm_owner_init);

static void credit_processor(struct m0_rm_resource_type *rt)
{
	M0_ENTRY();
	M0_PRE(rt != NULL);

	while (true) {
		m0_sm_group_lock(&rt->rt_sm_grp);
		if (rt->rt_stop_worker) {
			m0_sm_group_unlock(&rt->rt_sm_grp);
			M0_LEAVE("RM RT agent STOPPED");
			return;
		}
		m0_sm_group_unlock(&rt->rt_sm_grp);
		m0_chan_timedwait(&rt->rt_sm_grp.s_clink,
				  m0_time_from_now(RM_CREDIT_TIMEOUT, 0));
	}
}

M0_INTERNAL int m0_rm_owner_selfadd(struct m0_rm_owner *owner,
				    struct m0_rm_credit *r)
{
	struct m0_rm_credit *credit_transfer;
	struct m0_rm_loan   *nominal_capital;
	int                  rc;

	M0_ENTRY("owner: %p", owner);
	M0_PRE(r != NULL);
	M0_PRE(r->cr_owner == owner);
	/* Owner must be "top-most" */
	M0_PRE(owner->ro_creditor == NULL);

	M0_ALLOC_PTR(nominal_capital);
	if (nominal_capital != NULL) {
		/*
		 * Immediately transfer the credits. Otherwise owner will not
		 * be balanced.
		 */
		M0_ALLOC_PTR(credit_transfer);
		if (credit_transfer == NULL) {
			m0_free(nominal_capital);
			return -ENOMEM;
		}
		m0_rm_credit_init(credit_transfer, owner);
		rc = credit_copy(credit_transfer, r) ?:
		     m0_rm_loan_init(nominal_capital, r, NULL);
		if (rc == 0) {
			nominal_capital->rl_other = owner->ro_creditor;
			nominal_capital->rl_id = M0_RM_LOAN_SELF_ID;
			/* Add capital to the borrowed list. */
			m0_rm_ur_tlist_add(&owner->ro_borrowed,
					   &nominal_capital->rl_credit);
			/* Add credit transfer to the CACHED list. */
			m0_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   credit_transfer);
		} else {
			m0_free(nominal_capital);
			m0_free(credit_transfer);
		}
	} else
		rc = -ENOMEM;

	M0_POST(ergo(rc == 0, owner_invariant(owner)));
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rm_owner_selfadd);

static bool owner_is_idle (struct m0_rm_owner *o)
{
	return m0_forall(i, ARRAY_SIZE(o->ro_incoming),
			 m0_forall(j, ARRAY_SIZE(o->ro_incoming[i]),
			   m0_rm_ur_tlist_is_empty(&o->ro_incoming[i][j])));
}

static void owner_liquidate(struct m0_rm_owner *owner)
{
	struct m0_rm_credit   *credit;
	struct m0_rm_loan     *loan;
	struct m0_rm_incoming *in;
	int		       rc = 0;

	M0_ENTRY("owner: %p", owner);
	/*
	 * While processing the queues, if -ENOMEM or other error occurs
	 * then the owner will be in a limbo. A force cleanup remains one of
	 * the options.
	 */
	m0_tl_for(m0_rm_ur, &owner->ro_sublet, credit) {
		M0_ALLOC_PTR(in);
		if (in == NULL)
			break;
		m0_rm_incoming_init(in, owner, M0_RIT_LOCAL,
				    RIP_NONE, RIF_MAY_REVOKE);
		in->rin_priority = 0;
		in->rin_ops = &windup_incoming_ops;
		/**
		 * This is convoluted. Now that user incoming requests have
		 * drained, we add our incoming requests for REVOKE and CANCEL
		 * processing to the incoming queue.
		 *
		 * If there are any errors then loans (sublets, borrows) will
		 * remain in the list. Eventually owner will enter INSOLVENT
		 * state.
		 */
		M0_ASSERT(m0_rm_credit_bob_check(credit));
		rc = credit_copy(&in->rin_want, credit);
		if (rc == 0) {
			m0_rm_ur_tlist_add(
			    &owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			    &in->rin_want);
		} else
			break;
	} m0_tl_endfor;

	m0_tl_for(m0_rm_ur, &owner->ro_borrowed, credit) {
		M0_ASSERT(m0_rm_credit_bob_check(credit));
		loan = bob_of(credit, struct m0_rm_loan, rl_credit, &loan_bob);
		if (loan->rl_id == M0_RM_LOAN_SELF_ID) {
			m0_rm_ur_tlist_del(credit);
			m0_rm_loan_fini(loan);
			m0_free(loan);
		} else {
			/* @todo - pending cancel implementation */
			/* cancel_send(in, loan) */
		}
	} m0_tl_endfor;
	owner_balance(owner);
	M0_LEAVE();
}

M0_INTERNAL int m0_rm_owner_timedwait(struct m0_rm_owner *owner,
				      uint64_t state,
				      const m0_time_t abs_timeout)
{
	int rc;

	m0_rm_owner_lock(owner);
	rc = m0_sm_timedwait(&owner->ro_sm, state, abs_timeout);
	m0_rm_owner_unlock(owner);

	return rc ?: owner->ro_sm.sm_rc;
}

M0_INTERNAL void m0_rm_owner_windup(struct m0_rm_owner *owner)
{
	/*
	 * Put the owner in ROS_QUIESCE. This will prevent any new
	 * incoming requests on it.
	 */
	m0_rm_owner_lock(owner);
	if (owner_state(owner) != ROS_QUIESCE) {
		owner_state_set(owner, ROS_QUIESCE);
		owner_balance(owner);
	}
	m0_rm_owner_unlock(owner);
}

M0_INTERNAL void m0_rm_owner_fini(struct m0_rm_owner *owner)
{
	struct m0_rm_resource *res = owner->ro_resource;

	M0_ENTRY("owner: %p", owner);
	M0_PRE(owner_invariant(owner));
	M0_PRE(owner->ro_creditor == NULL);
	M0_PRE(M0_IN(owner_state(owner), (ROS_FINAL, ROS_INSOLVENT)));

	RM_OWNER_LISTS_FOR(owner, m0_rm_ur_tlist_fini);
	owner->ro_resource = NULL;
	resource_put(res);

	M0_LEAVE();
}
M0_EXPORTED(m0_rm_owner_fini);

M0_INTERNAL void m0_rm_credit_init(struct m0_rm_credit *credit,
				   struct m0_rm_owner *owner)
{
	M0_ENTRY("credit: %p with owner: %p", credit, owner);
	M0_PRE(credit != NULL);
	M0_PRE(owner->ro_resource->r_ops != NULL);
	M0_PRE(owner->ro_resource->r_ops->rop_credit_init != NULL);

	credit->cr_datum = 0;
	m0_rm_ur_tlink_init(credit);
	pr_tlist_init(&credit->cr_pins);
	m0_rm_credit_bob_init(credit);
	credit->cr_owner = owner;
	owner->ro_resource->r_ops->rop_credit_init(owner->ro_resource, credit);

	M0_POST(credit->cr_ops != NULL);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_credit_init);

M0_INTERNAL void m0_rm_credit_fini(struct m0_rm_credit *credit)
{
	M0_ENTRY("credit: %p", credit);
	M0_PRE(credit != NULL);

	m0_rm_ur_tlink_fini(credit);
	pr_tlist_fini(&credit->cr_pins);
	m0_rm_credit_bob_fini(credit);
	credit->cr_ops->cro_free(credit);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_credit_fini);

static const struct m0_sm_state_descr inc_states[] = {
	[RI_INITIALISED] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Initialised",
		.sd_allowed   = M0_BITS(RI_CHECK, RI_FAILURE, RI_FINAL)
	},
	[RI_CHECK] = {
		.sd_name      = "Check",
		.sd_allowed   = M0_BITS(RI_SUCCESS, RI_FAILURE, RI_WAIT)
	},
	[RI_SUCCESS] = {
		.sd_name      = "Success",
		.sd_allowed   = M0_BITS(RI_RELEASED)
	},
	[RI_FAILURE] = {
		.sd_flags     = M0_SDF_FAILURE,
		.sd_name      = "Failure",
		.sd_allowed   = M0_BITS(RI_FINAL)
	},
	[RI_WAIT] = {
		.sd_name      = "Wait",
		.sd_allowed   = M0_BITS(RI_WAIT, RI_FAILURE, RI_CHECK)
	},
	[RI_RELEASED] = {
		.sd_name      = "Released",
		.sd_allowed   = M0_BITS(RI_FINAL)
	},
	[RI_FINAL] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "Final",
	}
};

static const struct m0_sm_conf inc_conf = {
	.scf_name      = "Incoming Request",
	.scf_nr_states = ARRAY_SIZE(inc_states),
	.scf_state     = inc_states
};

static inline void incoming_state_set(struct m0_rm_incoming *in,
				      enum m0_rm_incoming_state state)
{
	M0_PRE(owner_smgrp_is_locked(in->rin_want.cr_owner));
	M0_LOG(M0_INFO, "Incoming req: %p, state change:[%d -> %d]\n",
	       in, in->rin_sm.sm_state, state);
	m0_sm_state_set(&in->rin_sm, state);
}

M0_INTERNAL void m0_rm_incoming_init(struct m0_rm_incoming *in,
				     struct m0_rm_owner *owner,
				     enum m0_rm_incoming_type type,
				     enum m0_rm_incoming_policy policy,
				     uint64_t flags)
{
	M0_ENTRY("incoming: %p for owner: %p", in, owner);
	M0_PRE(in != NULL);

	M0_SET0(in);
	m0_sm_init(&in->rin_sm, &inc_conf, RI_INITIALISED,
		   owner_grp(owner), NULL);
	in->rin_type   = type;
	in->rin_policy = policy;
	in->rin_flags  = flags;
	pi_tlist_init(&in->rin_pins);
	m0_rm_credit_init(&in->rin_want, owner);
	m0_rm_incoming_bob_init(in);
	M0_POST(incoming_invariant(in));
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_incoming_init);

M0_INTERNAL void incoming_surrender(struct m0_rm_incoming *in)
{
	incoming_state_set(in, RI_RELEASED);
	m0_rm_ur_tlist_del(&in->rin_want);
	M0_ASSERT(pi_tlist_is_empty(&in->rin_pins));
}

M0_INTERNAL void internal_incoming_fini(struct m0_rm_incoming *in)
{
	M0_ENTRY();
	m0_sm_fini(&in->rin_sm);
	m0_rm_incoming_bob_fini(in);
	m0_rm_credit_fini(&in->rin_want);
	pi_tlist_fini(&in->rin_pins);
	M0_LEAVE();
}

M0_INTERNAL void m0_rm_incoming_fini(struct m0_rm_incoming *in)
{
	M0_ENTRY();
	M0_PRE(incoming_invariant(in));
	m0_rm_owner_lock(in->rin_want.cr_owner);
	M0_PRE(M0_IN(incoming_state(in),
		    (RI_INITIALISED, RI_FAILURE, RI_RELEASED)));
	incoming_state_set(in, RI_FINAL);
	internal_incoming_fini(in);
	m0_rm_owner_unlock(in->rin_want.cr_owner);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_incoming_fini);

/*
 * Impossible condition.
 */
static void windup_incoming_conflict(struct m0_rm_incoming *in)
{
	M0_IMPOSSIBLE("Conflict not possible during windup");
}

static void windup_incoming_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_ENTRY();
	incoming_release(in);
	incoming_surrender(in);
	M0_PRE(incoming_invariant(in));
	M0_PRE(M0_IN(incoming_state(in),
	       (RI_INITIALISED, RI_FAILURE, RI_RELEASED)));
	incoming_state_set(in, RI_FINAL);
	internal_incoming_fini(in);

	m0_free(in);
	M0_LEAVE();
}

M0_INTERNAL void m0_rm_outgoing_init(struct m0_rm_outgoing *out,
				     enum m0_rm_outgoing_type req_type)
{
	M0_ENTRY("outgoing: %p", out);
	M0_PRE(out != NULL);

	out->rog_rc = 0;
	out->rog_type = req_type;
	m0_rm_outgoing_bob_init(out);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_outgoing_init);

M0_INTERNAL void m0_rm_outgoing_fini(struct m0_rm_outgoing *out)
{
	M0_ENTRY("outgoing: %p", out);
	M0_PRE(out != NULL);
	m0_rm_outgoing_bob_fini(out);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_outgoing_fini);

static int loan_dup(const struct m0_rm_loan *src_loan,
		    struct m0_rm_loan **dest_loan)
{
	return m0_rm_loan_alloc(dest_loan, &src_loan->rl_credit,
				src_loan->rl_other);
}

M0_INTERNAL int m0_rm_loan_alloc(struct m0_rm_loan **loan,
				 const struct m0_rm_credit *credit,
				 struct m0_rm_remote *creditor)
{
	struct m0_rm_loan *new_loan;
	int		   rc = -ENOMEM;

	M0_ENTRY("loan credit: %lu creditor: %p", credit->cr_datum, creditor);
	M0_PRE(loan != NULL);
	M0_PRE(credit != NULL);

	M0_ALLOC_PTR(new_loan);
	if (new_loan != NULL) {
		rc = m0_rm_loan_init(new_loan, credit, creditor);
		if (rc != 0) {
			m0_free(new_loan);
			new_loan = NULL;
		}
	}

	*loan = new_loan;
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rm_loan_alloc);

/*
 * Allocates a new loan and calculates the difference between
 * loan->rl_credit and credit.
 */
static int remnant_loan_get(const struct m0_rm_loan *loan,
			    const struct m0_rm_credit *credit,
			    struct m0_rm_loan **remnant_loan)
{
	struct m0_rm_loan *new_loan;
	int		   rc;

	M0_ENTRY("split loan credit: %lu with credit: %lu",
		loan->rl_credit.cr_datum, credit->cr_datum);
	M0_PRE(remnant_loan != NULL);
	M0_PRE(loan != NULL);

	rc = loan_dup(loan, &new_loan) ?:
		credit_diff(&new_loan->rl_credit, credit);
	if (rc != 0 && new_loan != NULL) {
		m0_rm_loan_fini(new_loan);
		m0_free(new_loan);
		new_loan = NULL;
	}
	*remnant_loan = new_loan;
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rm_loan_init(struct m0_rm_loan *loan,
				const struct m0_rm_credit *credit,
				struct m0_rm_remote *creditor)
{
	M0_PRE(loan != NULL);
	M0_PRE(credit != NULL);

	M0_ENTRY("loan: %p", loan);
	loan->rl_id = 0;
	m0_cookie_new(&loan->rl_id);
	m0_rm_credit_init(&loan->rl_credit, credit->cr_owner);
	m0_rm_loan_bob_init(loan);
	loan->rl_other = creditor;
	if (loan->rl_other != NULL)
		resource_get(loan->rl_other->rem_resource);

	M0_LEAVE();
	return credit_copy(&loan->rl_credit, credit);
}
M0_EXPORTED(m0_rm_loan_init);

M0_INTERNAL void m0_rm_loan_fini(struct m0_rm_loan *loan)
{
	M0_PRE(loan != NULL);

	M0_ENTRY("loan: %p", loan);
	m0_rm_credit_fini(&loan->rl_credit);
	if (loan->rl_other != NULL)
		resource_put(loan->rl_other->rem_resource);
	loan->rl_other = NULL;
	loan->rl_id = 0;
	m0_rm_loan_bob_fini(loan);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_loan_fini);

static int remote_find(struct m0_rm_remote **rem,
		       struct m0_rpc_session *session,
		       struct m0_rm_resource *res,
		       struct m0_cookie *cookie)
{
	struct m0_rm_remote *other;
	int		     rc = 0;

	M0_PRE(rem != NULL);
	M0_PRE(res != NULL);
	M0_PRE(cookie != NULL);

	m0_tl_for(remotes, &res->r_remote, other) {
		M0_ASSERT(other->rem_resource == res &&
			  m0_rm_remote_bob_check(other));
		if (other->rem_cookie.co_addr == cookie->co_addr &&
		    other->rem_cookie.co_generation == cookie->co_generation)
			break;
	} m0_tl_endfor;

	if (other == NULL) {
		M0_ALLOC_PTR(other);
		if (other != NULL) {
			m0_rm_remote_init(other, res);
			other->rem_session = session;
			other->rem_state = REM_SERVICE_LOCATED;
			other->rem_cookie = *cookie;
			/* @todo - Figure this out */
			/* other->rem_id = 0; */
			remotes_tlist_add(&res->r_remote, other);
		} else
			rc = -ENOMEM;
	}
	*rem = other;
	return rc;
}

M0_INTERNAL void m0_rm_remote_init(struct m0_rm_remote *rem,
				   struct m0_rm_resource *res)
{
	M0_PRE(rem->rem_state == REM_FREED);

	M0_ENTRY("remote: %p", rem);
	rem->rem_state = REM_INITIALISED;
	rem->rem_resource = res;
	m0_chan_init(&rem->rem_signal, &res->r_type->rt_lock);
	remotes_tlink_init(rem);
	m0_rm_remote_bob_init(rem);
	resource_get(res);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_remote_init);

M0_INTERNAL void m0_rm_remote_fini(struct m0_rm_remote *rem)
{
	M0_ENTRY("remote: %p", rem);
	M0_PRE(rem != NULL);
	M0_PRE(M0_IN(rem->rem_state, (REM_INITIALISED,
				      REM_SERVICE_LOCATED,
				      REM_OWNER_LOCATED)));
	rem->rem_state = REM_FREED;
	m0_chan_fini_lock(&rem->rem_signal);
	remotes_tlink_fini(rem);
	resource_put(rem->rem_resource);
	m0_rm_remote_bob_fini(rem);
	M0_LEAVE();
}
M0_EXPORTED(m0_rm_remote_fini);

static void cached_credits_clear(struct m0_rm_owner *owner)
{
	struct m0_rm_credit *credit;
	int		     i;

	M0_ENTRY("owner: %p", owner);
	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		m0_tl_for(m0_rm_ur, &owner->ro_owned[i], credit) {
			m0_rm_ur_tlist_del(credit);
			m0_rm_credit_fini(credit);
			m0_free(credit);
		} m0_tl_endfor;
	}
	M0_LEAVE();
}

/*
 * Remove the OWOS_CACHED credits that match incoming credits. If the credit(s)
 * completely intersects the incoming credit, remove it(them) from the cache.
 * If the CACHED credit partly intersects with the incoming credit, retain the
 * difference in the CACHE.
 */
static int cached_credits_remove(struct m0_rm_incoming *in)
{
	struct m0_rm_pin    *pin;
	struct m0_rm_credit *credit;
	struct m0_rm_credit *remnant_credit;
	struct m0_rm_owner  *owner = in->rin_want.cr_owner;
	struct m0_tl	     diff_list;
	struct m0_tl	     remove_list;
	int		     rc = 0;

	M0_ENTRY("owner: %p credit: %lu", owner, in->rin_want.cr_datum);
	/* Credits can be removed for remote requests */
	M0_PRE(in->rin_type != M0_RIT_LOCAL);

	m0_rm_ur_tlist_init(&diff_list);
	m0_rm_ur_tlist_init(&remove_list);
	m0_tl_for(pi, &in->rin_pins, pin) {
		M0_ASSERT(m0_rm_pin_bob_check(pin));
		M0_ASSERT(pin->rp_flags == M0_RPF_PROTECT);
		credit = pin->rp_credit;

		pin_del(pin);
		m0_rm_ur_tlist_move(&remove_list, credit);
		if (!credit->cr_ops->cro_is_subset(credit, &in->rin_want)) {
			/*
			 * The cached credit does not completely intersect
			 * incoming credit.
			 *
			 * Make a copy of the cached credit and calculate
			 * the difference with incoming credit. Store
			 * the difference in the remnant credit.
			 */
			rc = remnant_credit_get(credit, &in->rin_want,
					       &remnant_credit);
			if (rc == 0)
				m0_rm_ur_tlist_add(&diff_list, remnant_credit);
		}

	} m0_tl_endfor;

	/*
	 * On successful completion, remove the credits from the "remove-list"
	 * and move the remnant credits to the OWOS_CACHED. Do the opposite
	 * on failure.
	 */
	m0_tl_for(m0_rm_ur, rc ? &diff_list : &remove_list, credit) {
		     m0_rm_ur_tlist_del(credit);
		     m0_rm_credit_fini(credit);
		     m0_free(credit);

	} m0_tl_endfor;

	m0_tl_for(m0_rm_ur, rc ? &remove_list : &diff_list, credit) {
	     m0_rm_ur_tlist_move(&owner->ro_owned[OWOS_CACHED], credit);
	} m0_tl_endfor;

	m0_rm_ur_tlist_fini(&diff_list);
	m0_rm_ur_tlist_fini(&remove_list);
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rm_borrow_commit(struct m0_rm_remote_incoming *rem_in)
{
	struct m0_rm_incoming *in    = &rem_in->ri_incoming;
	struct m0_rm_owner    *owner = in->rin_want.cr_owner;
	struct m0_rm_loan     *loan = NULL;
	struct m0_rm_remote   *debtor = NULL;
	int                    rc;

	M0_ENTRY("owner: %p credit: %lu", owner, in->rin_want.cr_datum);
	M0_PRE(in->rin_type == M0_RIT_BORROW);

	/*
	 * Allocate loan and copy the credit (to be borrowed).
	 * Clear the credits cache and remove incoming credits from the cache.
	 * If everything succeeds add loan to the sublet list.
	 */
	rc = remote_find(&debtor, rem_in->ri_rem_session,
			 owner->ro_resource, &rem_in->ri_rem_owner_cookie) ?:
	     m0_rm_loan_alloc(&loan, &in->rin_want, debtor);
	rc = rc ?: cached_credits_remove(in);
	if (rc == 0) {
		/*
		 * Store the loan in the sublet list.
		 */
		m0_rm_ur_tlist_add(&owner->ro_sublet, &loan->rl_credit);
		/*
		 * Store loan cookie llocally. Copy it into
		 * rem_in->ri_loan_cookie.
		 */
		m0_cookie_init(&loan->rl_cookie, &loan->rl_id);
		rem_in->ri_loan_cookie = loan->rl_cookie;
	} else if (loan != NULL) {
		m0_rm_loan_fini(loan);
		m0_free(loan);
	}
	M0_POST(owner_invariant(owner));
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rm_borrow_commit);

M0_INTERNAL int m0_rm_revoke_commit(struct m0_rm_remote_incoming *rem_in)
{
	struct m0_rm_incoming *in       = &rem_in->ri_incoming;
	struct m0_rm_owner    *owner    = in->rin_want.cr_owner;
	struct m0_rm_loan     *rvk_loan = NULL;
	struct m0_rm_loan     *remnant_loan;
	struct m0_rm_loan     *add_loan;
	struct m0_rm_loan     *remove_loan;
	struct m0_rm_credit   *credit;
	struct m0_cookie      *cookie;
	int                    rc         = 0;
	bool		       is_remnant = false;

	M0_ENTRY("owner: %p credit: %lu", owner, in->rin_want.cr_datum);
	M0_PRE(in->rin_type == M0_RIT_REVOKE);
	M0_PRE(!m0_rm_ur_tlist_is_empty(&owner->ro_borrowed));

	cookie = &rem_in->ri_loan_cookie;
	/*
	 * Clear the credits cache and remove incoming credits from the cache.
	 *
	 * Check the difference between the borrowed credits and the revoke
	 * credits. If the revoke fully intersects the previously borrowed
	 * credit, remove it from the list.
	 *
	 * If it's a partial revoke, credit_diff() will retain the remnant
	 * borrowed credit. cached_credits_remove() will leave
	 * remnant credit in the CACHE.
	 */
	/*
	 * Remove the loan from borrowed list.
	 */
	m0_tl_for(m0_rm_ur, &owner->ro_borrowed, credit) {
		rvk_loan = bob_of(credit, struct m0_rm_loan,
				  rl_credit, &loan_bob);
		if (rvk_loan->rl_cookie.co_addr == cookie->co_addr &&
		    rvk_loan->rl_cookie.co_generation ==
			cookie->co_generation) {
			m0_rm_ur_tlist_del(credit);
			break;
		}
	} m0_tl_endfor;

	M0_ASSERT(rvk_loan != NULL);

	/*
	 * Check if there is partial revoke.
	 */
	if (!rvk_loan->rl_credit.cr_ops->cro_is_subset(&rvk_loan->rl_credit,
	    &in->rin_want)) {
		rc = remnant_loan_get(rvk_loan, &in->rin_want, &remnant_loan);
		is_remnant = true;
	}
	/*
	 * Now remove the corresponding credit from the OWOS_CACHED list.
	 */
	rc = rc ?: cached_credits_remove(in);
	/*
	 * If there is a failure add the original loan back to borrowed list.
	 * On success, if there is remnant credit, add that to the borrowed list
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
		m0_rm_ur_tlist_add(&owner->ro_borrowed, &add_loan->rl_credit);

	if (remove_loan != NULL) {
		m0_rm_loan_fini(remove_loan);
		m0_free(remove_loan);
	}

	M0_POST(owner_invariant(owner));
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rm_revoke_commit);

/**
 * @name Owner state machine
 *
 * m0_rm_owner and m0_rm_incoming together form a state machine where basic
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
 *     - local user releases a pin on a credit (as a by-product of destroying an
 *       incoming request);
 *
 *     - completion of an outgoing request to another domain (including a
 *       timeout or a failure).
 *
 * Any event is processed in a uniform manner:
 *
 *     - m0_rm_owner::ro_sm_grp Group lock is taken;
 *
 *     - m0_rm_owner lists are updated to reflect the event, see details
 *       below. This temporarily violates the owner_invariant();
 *
 *     - owner_balance() is called to restore the invariant, this might create
 *       new imbalances and go through several iterations;
 *
 *     - m0_rm_owner::ro_sm_grp Group lock is released.
 *
 * Event handling is serialised by the owner lock. It is not legal to wait for
 * networking or IO events under this lock.
 *
 */
/** @{ */

/**
   External resource manager entry point: request a credit from the resource
   owner.
 */
M0_INTERNAL void m0_rm_credit_get(struct m0_rm_incoming *in)
{
	struct m0_rm_owner *owner = in->rin_want.cr_owner;

	M0_ENTRY("owner: %p credit: %lu", owner, in->rin_want.cr_datum);
	M0_PRE(IS_IN_ARRAY(in->rin_priority, owner->ro_incoming));
	M0_PRE(in->rin_sm.sm_rc == 0);
	M0_PRE(in->rin_rc == 0);
	M0_PRE(pi_tlist_is_empty(&in->rin_pins));

	m0_rm_owner_lock(owner);
	/*
	 * This check will make sure that new requests are added
	 * while owner is in ACTIVE state. This will take care
	 * of races between owner state transition and credit requests.
	 */
	if (owner_state(owner) == ROS_ACTIVE) {
		/*
		 * Mark incoming request "excited". owner_balance() will
		 * process it.
		 */
		m0_rm_ur_tlist_add(
			&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			&in->rin_want);
		owner_balance(owner);
	} else {
		M0_LOG(M0_DEBUG, "Incoming req: %p, state change:[%d -> %d]",
				 in, in->rin_sm.sm_state, RI_FAILURE);
		m0_sm_move(&in->rin_sm, -ENODEV, RI_FAILURE);
		in->rin_rc = -ENODEV;
	}

	m0_rm_owner_unlock(owner);
	M0_LEAVE();
}

M0_INTERNAL void m0_rm_credit_put(struct m0_rm_incoming *in)
{
	struct m0_rm_owner *owner = in->rin_want.cr_owner;

	M0_ENTRY("owner: %p credit: %lu", owner, in->rin_want.cr_datum);
	incoming_release(in);
	m0_rm_owner_lock(owner);
	incoming_surrender(in);
	/*
	 * Release of this credit may excite other waiting incoming-requests.
	 * Hence, call owner_balance() to process them.
	 */
	owner_balance(owner);
	m0_rm_owner_unlock(owner);
	M0_LEAVE();
}

/*
 * After successful completion of incoming request, move OWOS_CACHED credits
 * to OWOS_HELD credits.
 */
static int cached_credits_hold(struct m0_rm_incoming *in)
{
	enum m0_rm_owner_owned_state  ltype;
	struct m0_rm_pin	     *pin;
	struct m0_rm_owner	     *owner = in->rin_want.cr_owner;
	struct m0_rm_credit	     *credit;
	struct m0_rm_credit	     *held_credit;
	struct m0_rm_credit	      rest;
	struct m0_tl		      transfers;
	int			      rc;

	M0_ENTRY("owner: %p credit: %lu", owner, in->rin_want.cr_datum);
	/* Only local request can hold the credits */
	M0_PRE(in->rin_type == M0_RIT_LOCAL);

	m0_rm_credit_init(&rest, in->rin_want.cr_owner);
	rc = credit_copy(&rest, &in->rin_want);
	if (rc != 0)
		goto out;

	m0_rm_ur_tlist_init(&transfers);
	m0_tl_for(pi, &in->rin_pins, pin) {
		M0_ASSERT(pin->rp_flags == M0_RPF_PROTECT);
		credit = pin->rp_credit;
		M0_ASSERT(credit != NULL);
		M0_ASSERT(credit->cr_ops != NULL);
		M0_ASSERT(credit->cr_ops->cro_is_subset != NULL);
		M0_ASSERT(credit_intersects(&rest, credit));

		/* If the credit is already part of HELD list, skip it */
		if (credit_pin_nr(credit, M0_RPF_PROTECT) > 1) {
			rc = credit_diff(&rest, credit);
			if (rc != 0)
				break;
			else
				continue;
		}

		/*
		 * Check if the cached credit is a subset (including a
		 * proper subset) of incoming credit (request).
		 */
		if (credit->cr_ops->cro_is_subset(credit, &rest)) {
			/* Move the subset from CACHED list to HELD list */
			m0_rm_ur_tlist_move(&transfers, credit);
			rc = credit_diff(&rest, credit);
			if (rc != 0)
				break;
		} else {
			M0_ALLOC_PTR(held_credit);
			if (held_credit == NULL) {
				rc = -ENOMEM;
				break;
			}

			m0_rm_credit_init(held_credit, owner);
			/*
			 * If incoming credit partly intersects, then move
			 * intersection to the HELD list. Retain the difference
			 * in the CACHED list. This may lead to fragmentation of
			 * credits.
			 */
			rc = credit->cr_ops->cro_disjoin(credit, &rest,
							held_credit);
			if (rc != 0) {
				m0_rm_credit_fini(held_credit);
				m0_free(held_credit);
				break;
			}
			m0_rm_ur_tlist_add(&transfers, held_credit);
			rc = credit_diff(&rest, held_credit);
			if (rc != 0)
				break;
			pin_add(in, held_credit, M0_RPF_PROTECT);
			pin_del(pin);
		}

	} m0_tl_endfor;

	M0_POST(ergo(rc == 0, credit_is_empty(&rest)));
	/*
	 * Only cached credits are part of transfer list.
	 * On success, move the credits to OWOS_HELD list. Otherwise move
	 * them back OWOS_CACHED list.
	 */
	ltype = rc ? OWOS_CACHED : OWOS_HELD;
	m0_tl_for(m0_rm_ur, &transfers, credit) {
	     m0_rm_ur_tlist_move(&owner->ro_owned[ltype], credit);
	} m0_tl_endfor;

	m0_rm_ur_tlist_fini(&transfers);

out:
	m0_rm_credit_fini(&rest);
	M0_RETURN(rc);
}

/**
 * Main owner state machine function.
 *
 * Goes through the lists of excited incoming and outgoing requests until all
 * the excitement is gone.
 */
static void owner_balance(struct m0_rm_owner *o)
{
	struct m0_rm_pin      *pin;
	struct m0_rm_credit   *credit;
	struct m0_rm_outgoing *out;
	struct m0_rm_incoming *in;
	bool                   todo;
	int                    prio;

	M0_ENTRY();
	do {
		todo = false;
		m0_tl_for(m0_rm_ur, &o->ro_outgoing[OQS_EXCITED], credit) {
			M0_ASSERT(m0_rm_credit_bob_check(credit));
			todo = true;
			out = bob_of(credit, struct m0_rm_outgoing,
				     rog_want.rl_credit, &outgoing_bob);
			/*
			 * Outgoing request completes: remove all pins stuck in
			 * and finalise them. Also pass the processing error, if
			 * any, to the corresponding incoming structure(s).
			 *
			 * Removing of pins might excite incoming requests
			 * waiting for outgoing request completion.
			 */
			m0_tl_for(pr, &credit->cr_pins, pin) {
				M0_ASSERT(m0_rm_pin_bob_check(pin));
				M0_ASSERT(pin->rp_flags == M0_RPF_TRACK);
				/*
				 * If one outgoing request has set an error,
				 * then don't overwrite the error code. It's
				 * possible that an error code could be
				 * reset to 0 if other requests succeed.
				 */
				pin->rp_incoming->rin_rc =
					pin->rp_incoming->rin_rc ?: out->rog_rc;
				pin_del(pin);
			} m0_tl_endfor;
			m0_rm_ur_tlink_del_fini(credit);
		} m0_tl_endfor;
		for (prio = ARRAY_SIZE(o->ro_incoming) - 1; prio >= 0; --prio) {
			m0_tl_for(m0_rm_ur,
				  &o->ro_incoming[prio][OQS_EXCITED], credit) {
				todo = true;
				in = bob_of(credit, struct m0_rm_incoming,
					    rin_want, &incoming_bob);
				/*
				 * All waits completed, go to CHECK
				 * state.
				 */
				m0_rm_ur_tlist_move(
					&o->ro_incoming[prio][OQS_GROUND],
					&in->rin_want);
				incoming_state_set(in, RI_CHECK);
				incoming_check(in);
			} m0_tl_endfor;
		}
	} while (todo);
	/*
	 * Check if owner needs to be finalised.
	 */
	owner_finalisation_check(o);
	M0_LEAVE();
}

/**
 * Takes an incoming request in RI_CHECK state and attempts to perform a
 * non-blocking state transition.
 *
 * This function leaves the request either in RI_WAIT, RI_SUCCESS or RI_FAILURE
 * state.
 */
static void incoming_check(struct m0_rm_incoming *in)
{
	struct m0_rm_credit rest;
	int		    rc;

	M0_ENTRY();
	M0_PRE(m0_rm_incoming_bob_check(in));
	/*
	 * This function is reentrant. An outgoing request might set
	 * the processing error for the incoming structure. Check for the
	 * error. If there is an error, there is no need to continue the
	 * processing.
	 */
	if (in->rin_rc == 0) {
		m0_rm_credit_init(&rest, in->rin_want.cr_owner);
		rc = credit_copy(&rest, &in->rin_want) ?:
		     incoming_check_with(in, &rest);
		m0_rm_credit_fini(&rest);
	} else
		rc = in->rin_rc;

	if (rc > 0)
		incoming_state_set(in, RI_WAIT);
	else {
		if (rc == 0) {
			M0_ASSERT(incoming_pin_nr(in, M0_RPF_TRACK) == 0);
			incoming_policy_apply(in);
			/*
			 * Transfer the CACHED credits to HELD list. Later
			 * it may be subsumed by policy functions (or
			 * vice versa). Credits are held only for local
			 * request. For remote requests, they are removed
			 * and converted into loans.
			 */
			if (in->rin_type == M0_RIT_LOCAL)
				rc = cached_credits_hold(in);
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
	M0_LEAVE();
}

/*
 * Checks if there are outstanding "outgoing requests" for this incoming
 * requests.
 */
static bool incoming_is_complete(struct m0_rm_incoming *in)
{
	return incoming_pin_nr(in, M0_RPF_TRACK) == 0;
}

/**
 * Main helper function to incoming_check(), which starts with "rest" set to the
 * wanted credit and goes though the sequence of checks, reducing "rest".
 *
 * CHECK logic can be described by means of "wait conditions". A wait condition
 * is something that prevents immediate fulfillment of the request.
 *
 *     - A request with RIF_LOCAL_WAIT bit set can be fulfilled iff the credits
 *       on ->ro_owned[OWOS_CACHED] list together imply the wanted credit;
 *
 *     - a request without RIF_LOCAL_WAIT bit can be fulfilled iff the credits
 *       on all ->ro_owned[] lists together imply the wanted credit.
 *
 * If there is not enough credits on ->ro_owned[] lists, an incoming request has
 * to wait until some additional credits are borrowed from the upward creditor
 * or revoked from downward debtors.
 *
 * A RIF_LOCAL_WAIT request, in addition, can wait until a credit moves from
 * ->ro_owned[OWOS_HELD] to ->ro_owned[OWOS_CACHED].
 *
 * This function performs no state transitions by itself. Instead its return
 * value indicates the target state:
 *
 *     - 0: the request is fulfilled, the target state is RI_SUCCESS,
 *     - +ve: more waiting is needed, the target state is RI_WAIT,
 *     - -ve: error, the target state is RI_FAILURE.
 */
static int incoming_check_with(struct m0_rm_incoming *in,
			       struct m0_rm_credit *rest)
{
	struct m0_rm_credit *want = &in->rin_want;
	struct m0_rm_owner  *o    = want->cr_owner;
	struct m0_rm_credit *r;
	struct m0_rm_loan   *loan;
	int                  i;
	int                  wait = 0;
	int		     rc   = 0;

	M0_ENTRY("incoming: %p credit: %lu", in, rest->cr_datum);
	M0_PRE(m0_rm_ur_tlist_contains(
		       &o->ro_incoming[in->rin_priority][OQS_GROUND], want));

	/*
	 * 1. Scan owned lists first. Check for "local" wait/try conditions.
	 */
	for (i = 0; i < ARRAY_SIZE(o->ro_owned); ++i) {
		m0_tl_for(m0_rm_ur, &o->ro_owned[i], r) {
			M0_ASSERT(m0_rm_credit_bob_check(r));
			if (!credit_intersects(r, want))
				continue;
			if (i == OWOS_HELD && credit_conflicts(r, want)) {
				if (in->rin_flags & RIF_LOCAL_WAIT) {
					rc = pin_add(in, r, M0_RPF_TRACK);
					if (rc == 0)
						in->rin_ops->rio_conflict(in);
					wait++;
				} else if (in->rin_flags & RIF_LOCAL_TRY)
					return -EBUSY;
			} else if (wait == 0)
				rc = pin_add(in, r, M0_RPF_PROTECT);
			rc = rc ?: credit_diff(rest, r);
			if (rc != 0)
				M0_RETURN(rc);
		} m0_tl_endfor;
	}

	/*
	 * 2. If the credit request cannot still be satisfied, check against the
	 *    sublet list.
	 */
	if (!credit_is_empty(rest)) {
		m0_tl_for(m0_rm_ur, &o->ro_sublet, r) {
			M0_ASSERT(m0_rm_credit_bob_check(r));
			if (!credit_intersects(r, rest))
				continue;
			if (!(in->rin_flags & RIF_MAY_REVOKE))
				return -EREMOTE;
			loan = bob_of(r, struct m0_rm_loan, rl_credit,
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
			/*
			 * @todo - Revoke entire loan?? rest could be subset
			 * of r
			 */
			rc = revoke_send(in, loan, r) ?: credit_diff(rest, r);
			if (rc != 0)
				M0_RETURN(rc);
		} m0_tl_endfor;
	}

	/*
	 * 3. If the credit request still cannot be satisfied, check
	 *    if it's possible borrow remaining credit from the creditor.
	 */
	if (!credit_is_empty(rest)) {
		if (o->ro_creditor != NULL) {
			if (!(in->rin_flags & RIF_MAY_BORROW))
				M0_RETURN(-EREMOTE);
			wait++;
			rc = borrow_send(in, rest);
		} else
			rc = -ESRCH;
	}

	M0_RETURN(rc ?: wait);
}

/**
 * Called when an outgoing request completes (possibly with an error, like a
 * timeout).
 */
M0_INTERNAL void m0_rm_outgoing_complete(struct m0_rm_outgoing *og)
{
	struct m0_rm_owner *owner;

	M0_ENTRY();
	M0_PRE(og != NULL);

	owner = og->rog_want.rl_credit.cr_owner;
	m0_rm_ur_tlist_move(&owner->ro_outgoing[OQS_EXCITED],
			    &og->rog_want.rl_credit);
	owner_balance(owner);
	M0_LEAVE();
}

/**
 * Helper function called when an incoming request processing completes.
 *
 * Sets m0_rm_incoming::rin_sm.sm_rc, updates request state, invokes completion
 * call-back, broadcasts request channel and releases request pins.
 */
static void incoming_complete(struct m0_rm_incoming *in, int32_t rc)
{
	struct m0_rm_owner *owner = in->rin_want.cr_owner;

	M0_ENTRY("incoming: %p error: [%d]", in, rc);
	M0_PRE(in->rin_ops != NULL);
	M0_PRE(in->rin_ops->rio_complete != NULL);
	M0_PRE(rc <= 0);

	in->rin_rc = rc;
	M0_LOG(M0_DEBUG, "Incoming req: %p, state change:[%d -> %d]\n",
			 in, in->rin_sm.sm_state,
			 rc == 0 ? RI_SUCCESS : RI_FAILURE);
	m0_sm_move(&in->rin_sm, rc, rc == 0 ? RI_SUCCESS : RI_FAILURE);
	/*
	 * incoming_release() might have moved the request into excited
	 * state when the last tracking pin was removed, shun it back
	 * into obscurity.
	 */
	m0_rm_ur_tlist_move(&owner->ro_incoming[in->rin_priority][OQS_GROUND],
			    &in->rin_want);
	if (rc != 0) {
		incoming_release(in);
		m0_rm_ur_tlist_del(&in->rin_want);
		M0_ASSERT(pi_tlist_is_empty(&in->rin_pins));
	}
	in->rin_ops->rio_complete(in, rc);
	M0_POST(owner_invariant(owner));
	M0_LEAVE();
}

static void incoming_policy_none(struct m0_rm_incoming *in)
{
}

static void incoming_policy_apply(struct m0_rm_incoming *in)
{
	static void (*generic[RIP_NR])(struct m0_rm_incoming *) = {
		[RIP_NONE]    = &incoming_policy_none,
		[RIP_INPLACE] = &incoming_policy_none,
		[RIP_STRICT]  = &incoming_policy_none,
		[RIP_JOIN]    = &incoming_policy_none,
		[RIP_MAX]     = &incoming_policy_none
	};

	if (IS_IN_ARRAY(in->rin_policy, generic))
		generic[in->rin_policy](in);
	else {
		struct m0_rm_resource *resource;

		resource = in->rin_want.cr_owner->ro_resource;
		resource->r_ops->rop_policy(resource, in);
	}
}

/**
 * Check if the outgoing request for requested credit is already pending.
 * If yes, attach a tracking pin.
 */
static int outgoing_check(struct m0_rm_incoming *in,
			  enum m0_rm_outgoing_type otype,
			  struct m0_rm_credit *credit,
			  struct m0_rm_remote *other)
{
	int		       i;
	int		       rc = 0;
	struct m0_rm_owner    *owner = in->rin_want.cr_owner;
	struct m0_rm_credit   *scan;
	struct m0_rm_outgoing *out;

	M0_ENTRY();
	for (i = 0; i < ARRAY_SIZE(owner->ro_outgoing); ++i) {
		m0_tl_for(m0_rm_ur, &owner->ro_outgoing[i], scan) {
			M0_ASSERT(m0_rm_credit_bob_check(scan));
			out = bob_of(scan, struct m0_rm_outgoing,
				     rog_want.rl_credit, &outgoing_bob);
			if (out->rog_type == otype &&
			    credit_intersects(scan, credit)) {
				M0_ASSERT(out->rog_want.rl_other == other);
				/**
				 * @todo adjust outgoing requests priority
				 * (priority inheritance)
				 */
				rc = pin_add(in, scan, M0_RPF_TRACK) ?:
				     credit_diff(credit, scan);
				if (rc != 0)
					break;
			}
		} m0_tl_endfor;

		if (rc != 0)
			break;
	}
	M0_RETURN(rc);
}

/**
 * Sends an outgoing revoke request to remote owner specified by the "loan". The
 * request will revoke the credit "credit", which might be a part of original
 * loan.
 */
static int revoke_send(struct m0_rm_incoming *in,
		       struct m0_rm_loan *loan, struct m0_rm_credit *credit)
{
	int rc;

	M0_ENTRY("incoming: %p credit: %lu", in, credit->cr_datum);
	rc = outgoing_check(in, M0_ROT_REVOKE, credit, loan->rl_other);
	if (!credit_is_empty(credit) && rc == 0)
		rc = m0_rm_request_out(M0_ROT_REVOKE, in, loan, credit);
	M0_RETURN(rc);
}

/**
 * Sends an outgoing borrow request to the upward creditor. The request will
 * borrow the credit "credit".
 */
static int borrow_send(struct m0_rm_incoming *in, struct m0_rm_credit *credit)
{
	int rc;

	M0_ENTRY("incoming: %p credit: %lu", in, credit->cr_datum);
	M0_PRE(in->rin_want.cr_owner->ro_creditor != NULL);

	rc = outgoing_check(in, M0_ROT_BORROW, credit,
				in->rin_want.cr_owner->ro_creditor);
	if (!credit_is_empty(credit) && rc == 0)
		rc = m0_rm_request_out(M0_ROT_BORROW, in, NULL, credit);
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rm_sublet_remove(struct m0_rm_credit *credit)
{
	struct m0_rm_owner  *owner = credit->cr_owner;
	struct m0_rm_credit *sublet;
	struct m0_rm_loan   *loan;
	struct m0_rm_loan   *remnant_loan;
	struct m0_tl	     diff_list;
	struct m0_tl	     remove_list;
	int		     rc = 0;

	M0_ENTRY("credit: %lu", credit->cr_datum);
	M0_PRE(credit != NULL);

	m0_rm_ur_tlist_init(&diff_list);
	m0_rm_ur_tlist_init(&remove_list);
	m0_tl_for(m0_rm_ur, &owner->ro_sublet, sublet) {
		m0_rm_ur_tlist_move(&remove_list, sublet);
		if (!credit->cr_ops->cro_is_subset(sublet, credit)) {
			loan = bob_of(sublet, struct m0_rm_loan, rl_credit,
				      &loan_bob);
			/* Get diff(loan->rl_credit, credit) */
			rc = remnant_loan_get(loan, credit, &remnant_loan);
			if (rc == 0)
				m0_rm_ur_tlist_add(&diff_list,
						   &remnant_loan->rl_credit);
		}
	} m0_tl_endfor;
	/*
	 * On successful completion, remove the credits from the "remove-list"
	 * and move the remnant credits to the OWOS_CACHED. Do the opposite
	 * on failure.
	 */
	m0_tl_for(m0_rm_ur, rc ? &diff_list : &remove_list, credit) {
		m0_rm_ur_tlist_del(credit);
		loan = bob_of(credit, struct m0_rm_loan, rl_credit, &loan_bob);
		m0_rm_loan_fini(loan);
		m0_free(loan);

	} m0_tl_endfor;

	m0_tl_for(m0_rm_ur, rc ? &remove_list : &diff_list, credit) {
	     m0_rm_ur_tlist_move(&owner->ro_sublet, credit);
	} m0_tl_endfor;

	m0_rm_ur_tlist_fini(&diff_list);
	m0_rm_ur_tlist_fini(&remove_list);
	M0_RETURN(rc);
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
 *  m0_rm_resource_type::rt_resources.
 */
static bool resource_list_check(const struct m0_rm_resource *res, void *datum)
{
	const struct m0_rm_resource_type *rt = datum;

	return resource_find(rt, res) == res && res->r_type == rt;
}

static bool resource_type_invariant(const struct m0_rm_resource_type *rt)
{
	struct m0_rm_domain *dom   = rt->rt_dom;
	const struct m0_tl  *rlist = &rt->rt_resources;

	return
		res_tlist_invariant_ext(rlist, resource_list_check,
					(void *)rt) &&
		rt->rt_nr_resources == res_tlist_length(rlist) &&
		dom != NULL && IS_IN_ARRAY(rt->rt_id, dom->rd_types) &&
		dom->rd_types[rt->rt_id] == rt;
}

/**
 * Invariant for m0_rm_incoming.
 */
static bool incoming_invariant(const struct m0_rm_incoming *in)
{
	return
		(in->rin_rc != 0) == (incoming_state(in) == RI_FAILURE) &&
		!(in->rin_flags & ~(RIF_MAY_REVOKE|RIF_MAY_BORROW|
				    RIF_LOCAL_WAIT|RIF_LOCAL_TRY)) &&
		IS_IN_ARRAY(in->rin_priority,
			    in->rin_want.cr_owner->ro_incoming) &&
		/* a request can be in "check" state only during owner_balance()
		   execution. */
		incoming_state(in) != RI_CHECK &&
		pi_tlist_invariant(&in->rin_pins) &&
		/* a request in the WAIT state... */
		ergo(incoming_state(in) == RI_WAIT,
		     /* waits on something... */
		     incoming_pin_nr(in, M0_RPF_TRACK) > 0 &&
		     /* and doesn't hold anything. */
		     incoming_pin_nr(in, M0_RPF_PROTECT) == 0) &&
		/* a fulfilled request... */
		ergo(incoming_state(in) == RI_SUCCESS,
		     /* holds something... */
		     incoming_pin_nr(in, M0_RPF_PROTECT) > 0 &&
		     /* and waits on nothing. */
		     incoming_pin_nr(in, M0_RPF_TRACK) == 0) &&
		ergo(incoming_state(in) == RI_FAILURE ||
		     incoming_state(in) == RI_INITIALISED,
		     incoming_pin_nr(in, ~0) == 0) &&
		pr_tlist_is_empty(&in->rin_want.cr_pins);
}

enum credit_queue {
	OIS_BORROWED = 0,
	OIS_SUBLET,
	OIS_OUTGOING,
	OIS_OWNED,
	OIS_INCOMING,
	OIS_NR
};

struct owner_invariant_state {
	enum credit_queue    is_phase;
	int                  is_owned_idx;
	struct m0_rm_credit  is_debit;
	struct m0_rm_credit  is_credit;
	struct m0_rm_owner  *is_owner;
};

static bool credit_invariant(const struct m0_rm_credit *credit, void *data)
{
	struct owner_invariant_state *is =
		(struct owner_invariant_state *) data;
	return
		/* only held credits have PROTECT pins */
		ergo((is->is_phase == OIS_OWNED &&
		     is->is_owned_idx == OWOS_HELD),
		     credit_pin_nr(credit, M0_RPF_PROTECT) > 0) &&
		ergo(is->is_phase == OIS_INCOMING,
		     incoming_invariant(container_of(credit,
						     struct m0_rm_incoming,
						     rin_want)));
}

/**
 * Checks internal consistency of a resource owner.
 */
static bool owner_invariant_state(const struct m0_rm_owner *owner,
				  struct owner_invariant_state *is)
{
	struct m0_rm_credit *credit;
	int		     i;
	int		     j;

	/*
	 * Iterate over all credits lists:
	 *
	 *    - checking their consistency as double-linked lists
         *      (m0_rm_ur_tlist_invariant_ext());
	 *
	 *    - making additional consistency checks:
	 *
	 *    - that a credit is for the same resource as the owner,
	 *
	 *    - that a credit on m0_rm_owner::ro_owned[X] is pinned iff X
         *            == OWOS_HELD.
	 *
	 *    - accumulating total credit and debit.
	 */
	is->is_phase = OIS_BORROWED;
	if (!m0_rm_ur_tlist_invariant_ext(&owner->ro_borrowed,
					  &credit_invariant, (void *)is))
		return false;
	is->is_phase = OIS_SUBLET;
	if (!m0_rm_ur_tlist_invariant_ext(&owner->ro_sublet,
					  &credit_invariant, (void *)is))
		return false;
	is->is_phase = OIS_OUTGOING;
	if (!m0_rm_ur_tlist_invariant_ext(&owner->ro_outgoing[0],
					  &credit_invariant, (void *)is))
		return false;

	is->is_phase = OIS_OWNED;
	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		is->is_owned_idx = i;
		if (!m0_rm_ur_tlist_invariant_ext(&owner->ro_owned[i],
					   &credit_invariant, (void *)is))
		    return false;
	}
	is->is_phase = OIS_INCOMING;
	for (i = 0; i < ARRAY_SIZE(owner->ro_incoming); ++i) {
		for (j = 0; j < ARRAY_SIZE(owner->ro_incoming[i]); ++j) {
			if (!m0_rm_ur_tlist_invariant(
				    &owner->ro_incoming[i][j]))
				return false;
		}
	}

	/* Calculate credit */
	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); ++i) {
		m0_tl_for(m0_rm_ur, &owner->ro_owned[i], credit) {
			if(credit->cr_ops->cro_join(&is->is_credit, credit))
				return false;
		} m0_tl_endfor;
	}
	m0_tl_for(m0_rm_ur, &owner->ro_sublet, credit) {
		if(credit->cr_ops->cro_join(&is->is_credit, credit))
			return false;
	} m0_tl_endfor;

	/* Calculate debit */
	m0_tl_for(m0_rm_ur, &owner->ro_borrowed, credit) {
		if(credit->cr_ops->cro_join(&is->is_debit, credit))
			return false;
	} m0_tl_endfor;

	return true;
}

/**
 * Checks internal consistency of a resource owner.
 */
static bool owner_invariant(struct m0_rm_owner *owner)
{
	bool                         rc;
	struct owner_invariant_state is;

	M0_SET0(&is);

	m0_rm_credit_init(&is.is_debit, owner);
	m0_rm_credit_init(&is.is_credit, owner);

	rc = owner_invariant_state(owner, &is) &&
		 credit_eq(&is.is_debit, &is.is_credit);

	m0_rm_credit_fini(&is.is_debit);
	m0_rm_credit_fini(&is.is_credit);
	return rc;
}

/** @} end of invariant group */

/**
   @name pin Pin helpers

  @{
 */

/**
 * Number of pins with a given flag combination, stuck in a given credit.
 */
static int credit_pin_nr(const struct m0_rm_credit *credit, uint32_t flags)
{
	int		  nr = 0;
	struct m0_rm_pin *pin;

	m0_tl_for(pr, &credit->cr_pins, pin) {
		M0_ASSERT(m0_rm_pin_bob_check(pin));
		if (pin->rp_flags & flags)
			++nr;
	} m0_tl_endfor;
	return nr;
}

/**
 * Number of pins with a given flag combination, issued by a given incoming
 * request.
 */
static int incoming_pin_nr(const struct m0_rm_incoming *in, uint32_t flags)
{
	int               nr;
	struct m0_rm_pin *pin;

	nr = 0;
	m0_tl_for(pi, &in->rin_pins, pin) {
		M0_ASSERT(m0_rm_pin_bob_check(pin));
		if (pin->rp_flags & flags)
			++nr;
	} m0_tl_endfor;
	return nr;
}

/**
 * Releases credits pinned by an incoming request, waking up other pending
 * incoming requests if necessary.
 */
static void incoming_release(struct m0_rm_incoming *in)
{
	struct m0_rm_pin    *kingpin;
	struct m0_rm_pin    *pin;
	struct m0_rm_credit *credit;
	struct m0_rm_owner  *o = in->rin_want.cr_owner;

	M0_ENTRY("incoming: %p", in);
	m0_tl_for(pi, &in->rin_pins, kingpin) {
		M0_ASSERT(m0_rm_pin_bob_check(kingpin));
		if (kingpin->rp_flags & M0_RPF_PROTECT) {
			credit = kingpin->rp_credit;
			/*
			 * If this was the last protecting pin, wake up incoming
			 * requests waiting on this credit release.
			 */
			if (credit_pin_nr(credit, M0_RPF_PROTECT) == 1) {
				/*
				 * Move the credit back to the CACHED list.
				 */
				m0_rm_ur_tlist_move(&o->ro_owned[OWOS_CACHED],
						    credit);
				/*
				 * I think we are introducing "thundering herd"
				 * problem here.
				 */
				m0_tl_for(pr, &credit->cr_pins, pin) {
					M0_ASSERT(m0_rm_pin_bob_check(pin));
					if (pin->rp_flags & M0_RPF_TRACK)
						pin_del(pin);
				} m0_tl_endfor;
			}
		}
		pin_del(kingpin);
	} m0_tl_endfor;
	M0_LEAVE();
}

/**
 * Removes a pin on a resource usage credit.
 *
 * If this was a last tracking pin issued by the request---excite the latter.
 * The function returns true if it excited an incoming request.
 */
static void pin_del(struct m0_rm_pin *pin)
{
	struct m0_rm_incoming *in;
	struct m0_rm_owner    *owner;

	M0_ENTRY();
	M0_ASSERT(pin != NULL);

	in = pin->rp_incoming;
	owner = in->rin_want.cr_owner;
	pi_tlink_del_fini(pin);
	pr_tlink_del_fini(pin);
	m0_rm_pin_bob_fini(pin);
	if (incoming_pin_nr(in, M0_RPF_TRACK) == 0 &&
	    pin->rp_flags & M0_RPF_TRACK) {
		/*
		 * Last tracking pin removed, excite the request.
		 */
		M0_LOG(M0_INFO, "Exciting incoming: %p\n", in);
		m0_rm_ur_tlist_move(
			&owner->ro_incoming[in->rin_priority][OQS_EXCITED],
			&in->rin_want);
	}
	m0_free(pin);
	M0_LEAVE();
}

/**
 * Sticks a tracking pin on credit. When credit is released, the all incoming
 * requests that stuck pins into it are notified.
 */
int pin_add(struct m0_rm_incoming *in,
	    struct m0_rm_credit *credit,
	    uint32_t flags)
{
	struct m0_rm_pin *pin;

	M0_ENTRY();
	/*
	 * In some cases, an incoming may scan owner lists multiple times.
	 * It may end up adding mutiple pins for the same credit. Hence, check
	 * before adding the pin.
	 */
	m0_tl_for(pi, &in->rin_pins, pin) {
		M0_ASSERT(pin->rp_incoming == in);
		if (pin->rp_credit == credit) {
			M0_LOG(M0_DEBUG, "pins exists for credit: %p\n",
			       credit);
			M0_RETURN(0);
		}
	} m0_tl_endfor;

	M0_ALLOC_PTR(pin);
	if (pin != NULL) {
		pin->rp_flags = flags;
		pin->rp_credit = credit;
		pin->rp_incoming = in;
		pr_tlink_init(pin);
		pi_tlink_init(pin);
		pr_tlist_add(&credit->cr_pins, pin);
		pi_tlist_add(&in->rin_pins, pin);
		m0_rm_pin_bob_init(pin);
		M0_RETURN(0);
	} else
		M0_RETURN(-ENOMEM);
}

/** @} end of pin group */

/**
 *  @name credit Credit helpers
 *
 * @{
 */

static bool credit_intersects(const struct m0_rm_credit *A,
			      const struct m0_rm_credit *B)
{
	M0_PRE(A->cr_ops != NULL);
	M0_PRE(A->cr_ops->cro_intersects != NULL);

	return A->cr_ops->cro_intersects(A, B);
}

static bool credit_conflicts(const struct m0_rm_credit *A,
			    const struct m0_rm_credit *B)
{
	M0_PRE(A->cr_ops != NULL);
	M0_PRE(A->cr_ops->cro_conflicts != NULL);

	return A->cr_ops->cro_conflicts(A, B);
}


static int credit_diff(struct m0_rm_credit *c0, const struct m0_rm_credit *c1)
{
	M0_PRE(c0->cr_ops != NULL);
	M0_PRE(c0->cr_ops->cro_diff != NULL);

	return c0->cr_ops->cro_diff(c0, c1);
}

static bool credit_eq(const struct m0_rm_credit *c0,
		      const struct m0_rm_credit *c1)
{
	int                 rc;
	bool                res;
	struct m0_rm_credit credit;

	/* no apples and oranges comparison. */
	M0_PRE(c0->cr_owner == c1->cr_owner);
	m0_rm_credit_init(&credit, c0->cr_owner);
	rc = credit_copy(&credit, c0);
	rc = rc ?: credit_diff(&credit, c1);

	res = rc ? false : credit_is_empty(&credit);
	m0_rm_credit_fini(&credit);

	return res;
}

/*
 * Allocates a new credit and calculates the difference between src and diff.
 * Stores the diff(src, diff) in the newly allocated credit.
 */
static int remnant_credit_get(const struct m0_rm_credit *src,
			     const struct m0_rm_credit *diff,
			     struct m0_rm_credit **remnant_credit)
{
	struct m0_rm_credit *new_credit;
	int		     rc;

	M0_ENTRY("splitting credits %lu and %lu", src->cr_datum,
						  diff->cr_datum);
	M0_PRE(remnant_credit != NULL);
	M0_PRE(src != NULL);
	M0_PRE(diff != NULL);

	rc = m0_rm_credit_dup(src, &new_credit) ?:
		credit_diff(new_credit, diff);
	if (rc != 0 && new_credit != NULL) {
		m0_rm_credit_fini(new_credit);
		m0_free(new_credit);
		new_credit = NULL;
	}
	*remnant_credit = new_credit;
	M0_RETURN(rc);
}

/**
 * Allocates memory and makes another copy of credit struct.
 */
M0_INTERNAL int m0_rm_credit_dup(const struct m0_rm_credit *src_credit,
				 struct m0_rm_credit **dest_credit)
{
	struct m0_rm_credit *credit;
	int		     rc = -ENOMEM;

	M0_ENTRY();
	M0_PRE(src_credit != NULL);

	M0_ALLOC_PTR(credit);
	if (credit != NULL) {
		m0_rm_credit_init(credit, src_credit->cr_owner);
		credit->cr_ops = src_credit->cr_ops;
		rc = credit_copy(credit, src_credit);
		if (rc != 0) {
			m0_rm_credit_fini(credit);
			m0_free(credit);
			credit = NULL;
		}
	}
	*dest_credit = credit;
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rm_credit_dup);

/**
 * Makes another copy of credit struct.
 */
static int credit_copy(struct m0_rm_credit *dst, const struct m0_rm_credit *src)
{
	M0_PRE(src != NULL);
	M0_PRE(dst->cr_datum == 0);

	return src->cr_ops->cro_copy(dst, src);
}

/**
 * Returns true when cr_datum is 0, else returns false.
 */
static bool credit_is_empty(const struct m0_rm_credit *credit)
{
	return credit->cr_datum == 0;
}

M0_INTERNAL int m0_rm_credit_encode(const struct m0_rm_credit *credit,
				   struct m0_buf *buf)
{
	struct m0_bufvec	datum_buf;
	struct m0_bufvec_cursor cursor;

	M0_ENTRY("credit: %lu", credit->cr_datum);
	M0_PRE(buf != NULL);
	M0_PRE(credit->cr_ops != NULL);
	M0_PRE(credit->cr_ops->cro_len != NULL);
	M0_PRE(credit->cr_ops->cro_encode != NULL);

	buf->b_nob = credit->cr_ops->cro_len(credit);
	buf->b_addr = m0_alloc(buf->b_nob);
	if (buf->b_addr == NULL)
		return -ENOMEM;

	datum_buf.ov_buf = &buf->b_addr;
	datum_buf.ov_vec.v_nr = 1;
	datum_buf.ov_vec.v_count = &buf->b_nob;

	m0_bufvec_cursor_init(&cursor, &datum_buf);
	M0_RETURN(credit->cr_ops->cro_encode(credit, &cursor));
}
M0_EXPORTED(m0_rm_credit_encode);

M0_INTERNAL int m0_rm_credit_decode(struct m0_rm_credit *credit,
				    struct m0_buf *buf)
{
	struct m0_bufvec	datum_buf = M0_BUFVEC_INIT_BUF(&buf->b_addr,
							       &buf->b_nob);
	struct m0_bufvec_cursor cursor;

	M0_ENTRY("credit: %lu", credit->cr_datum);
	M0_PRE(credit->cr_ops != NULL);
	M0_PRE(credit->cr_ops->cro_decode != NULL);

	m0_bufvec_cursor_init(&cursor, &datum_buf);
	M0_RETURN(credit->cr_ops->cro_decode(credit, &cursor));
}
M0_EXPORTED(m0_rm_credit_decode);

/** @} end of credit group */

/**
 * @name remote Code to deal with remote owners
 *
 * @{
 */
M0_INTERNAL int m0_rm_db_service_query(const char *name,
				       struct m0_rm_remote *rem)
{
        /* Create search query for DB using name as key and
         * find record  and assign service ID */
        rem->rem_state = REM_SERVICE_LOCATED;
        return 0;
}

M0_INTERNAL int m0_rm_remote_resource_locate(struct m0_rm_remote *rem)
{
         /* Send resource management fop to locate resource */
         rem->rem_state = REM_OWNER_LOCATED;
         return 0;
}

/**
 * A distributed resource location data-base is consulted to locate the service.
 */
static int service_locate(struct m0_rm_resource_type *rtype,
			  struct m0_rm_remote *rem)
{
	struct m0_clink clink;
	int		rc;

	M0_PRE(m0_mutex_is_locked(&rtype->rt_lock));
	M0_PRE(rem->rem_state == REM_SERVICE_LOCATING);

	m0_clink_init(&clink, NULL);
	m0_clink_add(&rem->rem_signal, &clink);
	/*
	 * DB callback should assign value to rem_service and
	 * rem_state should be changed to REM_SERVICE_LOCATED.
	 */
	rc = m0_rm_db_service_query(rtype->rt_name, rem);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "m0_rm_db_service_query failed!\n");
		goto error;
	}
	if (rem->rem_state != REM_SERVICE_LOCATED)
		m0_chan_wait(&clink);
	if (rem->rem_state != REM_SERVICE_LOCATED)
		rc = -EINVAL;

error:
	m0_clink_del(&clink);
	m0_clink_fini(&clink);

	return rc;
}

/**
 * Sends a resource management fop to the service. The service responds
 * with the remote owner identifier (m0_rm_remote::rem_id) used for
 * further communications.
 */
static int resource_locate(struct m0_rm_resource_type *rtype,
			   struct m0_rm_remote *rem)
{
	struct m0_clink clink;
	int		rc;

	M0_PRE(m0_mutex_is_locked(&rtype->rt_lock));
	M0_PRE(rem->rem_state == REM_OWNER_LOCATING);

	m0_clink_init(&clink, NULL);
	m0_clink_add(&rem->rem_signal, &clink);
	/*
	 * RPC callback should assign value to rem_id and
	 * rem_state should be set to REM_OWNER_LOCATED.
	 */
	rc = m0_rm_remote_resource_locate(rem);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "m0_rm_remote_resource_find failed!\n");
		goto error;
	}
	if (rem->rem_state != REM_OWNER_LOCATED)
		m0_chan_wait(&clink);
	if (rem->rem_state != REM_OWNER_LOCATED)
		rc = -EINVAL;

error:
	m0_clink_del(&clink);
	m0_clink_fini(&clink);

	return rc;
}

M0_INTERNAL int m0_rm_net_locate(struct m0_rm_credit *credit,
				 struct m0_rm_remote *other)
{
	struct m0_rm_resource_type *rtype;
	struct m0_rm_resource	   *res;
	int			    rc;

	M0_PRE(other->rem_state == REM_INITIALISED);

	rtype = credit->cr_owner->ro_resource->r_type;
	other->rem_state = REM_SERVICE_LOCATING;
	rc = service_locate(rtype, other);
	if (rc != 0)
		goto error;

	other->rem_state = REM_OWNER_LOCATING;
	rc = resource_locate(rtype, other);
	if (rc != 0)
		goto error;

	/* Search for resource having resource id equal to remote id */
	m0_mutex_lock(&rtype->rt_lock);
	m0_tl_for(res, &rtype->rt_resources, res) {
		if (rtype->rt_ops->rto_is(res, other->rem_id)) {
			other->rem_resource = res;
			break;
		}
	} m0_tl_endfor;
	m0_mutex_unlock(&rtype->rt_lock);

error:
	return rc;
}
M0_EXPORTED(m0_rm_net_locate);

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
