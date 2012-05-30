#include "lib/ut.h"
#include "lib/memory.h"
#include "rpc/formation2.h"
#include "rpc/rpc2.h"
#include "rpc/packet.h"

static int frm_ut_init(void);
static int frm_ut_fini(void);
static void frm_init_test(void);
static void frm_fini_test(void);
static void frm_enq_item_test(void);

const struct c2_test_suite frm_ut = {
	.ts_name = "formation-ut",
	.ts_init = frm_ut_init,
	.ts_fini = frm_ut_fini,
	.ts_tests = {
		{ "frm-init",     frm_init_test},
		{ "frm-enq-item", frm_enq_item_test},
		{ "frm-fini",     frm_fini_test},
		{ NULL,           NULL         }
	}
};

static int frm_ut_init(void)
{
	return 0;
}
static int frm_ut_fini(void)
{
	return 0;
}

static struct c2_rpc_frm frm;
static struct c2_rpc_frm_constraints constraints;
static struct c2_rpc_machine rmachine;
static struct c2_rpc_session session;
static struct c2_rpc_item items[FRMQ_NR_QUEUES];
static struct c2_rpc_slot slot;

static int pcount = 0;
static int bound_item_count = 0;

static void packet_ready(struct c2_rpc_packet *p)
{
	++pcount;
	c2_rpc_packet_remove_all_items(p);
	c2_rpc_packet_fini(p);
	c2_free(p);
	return;
}
static bool frm_bind_item(struct c2_rpc_item *item)
{
	item->ri_slot_refs[0].sr_slot = &slot;
	++bound_item_count;
	return true;
}
static struct c2_rpc_frm_ops frm_ops = {
	.fo_packet_ready = packet_ready,
	.fo_bind_item    = frm_bind_item
};

static void frm_init_test(void)
{
	c2_rpc_frm_constraints_get_defaults(&constraints);
	c2_rpc_frm_init(&frm, &rmachine, constraints, &frm_ops);
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
	frm.f_constraints.fc_max_nr_bytes_accumulated = 30;
	c2_rpc_frm_enq_item(&frm, &items[FRMQ_NR_QUEUES - 1]);

	C2_UT_ASSERT(frm.f_state == FRM_IDLE);
	C2_UT_ASSERT(frm.f_nr_items == 0);
	C2_UT_ASSERT(frm.f_nr_bytes_accumulated == 0);
	C2_UT_ASSERT(pcount == 4);
	C2_UT_ASSERT(bound_item_count == 2);
}
static void frm_fini_test(void)
{
	c2_rpc_frm_fini(&frm);
	C2_UT_ASSERT(frm.f_state == FRM_UNINITIALISED);
}
