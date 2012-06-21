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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 05/25/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/ut.h"
#include "lib/mutex.h"
#include "lib/timer.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/finject.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_UT
#include "lib/trace.h"
#include "rpc/formation2.h"
#include "rpc/rpc2.h"
#include "rpc/packet.h"

static struct c2_rpc_frm             frm;
static struct c2_rpc_frm_constraints constraints;
static struct c2_rpc_machine         rmachine;
static struct c2_rpc_chan            rchan;
static struct c2_rpc_session         session;
static struct c2_rpc_slot            slot;

static int frm_ut_init(void)
{
	c2_mutex_init(&rmachine.rm_mutex);
	c2_mutex_lock(&rmachine.rm_mutex);
	return 0;
}

static int frm_ut_fini(void)
{
	c2_mutex_unlock(&rmachine.rm_mutex);
	c2_mutex_fini(&rmachine.rm_mutex);
	return 0;
}

enum { STACK_SIZE = 100 };

static struct c2_rpc_packet *packet_stack[STACK_SIZE];
static int top = 0;

static void packet_push(struct c2_rpc_packet *p)
{
	C2_UT_ASSERT(p != NULL);
	C2_UT_ASSERT(top < STACK_SIZE - 1);
	packet_stack[top] = p;
	++top;
}

static struct c2_rpc_packet *packet_pop(void)
{
	C2_UT_ASSERT(top > 0 && top < STACK_SIZE);
	--top;
	return packet_stack[top];
}

static bool packet_stack_is_empty(void)
{
	return top == 0;
}

static bool packet_ready_called;
static bool item_bind_called;
static int  item_bind_count;

static void reset_flags(void)
{
	packet_ready_called = item_bind_called = false;
	item_bind_count = 0;
}

static bool packet_ready(struct c2_rpc_packet  *p,
			 struct c2_rpc_machine *machine,
			 struct c2_rpc_chan    *rchanp)
{
	C2_UT_ASSERT(machine == &rmachine);
	C2_UT_ASSERT(rchanp == &rchan);
	packet_push(p);
	packet_ready_called = true;
	return true;
}

static bool frm_item_bind(struct c2_rpc_item *item)
{
	item_bind_called = true;
	++item_bind_count;

	if (C2_FI_ENABLED("slot_unavailable"))
		return false;

	item->ri_slot_refs[0].sr_slot = &slot;
	return true;
}

static struct c2_rpc_frm_ops frm_ops = {
	.fo_packet_ready = packet_ready,
	.fo_item_bind    = frm_item_bind
};

static void frm_init_test(void)
{
	c2_rpc_frm_constraints_get_defaults(&constraints);
	c2_rpc_frm_init(&frm, &rmachine, &rchan, constraints, &frm_ops);
	C2_UT_ASSERT(frm.f_state == FRM_IDLE);
}

static c2_bcount_t twoway_item_size(const struct c2_rpc_item *item)
{
	return 10;
}

static struct c2_rpc_item_type_ops twoway_item_type_ops = {
	.rito_item_size = twoway_item_size
};

static struct c2_rpc_item_type twoway_item_type = {
	.rit_flags = C2_RPC_ITEM_TYPE_REQUEST,
	.rit_ops   = &twoway_item_type_ops,
};

static c2_bcount_t oneway_item_size(const struct c2_rpc_item *item)
{
	return 20;
}

static struct c2_rpc_item_type_ops oneway_item_type_ops = {
	.rito_item_size = oneway_item_size
};

static struct c2_rpc_item_type oneway_item_type = {
	.rit_flags = C2_RPC_ITEM_TYPE_UNSOLICITED,
	.rit_ops   = &oneway_item_type_ops,
};

enum {
	TIMEDOUT = 1,
	WAITING  = 2,
	NEVER    = 3,

	BOUND    = 1,
	UNBOUND  = 2,
	ONE_WAY  = 3,
};

static uint64_t timeout; /* nano seconds */
static void set_timeout(uint64_t milli)
{
	timeout = milli * 1000 * 1000;
}

static struct c2_rpc_item *new_item(int deadline, int kind)
{
	struct c2_rpc_item *item;
	C2_UT_ASSERT(C2_IN(deadline, (TIMEDOUT, WAITING, NEVER)));
	C2_UT_ASSERT(C2_IN(kind,     (BOUND, UNBOUND, ONE_WAY)));

	C2_ALLOC_PTR(item);
	C2_UT_ASSERT(item != NULL);

	switch (deadline) {
	case TIMEDOUT:
		item->ri_deadline = 0;
		break;
	case NEVER:
		item->ri_deadline = ~0;
		break;
	case WAITING:
		item->ri_deadline = c2_time_from_now(0, timeout);
		break;
	}
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_type = kind == ONE_WAY ? &oneway_item_type :
					  &twoway_item_type;
	item->ri_slot_refs[0].sr_slot = kind == BOUND ? &slot : NULL;
	item->ri_session = &session;

	return item;
}

static void
check_frm(enum frm_state state, uint64_t nr_items, uint64_t nr_packets)
{
	C2_UT_ASSERT(frm.f_state == state &&
		     frm.f_nr_items == nr_items &&
		     frm.f_nr_packets_enqed == nr_packets);
}

static void discard_packet(struct c2_rpc_packet *p)
{
	c2_rpc_packet_remove_all_items(p);
	c2_rpc_packet_fini(p);
	c2_free(p);
}

static void check_ready_packet_has_item(struct c2_rpc_item *item)
{
	struct c2_rpc_packet *p;

	p = packet_pop();
	C2_UT_ASSERT(packet_stack_is_empty());
	C2_UT_ASSERT(c2_rpc_packet_is_carrying_item(p, item));
	check_frm(FRM_BUSY, 0, 1);
	c2_rpc_frm_packet_done(p);
	discard_packet(p);
	check_frm(FRM_IDLE, 0, 0);
}

static void frm_test1(void)
{
	/*
	 * Timedout item triggers immediate formation.
	 * Waiting item do not trigger immediate formation, but they are
	 * formed once deadline is passed.
	 */
	void perform_test(int deadline, int kind)
	{
		struct c2_rpc_item   *item;

		set_timeout(100);
		item = new_item(deadline, kind);
		reset_flags();
		c2_rpc_frm_enq_item(&frm, item);
		if (deadline == WAITING) {
			C2_UT_ASSERT(!packet_ready_called &&
				     !item_bind_called);
			check_frm(FRM_BUSY, 1, 0);
			c2_nanosleep(c2_time(0, 2 * timeout), NULL);
			c2_rpc_frm_run_formation(&frm);
		}
		C2_UT_ASSERT(packet_ready_called &&
			     equi(kind == UNBOUND, item_bind_called));
		check_ready_packet_has_item(item);
		c2_free(item);
	}
	C2_ENTRY();

	/* Do not let formation trigger because of size limit */
	frm.f_constraints.fc_max_nr_bytes_accumulated = ~0;

	perform_test(TIMEDOUT, BOUND);
	perform_test(TIMEDOUT, UNBOUND);
	perform_test(TIMEDOUT, ONE_WAY);
	perform_test(WAITING,  BOUND);
	perform_test(WAITING,  UNBOUND);
	perform_test(WAITING,  ONE_WAY);

	C2_LEAVE();
}

static void frm_test2(void)
{
	/* formation triggers when accumulated bytes exceed limit */

	void perform_test(int kind)
	{
		enum { N = 4 };
		struct c2_rpc_item   *items[N];
		struct c2_rpc_packet *p;
		int                   i;
		c2_bcount_t           item_size;

		for (i = 0; i < N; ++i)
			items[i] = new_item(WAITING, kind);
		item_size = c2_rpc_item_size(items[0]);
		/* include all ready items */
		frm.f_constraints.fc_max_packet_size = ~0;
		/*
		 * set fc_max_nr_bytes_accumulated such that, formation triggers
		 * when last item from items[] is enqued
		 */
		frm.f_constraints.fc_max_nr_bytes_accumulated =
			(N - 1) * item_size + item_size / 2;

		reset_flags();
		for (i = 0; i < N - 1; ++i) {
			c2_rpc_frm_enq_item(&frm, items[i]);
			C2_UT_ASSERT(!packet_ready_called &&
				     !item_bind_called);
			check_frm(FRM_BUSY, i + 1, 0);
		}
		c2_rpc_frm_enq_item(&frm, items[N - 1]);
		C2_UT_ASSERT(packet_ready_called &&
			     equi(kind == UNBOUND, item_bind_count == N));
		check_frm(FRM_BUSY, 0, 1);

		p = packet_pop();
		C2_UT_ASSERT(packet_stack_is_empty());
		for (i = 0; i < N; ++i)
			C2_UT_ASSERT(
				c2_rpc_packet_is_carrying_item(p, items[i]));

		c2_rpc_frm_packet_done(p);
		check_frm(FRM_IDLE, 0, 0);

		discard_packet(p);
		for (i = 0; i < N; ++i)
			c2_free(items[i]);
	}

	C2_ENTRY();

	set_timeout(999);

	perform_test(BOUND);
	perform_test(UNBOUND);
	perform_test(ONE_WAY);

	C2_LEAVE();
}

static void frm_test3(void)
{
	/* If max_nr_packets_enqed is reached, formation is stopped */
	struct c2_rpc_item   *item;
	uint64_t              saved;

	C2_ENTRY();


	item = new_item(TIMEDOUT, BOUND);
	saved = frm.f_constraints.fc_max_nr_packets_enqed;
	frm.f_constraints.fc_max_nr_packets_enqed = 0;
	reset_flags();
	c2_rpc_frm_enq_item(&frm, item);
	C2_UT_ASSERT(!packet_ready_called && !item_bind_called);
	check_frm(FRM_BUSY, 1, 0);

	frm.f_constraints.fc_max_nr_packets_enqed = saved;
	c2_rpc_frm_run_formation(&frm);
	C2_UT_ASSERT(packet_ready_called && !item_bind_called);

	check_ready_packet_has_item(item);
	c2_free(item);

	C2_LEAVE();
}

static void frm_test4(void)
{
	/* packet is not formed if no slot is available */
	struct c2_rpc_item   *item;

	C2_ENTRY();

	item = new_item(TIMEDOUT, UNBOUND);
	c2_fi_enable_once("frm_item_bind", "slot_unavailable");
	reset_flags();
	c2_rpc_frm_enq_item(&frm, item);
	C2_UT_ASSERT(!packet_ready_called && item_bind_called);
	check_frm(FRM_BUSY, 1, 0);

	c2_rpc_frm_run_formation(&frm);
	C2_UT_ASSERT(packet_ready_called && item_bind_called);

	check_ready_packet_has_item(item);
	c2_free(item);

	C2_LEAVE();
}

static void frm_do_test5(const int N, const int ITEMS_PER_PACKET)
{
	/* Multiple packets are formed if ready items don't fit in one packet */
	struct c2_rpc_item   *items[N];
	struct c2_rpc_packet *p;
	c2_bcount_t           saved_max_nr_bytes_acc;
	c2_bcount_t           saved_max_packet_size;
	int                   nr_packets;
	int                   i;

	C2_ENTRY("N: %d ITEMS_PER_PACKET: %d", N, ITEMS_PER_PACKET);

	for (i = 0; i < N; ++i)
		items[i] = new_item(WAITING, BOUND);

	saved_max_nr_bytes_acc = frm.f_constraints.fc_max_nr_bytes_accumulated;
	frm.f_constraints.fc_max_nr_bytes_accumulated = ~0;

	reset_flags();
	for (i = 0; i < N; ++i)
		c2_rpc_frm_enq_item(&frm, items[i]);

	C2_UT_ASSERT(!packet_ready_called && !item_bind_called);
	check_frm(FRM_BUSY, N, 0);

	saved_max_packet_size = frm.f_constraints.fc_max_packet_size;
	/* Each packet should carry ITEMS_PER_PACKET items */
	frm.f_constraints.fc_max_packet_size =
			ITEMS_PER_PACKET * c2_rpc_item_size(items[0]) +
			  C2_RPC_PACKET_OW_HEADER_SIZE;
	/* trigger formation so that all items are formed */
	frm.f_constraints.fc_max_nr_bytes_accumulated = 0;
	c2_rpc_frm_run_formation(&frm);
	nr_packets = N / ITEMS_PER_PACKET + (N % ITEMS_PER_PACKET != 0 ? 1 : 0);
	C2_UT_ASSERT(packet_ready_called && top == nr_packets);
	check_frm(FRM_BUSY, 0, nr_packets);

	for (i = 0; i < nr_packets; ++i) {
		p = packet_stack[i];
		if (N % ITEMS_PER_PACKET == 0 ||
		    i != nr_packets - 1)
			C2_UT_ASSERT(p->rp_nr_items == ITEMS_PER_PACKET);
		else
			C2_UT_ASSERT(p->rp_nr_items == N % ITEMS_PER_PACKET);
		(void)packet_pop();
		c2_rpc_frm_packet_done(p);
		discard_packet(p);
	}
	check_frm(FRM_IDLE, 0, 0);
	for (i = 0; i < N; ++i)
		c2_free(items[i]);
	frm.f_constraints.fc_max_packet_size = saved_max_packet_size;
	frm.f_constraints.fc_max_nr_bytes_accumulated = saved_max_nr_bytes_acc;
	C2_UT_ASSERT(packet_stack_is_empty());

	C2_LEAVE();
}

static void frm_test5(void)
{
	C2_ENTRY();
	frm_do_test5(7, 3);
	frm_do_test5(7, 6);
	frm_do_test5(8, 8);
	frm_do_test5(8, 2);
	frm_do_test5(4, 1);
	C2_LEAVE();
}

static void frm_test6(void)
{
	/* If packet allocation fails then packet is not formed and items
	   remain in frm */
	struct c2_rpc_item   *item;

	C2_ENTRY();

	reset_flags();
	item = new_item(TIMEDOUT, BOUND);

	c2_fi_enable_once("c2_alloc", "fail_allocation");

	c2_rpc_frm_enq_item(&frm, item);
	C2_UT_ASSERT(!packet_ready_called);
	check_frm(FRM_BUSY, 1, 0);

	/* this time allocation succeds */
	c2_rpc_frm_run_formation(&frm);
	C2_UT_ASSERT(packet_ready_called);
	check_frm(FRM_BUSY, 0, 1);

	check_ready_packet_has_item(item);
	c2_free(item);

	C2_LEAVE();
}

static void frm_test7(void)
{
	/* higher priority item is added to packet before lower priority */
	struct c2_rpc_item   *item1;
	struct c2_rpc_item   *item2;
	c2_bcount_t           saved_max_packet_size;
	int                   saved_max_nr_packets_enqed;

	C2_ENTRY();

	item1 = new_item(TIMEDOUT, BOUND);
	item2 = new_item(TIMEDOUT, BOUND);
	item1->ri_prio = C2_RPC_ITEM_PRIO_MIN;
	item2->ri_prio = C2_RPC_ITEM_PRIO_MAX;

	/* Only 1 item should be included per packet */
	saved_max_packet_size = frm.f_constraints.fc_max_packet_size;
	frm.f_constraints.fc_max_packet_size = c2_rpc_item_size(item1) +
			C2_RPC_PACKET_OW_HEADER_SIZE;

	saved_max_nr_packets_enqed = frm.f_constraints.fc_max_nr_packets_enqed;
	frm.f_constraints.fc_max_nr_packets_enqed = 0; /* disable formation */

	reset_flags();

	c2_rpc_frm_enq_item(&frm, item1);
	C2_UT_ASSERT(!packet_ready_called);
	check_frm(FRM_BUSY, 1, 0);

	c2_rpc_frm_enq_item(&frm, item2);
	C2_UT_ASSERT(!packet_ready_called);
	check_frm(FRM_BUSY, 2, 0);

	/* Enable formation */
	frm.f_constraints.fc_max_nr_packets_enqed = saved_max_nr_packets_enqed;
	c2_rpc_frm_run_formation(&frm);

	/* Two packets should be generated */
	C2_UT_ASSERT(packet_ready_called && top == 2);
	check_frm(FRM_BUSY, 0, 2);

	/* First packet should be carrying item2 because it has higher
	   priority
	 */
	C2_UT_ASSERT(c2_rpc_packet_is_carrying_item(packet_stack[0], item2));
	C2_UT_ASSERT(c2_rpc_packet_is_carrying_item(packet_stack[1], item1));

	c2_rpc_frm_packet_done(packet_stack[0]);
	c2_rpc_frm_packet_done(packet_stack[1]);

	discard_packet(packet_pop());
	discard_packet(packet_pop());
	C2_UT_ASSERT(packet_stack_is_empty());

	c2_free(item1);
	c2_free(item2);

	C2_LEAVE();
}

static void frm_fini_test(void)
{
	c2_rpc_frm_fini(&frm);
	C2_UT_ASSERT(frm.f_state == FRM_UNINITIALISED);
}

const struct c2_test_suite frm_ut = {
	.ts_name = "formation-ut",
	.ts_init = frm_ut_init,
	.ts_fini = frm_ut_fini,
	.ts_tests = {
		{ "frm-init",     frm_init_test},
		{ "frm-test1",    frm_test1    },
		{ "frm-test2",    frm_test2    },
		{ "frm-test3",    frm_test3    },
		{ "frm-test4",    frm_test4    },
		{ "frm-test5",    frm_test5    },
		{ "frm-test6",    frm_test6    },
		{ "frm-test7",    frm_test7    },
		{ "frm-fini",     frm_fini_test},
		{ NULL,           NULL         }
	}
};
