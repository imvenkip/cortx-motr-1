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
	struct m0_dtm_hi c_hi[HI_MAX];
	struct m0_dtm_op c_op[OP_MAX];
	struct m0_dtm_up c_up[UP_MAX];
	int              c_idx;
} c;

static void (*c_ready) (struct m0_dtm_op *op);
static void (*c_miser) (struct m0_dtm_op *op);
static void (*c_late)  (struct m0_dtm_op *op);
static void (*c_stable)(struct m0_dtm_op *op);

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

static const struct m0_dtm_op_ops op_ops = {
	.doo_ready  = ready,
	.doo_miser  = miser,
	.doo_late   = late,
	.doo_stable = stable
};


static int init(void)
{
	M0_SET0(&c);
	return 0;
}

static void hi(void)
{
	struct m0_dtm_hi hi;

	m0_dtm_hi_init(&hi);
	m0_dtm_hi_fini(&hi);
}

static void op(void)
{
	struct m0_dtm_op op;

	m0_dtm_op_init(&op);
	m0_dtm_op_fini(&op);
}

static void ctx_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(c.c_hi); ++i) {
		m0_dtm_hi_init(&c.c_hi[i]);
		c.c_hi[i].hi_ver = 1;
	}
	for (i = 0; i < ARRAY_SIZE(c.c_op); ++i) {
		m0_dtm_op_init(&c.c_op[i]);
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
}

static void ctx_add(int hi, int op, enum m0_dtm_up_rule rule,
		    m0_dtm_ver_t ver, m0_dtm_ver_t orig_ver, bool seen)
{
	M0_UT_ASSERT(c.c_idx < ARRAY_SIZE(c.c_up));
	M0_UT_ASSERT(hi < ARRAY_SIZE(c.c_hi));
	M0_UT_ASSERT(op < ARRAY_SIZE(c.c_op));

	m0_dtm_up_init(&c.c_up[c.c_idx], &c.c_hi[hi], &c.c_op[op],
		       rule, ver, orig_ver, seen);
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

static void up(void)
{
	ctx_init();
	ctx_add(0, 0, M0_DUR_NOT, 0, 0, false);
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
		ctx_add(0, i, M0_DUR_INC, i + 2, i + 1, i < 3);
		for (j = 1; j < UP_NR; ++j) {
			ctx_add(j, i, M0_DUR_INC, 0, 0, i < 4);
		}
	}
	ctx_check();
	for (i = 0; i < OP_NR; ++i) {
		m0_dtm_op_add(&c.c_op[i]);
		M0_UT_ASSERT(M0_IN(c.c_op[i].op_state, (M0_DOS_FUTURE,
							M0_DOS_PREPARE)));
	}
	for (i = 0; i < OP_NR * UP_NR; ++i)
		M0_UT_ASSERT(c.c_up[i].up_ver != 0 &&
			     c.c_up[i].up_orig_ver != 0);
	ctx_check();
	ctx_fini();
}

const struct m0_test_suite dtm_nucleus_ut = {
	.ts_name = "dtm-nucleus-ut",
	.ts_init = init,
	.ts_fini = NULL,
	.ts_tests = {
		{ "hi",     hi },
		{ "op",     op },
		{ "up",     up },
		{ "op-add", op_add },
		{ NULL, NULL }
	}
};

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
