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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 01-Aug-2012
 */

#include "module/instance.h"    /* m0 */
#include "mero/init.h"          /* m0_init */
#include "lib/arith.h"          /* max_check */
#include "lib/types.h"          /* PRIu64 */
#include "lib/memory.h"
#include "lib/atomic.h"
#include "lib/errno.h"          /* ENOENT */
#include "lib/string.h"         /* strlen */
#include "lib/assert.h"
#include "lib/trace.h"          /* m0_console_printf */
#include "lib/list.h"
#include "lib/time.h"
#include "fop/fom_generic.h"
#include "lib/misc.h"           /* M0_IN() */
#include "ut/ut.h"
#include "ut/ut_internal.h"
#include "ut/cs_service.h"


/**
 * @addtogroup ut
 * @{
 */

/*
 * syslog(8) will trim leading spaces of each kernel log line, so we need to use
 * a non-space character at the beginning of each line to preserve formatting
 */
#ifdef __KERNEL__
#define LOG_PREFIX "."
#else
#define LOG_PREFIX
#endif

enum {
	SUITES_MAX = 4096,
};

struct m0_ut_ctx {
	struct m0_ut_cfg    ux_config;
	struct m0_atomic64  ux_asserts;
	unsigned            ux_used;
	struct m0_ut_suite *ux_suites[SUITES_MAX + 1];
};

struct m0_ut_entry {
	struct m0_list_link   ue_linkage;
	const char           *ue_suite_name;
	const char           *ue_test_name;
};

static struct m0_ut_ctx ctx = {
	.ux_config = {
		.uc_keep_sandbox = false,
		.uc_yaml_output  = false,
	},
	.ux_used = 0, /* initially there are no test suites registered */
};

M0_INTERNAL int m0_ut_init(struct m0_ut_cfg *cfg)
{
	static struct m0 instance;
	int rc;

	if (cfg != NULL)
		ctx.ux_config = *cfg;

	m0_atomic64_set(&ctx.ux_asserts, 0);

	rc = m0_arch_ut_init(&ctx.ux_config);
	if (rc == 0) {
		rc = m0_init(&instance);
		if (rc == 0) {
			rc = m0_cs_default_stypes_init();
			if (rc != 0)
				m0_fini();
		}
		if (rc != 0)
			m0_arch_ut_fini(&ctx.ux_config);
	}
	return rc;
}
M0_EXPORTED(m0_ut_init);

M0_INTERNAL void m0_ut_fini(void)
{
	m0_cs_default_stypes_fini();
	m0_fini();
	m0_arch_ut_fini(&ctx.ux_config);
}
M0_EXPORTED(m0_ut_fini);

M0_INTERNAL void m0_ut_add(struct m0_ut_suite *ts)
{
	M0_ASSERT(ctx.ux_used < SUITES_MAX);
	ctx.ux_suites[ctx.ux_used++] = ts;
}

static struct m0_ut_suite *suite_find(const char *name)
{
	int i;

	if (name == NULL)
		return NULL;

	for (i = 0; i < ctx.ux_used; ++i)
		if (m0_streq(ctx.ux_suites[i]->ts_name, name))
			return ctx.ux_suites[i];
	return NULL;
}

static struct m0_ut *get_test_by_name(const char *s_name, const char *t_name)
{
	struct m0_ut_suite *s = suite_find(s_name);
	struct m0_ut       *t;

	if (t_name == NULL)
		return NULL;

	if (s != NULL)
		for (t = s->ts_tests; t->t_name != NULL; ++t)
			if (m0_streq(t->t_name, t_name))
				return t;
	return NULL;
}

static void set_enabled_flag_to(bool value)
{
	struct m0_ut *t;
	int           i;

	for (i = 0; i < ctx.ux_used; ++i) {
		ctx.ux_suites[i]->ts_enabled = value;
		for (t = ctx.ux_suites[i]->ts_tests; t->t_name != NULL; ++t)
			t->t_enabled = value;
	}
}

static void
set_enabled_flag_for(const char *s_name, const char *t_name, bool value)
{
	struct m0_ut_suite *s;
	struct m0_ut       *t;

	M0_PRE(s_name != NULL);

	s = suite_find(s_name);
	M0_ASSERT(s != NULL); /* ensured by test_list_populate() */
	s->ts_enabled = value;

	if (t_name == NULL) {
		for (t = s->ts_tests; t->t_name != NULL; ++t)
			t->t_enabled = value;
	} else {
		t = get_test_by_name(s_name, t_name);
		M0_ASSERT(t != NULL); /* ensured by test_list_populate() */
		t->t_enabled = value;
	}
}

static bool exists(const char *s_name, const char *t_name)
{
	struct m0_ut_suite *s;
	struct m0_ut       *t;

	s = suite_find(s_name);
	if (s == NULL) {
		M0_LOG(M0_ERROR, "Unit-test suite '%s' not found!", s_name);
		return false;
	}

	/* if checking only suite existence */
	if (t_name == NULL)
		return true;

	/* got here? then need to check test existence */
	t = get_test_by_name(s_name, t_name);
	if (t == NULL) {
		M0_LOG(M0_ERROR, "Unit-test '%s:%s' not found!", s_name, t_name);
		return false;
	}
	return true;
}

static int test_add(struct m0_list *list, const char *suite, const char *test)
{
	struct m0_ut_entry *e;
	int                 rc = -ENOMEM;

	M0_PRE(suite != NULL);

	M0_ALLOC_PTR(e);
	if (e == NULL)
		return -ENOMEM;

	e->ue_suite_name = m0_strdup(suite);
	if (e->ue_suite_name == NULL)
		goto err;

	if (test != NULL) {
		e->ue_test_name = m0_strdup(test);
		if (e->ue_test_name == NULL)
			goto err;
	}

	if (exists(e->ue_suite_name, e->ue_test_name)) {
		m0_list_link_init(&e->ue_linkage);
		m0_list_add_tail(list, &e->ue_linkage);
		return 0;
	}
	rc = -ENOENT;
err:
	m0_free(e);
	return rc;
}

/**
 * Populates a list of m0_ut_entry elements by parsing input string,
 * which should conform with the format 'suite[:test][,suite[:test]]'.
 *
 * @param  str   input string.
 * @param  list  initialised and empty m0_list.
 */
static int test_list_populate(struct m0_list *list, const char *str)
{
	char *s;
	char *p;
	char *token;
	char *subtoken;
	int   rc = 0;

	if (str == NULL)
		return 0;

	s = m0_strdup(str);
	if (s == NULL)
		return -ENOMEM;
	p = s;

	while (true) {
		token = strsep(&p, ",");
		if (token == NULL)
			break;

		subtoken = strchr(token, ':');
		if (subtoken != NULL)
			*subtoken++ = '\0';

		rc = test_add(list, token, subtoken);
		if (rc != 0)
			break;
	}
	m0_free(s);
	return rc;
}

static void test_list_fini(struct m0_list *list)
{
	m0_list_entry_forall(e, list, struct m0_ut_entry, ue_linkage,
			     m0_list_del(&e->ue_linkage);
			     m0_free((char *)e->ue_suite_name);
			     m0_free((char *)e->ue_test_name);
			     m0_free(e);
			     true;);
	m0_list_fini(list);
}

static int test_list_init(struct m0_list *list, const char *str)
{
	int rc;

	m0_list_init(list);

	rc = test_list_populate(list, str);
	if (rc != 0)
		test_list_fini(list);
	return rc;
}

static int disable_suites(const char *disablelist_str)
{
	struct m0_list disable_list;
	int            rc;

	rc = test_list_init(&disable_list, disablelist_str);
	if (rc != 0)
		return rc;

	m0_list_entry_forall(e, &disable_list, struct m0_ut_entry, ue_linkage,
			     set_enabled_flag_for(e->ue_suite_name,
						  e->ue_test_name, false);
			     true;);
	test_list_fini(&disable_list);
	return 0;
}

static inline const char *skipspaces(const char *str)
{
	while (isspace(*str))
		++str;
	return str;
}

static void run_test(const struct m0_ut *test, bool obey_enabled_flag,
		     size_t max_name_len)
{
	static const char padding[256] = { [0 ... 254] = ' ', [255] = '\0' };

	size_t    pad_len;
	size_t    name_len;
	char      mem[16];
	uint64_t  mem_before;
	uint64_t  mem_after;
	uint64_t  mem_used;
	m0_time_t start;
	m0_time_t end;
	m0_time_t duration;

	if (obey_enabled_flag && !test->t_enabled)
		return;

	m0_console_printf(LOG_PREFIX "  %s  ", test->t_name);

	mem_before = m0_allocated_total();
	start      = m0_time_now();

	/* run the test */
	test->t_proc();

	end       = m0_time_now();
	mem_after = m0_allocated_total();

	name_len = strlen(test->t_name);
	/* max_check is for case when max_name_len == 0 */
	pad_len  = max_check(name_len, max_name_len) - name_len;
	pad_len  = min_check(pad_len, ARRAY_SIZE(padding) - 1);
	duration = m0_time_sub(end, start);
	mem_used = mem_after - mem_before;

	m0_console_printf("%.*s%4" PRIu64 ".%-2" PRIu64 " sec  %sB\n",
			  (int)pad_len, padding, m0_time_seconds(duration),
			  m0_time_nanoseconds(duration) / M0_TIME_ONE_MSEC / 10,
			  m0_bcount_with_suffix(mem, ARRAY_SIZE(mem), mem_used));
}

static int run_suite(const struct m0_ut_suite *suite, bool obey_enabled_flag,
		     int max_name_len)
{
	const struct m0_ut *test;

	char      leak[16];
	uint64_t  alloc_before;
	uint64_t  alloc_after;
	char      mem[16];
	uint64_t  mem_before;
	uint64_t  mem_after;
	uint64_t  mem_used;
	m0_time_t start;
	m0_time_t end;
	m0_time_t duration;
	int       rc = 0;

	if (obey_enabled_flag && !suite->ts_enabled)
		return 0;

	m0_console_printf("%s\n", suite->ts_name);

	alloc_before = m0_allocated();
	mem_before   = m0_allocated_total();
	start        = m0_time_now();

	if (suite->ts_init != NULL) {
		rc = suite->ts_init();
		if (rc != 0)
			M0_ERR(rc, "Unit-test suite initialization failure.");
	}

	for (test = suite->ts_tests; test->t_name != NULL; ++test)
		run_test(test, obey_enabled_flag, max_name_len);

	if (suite->ts_fini != NULL) {
		rc = suite->ts_fini();
		if (rc != 0)
			M0_ERR(rc, "Unit-test suite finalization failure.");
	}

	end         = m0_time_now();
	mem_after   = m0_allocated_total();
	alloc_after = m0_allocated();
	duration    = m0_time_sub(end, start);
	mem_used    = mem_after - mem_before;

	m0_console_printf(LOG_PREFIX "  [ time: %" PRIu64 ".%-" PRIu64 " sec,"
			  " mem: %sB, leaked: %sB ]\n", m0_time_seconds(duration),
			  m0_time_nanoseconds(duration) / M0_TIME_ONE_MSEC / 10,
			  skipspaces(m0_bcount_with_suffix(mem, ARRAY_SIZE(mem),
							   mem_used)),
			  skipspaces(m0_bcount_with_suffix(leak, ARRAY_SIZE(leak),
						  alloc_after - alloc_before)));
	return rc;
}

/*
 * Calculates maximum name length among all tests in all suites (if suite
 * parameter is non-NULL) or only in particular suite.
 */
static int get_max_test_name_len(const struct m0_ut_suite *suite)
{
	const struct m0_ut        *test;
	const struct m0_ut_suite **s;

	size_t max_len = 0;
	int    i;
	int    n;

	if (suite == NULL) {
		s = (const struct m0_ut_suite **)ctx.ux_suites;
		n = ctx.ux_used;
	} else {
		s = &suite;
		n = 1;
	}

	for (i = 0; i < n; ++i)
		for (test = s[i]->ts_tests; test->t_name != NULL; ++test)
			max_len = max_check(strlen(test->t_name), max_len);

	return max_len;
}

static int run_selected(const char *runlist_str)
{
	struct m0_list            run_list;
	const struct m0_ut_suite *suite;
	const struct m0_ut       *test;
	int                       rc = 0;

	rc = test_list_init(&run_list, runlist_str);
	if (rc != 0)
		return rc;

	m0_list_entry_forall( e, &run_list, struct m0_ut_entry, ue_linkage,
		if (e->ue_test_name == NULL) {
			suite = suite_find(e->ue_suite_name);
			rc = run_suite(suite, false,
				       get_max_test_name_len(suite));
		} else {
			test = get_test_by_name(e->ue_suite_name,
						e->ue_test_name);
			run_test(test, false, 0);
			rc = 0;
		}
		rc == 0;
	);
	test_list_fini(&run_list);
	return rc;
}

static int run_all(const char *excludelist_str)
{
	int i;
	int rc;

	set_enabled_flag_to(true);

	rc = disable_suites(excludelist_str);
	if (rc != 0)
		return rc;

	for (i = 0; i < ctx.ux_used && rc == 0; ++i)
		rc = run_suite(ctx.ux_suites[i], true,
			       get_max_test_name_len(NULL));

	return rc;
}

M0_INTERNAL int m0_ut_run(void)
{
	char        leak[16];
	const char *leak_str;
	uint64_t    alloc_before;
	uint64_t    alloc_after;
	char        mem[16];
	const char *mem_str;
	uint64_t    mem_before;
	uint64_t    mem_after;
	uint64_t    mem_used;
	m0_time_t   start;
	m0_time_t   end;
	m0_time_t   duration;
	uint64_t    csec;
	int         rc;

	alloc_before = m0_allocated();
	mem_before   = m0_allocated_total();
	start        = m0_time_now();

	if (ctx.ux_config.uc_run_list != NULL)
		rc = run_selected(ctx.ux_config.uc_run_list);
	else
		rc = run_all(ctx.ux_config.uc_exclude_list);

	end         = m0_time_now();
	mem_after   = m0_allocated_total();
	alloc_after = m0_allocated();
	mem_used    = mem_after - mem_before;
	duration    = m0_time_sub(end, start);
	csec        = m0_time_nanoseconds(duration) / M0_TIME_ONE_MSEC / 10;
	leak_str    = skipspaces(m0_bcount_with_suffix(mem, ARRAY_SIZE(mem),
						       mem_used));
	mem_str     = skipspaces(m0_bcount_with_suffix(leak, ARRAY_SIZE(leak),
						alloc_after - alloc_before));

	if (rc == 0)
		m0_console_printf("\nTime: %" PRIu64 ".%-2" PRIu64 " sec,"
				  " Mem: %sB, Leaked: %sB, Asserts: %" PRIu64
				  "\nUnit tests status: SUCCESS\n",
				  m0_time_seconds(duration), csec, leak_str,
				  mem_str, m0_atomic64_get(&ctx.ux_asserts));
	return rc;
}
M0_EXPORTED(m0_ut_run);

M0_INTERNAL void m0_ut_list(bool with_tests)
{
	const struct m0_ut *t;
	int                 i;

	for (i = 0; i < ctx.ux_used; ++i) {
		m0_console_printf("%s\n", ctx.ux_suites[i]->ts_name);
		if (with_tests)
			for (t = ctx.ux_suites[i]->ts_tests; t->t_name != NULL; ++t)
				m0_console_printf("  %s\n", t->t_name);
	}
}

M0_INTERNAL void m0_ut_list_owners(void)
{
	const struct m0_ut *t;

	bool test_owner_exists;
	int  i;

	for (i = 0; i < ctx.ux_used; ++i)
		if (ctx.ux_suites[i]->ts_owners != NULL) {
			m0_console_printf("%s: %s\n", ctx.ux_suites[i]->ts_name,
					  ctx.ux_suites[i]->ts_owners);
			for (t = ctx.ux_suites[i]->ts_tests; t->t_name != NULL; ++t)
				if (t->t_owner != NULL)
					m0_console_printf("  %s: %s\n",
							  t->t_name, t->t_owner);
		} else {
			test_owner_exists = false;
			for (t = ctx.ux_suites[i]->ts_tests; t->t_name != NULL; ++t)
				if (t->t_owner != NULL) {
					test_owner_exists = true;
					break;
				}
			if (test_owner_exists) {
				m0_console_printf("%s\n", ctx.ux_suites[i]->ts_name);
				for (t = ctx.ux_suites[i]->ts_tests;
				     t->t_name != NULL; ++t)
					if (t->t_owner != NULL)
						m0_console_printf("  %s: %s\n",
							t->t_name, t->t_owner);
			}
		}
}

M0_INTERNAL bool m0_ut_assertimpl(bool c, const char *str_c, const char *file,
				  int lno, const char *func)
{
	static char buf[4096];

	m0_atomic64_inc(&ctx.ux_asserts);

	if (!c) {
		snprintf(buf, sizeof buf,
			"Unit-test assertion failed: %s", str_c);
		m0_panic(&(struct m0_panic_ctx){
				.pc_expr = buf,  .pc_func   = func,
				.pc_file = file, .pc_lineno = lno,
				.pc_fmt  = NULL });
	}

	return c;
}
M0_EXPORTED(m0_ut_assertimpl);

#ifndef __KERNEL__
#include <stdlib.h>                       /* qsort */

static int order[SUITES_MAX];

static int cmp(struct m0_ut_suite **s0, struct m0_ut_suite **s1)
{
	int i0 = s0 - ctx.ux_suites;
	int i1 = s1 - ctx.ux_suites;

	return order[i0] - order[i1];
}

M0_INTERNAL void m0_ut_shuffle(unsigned seed)
{
	unsigned i;

	M0_ASSERT(ctx.ux_used > 0);

	srand(seed);
	for (i = 1; i < ctx.ux_used; ++i)
		order[i] = rand();
	qsort(ctx.ux_suites + 1, ctx.ux_used - 1, sizeof ctx.ux_suites[0],
	      (void *)&cmp);
}
#endif

/** @} end of ut group. */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
