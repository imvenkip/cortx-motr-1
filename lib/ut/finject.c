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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 03/26/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/finject.h"


#ifdef ENABLE_FAULT_INJECTION

static const char test_tag[] = "test_fp";

enum {
	STATE_DATA_MAGIC = 0xfa57ccd0,
	TEST_COUNT       = 100,
	N                = 7,
	M                = 4,
	P                = 30,
	P_ERROR          = 15,
};

typedef bool (*target_func_t)(void);

static bool target_func_delayed_registration(void)
{
	return C2_FI_ENABLED(test_tag);
}

static bool target_func_fpoint_types(void)
{
	return C2_FI_ENABLED(test_tag);
}

struct state_data {
	uint32_t magic;
	uint32_t n;
};

/* Returns true and false alternately */
static bool state_func(void *data)
{
	struct state_data *d = data;

	C2_PRE(data != NULL && d->magic == STATE_DATA_MAGIC &&
		(d->n == 0 || d->n == 1));

	d->n = d->n ^ 1;

	return d->n;
}

static void disable_fp(const char *func_name, const char *tag_name,
		       target_func_t target_func)
{
	/* disable FP and check that it's really OFF */
	c2_fi_disable(func_name, tag_name);
	C2_UT_ASSERT(!target_func());
}

static void test_enable_always(const char *func_name)
{
	int i;

	/* check that FP is always ON after simple "enable" */
	c2_fi_enable(func_name, test_tag);
	for (i = 1; i <= TEST_COUNT; ++i)
		C2_UT_ASSERT(target_func_fpoint_types());

	disable_fp(func_name, test_tag, target_func_fpoint_types);
}

static void test_enable_once(const char *func_name)
{
	int i;

	/* check that FP is ON only once after "enable_once" */
	c2_fi_enable_once(func_name, test_tag);
	for (i = 1; i <= TEST_COUNT; ++i)
		if (i == 1) /* first check, FP should be ON */
			C2_UT_ASSERT(target_func_fpoint_types());
		else        /* subsequent checks, FP should be OFF */
			C2_UT_ASSERT(!target_func_fpoint_types());

	disable_fp(func_name, test_tag, target_func_fpoint_types);
}

static void test_enable_each_nth_time(const char *func_name)
{
	int i;

	/* check that FP is ON each N-th time after "enable_each_nth_time" */
	c2_fi_enable_each_nth_time(func_name, test_tag, N);
	for (i = 1; i <= TEST_COUNT; ++i)
		if (i % N == 0) /* N-th check, FP should be ON */
			C2_UT_ASSERT(target_func_fpoint_types());
		else            /* all other checks, FP should be OFF */
			C2_UT_ASSERT(!target_func_fpoint_types());

	disable_fp(func_name, test_tag, target_func_fpoint_types);
}

static void test_enable_off_n_on_m(const char *func_name)
{
	int i;

	/*
	 * check that FP is OFF N times and ON M times after "enable_off_n_on_m"
	 */
	c2_fi_enable_off_n_on_m(func_name, test_tag, N, M);
	for (i = 0; i < TEST_COUNT; ++i) {
		uint32_t check_num = i % (N + M);

		if (check_num < N) /* FP should be OFF  */
			C2_UT_ASSERT(!target_func_fpoint_types());
		else               /* FP should be ON */
			C2_UT_ASSERT(target_func_fpoint_types());
	}

	disable_fp(func_name, test_tag, target_func_fpoint_types);
}

static void test_enable_random(const char *func_name)
{
	int      i;
	uint32_t on_count;

	/*
	 * check that FP is ON approximately P percents of times after
	 * "enable_random" including P_ERROR error
	 */
	c2_fi_enable_random(func_name, test_tag, P);
	for (i = 0, on_count = 0; i < TEST_COUNT; ++i)
		if (target_func_fpoint_types())
			on_count++;
	C2_UT_ASSERT(on_count >= P - P_ERROR && on_count <= P + P_ERROR);

	disable_fp(func_name, test_tag, target_func_fpoint_types);
}

static void test_enable_func(const char *func_name)
{
	int i;

	struct state_data sdata = {
		.magic = STATE_DATA_MAGIC,
		.n     = 0,
	};

	/*
	 * check that FP's ON/OFF state is really controlled by our custom func
	 */
	c2_fi_enable_func(func_name, test_tag, state_func, &sdata);
	for (i = 0; i < TEST_COUNT; ++i)
		if (i % 2 == 0) /* when i is even FP should be ON */
			C2_UT_ASSERT(target_func_fpoint_types());
		else            /* when i is odd FP should be OFF */
			C2_UT_ASSERT(!target_func_fpoint_types());

	disable_fp(func_name, test_tag, target_func_fpoint_types);
}

static void test_fpoint_types(void)
{
	const char *func_name = "target_func_fpoint_types";

	/* register FP on first run and check that it disabled initially */
	C2_UT_ASSERT(!target_func_fpoint_types());

	test_enable_always(func_name);
	test_enable_once(func_name);
	test_enable_each_nth_time(func_name);
	test_enable_off_n_on_m(func_name);
	test_enable_random(func_name);
	test_enable_func(func_name);
}

static void test_delayed_registration(void)
{
	/* enable FP before it's registered */
	c2_fi_enable("target_func_delayed_registration", test_tag);

	/* check that FP is enabled on first run */
	C2_UT_ASSERT(target_func_delayed_registration());
}

static void test_enable_disable(void)
{
	int i;

	/* manually create fault point */
	static struct c2_fi_fault_point fp = {
		.fp_state    = NULL,
		.fp_module   = "UNKNOWN",
		.fp_file     = __FILE__,
		.fp_line_num = __LINE__,
		.fp_func     = __func__,
		.fp_tag      = test_tag,
	};

	enum {
		TEST_COUNT = 10,
	};

	c2_fi_register(&fp);
	C2_UT_ASSERT(fp.fp_state != NULL);

	/* check that it's disabled initially */
	C2_UT_ASSERT(!c2_fi_enabled(fp.fp_state));

	/* check that FP is really ON after c2_fi_enable() */
	c2_fi_enable(__func__, test_tag);
	C2_UT_ASSERT(c2_fi_enabled(fp.fp_state));

	/* check that FP is really OFF after c2_fi_disable() */
	c2_fi_disable(__func__, test_tag);
	C2_UT_ASSERT(!c2_fi_enabled(fp.fp_state));

	/*
	 * check that there is no harm to enable FP several times in a row
	 * without disabling it
	 */
	for (i = 0; i < TEST_COUNT; ++i) {
		c2_fi_enable(__func__, test_tag);
		C2_UT_ASSERT(c2_fi_enabled(fp.fp_state));
	}

	/*
	 * check that there is no harm to disable FP several times in a row
	 * without enabling it
	 */
	for (i = 0; i < TEST_COUNT; ++i) {
		c2_fi_disable(__func__, test_tag);
		C2_UT_ASSERT(!c2_fi_enabled(fp.fp_state));
	}
}

void test_finject(void)
{
	test_enable_disable();
	test_delayed_registration();
	test_fpoint_types();
}
C2_EXPORTED(test_finject);

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
