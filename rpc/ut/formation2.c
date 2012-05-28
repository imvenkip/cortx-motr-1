#include "lib/ut.h"
#include "rpc/formation2.h"
#include "rpc/rpc2.h"

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

static void frm_init_test(void)
{
	c2_rpc_frm_init(&frm, &rmachine, constraints);
	C2_UT_ASSERT(frm.f_state == FRM_IDLE);
}

static struct c2_rpc_item items[FRMQ_NR_QUEUES];
static struct c2_rpc_slot slot;

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
		{ c2_time(0, 0), true,  false, &frm.f_itemq[FRMQ_TIMEDOUT_BOUND]},
		{ c2_time(0, 0), false, false, &frm.f_itemq[FRMQ_TIMEDOUT_UNBOUND]},
		{ c2_time(0, 0), false, true,  &frm.f_itemq[FRMQ_TIMEDOUT_ONE_WAY]},
		{ C2_TIME_NEVER, true,  false, &frm.f_itemq[FRMQ_WAITING_BOUND]},
		{ C2_TIME_NEVER, false, false, &frm.f_itemq[FRMQ_WAITING_UNBOUND]},
		{ C2_TIME_NEVER, false, true,  &frm.f_itemq[FRMQ_WAITING_ONE_WAY]},
	};
	struct test *test;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		item = &items[i];
		test = &tests[i];
		item->ri_deadline = test->deadline;
		item->ri_slot_refs[0].sr_slot = test->assign_slot ? &slot : NULL;
		item->ri_type = test->oneway ? &oneway_item_type : &twoway_item_type;
		c2_rpc_frm_enq_item(&frm, item);
		C2_UT_ASSERT(item->ri_itemq == test->result);
		C2_UT_ASSERT(frm.f_nr_items == i + 1);
		C2_UT_ASSERT(frm.f_state == FRM_BUSY);
	}
}
static void frm_fini_test(void)
{
	c2_rpc_frm_fini(&frm);
	C2_UT_ASSERT(frm.f_state == FRM_UNINITIALISED);
}
