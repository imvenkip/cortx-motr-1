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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "ut/ut.h"
#include "ut/ut_internal.h"
#include "module/instance.h"    /* m0 */
#include "lib/errno.h"          /* ENOENT */
#include "lib/string.h"         /* m0_streq */
#include "lib/memory.h"         /* M0_ALLOC_PTR */

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

static int test_suites_enable(const struct m0_ut_module *m);

struct ut_entry {
	struct m0_list_link ue_linkage;
	const char         *ue_suite_name;
	const char         *ue_test_name;
};

M0_INTERNAL int m0_ut_init(struct m0 *instance)
{
	struct m0_ut_module *m = &instance->i_ut;
	struct m0_ut_suite  *ts;
	int                  i;
	int                  rc;

	rc = test_suites_enable(m);
	if (rc != 0)
		return rc;

	m0_instance_setup(instance);
	m0_ut_module_setup(instance);
	/*
	 * Make sure the loop below will be able to create that many
	 * dependencies.
	 */
	M0_ASSERT(ARRAY_SIZE(m->ut_module.m_dep) >= m->ut_suites_nr);
	for (i = 0; i < m->ut_suites_nr; ++i) {
		ts = m->ut_suites[i];
		if (!ts->ts_enabled)
			continue;
		m0_ut_suite_module_setup(ts, instance);
		m0_module_dep_add(&m->ut_module, M0_LEVEL_UT_READY,
				  &ts->ts_module, M0_LEVEL_UT_SUITE_READY);
	}
	return m0_module_init(&instance->i_self, M0_LEVEL_INST_READY);
}
M0_EXPORTED(m0_ut_init);

M0_INTERNAL void m0_ut_fini(void)
{
	m0_module_fini(&m0_get()->i_self, M0_MODLEV_NONE);
}
M0_EXPORTED(m0_ut_fini);

M0_INTERNAL void m0_ut_add(struct m0_ut_module *m, struct m0_ut_suite *ts)
{
	M0_PRE(IS_IN_ARRAY(m->ut_suites_nr, m->ut_suites));
	m->ut_suites[m->ut_suites_nr++] = ts;
}

static struct m0_ut_suite *
suite_find(const struct m0_ut_module *m, const char *name)
{
	int i;

	if (name == NULL)
		return NULL;

	for (i = 0; i < m->ut_suites_nr; ++i)
		if (m0_streq(m->ut_suites[i]->ts_name, name))
			return m->ut_suites[i];
	return NULL;
}

static struct m0_ut *get_test_by_name(const struct m0_ut_module *m,
				      const char *s_name, const char *t_name)
{
	struct m0_ut_suite *s;
	struct m0_ut       *t;

	if (t_name == NULL)
		return NULL;

	s = suite_find(m, s_name);
	if (s != NULL)
		for (t = s->ts_tests; t->t_name != NULL; ++t)
			if (m0_streq(t->t_name, t_name))
				return t;
	return NULL;
}

static void set_enabled_flag_for(const struct m0_ut_module *m,
				 const char *s_name, const char *t_name,
				 bool value)
{
	struct m0_ut_suite *s;
	struct m0_ut       *t;

	M0_PRE(s_name != NULL);

	s = suite_find(m, s_name);
	M0_ASSERT(s != NULL); /* ensured by test_list_populate() */
	s->ts_enabled = value;

	if (t_name == NULL) {
		for (t = s->ts_tests; t->t_name != NULL; ++t)
			t->t_enabled = value;
	} else {
		t = get_test_by_name(m, s_name, t_name);
		M0_ASSERT(t != NULL); /* ensured by test_list_populate() */
		t->t_enabled = value;
	}
}

static bool
exists(const struct m0_ut_module *m, const char *s_name, const char *t_name)
{
	struct m0_ut_suite *s;
	struct m0_ut       *t;

	s = suite_find(m, s_name);
	if (s == NULL) {
		M0_LOG(M0_ERROR, "Unit-test suite '%s' not found!", s_name);
		return false;
	}

	/* if checking only suite existence */
	if (t_name == NULL)
		return true;

	/* got here? then need to check test existence */
	t = get_test_by_name(m, s_name, t_name);
	if (t == NULL) {
		M0_LOG(M0_ERROR, "Unit-test '%s:%s' not found!", s_name, t_name);
		return false;
	}
	return true;
}

static int test_add(struct m0_list *list, const char *suite, const char *test,
		    const struct m0_ut_module *m)
{
	struct ut_entry *e;
	int              rc = -ENOMEM;

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

	if (exists(m, e->ue_suite_name, e->ue_test_name)) {
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
 * Populates a list of ut_entry elements by parsing input string,
 * which should conform with the format 'suite[:test][,suite[:test]]'.
 *
 * @param  str   input string.
 * @param  list  initialised and empty m0_list.
 */
static int test_list_populate(struct m0_list *list, const char *str,
			      const struct m0_ut_module *m)
{
	char *s;
	char *p;
	char *token;
	char *subtoken;
	int   rc = 0;

	M0_PRE(str != NULL);

	s = m0_strdup(str);
	if (s == NULL)
		return -ENOMEM;
	p = s;

	while ((token = strsep(&p, ",")) != NULL) {
		subtoken = strchr(token, ':');
		if (subtoken != NULL)
			*subtoken++ = '\0';

		rc = test_add(list, token, subtoken, m);
		if (rc != 0)
			break;
	}
	m0_free(s);
	return rc;
}

static void test_list_destroy(struct m0_list *list)
{
	m0_list_entry_forall(e, list, struct ut_entry, ue_linkage,
			     m0_list_del(&e->ue_linkage);
			     m0_free((char *)e->ue_suite_name);
			     m0_free((char *)e->ue_test_name);
			     m0_free(e);
			     true;);
	m0_list_fini(list);
}

static int test_list_create(struct m0_list *list, const struct m0_ut_module *m)
{
	int rc;

	M0_PRE(m->ut_tests != NULL && *m->ut_tests != '\0');

	m0_list_init(list);
	rc = test_list_populate(list, m->ut_tests, m);
	if (rc != 0)
		test_list_destroy(list);
	return rc;
}

static int test_suites_exclude(const struct m0_ut_module *m)
{
	struct m0_list disable_list;
	int            rc;

	M0_PRE(m->ut_exclude && m->ut_tests != NULL);

	rc = test_list_create(&disable_list, m);
	if (rc != 0)
		return rc;

	m0_list_entry_forall(e, &disable_list, struct ut_entry, ue_linkage,
			     set_enabled_flag_for(m, e->ue_suite_name,
						  e->ue_test_name, false);
			     true;);
	test_list_destroy(&disable_list);
	return 0;
}

static int test_suites_enable(const struct m0_ut_module *m)
{
	struct m0_ut *t;
	int           i;

	for (i = 0; i < m->ut_suites_nr; ++i) {
		m->ut_suites[i]->ts_enabled = true;
		for (t = m->ut_suites[i]->ts_tests; t->t_name != NULL; ++t)
			t->t_enabled = true;
	}
	return m->ut_exclude ? test_suites_exclude(m) : 0;
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

static int max_test_name_len(const struct m0_ut_suite **suites, unsigned nr)
{
	const struct m0_ut *test;
	unsigned            i;
	size_t              max_len = 0;

	for (i = 0; i < nr; ++i) {
		for (test = suites[i]->ts_tests; test->t_name != NULL; ++test)
			max_len = max_check(strlen(test->t_name), max_len);
	}
	return max_len;
}

static int tests_run_selected(const struct m0_ut_module *m)
{
	struct m0_list            run_list;
	const struct m0_ut_suite *suite;
	const struct m0_ut       *test;
	int                       rc;

	rc = test_list_create(&run_list, m);
	if (rc != 0)
		return rc;

	m0_list_entry_forall(e, &run_list, struct ut_entry, ue_linkage,
		if (e->ue_test_name == NULL) {
			suite = suite_find(m, e->ue_suite_name);
			rc = run_suite(suite, false,
				       max_test_name_len(&suite, 1));
		} else {
			test = get_test_by_name(m, e->ue_suite_name,
						e->ue_test_name);
			run_test(test, false, 0);
			rc = 0;
		}
		rc == 0;
	);
	test_list_destroy(&run_list);
	return rc;
}

static int tests_run_all(const struct m0_ut_module *m)
{
	int i;
	int rc;

	for (i = rc = 0; i < m->ut_suites_nr && rc == 0; ++i)
		rc = run_suite(m->ut_suites[i], true,
			       max_test_name_len((const struct m0_ut_suite **)
						 m->ut_suites,
						 m->ut_suites_nr));
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
	m0_time_t   start;
	m0_time_t   duration;
	int         rc;
	const struct m0_ut_module *m = &m0_get()->i_ut;

	alloc_before = m0_allocated();
	mem_before   = m0_allocated_total();
	start        = m0_time_now();

	rc = (m->ut_tests == NULL || m->ut_exclude) ?
		tests_run_all(m) : tests_run_selected(m);

	mem_after   = m0_allocated_total();
	alloc_after = m0_allocated();
	duration    = m0_time_sub(m0_time_now(), start);
	leak_str    = skipspaces(m0_bcount_with_suffix(
					 mem, ARRAY_SIZE(mem),
					 mem_after - mem_before));
	mem_str     = skipspaces(m0_bcount_with_suffix(
					 leak, ARRAY_SIZE(leak),
					 alloc_after - alloc_before));
	if (rc == 0)
		m0_console_printf("\nTime: %" PRIu64 ".%-2" PRIu64 " sec,"
				  " Mem: %sB, Leaked: %sB, Asserts: %" PRIu64
				  "\nUnit tests status: SUCCESS\n",
				  m0_time_seconds(duration),
				  m0_time_nanoseconds(duration) /
					M0_TIME_ONE_MSEC / 10,
				  leak_str, mem_str,
				  m0_atomic64_get(&m->ut_asserts));
	return rc;
}
M0_EXPORTED(m0_ut_run);

M0_INTERNAL void m0_ut_list(const struct m0_ut_module *m, bool with_tests)
{
	const struct m0_ut *t;
	int                 i;

	for (i = 0; i < m->ut_suites_nr; ++i) {
		m0_console_printf("%s\n", m->ut_suites[i]->ts_name);
		if (with_tests)
			for (t = m->ut_suites[i]->ts_tests; t->t_name != NULL;
			     ++t)
				m0_console_printf("  %s\n", t->t_name);
	}
}

static void ut_owners_print(const struct m0_ut_suite *suite)
{
	const struct m0_ut *t;

	for (t = suite->ts_tests; t->t_name != NULL; ++t) {
		if (t->t_owner != NULL)
			m0_console_printf("  %s: %s\n", t->t_name, t->t_owner);
	}
}

M0_INTERNAL void m0_ut_list_owners(const struct m0_ut_module *m)
{
	const struct m0_ut_suite *s;
	const struct m0_ut       *t;
	int                       i;

	for (i = 0; i < m->ut_suites_nr; ++i) {
		s = m->ut_suites[i];
		if (s->ts_owners == NULL) {
			for (t = s->ts_tests; t->t_name != NULL; ++t) {
				if (t->t_owner != NULL) {
					m0_console_printf("%s\n", s->ts_name);
					ut_owners_print(s);
					break;
				}
			}
		} else {
			m0_console_printf("%s: %s\n", s->ts_name, s->ts_owners);
			ut_owners_print(s);
		}
	}
}

M0_INTERNAL bool m0_ut_assertimpl(bool c, const char *str_c, const char *file,
				  int lno, const char *func)
{
	static char buf[4096];

	m0_atomic64_inc(&m0_get()->i_ut.ut_asserts);
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

static int order[M0_UT_SUITES_MAX];

static int cmp(const struct m0_ut_suite **s0, const struct m0_ut_suite **s1)
{
	const struct m0_ut_suite **start =
		(const struct m0_ut_suite **)m0_get()->i_ut.ut_suites;

	M0_PRE(start < s0 && start < s1);
	return order[s0 - start] - order[s1 - start];
}

M0_INTERNAL void m0_ut_shuffle(struct m0_ut_module *m, unsigned seed)
{
	unsigned i;

	M0_PRE(m->ut_suites_nr > 0);

	srand(seed);
	for (i = 1; i < m->ut_suites_nr; ++i)
		order[i] = rand();
	qsort(m->ut_suites + 1, m->ut_suites_nr - 1, sizeof m->ut_suites[0],
	      (void *)&cmp);
}
#endif

/** @} ut */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
