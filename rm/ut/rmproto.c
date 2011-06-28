#include "lib/chan.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "rm/rm.h"
#include "lib/thread.h"
#include "lib/ut.h"
#include "lib/queue.h"
#include "rm/ut/rings.h"

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

void c2_rm_build_in_request(struct c2_rm_req_reply *req,
			    struct c2_rm_outgoing *out)
{
	struct c2_rm_remote *rem = out->rog_want.rl_other;
	struct c2_rm_owner owner = out->rog_owner;

	req->type = PRO_OUT_REQUEST;
	req->sig_id = out->rog_want.rl_id;
	req->reply_id = rem->rem_id;
	c2_chan_init(&req->in.rin_signal);
        c2_rm_right_init(&req->in.rin_want);
        in.rin_state = RI_INITIALISED;
        in.rin_owner = &owner;
        in.rin_priority = 0;
        in.rin_ops = &rings_incoming_ops;
        in.rin_want.ri_ops = &rings_right_ops;
        in.rin_type = RIT_LOCAL;
        in.rin_policy = RIP_INPLACE;
        in.rin_flags = RIF_LOCAL_TRY;
}

void rpc_process(int id)
{
	struct c2_rm_req_reply *req;
	struct c2_queue_link *link;
	struct c2_rm_proto_info *info;
	struct c2_rm_owner *owner;

	while (!rpc_signal) {
		link = c2_queue_get(&rcp_queue);
		if (link != NULL) {
			c2_mutex_lock(&rpc_lock);
			req = container_of(link, struct c2_rm_req_reply,
					   rq_link);
			c2_mutex_unlock(&rpc_lock);
			info = rm_info[req->reply_id];

			switch (req->type) {
			case PRO_LOAN_REPLY:
			case PRO_OUT_REQUEST;
				c2_mutex_lock(info->oq_lock);
				c2_queue_put(&info->owner_queue, &req->rq_link);
				c2_mutex_unlock(info->oq->lock);
				break;
			case PRO_REQ_FINISH:
				/* Signal the incoming reuest which send 
				 * this request*/
				info = rm_info[req->sig_id];
				owner = info->owner;
				c2_free(req);
				break;
			default:
				printf("Wrong request: Discard\n");
				c2_free(req);
			}
		}
	}
}

