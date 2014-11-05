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
 * Original author: Valery V. Vorotyntsev <valery.vorotyntsev@seagate.com>
 * Original creation date: 26-Oct-2014
 */

#include "ut/module.h"
#include "ut/ut.h"            /* m0_ut_suite */
#include "ut/ut_internal.h"   /* m0_ut_sandbox_init */
#include "ut/cs_service.h"    /* m0_cs_default_stypes_init */
#include "module/instance.h"  /* m0 */

/**
 * @addtogroup ut
 *
 * @{
 */

static int level_ut_enter(struct m0_module *module)
{
	struct m0_ut_module *m = M0_AMB(m, module, ut_module);

	switch (module->m_cur + 1) {
	case M0_LEVEL_UT_PREPARE:
		m0_atomic64_set(&m->ut_asserts, 0);
		return m0_ut_sandbox_init(m->ut_sandbox);
	case M0_LEVEL_UT_KLUDGE:
		return m0_cs_default_stypes_init();
	}
	M0_IMPOSSIBLE("");
}

static void level_ut_leave(struct m0_module *module)
{
	struct m0_ut_module *m = M0_AMB(m, module, ut_module);

	switch (module->m_cur) {
	case M0_LEVEL_UT_KLUDGE:
		m0_cs_default_stypes_fini();
		return;
	case M0_LEVEL_UT_PREPARE:
		m0_ut_sandbox_fini(m->ut_sandbox, m->ut_keep_sandbox);
		return;
	}
	M0_IMPOSSIBLE("");
}

static const struct m0_modlev levels_ut[] = {
	[M0_LEVEL_UT_PREPARE] = {
		.ml_name  = "M0_LEVEL_UT_PREPARE",
		.ml_enter = level_ut_enter,
		.ml_leave = level_ut_leave
	},
	[M0_LEVEL_UT_KLUDGE] = {
		.ml_name  = "M0_LEVEL_UT_KLUDGE",
		.ml_enter = level_ut_enter,
		.ml_leave = level_ut_leave
	},
	[M0_LEVEL_UT_READY] = {
		.ml_name  = "M0_LEVEL_UT_READY"
	}
};

static const struct m0_modlev levels_ut_suite[] = {
	[M0_LEVEL_UT_SUITE_READY] = {
		.ml_name = "M0_LEVEL_UT_SUITE_READY"
	}
};

M0_INTERNAL void m0_ut_module_setup(struct m0 *instance)
{
	struct m0_module *m = &instance->i_ut.ut_module;

	m0_module_setup(m, "UT module", levels_ut, ARRAY_SIZE(levels_ut),
			instance);
#if 1 /* XXX FIXME
       *
       * m0_ut_stob_init(), called when M0_LEVEL_INST_SUBSYSTEMS is entered,
       * requires a sandbox directory, which is created by
       * M0_LEVEL_UT_PREPARE's ->ml_enter().
       *
       * This is a temporary solution. It should go away together with
       * M0_LEVEL_INST_SUBSYSTEMS.
       */
	m0_module_dep_add(&instance->i_self, M0_LEVEL_INST_SUBSYSTEMS,
			  m, M0_LEVEL_UT_PREPARE);
#endif
	m0_module_dep_add(&instance->i_self, M0_LEVEL_INST_READY,
			  m, M0_LEVEL_UT_READY);
}

M0_INTERNAL void
m0_ut_suite_module_setup(struct m0_ut_suite *ts, struct m0 *instance)
{
	int i;

	m0_module_setup(&ts->ts_module, "m0_ut_suite module",
			levels_ut_suite, ARRAY_SIZE(levels_ut_suite), instance);
	for (i = 0; i < ts->ts_deps_nr; ++i) {
		M0_IMPOSSIBLE("XXX FIXME: This won't work, because we"
			      " don't know the address of a module"
			      " at compile time.");
		m0_module_dep_add(&ts->ts_module, M0_LEVEL_UT_SUITE_READY,
				  ts->ts_deps[i].ud_module,
				  ts->ts_deps[i].ud_level);
	}
}

/** @} ut */
