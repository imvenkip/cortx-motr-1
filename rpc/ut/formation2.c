#include "lib/ut.h"
#include "lib/mutex.h"
#include "lib/timer.h"
#include "lib/memory.h"
#include "rpc/formation2.h"
#include "rpc/rpc2.h"
#include "rpc/packet.h"

static int frm_ut_init(void);
static int frm_ut_fini(void);
static void frm_init_test(void);
static void frm_fini_test(void);
static void frm_enq_item_test(void);
static void frm_enqued_items_are_sorted_by_deadline(void);

const struct c2_test_suite frm_ut = {
	.ts_name = "formation-ut",
	.ts_init = frm_ut_init,
	.ts_fini = frm_ut_fini,
	.ts_tests = {
		{ "frm-init",     frm_init_test},
		{ "items-sorted", frm_enqued_items_are_sorted_by_deadline},
		{ "frm-enq-item", frm_enq_item_test},
		{ "frm-fini",     frm_fini_test},
		{ NULL,           NULL         }
	}
};

static struct c2_rpc_frm             frm;
static struct c2_rpc_frm_constraints constraints;
static struct c2_rpc_machine         rmachine;
static struct c2_rpc_chan            rchan;
static struct c2_rpc_session         session;
static struct c2_rpc_item            items[FRMQ_NR_QUEUES];
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

static int pcount = 0;
static int bound_item_count = 0;

static bool packet_ready(struct c2_rpc_packet  *p,
			 struct c2_rpc_machine *machine,
			 struct c2_rpc_chan    *rchanp)
{
	C2_UT_ASSERT(machine == &rmachine);
	C2_UT_ASSERT(rchanp == &rchan);
	++pcount;
	c2_rpc_packet_remove_all_items(p);
	c2_rpc_packet_fini(p);
	c2_free(p);
	return true;
}
static bool frm_item_bind(struct c2_rpc_item *item)
{
	item->ri_slot_refs[0].sr_slot = &slot;
	++bound_item_count;
	return true;
}
static bool frm_item_bind_on_second_turn(struct c2_rpc_item *item)
{
	static bool first_time = true;

	if (first_time) {
		first_time = false;
		return false;
	}
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
void frm_itemq_remove(struct c2_rpc_frm *frm, struct c2_rpc_item *item);

static void frm_enqued_items_are_sorted_by_deadline(void)
{
	struct c2_rpc_item *list;
	struct c2_rpc_item *item;
	uint64_t            seed;
	uint64_t            max;

	int                 i;
	enum { N = 100 };

	C2_ALLOC_ARR(list, N);
	C2_UT_ASSERT(list != NULL);

	frm.f_constraints.fc_max_nr_bytes_accumulated = ~0ULL;
	max = 2000;
	seed = (uint64_t)c2_time_now();
	for (i = 0; i < N; ++i) {
		item              = &list[i];
		item->ri_deadline = c2_time_from_now(c2_rnd(max, &seed) + 1000, 0);
		item->ri_type     = &twoway_item_type;
		item->ri_prio     = 2;
		c2_rpc_frm_enq_item(&frm, item);
	}
	C2_UT_ASSERT(frm.f_nr_items == N);
	for (i = 0; i < N; i++) {
		frm_itemq_remove(&frm, &list[i]);
	}
	C2_UT_ASSERT(frm.f_nr_items == 0);
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
static void frm_fini_test(void)
{
	c2_rpc_frm_fini(&frm);
	C2_UT_ASSERT(frm.f_state == FRM_UNINITIALISED);
}
