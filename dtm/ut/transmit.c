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
 * Original creation date: 22-Mar-2013
 */

/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/ut.h"
#include "lib/tlist.h"
#include "lib/cdefs.h"        /* IS_IN_ARRAY */
#include "lib/errno.h"        /* EPROTO */
#include "lib/assert.h"

#include "dtm/operation.h"
#include "dtm/history.h"
#include "dtm/update.h"
#include "dtm/dtm.h"

M0_INTERNAL void up_print(const struct m0_dtm_up *up);
M0_INTERNAL void op_print(const struct m0_dtm_op *op);
M0_INTERNAL void hi_print(const struct m0_dtm_hi *hi);

enum {
	OPER_NR   = 64,
	UPDATE_NR = 6
};

static struct m0_tl               uu;
static struct m0_dtm              dtm_src;
static struct m0_dtm              dtm_tgt;
static struct m0_dtm_remote       tgt;
static struct m0_dtm_remote       local;
static struct m0_dtm_oper         oper_src[OPER_NR];
static struct m0_dtm_oper         oper_tgt[OPER_NR];
static struct m0_dtm_update       update_src[OPER_NR][UPDATE_NR];
static struct m0_dtm_update       update_tgt[OPER_NR][UPDATE_NR];
static struct m0_dtm_history      history_src[UPDATE_NR];
static struct m0_dtm_history      history_tgt[UPDATE_NR];
static struct m0_dtm_update_descr udescr[UPDATE_NR];
static struct m0_dtm_update_descr udescr_reply[UPDATE_NR];
static struct m0_dtm_oper_descr   ode = {
	.od_nr     = UPDATE_NR,
	.od_update = udescr
};
static struct m0_dtm_oper_descr   reply = {
	.od_nr     = UPDATE_NR,
	.od_update = udescr_reply
};

static void noop(struct m0_dtm_op *op)
{
}

static const struct m0_dtm_op_ops op_ops = {
	.doo_ready      = noop,
	.doo_late       = noop,
	.doo_miser      = noop,
	.doo_stable     = noop,
	.doo_persistent = noop
};

static int src_find(const struct m0_dtm_history_type *ht,
		    const struct m0_uint128 *id,
		    struct m0_dtm_history **out)
{
	if (id->u_hi == 0 && IS_IN_ARRAY(id->u_lo, history_src)) {
		*out = &history_src[id->u_lo];
		return 0;
	} else
		return -EPROTO;
}

static const struct m0_dtm_history_type_ops src_htype_ops = {
	.hito_find = src_find
};

static const struct m0_dtm_history_type src_htype = {
	.hit_id   = 2,
	.hit_name = "source histories",
	.hit_ops  = &src_htype_ops
};

static void src_id(const struct m0_dtm_history *history, struct m0_uint128 *id)
{
	id->u_hi = 0;
	id->u_lo = history - history_src;
	M0_ASSERT(IS_IN_ARRAY(id->u_lo, history_src));
}

static const struct m0_dtm_history_ops src_ops = {
	.hio_type = &src_htype,
	.hio_id   = src_id
};

static int tgt_find(const struct m0_dtm_history_type *ht,
		    const struct m0_uint128 *id,
		    struct m0_dtm_history **out)
{
	if (id->u_hi == 0 && IS_IN_ARRAY(id->u_lo, history_tgt)) {
		*out = &history_tgt[id->u_lo];
		return 0;
	} else
		return -EPROTO;
}

static const struct m0_dtm_history_type_ops tgt_htype_ops = {
	.hito_find = tgt_find
};

static const struct m0_dtm_history_type tgt_htype = {
	.hit_id   = 2,
	.hit_name = "target histories",
	.hit_ops  = &tgt_htype_ops
};

static void tgt_id(const struct m0_dtm_history *history, struct m0_uint128 *id)
{
	id->u_hi = 0;
	id->u_lo = history - history_tgt;
	M0_ASSERT(IS_IN_ARRAY(id->u_lo, history_tgt));
}

static const struct m0_dtm_history_ops tgt_ops = {
	.hio_type = &tgt_htype,
	.hio_id   = tgt_id
};

static void transmit_test(void)
{
	int i;
	int result;

	m0_dtm_init(&dtm_src);
	m0_dtm_init(&dtm_tgt);
	m0_dtm_history_type_register(&dtm_src, &src_htype);
	m0_dtm_history_type_register(&dtm_tgt, &tgt_htype);
	for (i = 0; i < ARRAY_SIZE(history_src); ++i) {
		m0_dtm_history_init(&history_src[i], &dtm_src);
		history_src[i].h_hi.hi_ver = 1;
		history_src[i].h_ops = &src_ops;
		history_src[i].h_rem0.hr_dtm = &tgt;
		m0_dtm_history_add_remote(&history_src[i],
					  &history_src[i].h_rem0);
	}
	for (i = 0; i < ARRAY_SIZE(history_tgt); ++i) {
		m0_dtm_history_init(&history_tgt[i], &dtm_tgt);
		history_tgt[i].h_hi.hi_ver = 1;
		history_tgt[i].h_hi.hi_flags |= M0_DHF_OWNED;
		history_tgt[i].h_ops = &tgt_ops;
		history_tgt[i].h_rem0.hr_dtm = &local;
		m0_dtm_history_add_remote(&history_tgt[i],
					  &history_tgt[i].h_rem0);
	}
	for (i = 0; i < ARRAY_SIZE(oper_src); ++i) {
		m0_dtm_oper_init(&oper_src[i], &dtm_src);
		oper_src[i].oprt_op.op_ops = &op_ops;
	}
	for (i = 0; i < ARRAY_SIZE(oper_tgt); ++i) {
		m0_dtm_oper_init(&oper_tgt[i], &dtm_tgt);
		oper_tgt[i].oprt_op.op_ops = &op_ops;
	}

	for (i = 0; i < ARRAY_SIZE(oper_src); ++i) {
		unsigned nr = (i%UPDATE_NR) + 1;
		int      j;

		for (j = 0; j < nr; ++j) {
			m0_dtm_update_init(&update_src[i][j], &history_src[j],
					   &oper_src[i],
					   M0_DTM_USER_UPDATE_BASE + 1 + j,
					   M0_DUR_SET, 3 + j + i, 0);
		}
		m0_dtm_oper_close(&oper_src[i]);
		m0_dtm_oper_prepared(&oper_src[i]);

		ode.od_nr   = nr;
		reply.od_nr = nr;

		m0_dtm_oper_pack(&oper_src[i], &tgt, &ode);

		m0_dtm_update_list_init(&uu);
		m0_dtm_update_link(&uu, update_tgt[i], nr);

		result = m0_dtm_oper_build(&oper_tgt[i], &uu, &ode);
		M0_UT_ASSERT(result == 0);
		m0_dtm_oper_close(&oper_tgt[i]);
		m0_dtm_oper_prepared(&oper_tgt[i]);
		m0_dtm_oper_done(&oper_tgt[i], &local);

		m0_dtm_reply_pack(&oper_tgt[i], &ode, &reply);
		m0_dtm_reply_unpack(&oper_src[i], &reply);
		m0_dtm_oper_done(&oper_src[i], &tgt);
	}

	for (i = 0; i < ARRAY_SIZE(oper_src); ++i)
		m0_dtm_oper_fini(&oper_src[i]);
	for (i = 0; i < ARRAY_SIZE(oper_tgt); ++i)
		m0_dtm_oper_fini(&oper_tgt[i]);
	m0_dtm_update_list_fini(&uu);
	for (i = 0; i < ARRAY_SIZE(history_tgt); ++i)
		m0_dtm_history_fini(&history_tgt[i]);
	for (i = 0; i < ARRAY_SIZE(history_src); ++i)
		m0_dtm_history_fini(&history_src[i]);
	m0_dtm_history_type_deregister(&dtm_tgt, &tgt_htype);
	m0_dtm_history_type_deregister(&dtm_src, &src_htype);
	m0_dtm_fini(&dtm_src);
	m0_dtm_fini(&dtm_tgt);
}

const struct m0_test_suite dtm_transmit_ut = {
	.ts_name = "dtm-transmit-ut",
	.ts_tests = {
		{ "transmit",    transmit_test  },
		{ NULL, NULL }
	}
};
M0_EXPORTED(dtm_transmit_ut);

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
