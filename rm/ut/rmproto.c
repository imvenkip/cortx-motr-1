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
 * Original author: Rajesh Bhalerao <rajesh_bhalerao@xyratex.com>
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 06/08/2011
 */
#include "lib/chan.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "rm/rm.h"
#include "lib/thread.h"
#include "lib/ut.h"
#include "lib/queue.h"
#include "rm/ut/rings.h"
#include "rm/ut/rmproto.h"

#if 0
typedef void (*cb_func_t)(struct c2_rm_incoming *);

struct c2_rm_req {
	struct c2_queue_link rr_lnk;
	struct c2_rm_incoming rr_in;	
};

struct domq {
	struct c2_list_link linkage;

	/**
	  * This is equivalent of service id. It's a key used to search a
	  * domain.
	  */
	uint64_t *cookie;
        struct c2_rm_domain *d;

	struct c2_mutex req_lk;
	struct c2_chan req_chan;
        struct c2_queue req;

	struct c2_thread domthr;
	cb_func_t cb;
	bool stop;
};

static struct c2_list dom_list;

void rm_ut_process_inq(void *arg)
{
	struct domq *dq = (struct domq *)arg;
	struct c2_rm_req *rm_req;
	struct c2_queue_link *ql;
	struct c2_clink wait_evt;

	c2_clink_init(&wait_evt, NULL);
	c2_clink_add(&dq->req_chan, &wait_evt);

	while (!dq->stop) {
		c2_mutex_lock(&dq->req_lk);
		ql = c2_queue_get(&dq->req);
		c2_mutex_unlock(&dq->req_lk);

		if (ql != NULL) {
			rm_req = container_of(ql, struct c2_rm_req, rr_lnk);
			dq->cb(&rm_req->rr_in);
		} else {
			c2_chan_wait(&wait_evt);
		}
	}
}

int rm_ut_register_domain(struct c2_rm_domain *dom,
		      uint64_t *cookie,
			cb_func_t cb_func)
{
	struct domq *dq;

	dq = c2_alloc(sizeof (struct domq));
	if (dq == NULL)
		return -1;

	dq->d = dom;
	dq->cookie = cookie;
	dq->cb = cb_func;
	dq->stop = false;
	/*
	 * Initialize other fields.
	 */
	c2_mutex_init(&dq->req_lk);
	c2_chan_init(&dq->req_chan);
	c2_queue_init(&dq->req);

	c2_list_link_init(&dq->linkage);
	c2_list_add(&dom_list, &dq->linkage);

	c2_thread_init(&dq->domthr, NULL, rm_ut_process_inq, (void *)dq);	
	return 0;
}

static struct domq *c2_ut_lookup_dom(uint64_t *cookie)
{
	struct domq *d = NULL;

	c2_list_for_each_entry(&dom_list, d, struct domq, linkage) {
		if (d->cookie == cookie)
			break;
	}

	return d;
}
#endif

void c2_rm_rpc_init(void)
{
	c2_queue_init(&rpc_queue);
	c2_mutex_init(&rpc_lock);
}

void c2_rm_rpc_fini(void)
{
	c2_queue_fini(&rpc_queue);
	c2_mutex_fini(&rpc_lock);
}

/**
 * Search for the OUT request related to IN request(revoke,loan) and returns.
 */
static struct c2_rm_outgoing *find_out_request(struct c2_rm_owner *owner, 
					       uint64_t loan_id)
{
	struct c2_rm_outgoing *out;
	struct c2_rm_right    *right;
	struct c2_rm_right    *ri_tmp;
	struct c2_rm_loan     *loan;
	int		       i = 0;

	C2_PRE(c2_mutex_is_locked(&owner->ro_lock));

	for (i = 0; i < ARRAY_SIZE(owner->ro_outgoing); i++) {
		c2_list_for_each_entry_safe(&owner->ro_outgoing[i], right,
					    ri_tmp, struct c2_rm_right,
					    ri_linkage) {
			loan = container_of(right, struct c2_rm_loan, rl_right);
			/* if loan ID's of right got in rpc layer and out
			 * loan id matches then this out request is for this
			 * right request */
			if (loan->rl_id == loan_id) {
                        	out = container_of(loan,
                                           struct c2_rm_outgoing, rog_want);
				return out;
			}
		}
	}

	return NULL;
}

/**
 * Search for the IN requested related to wanted rights request and returns.
 */
static struct c2_rm_incoming *find_in_request(struct c2_rm_owner *owner,
					      struct c2_rm_right *want)
{
	struct c2_rm_incoming *in;
	struct c2_rm_right    *right;
	int		       prio = 0;

	C2_PRE(c2_mutex_is_locked(&owner->ro_lock));

	for (prio = ARRAY_SIZE(owner->ro_incoming) - 1; prio >= 0; prio--) {
		c2_list_for_each_entry(&owner->ro_incoming[prio][OQS_GROUND],
				       right, struct c2_rm_right, ri_linkage) {
			if (right->ri_ops->rro_implies(right, want) &&
			    right->ri_ops->rro_implies(want, right)) {
				in = container_of(right, struct c2_rm_incoming,
						  rin_want);
				return in;
			}
		}
	}
	return NULL;
}

/**
 * This acts as pesudo rpc layer. This will be run by thread.
 */
void rpc_process(int id)
{
	struct c2_rm_req_reply 	*req;
	struct c2_queue_link 	*link;
	struct c2_rm_proto_info *info;
	struct c2_rm_incoming 	*in;
	struct c2_rm_outgoing 	*out;
	struct c2_rm_loan	*ch_loan;
	struct c2_rm_loan	*br_loan;

	while (!rpc_signal) {
		c2_mutex_lock(&rpc_lock);
		link = c2_queue_get(&rpc_queue);
		c2_mutex_unlock(&rpc_lock);

		if (link == NULL) 
			continue;

		req = container_of(link, struct c2_rm_req_reply,
				   rq_link);
		info = &rm_info[req->reply_id];

		switch (req->type) {
		case PRO_LOAN_REPLY:
			/* Gets right reply for loan/borrow requests */
			c2_mutex_lock(&info->owner->ro_lock);
			in = find_in_request(info->owner, &req->in.rin_want);
			if (in == NULL) {
				pin_add(&req->in, &req->in.rin_want);
				c2_list_add(&info->owner->ro_owned[OWOS_CACHED],
				    	    &req->in.rin_want.ri_linkage);
				c2_queue_put(&info->owner_queue, &req->rq_link);
			} else {
				/* Add right to the respective woners list
				 * and wake up waiting thread for this right.*/
				C2_ALLOC_PTR(ch_loan);
				C2_ASSERT(ch_loan != NULL);
				C2_ALLOC_PTR(br_loan);
				C2_ASSERT(br_loan != NULL);

				c2_list_init(&ch_loan->rl_right.ri_pins);
				c2_list_init(&br_loan->rl_right.ri_pins);
				c2_list_init(&in->rin_pins);
				right_copy(&ch_loan->rl_right, 
					   &req->in.rin_want);
				right_copy(&br_loan->rl_right,
					   &req->in.rin_want);
				
				ch_loan->rl_id = req->sig_id;
				br_loan->rl_id = req->sig_id;
				ch_loan->rl_other.rem_id = req->reply_id;
				br_loan->rl_other.rem_id = req->reply_id;

				c2_list_add(&info->owner->ro_owned[OWOS_CACHED],
				    	    &ch_loan->rl_right.ri_linkage);
				c2_list_add(&info->owner->ro_borrowed,
				    	    &br_loan->rl_right.ri_linkage);

				pin_add(in, &ch_loan->rl_right);
				in->rin_state = RI_SUCCESS;
				in->rin_ops->rio_complete(in, in->rin_state);
				out = find_out_request(info->owner,
						       req->reply_id);
				if (out != NULL)
					c2_rm_outgoing_complete(out,
								in->rin_state);
				c2_free(req);
			}
			c2_mutex_unlock(&info->owner->ro_lock);
			break;
		case PRO_OUT_REQUEST:
			/* If request is from go_out then just add to
			 * repective domain owners queues. This is incoming
			 * request */
			c2_mutex_lock(&info->owner->ro_lock);
			c2_queue_put(&info->owner_queue, &req->rq_link);
			c2_mutex_unlock(&info->owner->ro_lock);
			break;
		default:
			/* Invalid request. Just discard. */
			c2_free(req);
		}
	}
}

