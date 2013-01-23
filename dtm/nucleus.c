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
 * Original creation date: 22-Jan-2013
 */


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/bob.h"
#include "lib/misc.h"           /* M0_IN */
#include "lib/arith.h"          /* min_check, max_check */
#include "lib/tlist.h"
#include "lib/assert.h"

#include "dtm/nucleus.h"

M0_TL_DESCR_DEFINE(hi, "dtm history updates", static, struct m0_dtm_up,
		   up_hi_linkage, up_magix,
		   M0_DTM_UP_MAGIX, M0_DTM_HI_MAGIX);
M0_TL_DEFINE(hi, static, struct m0_dtm_up);

M0_TL_DESCR_DEFINE(op, "dtm operation updates", static, struct m0_dtm_up,
		   up_op_linkage, up_magix,
		   M0_DTM_UP_MAGIX, M0_DTM_OP_MAGIX);
M0_TL_DEFINE(op, static, struct m0_dtm_up);

const static struct m0_bob_type hi_bob;
const static struct m0_bob_type op_bob;
static struct m0_bob_type up_bob;

M0_BOB_DEFINE(static, &hi_bob, m0_dtm_hi);
M0_BOB_DEFINE(static, &up_bob, m0_dtm_up);
M0_BOB_DEFINE(static, &op_bob, m0_dtm_op);

static void              advance  (struct m0_dtm_op *op);
static void              up_ready (struct m0_dtm_up *up);
static enum m0_dtm_state up_state (const struct m0_dtm_up *up);
static void              up_insert(struct m0_dtm_up *up);

#define up_for(op, up)				\
do {						\
	struct m0_dtm_up *up;			\
						\
	m0_tl_for(op, &op->op_ups, up)

#define up_endfor				\
	m0_tl_endfor;				\
} while (0)

#define hi_for(hi, up)				\
do {						\
	struct m0_dtm_up *up;			\
						\
	m0_tl_for(hi, &hi->hi_ups, up)

#define hi_endfor				\
	m0_tl_endfor;				\
} while (0)

M0_INTERNAL void m0_dtm_op_init(struct m0_dtm_op *op)
{
	m0_dtm_op_bob_init(op);
	op_tlist_init(&op->op_ups);
	M0_POST(m0_dtm_op_invariant(op));
}

M0_INTERNAL void m0_dtm_op_prepared(struct m0_dtm_op *op)
{
	M0_PRE(m0_dtm_op_invariant(op));
	M0_PRE(op->op_state == M0_DOS_PREPARE);

	op->op_state = M0_DOS_INPROGRESS;
	up_for(op, up) {
		struct m0_dtm_up *next = m0_dtm_up_later(up);

		if (next != NULL) {
			M0_ASSERT(op_state(next) <= M0_DOS_INPROGRESS);
			if (op_state(next) == M0_DOS_FUTURE)
				advance(next->up_op);
		}
	}
}

M0_INTERNAL void m0_dtm_op_done(struct m0_dtm_op *op)
{
	M0_PRE(m0_dtm_op_invariant(op));
	M0_PRE(op->op_state == M0_DOS_INPROGRESS);

	op->op_state == M0_DOS_VOLATILE;
	up_for(op, up) {
		m0_dtm_up_seen_set(up);
	} up_endfor;

	M0_POST(m0_dtm_op_invariant(op));
}

M0_INTERNAL void m0_dtm_op_up_add(struct m0_dtm_op *op, struct m0_dtm_up *up)
{
	M0_PRE(m0_dtm_op_invariant(op));
	M0_PRE(op->op_state == M0_DOS_LIMBO);
	M0_PRE(up->up_op == NULL);

	op_tlist_add(&op->op_ups, up);
	up->up_op = op;
	M0_POST(m0_dtm_op_invariant(op));
}

M0_INTERNAL void m0_dtm_op_add(struct m0_dtm_op *op)
{
	M0_PRE(m0_dtm_op_invariant(op));
	M0_PRE(op->op_state == M0_DOS_LIMBO);
	up_for(op, up) {
		m0_dtm_up_add(up);
	} up_endfor;
	op->op_state = M0_DOS_FUTURE;
	advance(op);
	M0_POST(m0_dtm_op_invariant(op));
}

M0_INTERNAL void m0_dtm_op_del(struct m0_dtm_op *op)
{
	M0_PRE(op->op_state <= M0_DOS_FUTURE);
	M0_PRE(m0_dtm_op_invariant(op));

	up_for(op, up) {
		m0_dtm_up_del(up);
	} up_endfor;
	op->op_state = M0_DOS_LIMBO;
	M0_POST(m0_dtm_op_invariant(op));
}

M0_INTERNAL void m0_dtm_op_fini(struct m0_dtm_op *op)
{
	M0_PRE(m0_dtm_op_invariant(op));
	up_for(op, up) {
		op_tlist_del(up);
		m0_dtm_up_fini(up);
	} up_endfor;
	op_tlist_fini(&op->op_ups);
	m0_dtm_op_bob_init(op);
}

M0_INTERNAL int m0_dtm_op_cmp(const struct m0_dtm_op *op)
{
	enum m0_dtm_ver_cmp min = M0_DVC_EARLY;
	enum m0_dtm_ver_cmp max = M0_DVC_LATE;

	M0_PRE(m0_dtm_op_invariant(op));

	/*
	 * max  ^
	 *      |
	 * EARLY|MISER EARLY EARLY
	 * READY|MISER READY   X
	 * LATE |LATE    X     X
	 * -----+-----+-----+--------> min
	 *       LATE  READY EARLY
	 */
	up_for(op, up) {
		enum m0_dtm_ver_cmp check;

		check = m0_dtm_up_cmp(up, up->up_hi->hi_ver);
		min = min_check(min, check);
		max = max_check(max, check);
	} up_endfor;
	return
		min == M0_DVC_LATE  && max == M0_DVC_LATE  ? M0_DVC_LATE  :
		min == M0_DVC_READY && max == M0_DVC_READY ? M0_DVC_READY :
		min >= M0_DVC_READY && max == M0_DVC_EARLY ? M0_DVC_EARLY :
		M0_DVC_MISER;
}


M0_INTERNAL void m0_dtm_op_seen_set(struct m0_dtm_op *op)
{
	M0_PRE(m0_dtm_op_invariant(op));
	up_for(op, up) {
		m0_dtm_up_seen_set(up);
	} up_endfor;
}

static void advance(const struct m0_dtm_op *op)
{
	M0_PRE(op->op_state == M0_DOS_FUTURE);

	switch (m0_dtm_op_cmp(op)) {
	case M0_DVC_READY:
		op->op_state = M0_DOS_PREPARE;
		up_for(op, up) {
			up_ready(up);
		} up_endfor;
		op->op_ops->doo_ready(op);
		break;
	case M0_DVC_EARLY:
		break;
	case M0_DVC_LATE:
		op->op_ops->doo_late(op);
		break;
	case M0_DVC_MISER:
		op->op_ops->doo_miser(op);
		break;
	default:
		C2_IMPOSSIBLE("Impossible m0_dtm_op_cmp() result");
	}
}

static void up_ready(struct m0_dtm_up *up)
{
	struct m0_dtm_hi *hi = up->up_hi;

	M0_PRE(m0_dtm_hi_invariant(hi));
	if (up->up_orig_ver == 0)
		up->up_orig_ver = hi->hi_ver;
	M0_ASSERT(up->up_orig_ver == hi->hi_ver);
	if (up->up_ver == 0) {
		if (up->up_rule == M0_DUR_NOT)
			up->up_ver = up->up_orig_ver;
		else
			up->up_ver = up->up_orig_ver + 1;
		hi_tlist_del(up);
		up_insert(up);
	}
	M0_ASSERT(ergo(up->up_rule != M0_DUR_NOT,
		       m0_dtm_up_cmp(up, up->up_hi->hi_ver) == M0_DVC_LATE));
	hi->hi_ver = up->up_ver;
	M0_POST(m0_dtm_hi_invariant(hi));
}

M0_INTERNAL void m0_dtm_hi_init(struct m0_dtm_hi *hi)
{
	M0_SET0(hi);
	m0_dtm_hi_bob_init(hi);
	hi_tlist_init(&hi->hi_ups);
}

M0_INTERNAL void m0_dtm_hi_fini(struct m0_dtm_hi *hi)
{
	hi_tlist_fini(&hi->hi_ups);
	m0_dtm_hi_bob_fini(hi);
}

M0_INTERNAL void m0_dtm_up_init(struct m0_dtm_up *up, struct m0_dtm_hi *hi,
				struct m0_dtm_op *op, enum m0_dtm_up_rule rule,
				m0_dtm_ver_t ver, m0_dtm_ver_t orig_ver,
				bool seen)
{
	M0_PRE(m0_dtm_op_invariant(op));
	M0_PRE(m0_dtm_hi_invariant(hi));
	M0_PRE(op->op_state == M0_DOS_LIMBO);

	m0_dtm_up_bob_init(up);
	m0_dtm_op_up_add(op, up);
	up->up_hi = hi;
	hi_tlink_init(up);
	up->up_rule = rule;
	up->up_ver  = ver;
	up->up_orig_ver = orig_ver;
	up->up_flags |= seen ? M0_DUF_SEEN : 0;
	M0_POST(m0_dtm_up_invariant(up));
}

M0_INTERNAL void m0_dtm_up_fini(struct m0_dtm_up *up)
{
	hi_tlink_fini(up);
	op_tlink_fini(up);
	m0_dtm_up_bob_fini(up);
}

M0_INTERNAL void m0_dtm_up_add(struct m0_dtm_up *up)
{
	struct m0_drm_hi *hi = up->up_hi;

	M0_PRE(hi != NULL);
	M0_PRE(m0_dtm_hi_invariant(hi));
	M0_PRE(m0_dtm_up_invariant(up));
	M0_PRE(up_state(up) == M0_DOS_LIMBO);

	if (up->up_ver == 0)
		hi_tlist_add(&hi->hi_ups, up);
	else
		up_insert(up);

	M0_PRE(m0_dtm_hi_invariant(hi));
	M0_PRE(m0_dtm_up_invariant(up));
}

static void up_insert(struct m0_dtm_up *up)
{
	struct m0_drm_hi *hi = up->up_hi;

	M0_PRE(up->up_ver != 0);

	hi_for(hi, scan) {
		if (scan->up_ver != 0 &&
		    m0_dtm_up_cmp(up, scan->up_ver) == M0_DVC_LATE) {
			hi_tlist_add_before(&hi->hi_ups, up, scan);
			return;
		}
	} hi_endfor;
	hi_tlist_add_tail(&hi->hi_ups, up);
}

M0_INTERNAL void m0_dtm_up_del(struct m0_dtm_up *up)
{
	M0_PRE(m0_dtm_up_invariant(up));
	M0_PRE(up_state(up) <= M0_DOS_FUTURE);

	if (up_state(up) == M0_DOS_FUTURE)
		hi_tlist_del(up);
	M0_POST(m0_dtm_up_invariant(up));
	M0_POST(m0_dtm_hi_invariant(up->up_hi));
}

M0_INTERNAL void m0_dtm_up_ver_set(struct m0_dtm_up *up,
				   m0_dtm_ver_t ver, m0_dtm_ver_t orig_ver)
{
	M0_PRE(up_state(up) == M0_DOS_INPROGRESS);
	M0_PRE(m0_dtm_hi_invariant(up->up_hi));
	M0_PRE(M0_IN(up->up_ver, (0, ver)));
	M0_PRE(M0_IN(up->up_orig_ver, (0, orig_ver)));

	up->up_ver      = ver;
	up->up_orig_ver = orig;
	M0_POST(m0_dtm_hi_invariant(up->up_hi));
}

M0_INTERNAL void m0_dtm_up_seen_set(struct m0_dtm_up *up)
{
	M0_PRE(m0_dtm_up_invariant(up));
	M0_PRE(M0_IN(up_state(up), (M0_DOS_LIMBO, M0_DOS_INPROGRESS)));
	M0_PRE(!(up->up_flags & M0_DUF_SEEN));

	up->up_flags |= M0_DUF_SEEN;
}

M0_INTERNAL int m0_dtm_up_cmp(const struct m0_dtm_up *up, m0_dtm_ver_t hver)
{
	m0_dtm_ver_t uver = up->up_ver;

	M0_PRE(m0_dtm_up_invariant(up));
	M0_PRE(hver != 0);

	if (uver == 0)
		return M0_DVC_READY;
	if (up->up_orig_ver != 0)
		return M0_3WAY(up->up_orig_ver, hver);

	switch (up->up_rule) {
	case M0_DUR_INC:
		return M0_3WAY(uver, hver + 1);
	case M0_DUR_SET:
		return uver <= hver ? M0_DVC_LATE : M0_DVC_READY;
	case M0_DUR_NOT:
		return M0_3WAY(uver, hver);
	case M0_DUR_APP:

	}
}

M0_INTERNAL struct m0_dtm_up *m0_dtm_up_prior(struct m0_dtm_up *up)
{
	return hi_tlist_next(&up->up_hi->hi_ups, up);
}

M0_INTERNAL struct m0_dtm_up *m0_dtm_up_later(struct m0_dtm_up *up)
{
	return hi_tlist_prev(&up->up_hi->hi_ups, up);
}


static bool up_pair_invariant(const struct m0_dtm_up *up,
			      const struct m0_dtm_up *next);

M0_INTERNAL bool m0_dtm_hi_invariant(const struct m0_dtm_hi *hi)
{
	return
		m0_dtm_hi_bob_check(hi) &&
		m0_tl_forall(hi, up, &hi->hi_ups, {
			m0_dtm_up_invariant(up) &&
			up_pair_invariant(up, hi_tlist_next(&hi->hi_ups, up))
		});
}

M0_INTERNAL bool m0_dtm_up_invariant(const struct m0_dtm_up *up)
{
	enum m0_dtm_state state = up_state(up);

	return
		m0_dtm_up_bob_check(up) &&
		0 <= up->up_rule && up->up_rule < M0_DUR_NR &&
		up->up_hi != NULL && up->up_op != NULL &&
		up->up_ver >= up->up_orig_ver &&
		ergo(up->up_ver == up->up_orig_ver,
		     up->up_ver == 0 || up->up_rule == M0_DUR_NOT) &&
		ergo(up->up_ver != 0,
		     ergo(up->up_rule == M0_DUR_INC,
			  up->up_ver == up->up_orig_ver + 1)) &&
		ergo(up->up_rule == M0_DUR_NOT,
		     up->up_ver == up->up_orig_ver) &&
		ergo(state > M0_DOS_INPROGRESS,
		     up->up_orig_ver != 0 && (up->up_flags & M0_DUF_SEEN)) &&
		hi_tlist_contains(&up->up_hi->hi_ups, up) ==
		                                   (state != M0_DOS_LIMBO) &&
		op_tlist_contains(&up->up_op->op_ups, up);
}

M0_INTERNAL bool m0_dtm_op_invariant(const struct m0_dtm_op *op)
{
	return
		m0_dtm_op_bob_check(op) &&
		0 <= op->op_state && op->op_state < M0_DOS_NR &&
		m0_tl_forall(op, up, &op->op_ups,
			     m0_dtm_up_invariant(up) && up->up_op == op) &&
		ergo(op->op_state > M0_DOS_LIMBO,
		     !m0_tl_forall(op, up, &op->op_ups,
				up->up_rule == M0_DUR_NOT || up->up_ver == 0));
}

static bool up_pair_invariant(const struct m0_dtm_up *up,
			      const struct m0_dtm_up *earlier)
{
	return earlier == NULL ||
		(up_state(up) <= up_state(earlier) &&
		 ergo(up->up_orig_ver != 0,
		      up->up_orig_ver >= earlier->up_ver &&
		      ergo(up->up_hi->hi_flags & M0_DHF_FULL,
			   up->up_orig_ver == earlier->up_ver)) &&
		 ergo(up->up_flags & M0_DUF_RESULT,
		      earlier->up_flags & M0_DUF_RESULT));

}

static enum m0_dtm_state up_state(const struct m0_dtm_up *up)
{
	return up->up_op->op_state;
}

const static struct m0_bob_type hi_bob = {
	.bt_name         = "dtm history",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_dtm_hi, hi_ups.t_magic),
	.bt_magix        = M0_DTM_HI_MAGIX
};

const static struct m0_bob_type op_bob = {
	.bt_name         = "dtm operation",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_dtm_op, op_ups.t_magic),
	.bt_magix        = M0_DTM_OP_MAGIX
};

M0_INTERNAL int m0_dtm_nuclei_init(void)
{
	m0_bob_type_tlist_init(&up_bob, &hi_td);
	return 0;
}

M0_INTERNAL void m0_dtm_nuclei_fini(void)
{
}

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
