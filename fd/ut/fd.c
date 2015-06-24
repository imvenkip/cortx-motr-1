/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nachiket Sahasrabudhe <Nachiket_Sahasrabudhe@xyratex.com>
 * Original creation date: 13-Feb-15.
 */

#include "fd/fd_internal.h"
#include "fd/fd.h"
#include "conf/diter.h"     /* m0_conf_diter */
#include "conf/ut/common.h" /* g_grp */
#include "conf/obj_ops.h"   /* M0_CONF_DIRNEXT */
#include "pool/pool.h"      /* m0_pool_version */
#include "lib/misc.h"       /* M0_SET0 and uint16_t etc. */
#include "lib/arith.h"      /* m0_rnd */
#include "ut/ut.h"          /* M0_UT_ASSERT */

/* Conf parameters. */
enum {
	PROF,
	FS,
	NODE,
	PROCESS0,
	SERVICE0,
	SERVICE1,
	SERVICE2,
	SERVICE3,
	SERVICE4,
	SERVICE5,
	SDEV0,
	SDEV1,
	SDEV2,
	SDEV3,
	SDEV4,
	RACK,
	ENCLOSURE,
	CONTROLLER,
	DISK0,
	DISK1,
	DISK2,
	DISK3,
	DISK4,
	POOL,
	PVER,
	RACKV,
	ENCLOSUREV,
	CONTROLLERV,
	DISKV0,
	DISKV1,
	DISKV2,
	DISKV3,
	DISKV4,
	UNKNOWN_SVC
};

static const struct m0_fid fids[] = {
	[PROF]        = M0_FID_TINIT('p', 1, 0),
	[FS]          = M0_FID_TINIT('f', 1, 1),
	[NODE]        = M0_FID_TINIT('n', 1, 2),
	[PROCESS0]    = M0_FID_TINIT('r', 1, 3),
	[SERVICE0]    = M0_FID_TINIT('s', 1, 4),
	[SERVICE1]    = M0_FID_TINIT('s', 1, 5),
	[SERVICE2]    = M0_FID_TINIT('s', 1, 6),
	[SERVICE3]    = M0_FID_TINIT('s', 1, 7),
	[SERVICE4]    = M0_FID_TINIT('s', 1, 8),
	[SERVICE5]    = M0_FID_TINIT('s', 1, 9),
	[SDEV0]       = M0_FID_TINIT('d', 1, 10),
	[SDEV1]       = M0_FID_TINIT('d', 1, 11),
	[SDEV2]       = M0_FID_TINIT('d', 1, 12),
	[SDEV3]       = M0_FID_TINIT('d', 1, 13),
	[SDEV4]       = M0_FID_TINIT('d', 1, 14),
	[RACK]        = M0_FID_TINIT('a', 1, 15),
	[ENCLOSURE]   = M0_FID_TINIT('e', 1, 16),
	[CONTROLLER]  = M0_FID_TINIT('c', 1, 17),
	[DISK0]       = M0_FID_TINIT('k', 1, 18),
	[DISK1]       = M0_FID_TINIT('k', 1, 19),
	[DISK2]       = M0_FID_TINIT('k', 1, 20),
	[DISK3]       = M0_FID_TINIT('k', 1, 21),
	[DISK4]       = M0_FID_TINIT('k', 1, 22),
	[POOL]        = M0_FID_TINIT('o', 1, 23),
	[PVER]        = M0_FID_TINIT('v', 1, 24),
	[RACKV]       = M0_FID_TINIT('j', 1, 25),
	[ENCLOSUREV]  = M0_FID_TINIT('j', 1, 26),
	[CONTROLLERV] = M0_FID_TINIT('j', 1, 27),
	[DISKV0]      = M0_FID_TINIT('j', 1, 28),
	[DISKV1]      = M0_FID_TINIT('j', 1, 29),
	[DISKV2]      = M0_FID_TINIT('j', 1, 30),
	[DISKV3]      = M0_FID_TINIT('j', 1, 31),
	[DISKV4]      = M0_FID_TINIT('j', 1, 32),
	[UNKNOWN_SVC] = M0_FID_TINIT('s', 5, 33),
};

char local_conf_str[] = "[34:\
   {0x74| (((0x7400000000000001, 0)), 1, [1 : (0x7000000000000001, 0)])},\
   {0x70| (((0x7000000000000001, 0)), (0x6600000000000001, 1))},\
   {0x66| (((0x6600000000000001, 1)),\
        (11, 22), 41212, [3: \"param-0\", \"param-1\", \"param-2\"],\
        (0x6f00000000000001, 23),\
           [1: (0x6e00000000000001, 2)],\
           [1: (0x6f00000000000001, 23)],\
           [1: (0x6100000000000001, 15)])},\
   {0x6e| (((0x6e00000000000001, 2)), 16000, 2, 3, 2, (0x6f00000000000001, 23),\
           [1: (0x7200000000000001, 3)])},\
   {0x72| (((0x7200000000000001, 3)), [1:3], 0, 0, 0, 0, [6: (0x7300000000000001, 4),\
                                                   (0x7300000000000001, 5),\
                                                   (0x7300000000000001, 6),\
                                                   (0x7300000000000001, 7),\
                                                   (0x7300000000000001, 8),\
                                                   (0x7300000000000001, 9)])},\
   {0x73| (((0x7300000000000001, 4)), 1, [3: \"addr-0\", \"addr-1\", \"addr-2\"],\
           [0])},\
   {0x73| (((0x7300000000000001, 5)), 2, [3: \"addr-0\", \"addr-1\", \"addr-2\"],\
           [5: (0x6400000000000001, 10), (0x6400000000000001, 11),\
               (0x6400000000000001, 12), (0x6400000000000001, 13),\
               (0x6400000000000001, 14)])},\
   {0x73| (((0x7300000000000001, 6)), 3, [3: \"addr-0\", \"addr-1\", \"addr-2\"],\
           [0])},\
   {0x73| (((0x7300000000000001, 7)), 4, [3: \"addr-0\", \"addr-1\", \"addr-2\"],\
           [0])},\
   {0x73| (((0x7300000000000001, 8)), 6, [3: \"addr-0\", \"addr-1\", \"addr-2\"],\
           [0])},\
   {0x73| (((0x7300000000000001, 9)), 2, [1: \"addr-3\"],\
           [0])},\
   {0x64| (((0x6400000000000001, 10)), 4, 1, 4096, 596000000000, 3, 4, \"/dev/sdev0\")},\
   {0x64| (((0x6400000000000001, 11)), 4, 1, 4096, 596000000000, 3, 4, \"/dev/sdev1\")},\
   {0x64| (((0x6400000000000001, 12)), 7, 2, 8192, 320000000000, 2, 4, \"/dev/sdev2\")},\
   {0x64| (((0x6400000000000001, 13)), 7, 2, 8192, 320000000000, 2, 4, \"/dev/sdev3\")},\
   {0x64| (((0x6400000000000001, 14)), 7, 2, 8192, 320000000000, 2, 4, \"/dev/sdev4\")},\
   {0x61| (((0x6100000000000001, 15)),\
           [1: (0x6500000000000001, 16)], [1: (0x7600000000000001, 24)])},\
   {0x65| (((0x6500000000000001, 16)),\
           [1: (0x6300000000000001, 17)], [1: (0x7600000000000001, 24)])},\
   {0x63| (((0x6300000000000001, 17)), (0x6e00000000000001, 2),\
           [5: (0x6b00000000000001, 18), (0x6b00000000000001, 19), (0x6b00000000000001, 20),\
               (0x6b00000000000001, 21), (0x6b00000000000001, 22)], [1: (0x7600000000000001, 24)])},\
   {0x6b| (((0x6b00000000000001, 18)), (0x6400000000000001, 10))},\
   {0x6b| (((0x6b00000000000001, 19)), (0x6400000000000001, 11))},\
   {0x6b| (((0x6b00000000000001, 20)), (0x6400000000000001, 12))},\
   {0x6b| (((0x6b00000000000001, 21)), (0x6400000000000001, 13))},\
   {0x6b| (((0x6b00000000000001, 22)), (0x6400000000000001, 14))},\
   {0x6f| (((0x6f00000000000001, 23)), 0, [1: (0x7600000000000001, 24)])},\
   {0x76| (((0x7600000000000001, 24)), 0, 3, 1, 5, [3: 1,2,4], [5: 1,0,0,0,0],\
           [1: (0x6a00000000000001, 25)])},\
   {0x6a| (((0x6a00000000000001, 25)), (0x6100000000000001, 15),\
           [1: (0x6a00000000000001, 26)])},\
   {0x6a| (((0x6a00000000000001, 26)), (0x6500000000000001, 16),\
           [1: (0x6a00000000000001, 27)])},\
   {0x6a| (((0x6a00000000000001, 27)), (0x6300000000000001, 17),\
           [5: (0x6a00000000000001, 28), (0x6a00000000000001, 29),\
               (0x6a00000000000001, 30), (0x6a00000000000001, 31),\
               (0x6a00000000000001, 32)])},\
   {0x6a| (((0x6a00000000000001, 28)), (0x6b00000000000001, 18), [0])},\
   {0x6a| (((0x6a00000000000001, 29)), (0x6b00000000000001, 19), [0])},\
   {0x6a| (((0x6a00000000000001, 30)), (0x6b00000000000001, 20), [0])},\
   {0x6a| (((0x6a00000000000001, 31)), (0x6b00000000000001, 21), [0])},\
   {0x6a| (((0x6a00000000000001, 32)), (0x6b00000000000001, 22), [0])}]";


enum layout_attr {
	la_N = 8,
        la_K = 2,
};

struct m0_pdclust_attr pd_attr  = {
	.pa_N         = la_N,
	.pa_K         = la_K,
	.pa_P         = la_N + 2 * la_K,
	.pa_unit_size = 4096,
	.pa_seed = {
		.u_hi = 0,
		.u_lo = 0,
	}
};

enum tree_ut_attr {
	TUA_ITER         = 20,
	/* Max number of children for any node in UT for the fd_tree-tree. */
	TUA_CHILD_NR_MAX = 13,
};
M0_BASSERT(TUA_CHILD_NR_MAX >= la_N + 2 * la_K);

static void children_nr_populate(uint64_t *children, uint32_t depth);
static uint32_t parity_group_size(struct m0_pdclust_attr *la_attr);
static uint32_t pool_width_count(uint64_t *children, uint32_t depth);
static bool __filter_pv(const struct m0_conf_obj *obj);
static void test_tile_mapping(void)
{
	struct m0_pool_version      pool_ver;
	struct m0_pdclust_src_addr  src;
	struct m0_pdclust_src_addr  src_new;
	struct m0_pdclust_tgt_addr  tgt;
	uint64_t                    row;
	uint64_t                    col;
	uint64_t                    G;
	uint64_t                    C;
	uint64_t                    P;
	uint32_t                    depth;
	int                         rc;


	G = parity_group_size(&pd_attr);
	P = pd_attr.pa_K;

	for (depth = 1; depth < M0_FTA_DEPTH_MAX; ++depth) {
		while (G > P) {
			children_nr_populate(pool_ver.pv_fd_tile.ft_children_nr,
					     depth);
			P = pool_width_count(pool_ver.pv_fd_tile.ft_children_nr,
					     depth);
		}
		rc = m0_fd__tile_init(&pool_ver.pv_fd_tile, &pd_attr,
				      pool_ver.pv_fd_tile.ft_children_nr,
				      depth);
		M0_UT_ASSERT(rc == 0);
		m0_fd__tile_populate(&pool_ver.pv_fd_tile);
		C = (pool_ver.pv_fd_tile.ft_rows * P) / G;
		for (row = C; row < 2 * C; ++row) {
			src.sa_group = row;
			for (col = 0; col < G; ++col) {
				src.sa_unit = col;
				M0_SET0(&src_new);
				m0_fd_src_to_tgt(&pool_ver.pv_fd_tile, &src,
						 &tgt);
				m0_fd_tgt_to_src(&pool_ver.pv_fd_tile, &tgt,
						 &src_new);
				M0_UT_ASSERT(src.sa_group == src_new.sa_group);
				M0_UT_ASSERT(src.sa_unit == src_new.sa_unit);
			}
		}
		m0_fd_tile_destroy(&pool_ver.pv_fd_tile);
		P = la_K;
	}
}

static uint32_t parity_group_size(struct m0_pdclust_attr *la_attr)
{
	M0_UT_ASSERT(la_attr != NULL);

	return la_attr->pa_N + 2 * la_attr->pa_K;

}

static void children_nr_populate(uint64_t *children, uint32_t depth)
{
	uint32_t i;
	uint32_t j;
	m0_time_t seed;

	seed = m0_time_now();
	for (i = 0; i < depth; ++i) {
		j = m0_rnd(TUA_CHILD_NR_MAX, &seed);
		children[i] = TUA_CHILD_NR_MAX - j;
	}
}

static uint32_t pool_width_count(uint64_t *children, uint32_t depth)
{
	uint32_t i;
	uint32_t cnt = 1;

	for (i = 0; i < depth; ++i) {
		cnt *= children[i];
	}

	return cnt;
}

static void test_pv2fd_conv(void)
{
	struct m0_confc         confc;
	struct m0_conf_obj     *fs_obj = NULL;
	struct m0_conf_diter    it;
	struct m0_conf_obj     *pv_obj;
	struct m0_conf_pver    *pv;
	struct m0_pool_version  pool_ver;
	uint64_t                failure_level;
	int                     i;
	int                     rc;

	M0_SET0(&confc);
	rc = m0_confc_init(&confc, &g_grp, NULL, NULL, local_conf_str);
	M0_UT_ASSERT(rc == 0);
	rc = m0_confc_open_sync(&fs_obj, confc.cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				M0_FID_TINIT('p', 1, 0),
				M0_CONF_PROFILE_FILESYSTEM_FID);
	M0_UT_ASSERT(rc == 0);
	rc = m0_conf_diter_init(&it, &confc, fs_obj,
				M0_CONF_FILESYSTEM_POOLS_FID,
				M0_CONF_POOL_PVERS_FID);
	M0_UT_ASSERT(rc == 0);
	rc = m0_conf_diter_next_sync(&it, __filter_pv);
	M0_UT_ASSERT(rc == M0_CONF_DIRNEXT);
	pv_obj = m0_conf_diter_result(&it);
	pv = M0_CONF_CAST(pv_obj, m0_conf_pver);
	for (i = 1; i < M0_FTA_DEPTH_MAX; ++i)
		pv->pv_nr_failures[i] = la_K + 1;
	failure_level = 0;
	do {
		rc = m0_fd_tolerance_check(pv, &failure_level);
		M0_UT_ASSERT(ergo(rc != 0, failure_level > 0));
		if (rc != 0) {
			--pv->pv_nr_failures[failure_level];
			failure_level = 0;
		}
	} while (rc != 0);
	rc = m0_fd_tile_build(pv, &pool_ver, &failure_level);
	M0_UT_ASSERT(rc == 0);
	m0_fd_tile_destroy(&pool_ver.pv_fd_tile);
	m0_conf_diter_fini(&it);
	m0_confc_close(fs_obj);
	m0_confc_fini(&confc);
}

static bool __filter_pv(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_PVER_TYPE;
}

struct m0_ut_suite failure_domains_ut = {
	.ts_name = "failure_domains-ut",
	.ts_init  = conf_ut_ast_thread_init,
	.ts_fini  = conf_ut_ast_thread_fini,
	.ts_tests = {
		{"test_tile_maping", test_tile_mapping},
		{"test_pv2fd_conv", test_pv2fd_conv},
		{ NULL, NULL }
	}
};
