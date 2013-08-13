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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-May-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "lib/misc.h"
#include "ut/ut.h"
#include "be/ut/helper.h"
#include "be/list.h"

/* -------------------------------------------------------------------------
 * Descriptors and stuff
 * ------------------------------------------------------------------------- */
enum {
	TEST_MAGIC      = 0x331fefefefefe177,
	TEST_LINK_MAGIC = 0x331acacacacac277
};

struct test {
	uint64_t        t_magic;
	struct m0_tlink t_linkage;
	int             t_payload;
};

M0_TL_DESCR_DEFINE(test, "test:m0-be-list", M0_INTERNAL, struct test,
		   t_linkage, t_magic, TEST_MAGIC, TEST_LINK_MAGIC);
M0_TL_DEFINE(test, M0_INTERNAL, struct test);

/* -------------------------------------------------------------------------
 * List construction test
 * ------------------------------------------------------------------------- */

static void check(struct m0_be_list *list, struct m0_be_seg *seg);
M0_UNUSED static void print(struct m0_be_list *list);

M0_INTERNAL void m0_be_ut_list_api(void)
{
	enum { SHIFT = 0 };
	M0_BE_TX_CREDIT(tcred); /* credits for structs "test" */
	M0_BE_TX_CREDIT(ccred); /* credits for list creation */
	M0_BE_TX_CREDIT(icred); /* credits for list insertions */
	M0_BE_TX_CREDIT(dcred); /* credits for list deletions */
	M0_BE_TX_CREDIT(cred);  /* total credits */
	struct m0_be_allocator *a;
	struct m0_be_list      *list;
	struct m0_be_ut_backend ut_be;
	struct m0_be_ut_seg     ut_seg;
	struct m0_be_seg       *seg;
	struct m0_be_op         op;
	struct m0_be_tx         tx;
	struct test            *elem[10];
	int                     rc;
	int                     i;

	M0_ENTRY();

	/* Init BE. */
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, 1ULL << 24);
	m0_be_ut_seg_allocator_init(&ut_seg, &ut_be);
	a = ut_seg.bus_allocator;
	seg = &ut_seg.bus_seg;

	{ /* XXX: calculate credits properly */
		struct m0_be_list l;

		m0_be_list_init(&l, &test_tl, seg);

		m0_be_allocator_credit(a, M0_BAO_ALLOC, sizeof(elem[0]), SHIFT,
				       &tcred);
		m0_be_allocator_credit(a, M0_BAO_FREE, sizeof(elem[0]), SHIFT,
				       &tcred);
		m0_be_tx_credit_mul(&tcred, ARRAY_SIZE(elem));

		m0_be_list_credit(&l, M0_BLO_CREATE, 1, &ccred);
		m0_be_list_credit(&l, M0_BLO_INSERT, ARRAY_SIZE(elem), &icred);
		m0_be_list_credit(&l, M0_BLO_DELETE, ARRAY_SIZE(elem), &dcred);

		m0_be_tx_credit_add(&cred, &ccred);
		m0_be_tx_credit_add(&cred, &tcred);
		m0_be_tx_credit_add(&cred, &icred);
		m0_be_tx_credit_add(&cred, &dcred);
	}

	m0_be_ut_tx_init(&tx, &ut_be);

	/* Open the transaction. */
	m0_be_tx_prep(&tx, &cred);
	rc = m0_be_tx_open_sync(&tx);
	M0_UT_ASSERT(rc == 0);

	/* Perform some operations over the list. */
	m0_be_op_init(&op);
	m0_be_list_create(&list, &test_tl, seg, &op, &tx);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	m0_be_op_fini(&op);
	M0_UT_ASSERT(list != NULL);

	/* add */
	for (i = 0; i < ARRAY_SIZE(elem); ++i) {
		m0_be_op_init(&op);
		elem[i] = m0_be_alloc(a, &tx, &op, sizeof(*elem[0]), SHIFT);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);
		M0_UT_ASSERT(elem[i] != NULL);

		m0_tlink_init(&test_tl, elem[i]);
		elem[i]->t_payload = i;
		M0_BE_TX_CAPTURE_PTR(seg, &tx, elem[i]);

		m0_be_op_init(&op);
		if (i < ARRAY_SIZE(elem) / 2) {
			if (i % 2 == 0)
				m0_be_list_add(list, &op, &tx, elem[i]);
			else
				m0_be_list_add_tail(list, &op, &tx, elem[i]);
		} else {
			if (i % 2 == 0)
				m0_be_list_add_after(list, &op, &tx,
						     elem[i - 1], elem[i]);
			else
				m0_be_list_add_before(list, &op, &tx,
						      elem[i - 1], elem[i]);
		}
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);
	}

	/* delete */
	for (i = 0; i < ARRAY_SIZE(elem); ++i) {
		if (!M0_IN(i, (0, 2, 7, 9)))
			continue;

		m0_be_op_init(&op);
		m0_be_list_del(list, &op, &tx, elem[i]);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);

		m0_be_op_init(&op);
		m0_be_free(a, &tx, &op, elem[i]);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);
		M0_BE_TX_CAPTURE_PTR(seg, &tx, elem[i]);
	}

	/* Make things persistent. */
	rc = m0_be_tx_close_sync(&tx);
	M0_UT_ASSERT(rc == 0);

	/* Reload segment and check data. */
	m0_be_ut_seg_check_persistence(&ut_seg);
	check(list, seg);

	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	m0_be_tx_fini(&tx);

	/* XXX can't destroy allocator because some memory wasn't freed */
	/* m0_be_ut_seg_allocator_fini(&ut_seg, &ut_be); */
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);

	M0_LEAVE();
}

/* -------------------------------------------------------------------------
 * List reloading test
 * ------------------------------------------------------------------------- */

static void *be_list_head(struct m0_be_list *list)
{
	void *p;
	struct m0_be_op op;

	m0_be_op_init(&op);
	p = m0_be_list_head(list, &op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	return p;
}

static void *be_list_next(struct m0_be_list *list, const void *obj)
{
	void *p;
	struct m0_be_op op;

	m0_be_op_init(&op);
	p = m0_be_list_next(list, &op, obj);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	return p;
}

#define m0_be_list_for(descr, head, obj)				\
do {									\
	void *__tl;							\
									\
	for (obj = be_list_head(head);					\
	     obj != NULL &&						\
	     ((void)(__tl = be_list_next(head, obj)), true);		\
	     obj = __tl)

#define m0_be_list_endfor ;(void)__tl; } while (0)

#define m0_be_for(name, head, obj) m0_be_list_for(& name ## _tl, head, obj)

#define m0_be_endfor m0_be_list_endfor

static void check(struct m0_be_list *list, struct m0_be_seg *seg)
{
	struct test *test;
	int expected[] = { 5, 8, 6, 4, 1, 3 };
	int i = 0;

	m0_be_list_init(list, &test_tl, seg);

	m0_be_for(test, list, test) {
		M0_UT_ASSERT(i < ARRAY_SIZE(expected));
		M0_UT_ASSERT(expected[i++] == test->t_payload);
	} m0_be_endfor;
}

M0_UNUSED static void print(struct m0_be_list *list)
{
	struct test *test;

	M0_LOG(M0_DEBUG, "----------");
	m0_be_for(test, list, test) {
		M0_LOG(M0_DEBUG, "-- %d", test->t_payload);
	} m0_be_endfor;
}

#undef M0_TRACE_SUBSYSTEM
