/* -*- c -*- */
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
 * Original creation date: 17-Sep-2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "conf/confc.h"     /* m0_confc__open */
#include "conf/ut/common.h"
#include "net/lnet/lnet.h"  /* m0_net_lnet_xprt */
#include "rpc/rpclib.h"     /* m0_rpc_server_ctx */
#include "ut/ut.h"

struct m0_semaphore g_sem;
struct m0_sm_group  g_grp;
struct m0_net_xprt *g_xprt = &m0_net_lnet_xprt;
struct conf_ut_ast  g_ast;

/* Filters out intermediate state transitions of m0_confc_ctx::fc_mach. */
static bool _filter(struct m0_clink *link)
{
	return !m0_confc_ctx_is_completed(&container_of(link,
							struct conf_ut_waiter,
							w_clink)->w_ctx);
}

M0_INTERNAL void conf_ut_waiter_init(struct conf_ut_waiter *w,
				     struct m0_confc *confc)
{
	m0_confc_ctx_init(&w->w_ctx, confc);
	m0_clink_init(&w->w_clink, _filter);
	m0_clink_add_lock(&w->w_ctx.fc_mach.sm_chan, &w->w_clink);
}

M0_INTERNAL void conf_ut_waiter_fini(struct conf_ut_waiter *w)
{
	m0_clink_del_lock(&w->w_clink);
	m0_clink_fini(&w->w_clink);
	m0_confc_ctx_fini(&w->w_ctx);
}

M0_INTERNAL int conf_ut_waiter_wait(struct conf_ut_waiter *w,
				    struct m0_conf_obj **result)
{
	int rc;

	while (!m0_confc_ctx_is_completed(&w->w_ctx))
		m0_chan_wait(&w->w_clink);

	rc = m0_confc_ctx_error(&w->w_ctx);
	if (rc == 0 && result != NULL)
		*result = m0_confc_ctx_result(&w->w_ctx);

	return rc;
}

M0_INTERNAL void conf_ut_ast_thread(int _ M0_UNUSED)
{
	while (g_ast.run) {
		m0_chan_wait(&g_grp.s_clink);
		m0_sm_group_lock(&g_grp);
		m0_sm_asts_run(&g_grp);
		m0_sm_group_unlock(&g_grp);
	}
}

M0_INTERNAL int conf_ut_ast_thread_init(void)
{
	M0_SET0(&g_grp);
	M0_SET0(&g_ast);
	m0_sm_group_init(&g_grp);
	g_ast.run = true;
	return M0_THREAD_INIT(&g_ast.thread, int, NULL, &conf_ut_ast_thread, 0,
			      "ast_thread");
}

M0_INTERNAL int conf_ut_ast_thread_fini(void)
{
	g_ast.run = false;
	m0_clink_signal(&g_grp.s_clink);
	m0_thread_join(&g_ast.thread);
	m0_sm_group_fini(&g_grp);

	return 0;
}

M0_INTERNAL void conf_ut_cache_expired_cb(struct m0_sm_group *grp,
					  struct m0_sm_ast *ast)
{
	struct m0_reqh *reqh = container_of(ast, struct m0_reqh,
					    rh_conf_cache_ast);

	M0_ENTRY("grp %p, ast %p", grp, ast);
	m0_chan_broadcast_lock(&reqh->rh_conf_cache_exp);
	M0_LEAVE();
}

M0_INTERNAL void conf_ut_cache_ready_cb(struct m0_sm_group *grp,
					struct m0_sm_ast *ast)
{
	struct m0_reqh *reqh = container_of(ast, struct m0_reqh,
					    rh_conf_cache_ast);

	M0_ENTRY("grp %p, ast %p", grp, ast);
	m0_chan_broadcast_lock(&reqh->rh_conf_cache_ready);
	m0_semaphore_up(&g_sem);
	M0_LEAVE();
}

M0_INTERNAL void conf_ut_confc_expired_cb(struct m0_rconfc *rconfc)
{
	struct m0_reqh *reqh = container_of(rconfc, struct m0_reqh, rh_rconfc);

	M0_ENTRY("rconfc %p", rconfc);
	reqh->rh_conf_cache_ast.sa_cb = conf_ut_cache_expired_cb;
	m0_sm_ast_post(&g_grp, &reqh->rh_conf_cache_ast);
	M0_LEAVE();
}

M0_INTERNAL void conf_ut_confc_ready_cb(struct m0_rconfc *rconfc)
{
	struct m0_reqh *reqh = container_of(rconfc, struct m0_reqh, rh_rconfc);

	M0_ENTRY("rconfc %p", rconfc);
	reqh->rh_conf_cache_ast.sa_cb = conf_ut_cache_ready_cb;
	m0_sm_ast_post(&g_grp, &reqh->rh_conf_cache_ast);
	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM
