#include "lib/chan.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "rm/rm.h"
#include "lib/thread.h"
#include "lib/ut.h"
#include "lib/queue.h"

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

int go_out(struct c2_rm_incoming *in, enum c2_rm_outgoing_type otype,
       struct c2_rm_loan *loan, struct c2_rm_right *right)
{
	struct c2_rm_req *rm_req;
	struct domq *dq = NULL;
	bool wake = false;

	rm_req = c2_alloc(sizeof (struct c2_rm_req));
	if (rm_req == NULL)
		return -1;

	c2_list_init(&rm_req->rr_in.rin_pins);
	c2_chan_init(&rm_req->rr_in.rin_signal);

	switch (otype) {
	case ROT_BORROW:
		rm_req->rr_in.rin_type = RIT_LOAN;
		break;
	case ROT_REVOKE:
	case ROT_CANCEL:
		rm_req->rr_in.rin_type = RIT_REVOKE;
		break;
	default:
		break;
	}
	rm_req->rr_in.rin_flags = RIT_REVOKE;
	rm_req->rr_in.rin_policy = RIP_INPLACE;

	rm_req->rr_in.rin_owner = in->rin_owner;
	rm_req->rr_in.rin_want = *right;
	rm_req->rr_in.rin_priority = in->rin_priority;

	rm_req->rr_in.rin_ops = in->rin_ops;

	dq = c2_ut_lookup_dom((uint64_t *)loan);
	C2_UT_ASSERT(dq != NULL);

	c2_mutex_lock(&dq->req_lk);
	if (c2_queue_is_empty(&dq->req))
		wake = true;
	c2_queue_put(&dq->req, &rm_req->rr_lnk);
	c2_mutex_unlock(&dq->req_lk);
	if (wake)
		c2_chan_signal(&dq->req_chan);

	return 0;
}
