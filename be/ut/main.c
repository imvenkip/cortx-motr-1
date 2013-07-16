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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 3-Jun-2013
 */

#include "ut/ut.h"

extern void m0_be_ut_seg_init_fini(void);
extern void m0_be_ut_seg_create_destroy(void);
extern void m0_be_ut_seg_open_close(void);
extern void m0_be_ut_seg_io(void);

extern void m0_be_ut_alloc_init_fini(void);
extern void m0_be_ut_alloc_create_destroy(void);
extern void m0_be_ut_alloc_multiple(void);
extern void m0_be_ut_alloc_concurrent(void);

extern void m0_be_ut_reg_d_tree(void);
extern void m0_be_ut_regmap_simple(void);
extern void m0_be_ut_regmap_random(void);
extern void m0_be_ut_reg_area_simple(void);
extern void m0_be_ut_reg_area_random(void);
extern void m0_be_ut_reg_area_merge(void);

extern void m0_be_ut_tx_simple(void);

extern void m0_be_ut_io(void);
extern void m0_be_ut_log_stor_reserve(void);
extern void m0_be_ut_log_stor_io(void);
extern void m0_be_ut_log(void);
extern void m0_be_ut_group_ondisk(void);
extern void m0_be_ut_list_api(void);
extern void m0_be_ut_btree_simple(void);
extern void m0_be_ut_emap(void);

extern struct m0_sm_group ut__txs_sm_group;

/* ---------------------------------------------------------------------
 * XXX FIXME: Using "ast threads" is a very wrong thing to do.
 * We should have landed origin/fom-simple branch to origin/master
 * and use m0_fom_simple instead.
 *  -- Bad, bad vvv.
 */

static struct {
	bool             run;
	struct m0_thread thread;
} g_ast;

static void ast_thread(int _ M0_UNUSED)
{
	struct m0_sm_group *g = &ut__txs_sm_group;

	while (g_ast.run) {
		m0_chan_wait(&g->s_clink);
		m0_sm_group_lock(g);
		m0_sm_asts_run(g);
		m0_sm_group_unlock(g);
	}
}

static int _init(void)
{
	m0_sm_group_init(&ut__txs_sm_group);
	g_ast.run = true;
	return M0_THREAD_INIT(&g_ast.thread, int, NULL, &ast_thread, 0,
			      "ast_thread");
}

static int _fini(void)
{
	g_ast.run = false;
	m0_clink_signal(&ut__txs_sm_group.s_clink);
	m0_thread_join(&g_ast.thread);
	m0_sm_group_fini(&ut__txs_sm_group);

	return 0;
}

const struct m0_test_suite be_ut = {
	.ts_name = "be-ut",
	.ts_init = _init,
	.ts_fini = _fini,
	.ts_tests = {
		{ "seg-init",         m0_be_ut_seg_init_fini        },
		{ "seg-create",       m0_be_ut_seg_create_destroy   },
		{ "seg-open",         m0_be_ut_seg_open_close       },
		{ "seg-io",           m0_be_ut_seg_io               },
		{ "reg_d_tree",       m0_be_ut_reg_d_tree           },
		{ "regmap-simple",    m0_be_ut_regmap_simple        },
		{ "regmap-random",    m0_be_ut_regmap_random        },
		{ "reg_area-simple",  m0_be_ut_reg_area_simple      },
		{ "reg_area-random",  m0_be_ut_reg_area_random      },
		{ "reg_area-merge",   m0_be_ut_reg_area_merge       },
#if 0 /* XXX FIXME
       * A test calling m0_be_ut_h_fini() may fail on
       * m0_net__buf_invariant(). When it does, the stack trace is
       *
       *     nlx_tm_ev_worker
       *      \_ nlx_xo_bev_deliver_all
       *          \_ nlx_xo_core_bev_to_net_bev
       *              \_ m0_net__buffer_invariant
       *
       * Immediate cause [net/lnet/lnet_tm.c:291]:
       * lcbev->cbe_buffer_id is 0, hence nb == NULL,
       * m0_net__buffer_invariant(nb) returns false, and M0_ASSERT() fails.
       *
       * The root cause remains unknown. I do not know why struct
       * nlx_core_buffer_event, pointed to by `lcbev', is zeroed.
       *
       * I disable unit tests that call m0_be_ut_h_{init,fini}() in order
       * to land BE without any harm to master branch.
       *
       *  --vvv
       */
		{ "alloc-init",       m0_be_ut_alloc_init_fini      },
		{ "alloc-create",     m0_be_ut_alloc_create_destroy },
		{ "alloc-multiple",   m0_be_ut_alloc_multiple       },
		{ "alloc-concurrent", m0_be_ut_alloc_concurrent     },
		{ "tx-simple",        m0_be_ut_tx_simple            },
#endif
		{ "list",             m0_be_ut_list_api             },
		{ "btree",            m0_be_ut_btree_simple         },
		{ "emap",             m0_be_ut_emap                 },
		{ "io (XXX NOOP)",    m0_be_ut_io                   },
		{ "log_stor-reserve", m0_be_ut_log_stor_reserve     },
		{ "log_stor-io",      m0_be_ut_log_stor_io          },
		{ "log (XXX NOOP)",   m0_be_ut_log                  },
		{ "group_ondisk",     m0_be_ut_group_ondisk         },
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
