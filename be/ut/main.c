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
#include "ut/ast_thread.h"

/*
 * TODO
 * - XXX s/rand_r/m0_rnd/g
 */

extern void m0_be_ut_reg_d_tree(void);
extern void m0_be_ut_regmap_simple(void);
extern void m0_be_ut_regmap_random(void);
extern void m0_be_ut_reg_area_simple(void);
extern void m0_be_ut_reg_area_random(void);
extern void m0_be_ut_reg_area_merge(void);

extern void m0_be_ut_io(void);
extern void m0_be_ut_log_store_reserve(void);
extern void m0_be_ut_log_store_io(void);
extern void m0_be_ut_log(void);

extern void m0_be_ut_seg_create_destroy(void);
extern void m0_be_ut_seg_open_close(void);
extern void m0_be_ut_seg_io(void);
extern void m0_be_ut_seg_multiple(void);

extern void m0_be_ut_group_ondisk(void);

extern void m0_be_ut_domain(void);

extern void m0_be_ut_tx_states(void);
extern void m0_be_ut_tx_empty(void);
extern void m0_be_ut_tx_usecase_success(void);
extern void m0_be_ut_tx_usecase_failure(void);
extern void m0_be_ut_tx_single(void);
extern void m0_be_ut_tx_several(void);
extern void m0_be_ut_tx_persistence(void);
extern void m0_be_ut_tx_fast(void);
extern void m0_be_ut_tx_concurrent(void);

extern void m0_be_ut_alloc_init_fini(void);
extern void m0_be_ut_alloc_create_destroy(void);
extern void m0_be_ut_alloc_multiple(void);
extern void m0_be_ut_alloc_concurrent(void);
extern void m0_be_ut_alloc_transactional(void);

extern void m0_be_ut_list_api(void);
extern void m0_be_ut_btree_simple(void);
extern void m0_be_ut_emap(void);
extern void m0_be_ut_dict(void);

const struct m0_test_suite be_ut = {
	.ts_name = "be-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "reg_d_tree",          m0_be_ut_reg_d_tree           },
		{ "regmap-simple",       m0_be_ut_regmap_simple        },
		{ "regmap-random",       m0_be_ut_regmap_random        },
		{ "reg_area-simple",     m0_be_ut_reg_area_simple      },
		{ "reg_area-random",     m0_be_ut_reg_area_random      },
		{ "reg_area-merge",      m0_be_ut_reg_area_merge       },
		{ "io (XXX NOOP)",       m0_be_ut_io                   },
		{ "log_store-reserve",   m0_be_ut_log_store_reserve    },
		{ "log_store-io",        m0_be_ut_log_store_io         },
		{ "log (XXX NOOP)",      m0_be_ut_log                  },
		{ "seg-open",            m0_be_ut_seg_open_close       },
		{ "seg-io",              m0_be_ut_seg_io               },
		{ "seg-multiple",        m0_be_ut_seg_multiple         },
		{ "group_ondisk",        m0_be_ut_group_ondisk         },
		{ "domain",              m0_be_ut_domain               },
		{ "tx-states",		 m0_be_ut_tx_states	       },
		{ "tx-empty",		 m0_be_ut_tx_empty	       },
		{ "tx-usecase_success",  m0_be_ut_tx_usecase_success   },
		{ "tx-usecase_failure",  m0_be_ut_tx_usecase_failure   },
		{ "tx-single",           m0_be_ut_tx_single            },
		{ "tx-several",          m0_be_ut_tx_several           },
		{ "tx-persistence",      m0_be_ut_tx_persistence       },
		{ "tx-fast",             m0_be_ut_tx_fast              },
		{ "tx-concurrent",	 m0_be_ut_tx_concurrent	       },
		{ "alloc-init",          m0_be_ut_alloc_init_fini      },
		{ "alloc-create",        m0_be_ut_alloc_create_destroy },
		{ "alloc-multiple",      m0_be_ut_alloc_multiple       },
		{ "alloc-concurrent",    m0_be_ut_alloc_concurrent     },
		{ "alloc-transactional", m0_be_ut_alloc_transactional  },
		{ "list",                m0_be_ut_list_api             },
		{ "btree",               m0_be_ut_btree_simple         },
		{ "emap",                m0_be_ut_emap                 },
		{ "dict",                m0_be_ut_dict                 },
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
