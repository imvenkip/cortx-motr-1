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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 08/06/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "lib/ut.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "lib/thread.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"
#include "ut/rpc.h"

#include "fop/ut/long_lock/rdwr_fop.h"
#include "fop/ut/long_lock/rdwr_fom.h"
#include "fop/ut/long_lock/rdwr_fop_u.h"
#include "fop/ut/long_lock/rdwr_test_bench.h"
#include "fop/fom_long_lock.h"

enum tb_request_type {
	RQ_READ,
	RQ_WRITE,
	RQ_WAKE_UP
};

enum {
	RQ_LAST = RQ_WAKE_UP + 1
};

enum tb_request_phase {
	PH_REQ_LOCK = C2_FOPH_NR + 1,
	PH_GOT_LOCK
};

static struct c2_fom      *sleeper;
static struct c2_chan      chan[RDWR_REQUEST_MAX];
static struct c2_clink	   clink[RDWR_REQUEST_MAX];
static struct c2_long_lock long_lock;

/**
 * State function for rdwr request
 */
int c2_fom_rdwr_state(struct c2_fom *fom)
{
	struct c2_fop		*fop;
        struct c2_fop_rdwr_rep	*rdwr_fop_rep;
        struct c2_rpc_item      *item;
        struct c2_fom_rdwr	*fom_obj;
	struct c2_fop_rdwr	*request;
	int			 rq_type;
	int			 rq_seqn;
	int			 result;

	fom_obj = container_of(fom, struct c2_fom_rdwr, fp_gen);
	request = c2_fop_data(fom->fo_fop);
	C2_ASSERT(request != NULL);
	rq_type = request->fr_type;
	rq_seqn = request->fr_seqn;

	/* printf("fo_phase: %d:%d, rq_{type,seqn}: {%d:%d}, fom: %p\n", */
	/*      C2_FOPH_NR, fom->fo_phase, rq_type, rq_seqn, fom); */

        if (fom->fo_phase < C2_FOPH_NR) {
                result = c2_fom_state_generic(fom);
	} else if (fom->fo_phase == PH_GOT_LOCK) {
		switch (rq_type) {
		case RQ_READ:
			C2_UT_ASSERT(c2_long_is_read_locked(&long_lock,
							    &fom_obj->fp_link));
			c2_long_read_unlock(&long_lock, &fom_obj->fp_link);
			break;
		case RQ_WRITE:
			C2_UT_ASSERT(c2_long_is_write_locked(&long_lock,
							     &fom_obj->fp_link));
			c2_long_write_unlock(&long_lock, &fom_obj->fp_link);
			break;
		case RQ_WAKE_UP:
		default:
			;
		}

		/* send reply */
		fop = c2_fop_alloc(&c2_fop_rdwr_rep_fopt, NULL);
		C2_ASSERT(fop != NULL);
		rdwr_fop_rep = c2_fop_data(fop);
		rdwr_fop_rep->fr_rc = true;
		item = c2_fop_to_rpc_item(fop);
		item->ri_group = NULL;
		c2_rpc_reply_post(&fom_obj->fp_fop->f_item, item);
		fom->fo_phase = C2_FOPH_FINISH;

		/* notify, fom ready */
		c2_chan_signal(&chan[rq_seqn]);
		result = 0;
        } else if (fom->fo_phase == PH_REQ_LOCK) {
		if (rq_seqn == 0)
			sleeper = fom;

		switch (rq_type) {
		case RQ_READ:
			result = c2_long_read_lock(&long_lock,
						   &fom_obj->fp_link,
						   PH_GOT_LOCK);
			C2_UT_ASSERT(result == (rq_seqn == 0));
			result = C2_FSO_WAIT;
			break;
		case RQ_WRITE:
			result = c2_long_write_lock(&long_lock,
						    &fom_obj->fp_link,
						    PH_GOT_LOCK);
			C2_UT_ASSERT(result == (rq_seqn == 0));
			result = C2_FSO_WAIT;
			break;
		case RQ_WAKE_UP:
		default:
			c2_fom_ready_remote(sleeper);
			fom->fo_phase = PH_GOT_LOCK;
			result = C2_FSO_AGAIN;
		}

		/* notify, fom ready */
		c2_chan_signal(&chan[rq_seqn]);
	} else {
		C2_ASSERT(request != NULL);
		result = -1;
	}

	return result;
}

static void c2_rdwr_send_fop_type(struct c2_rpc_session *session, int type, int seqn)
{
	struct c2_fop      *fop;
	struct c2_fop_rdwr *rdwr_fop;

	fop = c2_fop_alloc(&c2_fop_rdwr_fopt, NULL);
	C2_ASSERT(fop);

	rdwr_fop = c2_fop_data(fop);
	rdwr_fop->fr_type = type;
	rdwr_fop->fr_seqn = seqn;

	c2_rpc_client_call(fop, session, &c2_fop_default_item_ops, 0);

	/* FIXME: freeing fop here will lead to endless loop in
	 * nr_active_items_count(), which is called from
	 * c2_rpc_session_terminate() */
	/*c2_fop_free(fop);*/
}

void c2_rdwr_send_fop(struct c2_rpc_session *session)
{
	int i;
	int j;

	int test[3][RDWR_REQUEST_MAX] = {
		[0] = {
			RQ_READ, RQ_WRITE, RQ_READ, RQ_READ, RQ_READ, RQ_WRITE,
			RQ_READ, RQ_READ,  RQ_READ, RQ_WAKE_UP, RQ_LAST
		},
		[1] = {
			RQ_WRITE, RQ_READ, RQ_WRITE, RQ_READ, RQ_READ, RQ_READ,
			RQ_WRITE, RQ_READ, RQ_READ, RQ_READ, RQ_WAKE_UP, RQ_LAST
		},
		[2] = {
			RQ_WRITE, RQ_READ, RQ_WRITE, RQ_WRITE, RQ_WRITE, RQ_READ,
			RQ_WRITE, RQ_READ, RQ_READ, RQ_READ, RQ_WAKE_UP, RQ_LAST
		},
	};
	
	/* printf("\n"); */

	for (j = 0; j < ARRAY_SIZE(test); ++j) {
		c2_long_lock_init(&long_lock);

		for (i = 0; test[j][i] != RQ_LAST; ++i) {
			c2_chan_init(&chan[i]);
			c2_clink_init(&clink[i], NULL);
			c2_clink_add(&chan[i], &clink[i]);

			c2_rdwr_send_fop_type(session, test[j][i], i);

			c2_chan_wait(&clink[i]);
		}

		for (i = 0; test[j][i] != RQ_LAST; ++i) {
			c2_chan_wait(&clink[i]);
		}

		for (i = 0; test[j][i] != RQ_LAST; ++i) {
			c2_clink_del(&clink[i]);
			c2_chan_fini(&chan[i]);
			c2_clink_fini(&clink[i]);
		}

		c2_long_lock_fini(&long_lock);

		/* printf("end %d\n", j); */
	}
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
