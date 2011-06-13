/* -*- C -*- */

#include "lib/types.h"            /* uint64_t */

#include "lib/user_space/assert.h"
#include "lib/ut.h"
#include "lib/ub.h"
#include "rm/rm.h"

#include "rings.h"
/**
   @addtogroup rm

   @{
 */

/**
   @name Use cases and unit tests
 */

/** @{ */


static struct c2_rm_domain dom;

/*
 * A simple resource type for testing.
 */

static struct rings {
	struct c2_rm_resource rs_resource;
	uint64_t              rs_id;
} R;

/* from http://en.wikipedia.org/wiki/Rings_of_Power */
enum {
	/* Three Rings for the Elven-kings under the sky... */
	/* Narya, the Ring of Fire (set with a ruby) */
	NARYA = 1 << 0,
	/* Nenya, the Ring of Water or Ring of Adamant (made of mithril and set
	   with a "white stone") */
	NENYA = 1 << 1,
	/* and Vilya, the Ring of Air, the "mightiest of the Three" (made of
	   gold and set with a sapphire)*/
	VILYA = 1 << 2,

	/* Seven for the Dwarf-lords in their halls of stone... */
	/* Ring of Durin */
	DURIN = 1 << 3,
	/*Ring of Thror */
	THROR = 1 << 4,
	/* Unnamed gnome rings... */
	GR_2  = 1 << 5,
	GR_3  = 1 << 6,
	GR_4  = 1 << 7,
	GR_5  = 1 << 8,
	GR_6  = 1 << 9,

	/* Nine for Mortal Men doomed to die... */
	/* Witch-king of Angmar */
	ANGMAR = 1 << 10,
	/* Khamul */
	KHAMUL = 1 << 11,
	/* unnamed man rings... */
	MR_2   = 1 << 12,
	MR_3   = 1 << 13,
	MR_4   = 1 << 14,
	MR_5   = 1 << 15,
	MR_6   = 1 << 16,
	MR_7   = 1 << 17,
	MR_8   = 1 << 18,

	/* One for the Dark Lord on his dark throne */
	THE_ONE = 1 << 19,

	ALLRINGS = NARYA | NENYA | VILYA | DURIN | THROR | GR_2 |
	GR_3 | GR_4 | GR_5 | GR_6 | ANGMAR | KHAMUL | MR_2 | MR_3 | MR_4 |
	MR_5 | MR_6 | MR_7 | MR_8 | THE_ONE

};

static struct c2_rm_right everything;
static struct c2_rm_owner Sauron; /* owns all rings initially. */
static struct c2_rm_owner elves;
static struct c2_rm_owner dwarves;
static struct c2_rm_owner men;

static int                   result;
static struct c2_rm_incoming in;
static struct c2_rm_incoming inother;

static struct c2_rm_resource_type rt = {
	.rt_ops  = &rings_rtype_ops,
	.rt_name = "rm ut resource type",
	.rt_id   = 1
};

static void rm_init(void)
{
	c2_rm_domain_init(&dom);
	c2_rm_type_register(&dom, &rt);
	c2_rm_resource_add(&rt, &R.rs_resource);
	c2_rm_right_init(&everything);
	everything.ri_ops = &rings_right_ops;
	everything.ri_datum = ALLRINGS;
	Sauron.ro_state = ROS_FINAL;
	c2_rm_owner_init_with(&Sauron, &R.rs_resource, &everything);
	elves.ro_state = ROS_FINAL;
	c2_rm_owner_init(&elves, &R.rs_resource);
	dwarves.ro_state = ROS_FINAL;
	c2_rm_owner_init(&dwarves, &R.rs_resource);
	men.ro_state = ROS_FINAL;
	c2_rm_owner_init(&men, &R.rs_resource);
	c2_rm_incoming_init(&in);
	c2_rm_incoming_init(&inother);
}

static void rm_fini(void)
{
	c2_rm_incoming_fini(&inother);
	c2_rm_incoming_fini(&in);
	men.ro_state = ROS_FINAL;
	c2_rm_owner_fini(&men);
	dwarves.ro_state = ROS_FINAL;
	c2_rm_owner_fini(&dwarves);
	elves.ro_state = ROS_FINAL;
	c2_rm_owner_fini(&elves);
	Sauron.ro_state = ROS_FINAL;
	c2_list_del(&everything.ri_linkage);
	c2_rm_owner_fini(&Sauron);
	c2_rm_right_fini(&everything);
	c2_rm_resource_del(&R.rs_resource);
	c2_rm_type_deregister(&rt);
	c2_rm_domain_fini(&dom);
}

static int rm_reset(void)
{
	return 0;
}

/**
   <b>Basic infrastructure setup.</b>
 */
static void basic_test(void)
{
	rm_init();
	rm_fini();
}

/**
   <b>right_get 0.</b>

   Simple get a right and immediately release it.
 */
static void right_get_test0(void)
{
	rt.rt_id = 1;
	rm_init();

	c2_chan_init(&in.rin_signal);
	c2_rm_right_init(&in.rin_want);
	in.rin_state = RI_INITIALISED;
	in.rin_owner = &Sauron;
	in.rin_priority = 0;
	in.rin_ops = &rings_incoming_ops;
	in.rin_want.ri_ops = &rings_right_ops;
	in.rin_type = RIT_LOCAL;
	in.rin_policy = RIP_NONE;
	in.rin_flags |= RIF_LOCAL_WAIT;

	in.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &in);
	C2_ASSERT(result == 0);
	C2_ASSERT(in.rin_state == RI_SUCCESS);

	c2_rm_right_put(&in);
	c2_list_del(&in.rin_want.ri_linkage);
	c2_rm_right_fini(&in.rin_want);
	c2_chan_fini(&in.rin_signal);

	rm_fini();
}

/**
   <b>right_get 1.</b>

   Get a right and then get a non-conflicting right.
 */
static void right_get_test1(void)
{

	rt.rt_id = 1;
	rm_init();

	c2_chan_init(&in.rin_signal);
	c2_rm_right_init(&in.rin_want);
	in.rin_state = RI_INITIALISED;
	in.rin_owner = &Sauron;
	in.rin_priority = 0;
	in.rin_ops = &rings_incoming_ops;
	in.rin_want.ri_ops = &rings_right_ops;
	in.rin_type = RIT_LOCAL;
	in.rin_policy = RIP_INPLACE;
	in.rin_flags |= RIF_LOCAL_WAIT;

	in.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &in);
	C2_ASSERT(result == 0);

	c2_chan_init(&inother.rin_signal);
	c2_rm_right_init(&inother.rin_want);
	inother.rin_state = RI_INITIALISED;
	inother.rin_owner = &Sauron;
	inother.rin_priority = 0;
	inother.rin_ops = &rings_incoming_ops;
	inother.rin_want.ri_ops = &rings_right_ops;
	inother.rin_type = RIT_LOCAL;
	inother.rin_policy = RIP_INPLACE;
	inother.rin_flags |= RIF_LOCAL_WAIT;

	inother.rin_want.ri_datum = KHAMUL;
	result = c2_rm_right_get_wait(&Sauron, &inother);
	C2_ASSERT(result == 0);

	c2_rm_right_put(&in);
	c2_list_del(&in.rin_want.ri_linkage);
	c2_rm_right_fini(&in.rin_want);
	c2_chan_fini(&in.rin_signal);

	c2_rm_right_put(&inother);
	c2_list_del(&inother.rin_want.ri_linkage);
	c2_rm_right_fini(&inother.rin_want);
	c2_chan_fini(&inother.rin_signal);


	rm_fini();
}

/**
   <b>right_get 2.</b>

   Get a right and get a conflicting right. This should succeed, because local
   requests do not conflict by default.

   @see RIF_LOCAL_WAIT
 */
static void right_get_test2(void)
{

	rt.rt_id = 1;
	rm_init();

	c2_chan_init(&in.rin_signal);
	c2_rm_right_init(&in.rin_want);
	in.rin_state = RI_INITIALISED;
	in.rin_owner = &Sauron;
	in.rin_priority = 0;
	in.rin_ops = &rings_incoming_ops;
	in.rin_want.ri_ops = &rings_right_ops;
	in.rin_type = RIT_LOCAL;
	in.rin_policy = RIP_INPLACE;
	in.rin_flags |= RIF_LOCAL_WAIT;

	in.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &in);
	C2_ASSERT(result == 0);

	c2_chan_init(&inother.rin_signal);
	c2_rm_right_init(&inother.rin_want);
	inother.rin_state = RI_INITIALISED;
	inother.rin_owner = &Sauron;
	inother.rin_priority = 0;
	inother.rin_ops = &rings_incoming_ops;
	inother.rin_want.ri_ops = &rings_right_ops;
	inother.rin_type = RIT_LOCAL;
	inother.rin_policy = RIP_INPLACE;
	inother.rin_flags |= RIF_LOCAL_WAIT;

	inother.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &inother);
	C2_ASSERT(result == 0);

	c2_rm_right_put(&in);
	c2_list_del(&in.rin_want.ri_linkage);
	c2_rm_right_fini(&in.rin_want);
	c2_chan_fini(&in.rin_signal);

	c2_rm_right_put(&inother);
	c2_list_del(&inother.rin_want.ri_linkage);
	c2_rm_right_fini(&inother.rin_want);
	c2_chan_fini(&inother.rin_signal);

	rm_fini();
}

/**
   <b>right_get 3.</b>

   Get a right and get a conflicting right with RIF_LOCAL_TRY flag. This should
   fail.

   @see RIF_LOCAL_TRY
 */
static void right_get_test3(void)
{
	rt.rt_id = 1;
	rm_init();

	c2_chan_init(&in.rin_signal);
	c2_rm_right_init(&in.rin_want);
	in.rin_state = RI_INITIALISED;
	in.rin_owner = &Sauron;
	in.rin_priority = 0;
	in.rin_ops = &rings_incoming_ops;
	in.rin_want.ri_ops = &rings_right_ops;
	in.rin_type = RIT_LOCAL;
	in.rin_policy = RIP_INPLACE;

	in.rin_flags |= RIF_LOCAL_TRY;
	in.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &in);
	C2_ASSERT(result == 0);

	c2_chan_init(&inother.rin_signal);
	c2_rm_right_init(&inother.rin_want);
	inother.rin_state = RI_INITIALISED;
	inother.rin_owner = &Sauron;
	inother.rin_priority = 0;
	inother.rin_ops = &rings_incoming_ops;
	inother.rin_want.ri_ops = &rings_right_ops;
	inother.rin_type = RIT_LOAN;
	inother.rin_policy = RIP_INPLACE;

	inother.rin_want.ri_datum = NARYA;
	inother.rin_flags |= RIF_LOCAL_TRY;
	result = c2_rm_right_get_wait(&Sauron, &inother);
	C2_ASSERT(result == -EWOULDBLOCK);

	//c2_rm_right_put(&inother);
	c2_list_del(&inother.rin_want.ri_linkage);
	c2_rm_right_fini(&inother.rin_want);
	c2_chan_fini(&inother.rin_signal);

	c2_rm_right_put(&in);
	c2_list_del(&in.rin_want.ri_linkage);
	c2_rm_right_fini(&in.rin_want);
	c2_chan_fini(&in.rin_signal);

	rm_fini();
}

static struct c2_rm_domain server;
static struct c2_rm_domain client;
static struct rings RC0;

static struct c2_rm_resource_type rts = {
	.rt_ops  = &rings_rtype_ops,
	.rt_name = "rm ut resource type",
	.rt_id   = 1
};

static struct c2_rm_resource_type rtc = {
	.rt_ops  = &rings_rtype_ops,
	.rt_name = "rm ut resource type",
	.rt_id   = 1
};


static void rm_server_init(void)
{
	c2_rm_domain_init(&server);
	c2_rm_type_register(&server, &rts);
	c2_rm_resource_add(&rts, &R.rs_resource);
	c2_rm_right_init(&everything);
	everything.ri_ops = &rings_right_ops;
	everything.ri_datum = ALLRINGS;
	Sauron.ro_state = ROS_FINAL;
	c2_rm_owner_init_with(&Sauron, &R.rs_resource, &everything);
}

static void rm_server_fini(void)
{
	Sauron.ro_state = ROS_FINAL;
	c2_list_del(&everything.ri_linkage);
	c2_rm_owner_fini(&Sauron);
	c2_rm_right_fini(&everything);
	c2_rm_resource_del(&R.rs_resource);
	c2_rm_type_deregister(&rts);
	c2_rm_domain_fini(&server);
}

static void rm_client_init(void)
{
	c2_rm_domain_init(&client);
	c2_rm_type_register(&client, &rtc);
	c2_rm_resource_add(&rtc, &RC0.rs_resource);
	elves.ro_state = ROS_FINAL;
	c2_rm_owner_init(&elves, &RC0.rs_resource);
}

static void rm_client_fini(void)
{
	elves.ro_state = ROS_FINAL;
	c2_rm_owner_fini(&elves);
	c2_rm_resource_del(&RC0.rs_resource);
	c2_rm_type_deregister(&rtc);
	c2_rm_domain_fini(&client);
}

/**
   <b>Intent mode.</b>

   - setup two resource domains CLIENT and SERVER;

   - acquire a right on SERVER (on client's behalf) and return it to CLIENT;

   - release the right on CLIENT.
 */
static void intent_mode_test(void)
{
	rm_server_init();
	rm_client_init();

	c2_chan_init(&in.rin_signal);
	c2_rm_right_init(&in.rin_want);
	in.rin_state = RI_INITIALISED;
	in.rin_owner = &Sauron;
	in.rin_priority = 0;
	in.rin_ops = &rings_incoming_ops;
	in.rin_want.ri_ops = &rings_right_ops;
	in.rin_type = RIT_LOAN;
	in.rin_policy = RIP_INPLACE;

	in.rin_flags |= RIF_MAY_BORROW;
	in.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &in);
	C2_ASSERT(result == 0);

	rm_server_fini();
	rm_client_fini();
}

/**
   <b>WBC mode.</b>

   - setup two resource domains CLIENT and SERVER;

   - borrow a right from SERVER to client. Use it;

   - release the right.
 */
static void wbc_mode_test(void)
{
}

/**
   <b>Resource manager separate from resource provider.<b>
 */
static void separate_test(void)
{
}

/**
   <b>Network partitioning, leases.</b>
 */
static void lease_test(void)
{
}

/**
   <b>Optimistic concurrency.</b>
 */
static void optimist_test(void)
{
}

/**
   <b>A use case for each resource type.</b>
 */
static void resource_type_test(void)
{
}

/**
   <b>Use cases involving multiple resources acquired.</b>
 */
static void multiple_test(void)
{
}

/**
   <b>Cancellation.</b>

   - setup two resource domains CLIENT and SERVER;

   - borrow a right R from SERVER to CLIENT;

   - cancel R;

   - witness SERVER possesses R.
 */
static void cancel_test(void)
{
}

/**
   <b>Caching.</b>

   - setup two resource domains CLIENT and SERVER;

   - borrow a right R from SERVER to CLIENT;

   - release R;

   - get R again on CLIENT, witness it is found in cache.
 */
static void caching_test(void)
{
}

/**
   <b>Call-back.</b>

   - setup 3 resource domains: SERVER, CLIENT0 and CLIENT1;

   - borrow a right from SERVER to CLIENT0;

   - request a conflicting right to CLIENT1;

   - witness a REVOKE call-back to CLIENT0;

   - cancel the right on CLIENT0;

   - obtain the right on CLIENT1;

   - release the right on CLIENT1.
 */
static void callback_test(void)
{
}

/**
   <b>Hierarchy.</b>
 */
static void hierarchy_test(void)
{
}

/**
   <b>Extents and multiple readers-writers (scalability).</b>
 */
static void ext_multi_test(void)
{
}


const struct c2_test_suite rm_ut = {
	.ts_name = "librm-ut",
	.ts_init = rm_reset,
	.ts_fini = rm_reset,
	.ts_tests = {
		{ "basic", basic_test },
		{ "right_get0", right_get_test0 },
		{ "right_get1", right_get_test1 },
		{ "right_get2", right_get_test2 },
		{ "right_get3", right_get_test3 },
		{ "intent mode", intent_mode_test },
		{ "wbc", wbc_mode_test },
		{ "separate", separate_test },
		{ "lease", lease_test },
		{ "optimist", optimist_test },
		{ "resource type", resource_type_test },
		{ "multiple", multiple_test },
		{ "cancel", cancel_test },
		{ "caching", caching_test },
		{ "callback", callback_test },
		{ "hierarchy", hierarchy_test },
		{ "extents multiple", ext_multi_test },
		{ NULL, NULL }
	}
};

/** @} end of use cases and unit tests */

/*
 * UB
 */

enum {
	UB_ITER = 200000,
};

static void ub_init(void)
{
}

static void ub_fini(void)
{
}

struct c2_ub_set c2_rm_ub = {
	.us_name = "rm-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = NULL }
	}
};

/** @} end of rm group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
