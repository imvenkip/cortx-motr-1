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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 13-Aug-2013
 */

#include "ut/ast_thread.h"
#include "sm/sm.h"
#include "lib/types.h"  /* bool */
#include "lib/thread.h"
#include "lib/chan.h"   /* m0_chan_wait */

/**
 * @addtogroup XXX
 *
 * @{
 */

struct m0_sm_group ut__txs_sm_group;

static struct {
	bool                run;
	struct m0_sm_group *grp;
	struct m0_thread    thread;
} g_ast2;

static void ast_thread2(struct m0_sm_group *grp)
{
	struct m0_sm_group *g = grp;

	while (g_ast2.run) {
		m0_chan_wait(&g->s_clink);
		m0_sm_group_lock(g);
		m0_sm_asts_run(g);
		m0_sm_group_unlock(g);
	}
}

M0_INTERNAL int m0_ut_ast_thread_start(struct m0_sm_group *grp)
{
	m0_sm_group_init(grp);
	g_ast2.grp = grp;
	g_ast2.run = true;
	return M0_THREAD_INIT(&g_ast2.thread, struct m0_sm_group *, NULL,
			      &ast_thread2, grp, "ut_ast_thread");
}

M0_INTERNAL void m0_ut_ast_thread_stop(void)
{
	g_ast2.run = false;
	m0_clink_signal(&g_ast2.grp->s_clink);
	m0_thread_join(&g_ast2.thread);
	m0_sm_group_fini(g_ast2.grp);
}

/** @} end of XXX group */

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
