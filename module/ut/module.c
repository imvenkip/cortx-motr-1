/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 10-Jan-2014
 */

#include "module/instance.h"
#include "lib/string.h"       /* m0_streq */
#include "ut/ut.h"

static char      g_log[32] = "";
static struct m0 g_instance;

static void _log(char c1, char c2)
{
	int    rc;
	size_t len = strlen(g_log);

	M0_PRE(len + 2 < sizeof g_log);

	rc = snprintf(g_log + len, sizeof(g_log) - len, "%c%c", c1, c2);
	M0_ASSERT(rc == 2);
}

static int modlev_enter(struct m0_module *module)
{
	/* Append "<module name> <next level>" record (2 chars) to the log. */
	_log(*module->m_name, '0' + module->m_cur + 1);
	return 0;
}

static void modlev_leave(struct m0_module *module)
{
	/* Append "<module name> <current level>" record to the log. */
	_log(*module->m_name, '0' + module->m_cur);
}

static struct m0_module modules[] = {
	{ .m_name = "a" },
	{ .m_name = "b" },
	{ .m_name = "c" },
	{ .m_name = "d" },
};
enum module_id { A, B, C, D };

/*                             +------+
 *                             |  c2  |
 *               +------+      +------+
 *               |  b4  |----->|  c1  |---.
 * +------+      +------+      +------+   |
 * |  a4  | ~~-> |  b3  |<--.  |######|   |
 * +------+      +------+   |  +------+   |
 * |  a3  |      |  b2  |   |             |
 * +------+      +------+   |  +------+   |
 * |  a2  |<-----|  b1  |   |  |  d3  |<--'
 * +------+      +------+   |  +------+
 * |  a1  |      |######|   `--|  d2  |
 * +------+      +------+      +------+
 * |######|                    |  d1  |
 * +------+                    +------+
 *                             |######|
 *                             +------+
 */

static struct m0_modlev levels[5];

static struct m0_moddep dep_a[] = { /* no dependencies initially */ };
static struct m0_moddep inv_a[] = {
	{ .md_other = &modules[B], .md_src = 1, .md_dst = 2 }
};

static struct m0_moddep dep_b[] = {
	{ .md_other = &modules[A], .md_src = 1, .md_dst = 2 },
	{ .md_other = &modules[C], .md_src = 4, .md_dst = 1 }
};
static struct m0_moddep inv_b[] = {
	{ .md_other = &modules[D], .md_src = 2, .md_dst = 3 }
};

static struct m0_moddep dep_c[] = {
	{ .md_other = &modules[D], .md_src = 1, .md_dst = 3 }
};
static struct m0_moddep inv_c[] = {
	{ .md_other = &modules[B], .md_src = 4, .md_dst = 1 }
};

static struct m0_moddep dep_d[] = {
	{ .md_other = &modules[B], .md_src = 2, .md_dst = 3 }
};
static struct m0_moddep inv_d[] = {
	{ .md_other = &modules[C], .md_src = 1, .md_dst = 3 }
};

static void _reset(void)
{
	unsigned i;
	struct {
		unsigned          level_nr;
		struct m0_moddep *dep;
		unsigned          dep_nr;
		struct m0_moddep *inv;
		unsigned          inv_nr;
	} mods[] = {
		{ 5, dep_a, ARRAY_SIZE(dep_a), inv_a, ARRAY_SIZE(inv_a) },
		{ 5, dep_b, ARRAY_SIZE(dep_b), inv_b, ARRAY_SIZE(inv_b) },
		{ 3, dep_c, ARRAY_SIZE(dep_c), inv_c, ARRAY_SIZE(inv_c) },
		{ 4, dep_d, ARRAY_SIZE(dep_d), inv_d, ARRAY_SIZE(inv_d) }
	};

	for (i = 1; i < ARRAY_SIZE(levels); ++i) {
		levels[i].ml_enter = modlev_enter;
		levels[i].ml_leave = modlev_leave;
	}
	M0_ASSERT(levels[0].ml_enter == NULL); /* level 0 is not "entered" */
	M0_ASSERT(levels[0].ml_leave == NULL); /* neither "leaved" */

	M0_CASSERT(ARRAY_SIZE(mods) == ARRAY_SIZE(modules));
	for (i = 0; i < ARRAY_SIZE(mods); ++i) {
		modules[i].m_m0       = NULL;
		modules[i].m_cur      = 0;
		M0_ASSERT(mods[i].level_nr <= ARRAY_SIZE(levels));
		modules[i].m_level    = levels;
		modules[i].m_level_nr = mods[i].level_nr;
		modules[i].m_dep_nr   = mods[i].dep_nr;
		modules[i].m_inv_nr   = mods[i].inv_nr;
		memcpy(modules[i].m_dep, mods[i].dep,
		       mods[i].dep_nr * sizeof modules[i].m_dep[0]);
		memcpy(modules[i].m_inv, mods[i].inv,
		       mods[i].inv_nr * sizeof modules[i].m_inv[0]);
	}

	*g_log = 0;
}

static unsigned cur(enum module_id id)
{
	M0_PRE(IS_IN_ARRAY(id, modules));
	return modules[id].m_cur;
}

static void _test_module_init(void)
{
	int rc;

	_reset();

	M0_UT_ASSERT(m0_forall(i, ARRAY_SIZE(modules), cur(i) == 0));
	rc = m0_module_init(&modules[B], 2);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(cur(A) == 2);
	M0_UT_ASSERT(cur(B) == 2);
	M0_UT_ASSERT(cur(C) == 0);
	M0_UT_ASSERT(cur(D) == 0);
	M0_UT_ASSERT(m0_streq(g_log, "a1a2b1b2"));

	rc = m0_module_init(&modules[B], 4);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(cur(A) == 2);
	M0_UT_ASSERT(cur(B) == 4);
	M0_UT_ASSERT(cur(C) == 1);
	M0_UT_ASSERT(cur(D) == 3);
	M0_UT_ASSERT(m0_streq(g_log, "a1a2b1b2b3d1d2d3c1b4"));
}

static void _test_module_fini(void)
{
	*g_log = 0;

	m0_module_fini(&modules[B], 0);
	/* Doesn't go lower than b3, which d2 depends on. */
	M0_UT_ASSERT(cur(A) == 2);
	M0_UT_ASSERT(cur(B) == 3);
	M0_UT_ASSERT(cur(C) == 0); /* b4 -> c1 => c1 gets downgraded */
	M0_UT_ASSERT(cur(D) == 2); /* c1 -> d3 => d3 gets downgraded */
	M0_UT_ASSERT(m0_streq(g_log, "b4c1d3"));

	m0_module_fini(&modules[D], 0);
	M0_UT_ASSERT(cur(A) == 2);
	M0_UT_ASSERT(cur(B) == 2);
	M0_UT_ASSERT(cur(C) == 0);
	M0_UT_ASSERT(cur(D) == 0);
	M0_UT_ASSERT(m0_streq(g_log, "b4c1d3d2b3d1"));

	m0_module_fini(&modules[B], 1);
	M0_UT_ASSERT(cur(A) == 2);
	M0_UT_ASSERT(cur(B) == 1);
	M0_UT_ASSERT(cur(C) == 0);
	M0_UT_ASSERT(cur(D) == 0);
	M0_UT_ASSERT(m0_streq(g_log, "b4c1d3d2b3d1b2"));

	m0_module_fini(&modules[A], 1);
	/* A noop, since b1 depends on a2. */
	M0_UT_ASSERT(cur(A) == 2);
	M0_UT_ASSERT(cur(B) == 1);
	M0_UT_ASSERT(cur(C) == 0);
	M0_UT_ASSERT(cur(D) == 0);
	M0_UT_ASSERT(m0_streq(g_log, "b4c1d3d2b3d1b2"));

	m0_module_fini(&modules[B], 0);
	M0_UT_ASSERT(cur(A) == 1);
	M0_UT_ASSERT(cur(B) == 0);
	M0_UT_ASSERT(cur(C) == 0);
	M0_UT_ASSERT(cur(D) == 0);
	M0_UT_ASSERT(m0_streq(g_log, "b4c1d3d2b3d1b2b1a2"));

	m0_module_fini(&modules[A], 0);
	M0_UT_ASSERT(cur(A) == 0);
	M0_UT_ASSERT(cur(B) == 0);
	M0_UT_ASSERT(cur(C) == 0);
	M0_UT_ASSERT(cur(D) == 0);
	M0_UT_ASSERT(m0_streq(g_log, "b4c1d3d2b3d1b2b1a2a1"));
}

static int modlev_a3_enter(struct m0_module *module)
{
	m0_module_dep_add(module, 4, &modules[B], 3);
	return modlev_enter(module);
}

static void _test_module_dep_add(void)
{
	struct m0_modlev levels_a[ARRAY_SIZE(levels)];
	int              rc;

	_reset();

	memcpy(levels_a, levels, sizeof levels);
	levels_a[3].ml_enter = modlev_a3_enter;
	M0_ASSERT(modules[A].m_level == levels);
	modules[A].m_level = levels_a;

	M0_UT_ASSERT(modules[A].m_dep_nr == 0);
	/*
	 * m0_module_dep_add() is called implicitly: ->m_enter() callback,
	 * invoked when module A enters level 3, creates a4 -> b3 dependency.
	 */
	rc = m0_module_init(&modules[A], 4);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(modules[A].m_dep_nr == 1);
	M0_UT_ASSERT(cur(A) == 4);
	M0_UT_ASSERT(cur(B) == 3);
	M0_UT_ASSERT(cur(C) == 0);
	M0_UT_ASSERT(cur(D) == 0);
	M0_UT_ASSERT(m0_streq(g_log, "a1a2a3b1b2b3a4"));
}

static void test_module(void)
{
	_test_module_init();
	_test_module_fini();
	_test_module_dep_add();
}

static void inherit(int _)
{
	struct m0 *inst;
	struct m0  local;

	inst = m0_get();
	M0_UT_ASSERT(inst == &g_instance);

	m0_set(&local);
	inst = m0_get();
	M0_UT_ASSERT(inst == &local);
}

static void test_instance(void)
{
	struct m0       *inst;
	struct m0_thread t = {0};
	int              rc;

	m0_set(&g_instance);
	inst = m0_get();
	M0_UT_ASSERT(inst == &g_instance);

	rc = M0_THREAD_INIT(&t, int, NULL, &inherit, 0, "heir");
	M0_ASSERT(rc == 0);
	m0_thread_join(&t);

	inst = m0_get();
	M0_UT_ASSERT(inst == &g_instance);
}

const struct m0_test_suite module_ut = {
	.ts_name  = "module-ut",
	.ts_tests = {
		{ "module",   test_module },
		{ "instance", test_instance },
		{ NULL, NULL }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
