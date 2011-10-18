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

#include "rm/rm.h"

/*@todo remove after RM:Fops is developed */
#include "rm/ut/rmproto.h" /* PRO_LOAN_REPLY,PRO_OUT_REQUEST */
/**
   @addtogroup rm
   @{
 */

static void owner_balance(struct c2_rm_owner *o);
static void pin_remove(struct c2_rm_pin *pin, bool move);
static int go_out(struct c2_rm_incoming *in, enum c2_rm_outgoing_type otype,
		  struct c2_rm_loan *loan, struct c2_rm_right *right);
static void incoming_check(struct c2_rm_incoming *in);
static void incoming_check_local(struct c2_rm_incoming *in,
				 struct c2_rm_right *rest);
static int sublet_revoke(struct c2_rm_incoming *in,
			  struct c2_rm_right *right);

int pin_add(struct c2_rm_incoming *in, struct c2_rm_right *right);


void c2_rm_domain_init(struct c2_rm_domain *dom)
{
	C2_PRE(dom != NULL);

	C2_SET_ARR0(dom->rd_types);
	c2_mutex_init(&dom->rd_lock);
}

void c2_rm_domain_fini(struct c2_rm_domain *dom)
{
	int  i;

	for (i = 0; i < ARRAY_SIZE(dom->rd_types); i++)
		C2_ASSERT(dom->rd_types[i] == NULL);

	c2_mutex_fini(&dom->rd_lock);
}

void c2_rm_type_register(struct c2_rm_domain *dom,
			 struct c2_rm_resource_type *rt)
{
	C2_PRE(rt->rt_dom == NULL);
	C2_PRE(IS_IN_ARRAY(rt->rt_id, dom->rd_types));
	C2_PRE(dom->rd_types[rt->rt_id] == NULL);

	c2_mutex_init(&rt->rt_lock);
	c2_list_init(&rt->rt_resources);
	rt->rt_nr_resources = 0;

	c2_mutex_lock(&dom->rd_lock);
	dom->rd_types[rt->rt_id] = rt;
	rt->rt_dom = dom;
	c2_mutex_unlock(&dom->rd_lock);

	C2_POST(dom->rd_types[rt->rt_id] == rt);
	C2_POST(rt->rt_dom == dom);
}

void c2_rm_type_deregister(struct c2_rm_resource_type *rtype)
{
	struct c2_rm_domain *dom = rtype->rt_dom;

	C2_PRE(dom != NULL);
	C2_PRE(IS_IN_ARRAY(rtype->rt_id, dom->rd_types));
	C2_PRE(dom->rd_types[rtype->rt_id] == rtype);
	C2_PRE(c2_list_is_empty(&rtype->rt_resources));
	C2_PRE(rtype->rt_nr_resources == 0);

	c2_mutex_lock(&dom->rd_lock);
	dom->rd_types[rtype->rt_id] = NULL;
	rtype->rt_dom = NULL;
	rtype->rt_id = C2_RM_RESOURCE_TYPE_ID_INVALID;
	c2_list_fini(&rtype->rt_resources);
	c2_mutex_fini(&rtype->rt_lock);
	c2_mutex_unlock(&dom->rd_lock);

	C2_POST(rtype->rt_id == C2_RM_RESOURCE_TYPE_ID_INVALID);
	C2_POST(rtype->rt_dom == NULL);
}

void c2_rm_resource_add(struct c2_rm_resource_type *rtype,
			struct c2_rm_resource *res)
{
	struct c2_rm_resource *r;

	c2_mutex_lock(&rtype->rt_lock);
	C2_PRE(res->r_ref == 0);
	/* rtype->rt_resources does not contain a resource equal to res */
	c2_list_for_each_entry(&rtype->rt_resources, r,
			       struct c2_rm_resource, r_linkage) {
		C2_ASSERT(!rtype->rt_ops->rto_eq(r, res));
	}
	res->r_type = rtype;
	c2_list_add(&rtype->rt_resources, &res->r_linkage);
	rtype->rt_nr_resources++;
	C2_POST(rtype->rt_nr_resources > 0);
	C2_POST(c2_list_contains(&rtype->rt_resources, &res->r_linkage));
	c2_mutex_unlock(&rtype->rt_lock);
	C2_POST(res->r_type == rtype);
}

void c2_rm_resource_del(struct c2_rm_resource *res)
{
	struct c2_rm_resource_type *rtype = res->r_type;

	c2_mutex_lock(&rtype->rt_lock);
	C2_PRE(rtype->rt_nr_resources > 0);
	C2_PRE(c2_list_contains(&rtype->rt_resources, &res->r_linkage));

	c2_list_del(&res->r_linkage);
	rtype->rt_nr_resources--;

	C2_POST(!c2_list_contains(&rtype->rt_resources, &res->r_linkage));
	c2_mutex_unlock(&rtype->rt_lock);
}

static void owner_init_internal(struct c2_rm_owner *owner,
			        struct c2_rm_resource *res)
{
	struct c2_rm_resource_type *rtype = res->r_type;
	int			    i;
	int			    j;

	owner->ro_resource = res;
	owner->ro_state = ROS_INITIALISING;
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

	c2_mutex_lock(&rtype->rt_lock);
	res->r_ref++;
	c2_mutex_unlock(&rtype->rt_lock);
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

	/* Add The right to the owner's cached list.*/
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

	for (i = 0; i < ARRAY_SIZE(owner->ro_owned); i++)
		c2_list_fini(&owner->ro_owned[i]);

	for (i = 0; i < ARRAY_SIZE(owner->ro_incoming); i++) {
		for(j = 0; j < ARRAY_SIZE(owner->ro_incoming[i]); j++)
			c2_list_fini(&owner->ro_incoming[i][j]);
	}

	for (i = 0; i < ARRAY_SIZE(owner->ro_outgoing); i++)
		c2_list_fini(&owner->ro_outgoing[i]);

	owner->ro_resource = NULL;
	c2_mutex_fini(&owner->ro_lock);

	c2_mutex_lock(&rtype->rt_lock);
	res->r_ref--;
	c2_mutex_unlock(&rtype->rt_lock);
}

void c2_rm_right_init(struct c2_rm_right *right)
{
	C2_PRE(right != NULL);

	right->ri_datum = 0;
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

	in->rin_state = RI_INITIALISED;
	c2_list_init(&in->rin_pins);
	c2_chan_init(&in->rin_signal);
	c2_rm_right_init(&in->rin_want);
}

void c2_rm_incoming_fini(struct c2_rm_incoming *in)
{
	C2_PRE(c2_list_is_empty(&in->rin_pins));

	in->rin_state = RI_INITIALISED;
	c2_list_fini(&in->rin_pins);
	c2_chan_fini(&in->rin_signal);
	c2_rm_right_fini(&in->rin_want);
}

void c2_rm_remote_init(struct c2_rm_remote *rem)
{
	rem->rem_state = REM_INITIALIZED;
	c2_chan_init(&rem->rem_signal);
}

void c2_rm_remote_fini(struct c2_rm_remote *rem)
{
	C2_PRE(rem->rem_state == REM_INITIALIZED);

	rem->rem_state = REM_FREED;
	c2_chan_fini(&rem->rem_signal);
}

/**
   Releases rights by removing pins associated with incoming request.
   Revocation or cancellation of rights requires this.
 */
static void incoming_release(struct c2_rm_incoming *in)
{
	struct c2_rm_pin   *pin;
	struct c2_rm_pin   *tmp_pin;

	c2_list_for_each_entry_safe(&in->rin_pins, pin, tmp_pin,
				    struct c2_rm_pin, rp_incoming_linkage) {
		if (pin->rp_flags & RPF_TRACK)
			pin_remove(pin, false);
	}
}

/**
   Frees the outgoing request after completion.
 */
static void outgoing_delete(struct c2_rm_outgoing *out)
{
	C2_PRE(out != NULL);

	c2_list_del(&out->rog_want.rl_right.ri_linkage);
	c2_free(out);
}

/**
   Makes another copy of right struct.
 */
void right_copy(struct c2_rm_right *dest, const struct c2_rm_right *src)
{
	C2_PRE(src != NULL);

	dest->ri_resource = src->ri_resource;
	dest->ri_ops = src->ri_ops;
	src->ri_ops->rro_copy(dest, src);

	/* Two rights are equal when both rights implies to each other */
	C2_POST(src->ri_ops->rro_implies(dest, src) &&
		src->ri_ops->rro_implies(src, dest));
}

/**
   Applies universal policies
 */
static void apply_policy(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;
	struct c2_rm_right *pin_right;
	struct c2_rm_pin   *pin;
	struct c2_rm_pin   *pin_tmp;
	bool		    first;

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
		c2_list_for_each_entry_safe(&in->rin_pins, pin, pin_tmp,
					    struct c2_rm_pin,
					    rp_incoming_linkage) {
			pin_right = pin->rp_right;
			pin_right->ri_ops->rro_diff(pin->rp_right,
						    &in->rin_want);
			pin->rp_right = &in->rin_want;
			c2_list_move(&in->rin_want.ri_pins,
				     &pin->rp_right_linkage);
		}
		break;
	case RIP_STRICT:
		/*
		 * Remove all pinned rights and insert right equivalent
		 * to requested right.
		 */
		C2_ALLOC_PTR(loan);
		C2_ASSERT(loan != NULL);
		incoming_release(in);
		c2_rm_right_init(&loan->rl_right);
		right_copy(&loan->rl_right, &in->rin_want);
		pin_add(in, &loan->rl_right);
		c2_list_add(&owner->ro_owned[OWOS_CACHED],
			    &loan->rl_right.ri_linkage);
		break;
	case RIP_JOIN:
		/*
		 * New right equivalent to joined rights of granted
		 * rights is inserted into owned rights list.
		 */
		if (c2_list_is_empty(&in->rin_pins))
			break;

		first = true;
		c2_list_for_each_entry_safe(&in->rin_pins, pin, pin_tmp,
					    struct c2_rm_pin,
					    rp_incoming_linkage) {
			if (first) {
				right = pin->rp_right;
				first = false;
				continue;
			}
			pin_right = pin->rp_right;
			right->ri_ops->rro_join(right, pin_right);
			c2_list_del(&pin_right->ri_linkage);
			loan = container_of(pin_right, struct c2_rm_loan,
					    rl_right);
			pin_remove(pin, false);
			c2_free(loan);
		}
		c2_list_move(&owner->ro_owned[OWOS_CACHED], &right->ri_linkage);
		break;
	case RIP_MAX:
		/*
		 * Maximum rights which intersects to requested right
		 * are granted from cached right list.
		 */
		c2_list_for_each_entry(&owner->ro_owned[OWOS_CACHED], right,
				       struct c2_rm_right, ri_linkage) {
			if (right->ri_ops->rro_intersects(right,
							  &in->rin_want)) {
				c2_list_for_each_entry(&in->rin_pins, pin,
						       struct c2_rm_pin,
						       rp_incoming_linkage) {
					if (!c2_list_contains(&right->ri_pins,
							      &pin->
							      rp_right_linkage))
						pin_add(in, right);
				}
			}
		}
	}
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
         rem->rem_state = REM_RESOURCE_LOCATED;
         return 0;
}

/**
   A distributed resource location data-base is consulted to locate the service.
 */
static int service_locate(struct c2_rm_resource_type *rtype,
			  struct c2_rm_remote *rem, c2_time_t deadline)
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
		c2_chan_timedwait(&clink, deadline);
	if (rem->rem_state != REM_SERVICE_LOCATED)
		rc = -ETIMEDOUT;

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
			   struct c2_rm_remote *rem, c2_time_t deadline)
{
	struct c2_clink clink;
	int		rc;

	C2_PRE(c2_mutex_is_locked(&rtype->rt_lock));
	C2_PRE(rem->rem_state == REM_RESOURCE_LOCATING);

	c2_clink_init(&clink, NULL);
	c2_clink_add(&rem->rem_signal, &clink);
	/*
	 * RPC callback should assign value to rem_id and
	 * rem_state should be set to REM_RESOURCE_LOCATED.
	 */
	rc = c2_rm_remote_resource_locate(rem);
	if (rc != 0) {
		fprintf(stderr, "c2_rm_remote_resource_find failed!\n");
		goto error;
	}
	if (rem->rem_state != REM_RESOURCE_LOCATED)
		c2_chan_timedwait(&clink, deadline);
	if (rem->rem_state != REM_RESOURCE_LOCATED)
		rc = -ETIMEDOUT;

error:
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return rc;
}

int c2_rm_net_locate(struct c2_rm_right *right, struct c2_rm_remote *other,
		     c2_time_t deadline)
{
	struct c2_rm_resource_type *rtype = right->ri_resource->r_type;
	struct c2_rm_resource	   *res;
	int			    result;

	C2_PRE(other->rem_state == REM_INITIALIZED);

	c2_mutex_lock(&rtype->rt_lock);
	other->rem_state = REM_SERVICE_LOCATING;
	result = service_locate(rtype, other, deadline);
	if (result != 0)
		goto error;

	other->rem_state = REM_RESOURCE_LOCATING;
	result = resource_locate(rtype, other, deadline);
	if (result != 0)
		goto error;

	c2_list_for_each_entry(&rtype->rt_resources, res,
			       struct c2_rm_resource, r_linkage) {
		if (rtype->rt_ops->rto_res_is_valid(other->rem_id, res)) {
			other->rem_resource = res;
			break;
		}
	}

error:
	c2_mutex_unlock(&rtype->rt_lock);
	C2_POST(ergo(result == 0, other->rem_resource == right->ri_resource));

	return result;
}

/**
   Locate c2_rm_remote for every right in the incoming request (for loan)
   and move them to sublet list of owner.
 */
static void move_to_sublet(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
	struct c2_rm_pin   *pin;
	struct c2_rm_pin   *pin_tmp;
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;

	C2_PRE(c2_mutex_is_locked(&owner->ro_lock));

	C2_ALLOC_PTR(loan);
	C2_ASSERT(loan != NULL);
	c2_rm_right_init(&loan->rl_right);

	/* Constructs a single cumulative right */
	c2_list_for_each_entry_safe(&in->rin_pins, pin, pin_tmp,
				    struct c2_rm_pin, rp_incoming_linkage) {
		right = pin->rp_right;
		loan->rl_right.ri_ops = right->ri_ops;
		right->ri_ops->rro_join(&loan->rl_right, right);
		pin_remove(pin, false);
		/* Loaned right should not be on cached list */
		c2_list_del(&right->ri_linkage);
		c2_free(right);
	}
	c2_list_add(&owner->ro_sublet, &loan->rl_right.ri_linkage);
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
	struct c2_rm_right     *tmp_right;
	struct c2_rm_pin       *pin;
	struct c2_rm_pin       *tmp_pin;
	struct c2_rm_loan      *loan;
	struct c2_rm_req_reply *request;
	struct c2_rm_owner     *owner = in->rin_owner;

	c2_list_for_each_entry_safe(&in->rin_pins, pin, tmp_pin,
				    struct c2_rm_pin, rp_incoming_linkage) {
		right = pin->rp_right;
		loan = container_of(right, struct c2_rm_loan, rl_right);
		if (c2_rm_net_locate(right, &loan->rl_other, 30) == 0) {
			C2_ALLOC_PTR(request);
			if (request == NULL)
				return -ENOMEM;
			request_build(request, loan, PRO_LOAN_REPLY);
			right_copy(&request->in.rin_want, &loan->rl_right);
			/*@todo will be replaced by rpc call and fop will be using
			 * rpc layer API's */
			c2_mutex_lock(&rpc_lock);
			c2_queue_put(&rpc_queue, &request->rq_link);
			c2_mutex_unlock(&rpc_lock);

			/* Revoke(cancel) request will remove rights from borrow
			 * and owned lists
			 */
			c2_list_for_each_entry_safe(&owner->ro_borrowed,
						    bo_right, tmp_right,
						    struct c2_rm_right,
						    ri_linkage) {
				if (right->ri_ops->rro_implies(right,
							       bo_right) &&
				    right->ri_ops->rro_implies(bo_right,
							       right)) {
				    c2_list_del(&bo_right->ri_linkage);
				    c2_rm_right_fini(bo_right);
				    c2_free(bo_right);
				}
			}
			pin_remove(pin, false);
			c2_list_del(&right->ri_linkage);
			c2_free(right);
		}
	}
	return 0;
}

/**
   Reply to the incoming loan request when wanted rights granted
 */
static int loan_request_reply(struct c2_rm_incoming *in)
{
	struct c2_rm_right   *right;
	struct c2_rm_pin     *pin;
	struct c2_rm_pin     *tmp_pin;
	struct c2_rm_loan    *loan;
	struct c2_rm_req_reply *request;
	int		      result = 0;

	c2_list_for_each_entry_safe(&in->rin_pins, pin, tmp_pin,
				    struct c2_rm_pin, rp_incoming_linkage) {
		right = pin->rp_right;
		loan = container_of(right, struct c2_rm_loan, rl_right);
		/*@todo Form fop for each loan and reply or
		 * we can apply rpc grouping and send reply
		 */
		if (!c2_rm_net_locate(right, &loan->rl_other, 30)) {
			C2_ALLOC_PTR(request);
			if (request == NULL)
				return -ENOMEM;
			request_build(request, loan, PRO_LOAN_REPLY);
			right_copy(&request->in.rin_want, &loan->rl_right);
			c2_mutex_lock(&rpc_lock);
			c2_queue_put(&rpc_queue, &request->rq_link);
			c2_mutex_unlock(&rpc_lock);
		}
	}

	return result;
}

/**
   It sends out outgoing excited requests
 */
static int out_request_send(struct c2_rm_outgoing *out)
{
	struct c2_rm_req_reply *request;

	C2_ALLOC_PTR(request);
	if (request == NULL)
		return -ENOMEM;
	request_build(request, &out->rog_want, PRO_OUT_REQUEST);
	right_copy(&request->in.rin_want, &out->rog_want.rl_right);
	/*@todo following code will be replaced by rpc call */
	c2_mutex_lock(&rpc_lock);
	c2_queue_put(&rpc_queue, &request->rq_link);
	c2_mutex_unlock(&rpc_lock);

	return 0;
}

/**
   Check for held conflict rights.
 */
static bool conflicts_exist(const struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
	struct c2_rm_pin   *pin;
	struct c2_rm_right *right;

	if (in->rin_type == RIT_LOCAL)
		return false;

	c2_list_for_each_entry(&in->rin_pins, pin, struct c2_rm_pin,
			       rp_incoming_linkage) {
		right = pin->rp_right;
		if (c2_list_contains(&owner->ro_owned[OWOS_HELD],
				     &right->ri_linkage))
			return true;
	}

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
void c2_rm_right_get(struct c2_rm_incoming *in)
{
	struct c2_rm_owner *owner = in->rin_owner;
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

void c2_rm_right_put(struct c2_rm_incoming *in)
{
	C2_PRE(in->rin_state == RI_SUCCESS);

	c2_mutex_lock(&in->rin_owner->ro_lock);
	incoming_release(in);
	c2_mutex_unlock(&in->rin_owner->ro_lock);
	C2_POST(c2_list_is_empty(&in->rin_pins));
}

/**
   Called when an outgoing request completes (possibly with an error, like a
   timeout).
 */
void c2_rm_outgoing_complete(struct c2_rm_outgoing *og, int rc)
{
	struct c2_rm_owner *owner;

	C2_PRE(og != NULL);

	owner = og->rog_owner;
	c2_list_move(&owner->ro_outgoing[OQS_EXCITED],
		     &og->rog_want.rl_right.ri_linkage);
	owner_balance(owner);
}

/**
   Removes a tracking pin on a resource usage right.

   If this was a last pin issued by an incoming request, excite the request.
 */
static void pin_remove(struct c2_rm_pin *pin, bool move)
{
	struct c2_rm_incoming	*in;
	struct c2_rm_owner	*owner;

	C2_ASSERT(pin != NULL);

	in = pin->rp_incoming;
	owner = in->rin_owner;
	c2_list_del(&pin->rp_incoming_linkage);
	c2_list_del(&pin->rp_right_linkage);
	c2_free(pin);
	if (c2_list_is_empty(&in->rin_pins) && move) {
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
			c2_list_for_each_entry_safe(&out->rog_want.rl_right.
						    ri_pins, pin, pin_tmp,
						    struct c2_rm_pin,
						    rp_incoming_linkage) {
				pin_remove(pin, true);
			}

			outgoing_delete(out);
		}
		for (prio = ARRAY_SIZE(o->ro_incoming) - 1; prio >= 0; prio--) {
			c2_list_for_each_entry_safe(&o->ro_incoming[prio][OQS_EXCITED],
						    right, ri_tmp,
						    struct c2_rm_right,
						    ri_linkage) {

				todo = true;
				in = container_of(right, struct c2_rm_incoming,
						  rin_want);
				C2_ASSERT(in != NULL);
				C2_ASSERT(in->rin_state == RI_WAIT ||
					  in->rin_state == RI_INITIALISED);
				//C2_ASSERT(c2_list_is_empty(&in->rin_pins));
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
	struct c2_rm_right  rest;
	int		    result;
	bool		    held;


	C2_PRE(c2_mutex_is_locked(&o->ro_lock));
	C2_PRE(c2_list_contains(&o->ro_incoming[in->rin_priority][OQS_GROUND],
				&in->rin_want.ri_linkage));
	C2_PRE(in->rin_state == RI_CHECK);
	//C2_PRE(c2_list_is_empty(&in->rin_pins));

	/*
	 * This function goes through owner rights lists checking for "wait"
	 * conditions that should be satisfied before the request could be
	 * fulfilled.
	 *
	 * If there is nothing to wait for, the request is either fulfilled
	 * immediately or fails.
	 */

	c2_rm_right_init(&rest);
	right_copy(&rest, &in->rin_want);

	/*
	 * Check for "local" wait conditions.
	 */
	incoming_check_local(in, &rest);

	held = conflicts_exist(in);
	/* Try Lock */
	if (held && (in->rin_flags & RIF_LOCAL_TRY)) {
		incoming_release(in);
		goto error;
	}

	if (right_is_empty(&rest) ||
	    (in->rin_type == RIT_LOCAL &&
	    !c2_list_is_empty(&in->rin_pins))) {
		/*
		 * The wanted right is completely covered by the local
		 * rights. There are no remote conditions to wait for.
		 */
		if (held && (in->rin_flags & RIF_LOCAL_WAIT)) {
			/*
			 * conflicting held rights were found, has to
			 * wait until local rights are released.
			 */
			in->rin_state = RI_WAIT;
		} else {
			/*
			 * all conflicting rights are cached (not
			 * held).
			 * Apply the policy.
			 */
			apply_policy(in);
			switch (in->rin_type) {
			case RIT_LOAN:
				move_to_sublet(in);
				loan_request_reply(in);
			case RIT_LOCAL:
				break;
			case RIT_REVOKE:
				revoke_request_reply(in);
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
			result = sublet_revoke(in, &rest);
		if (in->rin_flags & RIF_MAY_BORROW) {
			/* borrow more */
			struct c2_rm_loan *borrow;
			while (!right_is_empty(&rest)) {
				C2_ALLOC_PTR(borrow);
				if (borrow == NULL)
					goto error;

				c2_rm_right_init(&borrow->rl_right);
				right_copy(&borrow->rl_right, &rest);
				c2_rm_net_locate(&rest, &borrow->rl_other, 30);
				result = go_out(in, ROT_BORROW, borrow,
						&borrow->rl_right);
				if (result == 0)
					right_copy(&rest, &borrow->rl_right);
			}
		}
		if (right_is_empty(&rest)) {
			in->rin_state = RI_WAIT;
		} else {
			/* cannot fulfill the request. */
			in->rin_state = RI_FAILURE;
		}
	}

	return;
error:
	in->rin_state = RI_FAILURE;
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
	bool		    track_local = false;
	bool		    coverage = false;
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
	if (in->rin_type != RIT_LOCAL ||
	    in->rin_policy == RIP_INPLACE ||
	    in->rin_flags & RIF_LOCAL_WAIT)
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
			if (rest->ri_ops->rro_intersects(right, rest)) {
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
static int sublet_revoke(struct c2_rm_incoming *in,
			  struct c2_rm_right *rest)
{
	struct c2_rm_owner *o = in->rin_owner;
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;
	int		    result = 1;

	c2_list_for_each_entry(&o->ro_sublet, right,
			       struct c2_rm_right, ri_linkage) {
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
				break;
		}
	}

	return result;
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
	struct c2_rm_outgoing	*out;
	struct c2_rm_right	*out_right;
	struct c2_rm_right	*tmp;
	struct c2_rm_loan	*out_loan;
	int			 result = 0;
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
				right->ri_ops->rro_intersects(out_right,
							      right)) {
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
		out->rog_owner = in->rin_owner;
		out->rog_want.rl_other = loan->rl_other;
		out->rog_want.rl_id = loan->rl_id;
		c2_rm_right_init(&out->rog_want.rl_right);
		right_copy(&out->rog_want.rl_right, right);
		c2_list_add(&in->rin_owner->ro_outgoing[OQS_GROUND],
			    &out->rog_want.rl_right.ri_linkage);
		pin_add(in, right);
		right->ri_ops->rro_diff(right, &out->rog_want.rl_right);
	}

	result = out_request_send(out);

	return result;
}

/**
   Helper function to get right with timed wait(deadline).
 */
int c2_rm_right_timedwait(struct c2_rm_incoming *in,
			  const c2_time_t deadline)
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
		return SUCCESS;
	case RI_FAILURE:
	default:
		return -EWOULDBLOCK;
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
		return SUCCESS;
	case RI_WAIT:
	case RI_FAILURE:
	default:
		return -EWOULDBLOCK;
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
