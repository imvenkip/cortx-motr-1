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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 10/12/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "lib/ut.h"
#include "lib/memory.h"/* C2_ALLOC_PTR */
#include "lib/misc.h"  /* C2_SET0 */
#include "lib/thread.h"/* C2_THREAD_INIT */
#include "net/bulk_sunrpc.h"
#include <unistd.h>
#include <stdio.h>
#include "net/buffer_pool.h"

void notempty(struct c2_net_buffer_pool *bp);
void low(struct c2_net_buffer_pool *bp);
void buffers_get_put(int rc);
struct c2_net_buffer_pool bp;
struct c2_chan buf_chan;
struct c2_net_xprt *xprt;
struct c2_net_buffer_pool_ops b_ops = {
	.nbpo_not_empty	      = notempty,
	.nbpo_below_threshold = low,
};

/**
   Test function for buf_pool ut
 */
void test_init()
{

	int rc;

	c2_chan_init(&buf_chan);
	xprt = &c2_net_bulk_sunrpc_xprt;
	c2_net_xprt_init(xprt);
	C2_ALLOC_PTR(bp.nbp_ndom);
	C2_UT_ASSERT(bp.nbp_ndom != NULL);
	rc = c2_net_domain_init(bp.nbp_ndom, xprt);
	C2_ASSERT(rc == 0);
	bp.nbp_ops = &b_ops;
	c2_net_buffer_pool_init(&bp, bp.nbp_ndom, 2, 64, 4096, 10);
	c2_net_buffer_pool_lock(&bp);
	rc = c2_net_buffer_pool_provision(&bp, 10);
	c2_net_buffer_pool_unlock(&bp);
	C2_UT_ASSERT(rc == 10);
}

void test_get_put()
{
	struct c2_net_buffer *nb = NULL;
	c2_net_buffer_pool_lock(&bp);
	nb = c2_net_buffer_pool_get(&bp, 0);
	C2_UT_ASSERT(nb != NULL);
	c2_net_buffer_pool_put(&bp, nb, 0);
	c2_net_buffer_pool_unlock(&bp);
}

void test_get_put_colour()
{
	struct c2_net_buffer *nb = NULL;
	c2_net_buffer_pool_lock(&bp);
	nb = c2_net_buffer_pool_get(&bp, 1);
	C2_UT_ASSERT(nb != NULL);
	c2_net_buffer_pool_put(&bp, nb, 1);
	c2_net_buffer_pool_unlock(&bp);
}

void test_grow()
{
	c2_net_buffer_pool_lock(&bp);
	/* Buffer pool grow by one */
	C2_UT_ASSERT(c2_net_buffer_pool_provision(&bp, 1) == 1);
	c2_net_buffer_pool_unlock(&bp);
}

void test_prune()
{
	c2_net_buffer_pool_lock(&bp);
	C2_UT_ASSERT(c2_net_buffer_pool_prune(&bp));
	c2_net_buffer_pool_unlock(&bp);
}

void test_get_put_multiple()
{
	int nr_client_threads = 5;
	int i;
	int rc;

	struct c2_thread *client_thread;
	C2_ALLOC_ARR(client_thread, nr_client_threads);
	C2_UT_ASSERT(client_thread != NULL);
	for (i = 0; i < nr_client_threads; i++) {
		C2_SET0(&client_thread[i]);
		rc = C2_THREAD_INIT(&client_thread[i], int,
				     NULL, &buffers_get_put,
					0, "client_%d", i);
		C2_ASSERT(rc == 0);
	}
	for (i = 0; i < nr_client_threads; i++) {
		c2_thread_join(&client_thread[i]);
	}
}

void test_fini()
{
	c2_net_buffer_pool_lock(&bp);
	c2_net_buffer_pool_fini(&bp);
	c2_net_buffer_pool_unlock(&bp);
	c2_net_domain_fini(bp.nbp_ndom);
	c2_free(bp.nbp_ndom);
	c2_net_xprt_fini(xprt);
	c2_chan_fini(&buf_chan);

}

void buffers_get_put(int rc)
{
	struct c2_net_buffer *nb = NULL;
	struct c2_clink buf_link;
	c2_clink_init(&buf_link, NULL);
	c2_clink_add(&buf_chan, &buf_link);
	do {
		c2_net_buffer_pool_lock(&bp);
		nb = c2_net_buffer_pool_get(&bp, 0);
		c2_net_buffer_pool_unlock(&bp);
		if (nb == NULL)
			c2_chan_wait(&buf_link);
	} while(nb == NULL);
	sleep(1);
	c2_net_buffer_pool_lock(&bp);
	if (nb != NULL)
		c2_net_buffer_pool_put(&bp, nb, 0);
	c2_net_buffer_pool_unlock(&bp);
	c2_clink_del(&buf_link);
	c2_clink_fini(&buf_link);
}

void notempty(struct c2_net_buffer_pool *bp)
{
	c2_chan_signal(&buf_chan);
}

void low(struct c2_net_buffer_pool *bp)
{
	printf("Buffer pool is LOW \n");
}

const struct c2_test_suite buffer_pool_ut = {
	.ts_name = "buf_pool_ut... this takes about 30 seconds",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "buffer_pool_init		", test_init},
		{ "buffer_pool_get_put		", test_get_put},
		{ "buffer_pool_get_put_colour	", test_get_put_colour},
		{ "buffer_pool_grow		", test_grow},
		{ "buffer_pool_prune		", test_prune},
		{ "buffer_pool_get_put_multiple	", test_get_put_multiple},
		{ "buffer_pool_fini		", test_fini},
		{ NULL, 			   NULL }
	}
};
C2_EXPORTED(bulk_pool_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

