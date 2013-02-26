/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 25-Jan-2013
 */

/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/ut.h"
#include "lib/misc.h"         /* M0_SET0 */
#include "lib/cdefs.h"        /* ARRAY_SIZE */
#include "lib/trace.h"

#include "dtm/nucleus.h"

enum {
	HI_MAX = 32,
	OP_MAX = 32,
	UP_MAX = 1024
};

static struct ctx {
	struct m0_dtm_nu c_nu;
	struct m0_dtm_hi c_hi[HI_MAX];
	struct m0_dtm_op c_op[OP_MAX];
	struct m0_dtm_up c_up[UP_MAX];
	int              c_idx;
} c;

static void (*c_ready) (struct m0_dtm_op *op);
static void (*c_miser) (struct m0_dtm_op *op);
static void (*c_late)  (struct m0_dtm_op *op);
static void (*c_stable)(struct m0_dtm_op *op);
static void (*c_persistent)(struct m0_dtm_op *op);

static void ready(struct m0_dtm_op *op)
{
	if (c_ready != NULL)
		c_ready(op);
}

static void miser(struct m0_dtm_op *op)
{
	if (c_miser != NULL)
		c_miser(op);
}

static void late(struct m0_dtm_op *op)
{
	if (c_late != NULL)
		c_late(op);
}

static void stable(struct m0_dtm_op *op)
{
	if (c_stable != NULL)
		c_stable(op);
}

static void persistent(struct m0_dtm_op *op)
{
	if (c_persistent != NULL)
		c_persistent(op);
}

static const struct m0_dtm_op_ops op_ops = {
	.doo_ready      = ready,
	.doo_late       = late,
	.doo_miser      = miser,
	.doo_stable     = stable,
	.doo_persistent = persistent
};

static void h_release(struct m0_dtm_hi *hi)
{
}

static void h_persistent(struct m0_dtm_hi *hi, struct m0_dtm_up *up)
{
}

static const struct m0_dtm_hi_ops hi_ops = {
	.dho_release    = h_release,
	.dho_persistent = h_persistent
};


static void nu(void)
{
	m0_dtm_nu_init(&c.c_nu);
	m0_dtm_nu_fini(&c.c_nu);
}

static const struct m0_dtm_hi_ops hi_ops = {
	.dho_release    = release,
	.dho_persistent = persistent
};

static void hi(void)
{
	struct m0_dtm_hi hi;

	m0_dtm_nu_init(&c.c_nu);
	m0_dtm_hi_init(&hi, &c.c_nu);
	m0_dtm_hi_fini(&hi);
	m0_dtm_nu_fini(&c.c_nu);
}

static void op(void)
{
	struct m0_dtm_op op;

	m0_dtm_nu_init(&c.c_nu);
	m0_dtm_op_init(&op, &c.c_nu);
	m0_dtm_op_fini(&op);
	m0_dtm_nu_fini(&c.c_nu);
}

static void ctx_init(void)
{
	int i;

	m0_dtm_nu_init(&c.c_nu);
	for (i = 0; i < ARRAY_SIZE(c.c_hi); ++i) {
		m0_dtm_hi_init(&c.c_hi[i], &c.c_nu);
		c.c_hi[i].hi_ver = 1;
		c.c_hi[i].hi_ops = &hi_ops;
		c.c_hi[i].hi_flags |= M0_DHF_OWNED;
	}
	for (i = 0; i < ARRAY_SIZE(c.c_op); ++i) {
		m0_dtm_op_init(&c.c_op[i], &c.c_nu);
		c.c_op[i].op_ops = &op_ops;
	}
	c.c_idx = 0;
}

static void ctx_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(c.c_op); ++i)
		m0_dtm_op_fini(&c.c_op[i]);
	for (i = 0; i < ARRAY_SIZE(c.c_hi); ++i)
		m0_dtm_hi_fini(&c.c_hi[i]);
	m0_dtm_nu_fini(&c.c_nu);
}

static void ctx_add(int hi, int op, enum m0_dtm_up_rule rule,
		    m0_dtm_ver_t ver, m0_dtm_ver_t orig_ver)
{
	M0_UT_ASSERT(c.c_idx < ARRAY_SIZE(c.c_up));
	M0_UT_ASSERT(hi < ARRAY_SIZE(c.c_hi));
	M0_UT_ASSERT(op < ARRAY_SIZE(c.c_op));

	m0_dtm_up_init(&c.c_up[c.c_idx], &c.c_hi[hi], &c.c_op[op],
		       rule, ver, orig_ver);
	c.c_idx++;
}

M0_INTERNAL void up_print(const struct m0_dtm_up *up);
M0_INTERNAL void op_print(const struct m0_dtm_op *op);
M0_INTERNAL void hi_print(const struct m0_dtm_hi *hi);

static void __attribute__((unused)) ctx_print(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(c.c_hi); ++i)
		hi_print(&c.c_hi[i]);
	for (i = 0; i < ARRAY_SIZE(c.c_op); ++i)
		op_print(&c.c_op[i]);
}

static void ctx_check(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(c.c_hi); ++i)
		m0_dtm_hi_invariant(&c.c_hi[i]);
	for (i = 0; i < ARRAY_SIZE(c.c_op); ++i)
		m0_dtm_op_invariant(&c.c_op[i]);
	for (i = 0; i < c.c_idx; ++i)
		m0_dtm_up_invariant(&c.c_up[i]);
}

static void fail(struct m0_dtm_op *op)
{
	M0_UT_ASSERT(1 == 0);
}

static void ctx_op_add(int i)
{
	c_late = c_miser = fail;
	m0_dtm_op_add(&c.c_op[i]);
	c_late = c_miser = NULL;
}

M0_INTERNAL bool op_state(struct m0_dtm_op *op, enum m0_dtm_state state);

static void ctx_state(int i, enum m0_dtm_state state)
{
	M0_UT_ASSERT(op_state(&c.c_op[i], state));
}

static void up_init(void)
{
	ctx_init();
	ctx_add(0, 0, M0_DUR_NOT, 0, 0);
	ctx_fini();
}

static void op_add(void)
{
	enum {
		OP_NR = 10,
		UP_NR =  5
	};

	int i;
	int j;

	ctx_init();
	for (i = 0; i < OP_NR; ++i) {
		ctx_add(0, i, M0_DUR_INC, i + 2, i + 1);
		for (j = 1; j < UP_NR; ++j)
			ctx_add(j, i, M0_DUR_INC, 0, 0);
	}
	ctx_check();
	for (i = 0; i < OP_NR; ++i) {
		ctx_op_add(i);
		M0_UT_ASSERT(m0_forall(k, OP_NR,
			       op_state(&c.c_op[k],
				 (k <  i ? M0_DOS_INPROGRESS :
				  k == i ? M0_DOS_PREPARE : M0_DOS_LIMBO))));
		m0_dtm_op_prepared(&c.c_op[i]);
		M0_UT_ASSERT(m0_forall(k, OP_NR,
			       op_state(&c.c_op[k],
				 (k <= i ? M0_DOS_INPROGRESS : M0_DOS_LIMBO))));
	}
	for (i = 0; i < OP_NR * UP_NR; ++i)
		M0_UT_ASSERT(c.c_up[i].up_ver != 0 &&
			     c.c_up[i].up_orig_ver != 0);
	ctx_check();
	ctx_fini();
}

static void op_gap(void)
{
	ctx_init();
	ctx_add(0, 0, M0_DUR_INC, 3, 2);
	ctx_op_add(0);
	ctx_state(0, M0_DOS_FUTURE);
	ctx_add(0, 1, M0_DUR_INC, 2, 1);
	ctx_op_add(1);
	ctx_state(1, M0_DOS_PREPARE);
	m0_dtm_op_prepared(&c.c_op[1]);
	ctx_state(1, M0_DOS_INPROGRESS);
	ctx_state(0, M0_DOS_PREPARE);
	m0_dtm_op_prepared(&c.c_op[0]);
	ctx_state(0, M0_DOS_INPROGRESS);
	ctx_check();
	ctx_fini();
}

static bool flag;

static void set_flag(struct m0_dtm_op *op)
{
	M0_UT_ASSERT(!flag);
	flag = true;
}

static void op_late(void)
{
	ctx_init();
	ctx_add(0, 0, M0_DUR_SET, 3, 1);
	ctx_op_add(0);
	ctx_state(0, M0_DOS_PREPARE);
	m0_dtm_op_prepared(&c.c_op[0]);
	ctx_state(0, M0_DOS_INPROGRESS);
	ctx_add(0, 1, M0_DUR_INC, 2, 1);
	c_late = set_flag;
	flag = false;
	m0_dtm_op_add(&c.c_op[1]);
	M0_UT_ASSERT(flag);
	c_late = NULL;
	ctx_check();
	ctx_fini();
}

static void op_miser(void)
{
	ctx_init();
	ctx_add(0, 0, M0_DUR_SET, 3, 1);
	ctx_add(1, 0, M0_DUR_SET, 2, 1);
	ctx_op_add(0);
	ctx_state(0, M0_DOS_PREPARE);
	m0_dtm_op_prepared(&c.c_op[0]);
	ctx_state(0, M0_DOS_INPROGRESS);

	ctx_add(0, 1, M0_DUR_SET, 2, 1);
	ctx_add(1, 1, M0_DUR_SET, 3, 2);
	c_miser = set_flag;
	flag = false;
	m0_dtm_op_add(&c.c_op[1]);
	M0_UT_ASSERT(flag);
	c_miser = NULL;
	ctx_check();
	ctx_fini();
}

static void op_miser_delayed(void)
{
	ctx_init();
	ctx_add(0, 0, M0_DUR_INC, 2, 1);
	ctx_add(1, 0, M0_DUR_INC, 0, 0);

	ctx_add(0, 1, M0_DUR_INC, 3, 2);
	ctx_add(1, 1, M0_DUR_INC, 0, 0);

	ctx_add(0, 2, M0_DUR_INC, 4, 3);
	ctx_add(1, 2, M0_DUR_INC, 2, 1);

	ctx_op_add(2);
	ctx_state(0, M0_DOS_LIMBO);
	ctx_state(1, M0_DOS_LIMBO);
	ctx_state(2, M0_DOS_FUTURE);
	ctx_op_add(0);
	ctx_state(0, M0_DOS_PREPARE);
	ctx_state(1, M0_DOS_LIMBO);
	ctx_state(2, M0_DOS_FUTURE);
	ctx_op_add(1);
	ctx_state(0, M0_DOS_PREPARE);
	ctx_state(1, M0_DOS_FUTURE);
	ctx_state(2, M0_DOS_FUTURE);

	c_miser = set_flag;
	flag = false;
	m0_dtm_op_prepared(&c.c_op[0]);
	ctx_state(0, M0_DOS_INPROGRESS);
	ctx_state(1, M0_DOS_PREPARE);
	ctx_state(2, M0_DOS_LIMBO);
	M0_UT_ASSERT(flag);
	c_miser = NULL;

	ctx_check();
	ctx_fini();
}

static void op_done(void)
{
	ctx_init();
	ctx_add(0, 0, M0_DUR_INC, 2, 1);
	ctx_add(1, 0, M0_DUR_INC, 0, 0);

	ctx_add(0, 1, M0_DUR_INC, 3, 2);
	ctx_add(1, 1, M0_DUR_INC, 0, 0);

	ctx_op_add(0);
	m0_dtm_op_prepared(&c.c_op[0]);
	ctx_op_add(1);
	m0_dtm_op_prepared(&c.c_op[1]);
	ctx_state(0, M0_DOS_INPROGRESS);
	ctx_state(1, M0_DOS_INPROGRESS);

	m0_dtm_op_done(&c.c_op[0]);
	m0_dtm_op_done(&c.c_op[1]);
	ctx_state(0, M0_DOS_VOLATILE);
	ctx_state(1, M0_DOS_VOLATILE);

	ctx_check();
	ctx_fini();
}

static void op_persistent(void)
{
	ctx_init();
	ctx_add(0, 0, M0_DUR_INC, 2, 1);
	ctx_add(1, 0, M0_DUR_INC, 0, 0);
	ctx_add(2, 0, M0_DUR_NOT, 0, 0); /* ltx */

	ctx_add(0, 1, M0_DUR_INC, 3, 2);
	ctx_add(1, 1, M0_DUR_INC, 0, 0);
	ctx_add(2, 1, M0_DUR_NOT, 0, 0); /* ltx */

	ctx_add(2, 2, M0_DUR_INC, 2, 1); /* close */

	ctx_op_add(0);
	m0_dtm_op_prepared(&c.c_op[0]);
	ctx_op_add(1);
	m0_dtm_op_prepared(&c.c_op[1]);
	ctx_state(0, M0_DOS_INPROGRESS);
	ctx_state(1, M0_DOS_INPROGRESS);

	m0_dtm_op_done(&c.c_op[0]);
	m0_dtm_op_done(&c.c_op[1]);
	ctx_state(0, M0_DOS_VOLATILE);
	ctx_state(1, M0_DOS_VOLATILE);
	ctx_op_add(2);
	m0_dtm_op_prepared(&c.c_op[2]);
	m0_dtm_op_done(&c.c_op[2]);
	ctx_state(2, M0_DOS_VOLATILE);

	m0_dtm_op_persistent(&c.c_op[0]);
	m0_dtm_op_persistent(&c.c_op[1]);
	m0_dtm_op_persistent(&c.c_op[2]);
	ctx_state(0, M0_DOS_PERSISTENT);
	ctx_state(1, M0_DOS_PERSISTENT);
	ctx_state(2, M0_DOS_PERSISTENT);

	ctx_check();
	ctx_fini();
}

const struct m0_test_suite dtm_nucleus_ut = {
	.ts_name = "dtm-nucleus-ut",
	.ts_tests = {
		{ "nu",            nu },
		{ "hi",            hi },
		{ "op",            op },
		{ "up",            up_init },
		{ "op-add",        op_add },
		{ "gap",           op_gap },
		{ "late",          op_late },
		{ "miser",         op_miser },
		{ "miser-delayed", op_miser_delayed },
		{ "done",          op_done },
		{ "persistent",    op_persistent },
		{ NULL, NULL }
	}
};
M0_EXPORTED(dtm_nucleus_ut);

/** @} end of dtm group */

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
