#include "lib/ut.h"
#include "lib/mutex.h"
#include "lib/timer.h"
#include "lib/memory.h"
#include "lib/misc.h"
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

enum {
	STACK_SIZE = 100,
};

static struct c2_rpc_packet *packet_stack[STACK_SIZE];
static int top = 0;

void packet_push(struct c2_rpc_packet *p)
{
	C2_UT_ASSERT(p != NULL);
	C2_UT_ASSERT(top < STACK_SIZE - 1);
	packet_stack[top] = p;
	++top;
}

struct c2_rpc_packet *packet_pop(void)
{
	C2_UT_ASSERT(top > 0 && top < STACK_SIZE);
	--top;
	return packet_stack[top];
}

bool packet_stack_is_empty(void)
{
	return top == 0;
}

bool packet_ready_called;
bool item_bind_called;

void reset_flags(void)
{
	packet_ready_called = item_bind_called = false;
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
	item->ri_slot_refs[0].sr_slot = &slot;
	item_bind_called = true;
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
	TIMEDOUT  = 1,
	WAITING   = 2,
	NEVER = 3,
	BOUND     = 1,
	UNBOUND   = 2,
	ONE_WAY   = 3,
};

uint64_t timeout; /* nano seconds */
void set_timeout(uint64_t milli)
{
	timeout = milli * 1000 * 1000;
}
struct c2_rpc_item *new_item(int deadline, int kind)
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

void check_frm(enum frm_state state, uint64_t nr_items, uint64_t nr_packets)
{
	C2_UT_ASSERT(frm.f_state == state &&
		     frm.f_nr_items == nr_items &&
		     frm.f_nr_packets_enqed == nr_packets);
}

void discard_packet(struct c2_rpc_packet *p)
{
	c2_rpc_packet_remove_all_items(p);
	c2_rpc_packet_fini(p);
	c2_free(p);
}

static void frm_test1(void)
{
	/*
	 * Timedout item triggers immediate formation.
	 * Waiting item do not trigger immediate formation, but they are
	 * formed once deadline is passed.
	 */
	struct c2_rpc_item   *item;
	struct c2_rpc_packet *p;

	void perform_test(int deadline, int kind)
	{
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
		p = packet_pop();
		C2_UT_ASSERT(packet_stack_is_empty());
		C2_UT_ASSERT(c2_rpc_packet_is_carrying_item(p, item));
		check_frm(FRM_BUSY, 0, 1);
		c2_rpc_frm_packet_done(p);
		discard_packet(p);
		check_frm(FRM_IDLE, 0, 0);
		c2_free(item);
	}
	/* Do not let formation trigger because of size limit */
	frm.f_constraints.fc_max_nr_bytes_accumulated = ~0;

	perform_test(TIMEDOUT, BOUND);
	perform_test(TIMEDOUT, UNBOUND);
	perform_test(TIMEDOUT, ONE_WAY);
	perform_test(WAITING,  BOUND);
	perform_test(WAITING,  UNBOUND);
	perform_test(WAITING,  ONE_WAY);
}

static void frm_test2(void)
{
	/* formation triggers when accumulated bytes exceed limit */
	enum { N = 4 };
	struct c2_rpc_item   *items[N];
	struct c2_rpc_packet *p;
	int                   i;
	c2_bcount_t           item_size;

	set_timeout(999);
	for (i = 0; i < N; ++i)
		items[i] = new_item(WAITING, BOUND);
	item_size = c2_rpc_item_size(items[0]);
	frm.f_constraints.fc_max_packet_size = ~0; /* include all ready items */
	frm.f_constraints.fc_max_nr_bytes_accumulated =
		(N - 1) * item_size + item_size / 2;

	reset_flags();
	for (i = 0; i < N - 1; ++i) {
		c2_rpc_frm_enq_item(&frm, items[i]);
		C2_UT_ASSERT(!packet_ready_called && !item_bind_called);
		check_frm(FRM_BUSY, i + 1, 0);
	}
	c2_rpc_frm_enq_item(&frm, items[N - 1]);
	C2_UT_ASSERT(packet_ready_called && !item_bind_called);
	check_frm(FRM_BUSY, 0, 1);

	p = packet_pop();
	C2_UT_ASSERT(packet_stack_is_empty());
	for (i = 0; i < N; ++i)
		C2_UT_ASSERT(c2_rpc_packet_is_carrying_item(p, items[0]));

	c2_rpc_frm_packet_done(p);
	discard_packet(p);
	check_frm(FRM_IDLE, 0, 0);

	for (i = 0; i < N; ++i)
		c2_free(items[i]);
}

#if 0
static void frm_enqued_items_are_sorted_by_deadline(void)
{
	struct c2_rpc_item *list;
	struct c2_rpc_item *item;
	uint64_t            seed;
	uint64_t            max;
	c2_bcount_t         saved;
	int                 i;
	enum { N = 100 };

	C2_ALLOC_ARR(list, N);
	C2_UT_ASSERT(list != NULL);

	saved = frm.f_constraints.fc_max_nr_bytes_accumulated;
	frm.f_constraints.fc_max_nr_bytes_accumulated = ~0ULL;
	max = 2000;
	seed = (uint64_t)c2_time_now();
	for (i = 0; i < N; ++i) {
		item              = &list[i];
		item->ri_deadline = c2_time_from_now(c2_rnd(max, &seed) + 1000,
						     0);
		item->ri_type     = &twoway_item_type;
		item->ri_prio     = 2;
		item->ri_session  = &session;
		c2_rpc_frm_enq_item(&frm, item);
	}
	C2_UT_ASSERT(frm.f_nr_items == N);

	/* make frm to pack all items */
	frm.f_constraints.fc_max_nr_bytes_accumulated = 0;
	pcount = 0;
	c2_rpc_frm_run_formation(&frm);
	C2_UT_ASSERT(frm.f_nr_items == 0);
	C2_UT_ASSERT(bound_item_count == N);
	C2_UT_ASSERT(pcount > 0);
	pcount = 0;
	bound_item_count = 0;
	frm.f_constraints.fc_max_nr_bytes_accumulated = saved;
}

static void frm_enq_item_test(void)
{
	struct c2_rpc_item *item;
	int                 i;
	struct test {
		c2_time_t     deadline;
		bool          assign_slot;
		bool          oneway;
		struct c2_tl *result;
	} tests[FRMQ_NR_QUEUES] = {
	    { c2_time(0, 0), true,  false, NULL},
	    { c2_time(0, 0), false, false, NULL},
	    { c2_time(0, 0), false, true,  NULL},
	    { C2_TIME_NEVER, true,  false, &frm.f_itemq[FRMQ_WAITING_BOUND]},
	    { C2_TIME_NEVER, false, false, &frm.f_itemq[FRMQ_WAITING_UNBOUND]},
	    { C2_TIME_NEVER, false, true,  &frm.f_itemq[FRMQ_WAITING_ONE_WAY]},
	};
	struct test *test;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		item = &items[i];
		test = &tests[i];
		item->ri_deadline = test->deadline;
		item->ri_slot_refs[0].sr_slot = test->assign_slot ? &slot
								  : NULL;
		item->ri_type = test->oneway ? &oneway_item_type
					     : &twoway_item_type;
		item->ri_session = &session;
		if (i != ARRAY_SIZE(tests) - 1) {
			c2_rpc_frm_enq_item(&frm, item);
			C2_UT_ASSERT(item->ri_itemq == test->result);
		}
	}
	C2_UT_ASSERT(frm.f_state == FRM_BUSY);
	C2_UT_ASSERT(frm.f_nr_items == 2);
	C2_UT_ASSERT(frm.f_nr_bytes_accumulated == 20);
	C2_UT_ASSERT(pcount == 3);
	C2_UT_ASSERT(bound_item_count == 1);

	/* Check whether packet is formed when accumulated bytes exceed
	   limit */
	frm.f_constraints.fc_max_nr_bytes_accumulated = 30;
	c2_rpc_frm_enq_item(&frm, &items[FRMQ_NR_QUEUES - 1]);

	C2_UT_ASSERT(frm.f_state == FRM_IDLE);
	C2_UT_ASSERT(frm.f_nr_items == 0);
	C2_UT_ASSERT(frm.f_nr_bytes_accumulated == 0);
	C2_UT_ASSERT(pcount == 4);
	C2_UT_ASSERT(bound_item_count == 2);

	/* Check whether a timedout item stays in TIMEDOUT_UNBOUND queue when
	   there is no slot to bind the item */
	frm_ops.fo_item_bind = frm_item_bind_on_second_turn;
	C2_ALLOC_PTR(item);
	C2_ASSERT(item != NULL);
	item->ri_deadline             = c2_time(0, 0);
	item->ri_type                 = &twoway_item_type;
	item->ri_slot_refs[0].sr_slot = NULL;
	item->ri_itemq                = NULL;
	item->ri_session              = &session;

	c2_rpc_frm_enq_item(&frm, item);
	C2_UT_ASSERT(frm.f_state == FRM_BUSY);
	C2_UT_ASSERT(frm.f_nr_items == 1);
	C2_UT_ASSERT(frm.f_nr_bytes_accumulated == 10);
	C2_UT_ASSERT(item->ri_itemq != NULL &&
		     item->ri_itemq == &frm.f_itemq[FRMQ_TIMEDOUT_UNBOUND]);

	c2_rpc_frm_run_formation(&frm);

	C2_UT_ASSERT(frm.f_state == FRM_IDLE);
	C2_UT_ASSERT(frm.f_nr_items == 0);
	C2_UT_ASSERT(frm.f_nr_bytes_accumulated == 0);
	C2_UT_ASSERT(pcount == 5);
	C2_UT_ASSERT(item->ri_itemq == NULL);
	c2_free(item);

/*
	frm_ops.fo_item_bind = frm_item_bind;
	C2_ALLOC_PTR(item);
	C2_UT_ASSERT(item != NULL);
	item->ri_deadline             = c2_time_from_now(1, 0);
	item->ri_type                 = &twoway_item_type;
	item->ri_slot_refs[0].sr_slot = NULL;
	item->ri_itemq                = NULL;
	item->ri_session              = &session;

	c2_rpc_frm_enq_item(&frm, item);
	C2_UT_ASSERT(frm.f_state == FRM_BUSY);
	C2_UT_ASSERT(frm.f_nr_items == 1);
	C2_UT_ASSERT(frm.f_nr_bytes_accumulated == 10);
	C2_UT_ASSERT(item->ri_itemq != NULL &&
		     item->ri_itemq == &frm.f_itemq[FRMQ_WAITING_UNBOUND]);

	c2_nanosleep(c2_time(2, 0), NULL);

	C2_UT_ASSERT(frm.f_state == FRM_IDLE);
	C2_UT_ASSERT(frm.f_nr_items == 0);
	C2_UT_ASSERT(frm.f_nr_bytes_accumulated == 0);
	C2_UT_ASSERT(pcount == 6);
	C2_UT_ASSERT(item->ri_itemq == NULL);
	c2_free(item);
*/
}
#endif
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
		{ "frm-fini",     frm_fini_test},
		{ NULL,           NULL         }
	}
};
