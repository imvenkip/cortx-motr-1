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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 11/05/2011
 */

#include "lib/ut.h"
#include "lib/aqueue.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/thread.h"

enum {
	NR = 2,
	QSIZE = 10000,
	NR_PRODUCERS = 8,
	NR_CONSUMERS = 8,
	MT_QSIZE_PER_THREAD = 100000
};

static struct c2_aqueue aq;
static bool produced;

static void test_aq()
{
	struct c2_aqueue q;
	static struct c2_aqueue_link ql[QSIZE];
	int i;

	c2_aqueue_init(&q);
	for (i = 0; i < QSIZE; ++i)
		c2_aqueue_link_init(&ql[i]);

	C2_UT_ASSERT(c2_aqueue_get(&q) == NULL);

	for (i = 0; i < QSIZE; ++i)
		c2_aqueue_put(&q, &ql[i]);
	for (i = 0; i < QSIZE; ++i)
		C2_UT_ASSERT(c2_aqueue_get(&q)== &ql[i]);

	C2_UT_ASSERT(c2_aqueue_get(&q) == NULL);

	for (i = 0; i < QSIZE; ++i)
		c2_aqueue_link_fini(&ql[i]);
	c2_aqueue_fini(&q);
}

static void producer(struct c2_aqueue_link *ql)
{
	int i;

	for (i = 0; i < MT_QSIZE_PER_THREAD; ++i) {
		c2_aqueue_link_init(&ql[i]);
		c2_aqueue_put(&aq, &ql[i]);
	}
}

static long consume()
{
	long count = 0;
	struct c2_aqueue_link *ql;

	while ((ql = c2_aqueue_get(&aq)) != NULL) {
		c2_aqueue_link_fini(ql);
		count++;
	}
	return count;
}

static void consumer(long *size)
{
	while (1) {
		*size += consume();
		if (produced)
			break;
	}
	*size += consume();
}

static void test_aq_mt()
{
	static struct c2_thread producers[NR_PRODUCERS];
	static struct c2_thread consumers[NR_CONSUMERS];
	static struct c2_aqueue_link *link[NR_PRODUCERS];
	static long size[NR_CONSUMERS];
	int i;
	int rc;
	int total_size;

	c2_aqueue_init(&aq);

	for (i = 0; i < NR_PRODUCERS; ++i)
		link[i] = c2_alloc(MT_QSIZE_PER_THREAD * sizeof (struct c2_aqueue_link));
	for (i = 0; i < NR_CONSUMERS; ++i)
		size[i] = 0;
	produced = false;
	for (i = 0; i < NR_PRODUCERS; ++i) {
		rc = C2_THREAD_INIT(&producers[i], struct c2_aqueue_link *, NULL,
				&producer, link[i], "aqueue_producer");
		C2_ASSERT(rc == 0);
				
	}
	for (i = 0; i < NR_CONSUMERS; ++i) {
		rc = C2_THREAD_INIT(&consumers[i], long *, NULL,
				&consumer, &size[i], "aqueue_consumer");
		C2_ASSERT(rc == 0);
				
	}
	for (i = 0; i < NR_PRODUCERS; ++i) {
		c2_thread_join(&producers[i]);
		c2_thread_fini(&producers[i]);
	}
	produced = true;
	for (i = 0; i < NR_CONSUMERS; ++i) {
		c2_thread_join(&consumers[i]);
		c2_thread_fini(&consumers[i]);
	}
	for (i = 0; i < NR_PRODUCERS; ++i)
		c2_free(link[i]);

	total_size = 0;
	for (i = 0; i < NR_CONSUMERS; ++i)
		total_size += size[i];
	C2_UT_ASSERT(total_size == MT_QSIZE_PER_THREAD * NR_PRODUCERS);

	c2_aqueue_fini(&aq);
}

void test_aqueue(void)
{
	int i;

	for (i = 0; i < NR; ++i) {
		test_aq();
		test_aq_mt();
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
