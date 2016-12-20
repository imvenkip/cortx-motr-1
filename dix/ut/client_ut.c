/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Leonid Nikulin <leonid.nikulin@seagate.com>
 * Original creation date: 5-Jun-2016
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DIX
#include "lib/trace.h"
#include "lib/memory.h"
#include "lib/finject.h"
#include "layout/pdclust.h"
#include "layout/linear_enum.h"
#include "pool/pool.h"
#include "rpc/rpclib.h"            /* m0_rpc_server_ctx */
#include "cas/client.h"
#include "dix/imask.h"
#include "dix/layout.h"
#include "dix/meta.h"
#include "dix/client.h"
#include "dix/fid_convert.h"
#include "ut/ut.h"
#include "ut/misc.h"

enum {
	DIX_M0T1FS_LAYOUT_P = 9,
	DIX_M0T1FS_LAYOUT_N = 1,
	DIX_M0T1FS_LAYOUT_K = 3
};

enum {
	COUNT       = 10,
	COUNT_INDEX = 3
};

enum {
	MAX_RPCS_IN_FLIGHT = 10,
	/*
	 * Normally it should be M0_NET_TM_RECV_QUEUE_DEF_LEN, but the bug
	 * MERO-2177 prevents it. After the bug is fixed, revert it back to
	 * M0_NET_TM_RECV_QUEUE_DEF_LEN.
	 */
	RECV_QUEUE_LEN     = 10
};

enum ut_dix_req_type {
	REQ_CREATE,
	REQ_DELETE,
	REQ_LOOKUP,
	REQ_NEXT,
	REQ_GET,
	REQ_PUT,
	REQ_DEL
};
/* Client context */
struct cl_ctx {
	/* Client network domain. */
	struct m0_net_domain     cl_ndom;
	struct m0_layout_domain  cl_ldom;
	/* Client rpc context. */
	struct m0_rpc_client_ctx cl_rpc_ctx;
	struct m0_pools_common   cl_pools_common;
	struct m0_rpc_machine    cl_rpc_machine;
	struct m0_dix_cli        cl_cli;
	struct m0_sm_group      *cl_grp;
	struct m0_fid            cl_pver;
	struct m0_dix_ldesc      cl_dld1;
	struct m0_dix_ldesc      cl_dld2;
};

struct dix_rep_arr {
	struct dix_rep *dra_rep;
	uint32_t        dra_nr;
};

struct dix_rep {
	int           dre_rc;
	struct m0_buf dre_key;
	struct m0_buf dre_val;
};

#define SERVER_LOG_FILE_NAME "dix_ut.errlog"

/* Configures mero environment with given parameters. */
static char *dix_startup_cmd[] = { "m0d", "-T", "linux",
				"-D", "cs_sdb", "-S", "cs_stob",
				"-A", "linuxstob:cs_addb_stob",
				"-e", "lnet:0@lo:12345:34:1",
				"-H", "0@lo:12345:34:1",
				"-w", "10",
				"-F",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_SRC_PATH("dix/ut/conf.xc")};
static const char *cdbnames[]         = { "dix1" };
static const char *cl_ep_addrs[]      = { "0@lo:12345:34:2" };
static const char *srv_ep_addrs[]     = { "0@lo:12345:34:1" };
static struct m0_net_xprt *cs_xprts[] = { &m0_net_lnet_xprt};

static struct cl_ctx            dix_ut_cctx;
static struct m0_rpc_server_ctx dix_ut_sctx = {
		.rsx_xprts            = cs_xprts,
		.rsx_xprts_nr         = ARRAY_SIZE(cs_xprts),
		.rsx_argv             = dix_startup_cmd,
		.rsx_argc             = ARRAY_SIZE(dix_startup_cmd),
		.rsx_log_file_name    = SERVER_LOG_FILE_NAME
};

static const char *ut_profile = "<0x7000000000000001:0>";
#define DFID(x, y) M0_FID_TINIT('x', (x), (y))

/*
 * Ranges: (8, 9);  (3, 4); (2, 5); (1, 2); (6, 7)
 * Answer: (1, 5); (6, 7); (8, 9)
 */
void imask(void)
{
	struct m0_dix_imask mask;
	int                 rc;
	struct m0_ext       range[] = {
		{ .e_start = 8, .e_end = 9 },
		{ .e_start = 3, .e_end = 4 },
		{ .e_start = 2, .e_end = 5 },
		{ .e_start = 1, .e_end = 2 },
		{ .e_start = 6, .e_end = 7 },
	};

	M0_SET0(&mask);
	rc = m0_dix_imask_init(&mask, range, ARRAY_SIZE(range));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(mask.im_nr == ARRAY_SIZE(range));
	M0_UT_ASSERT(m0_forall(i, mask.im_nr,
				 mask.im_range[i].e_start == range[i].e_start &&
				 mask.im_range[i].e_end == range[i].e_end));
	m0_dix_imask_fini(&mask);
}

#define BYTES(bit) ((bit + 7) / 8)

static char get_bit(void *buffer, m0_bcount_t pos)
{
	m0_bcount_t byte_num = BYTES(pos + 1) - 1;
	char        byte_val = *((char*)buffer + byte_num);
	m0_bcount_t bit_shift = pos % 8;
	char        bit_val  = 0x1 & (byte_val >> bit_shift);

	return bit_val;
}

void imask_apply(void)
{
	struct m0_dix_imask  mask;
	int                  rc;
	char                 buffer[200];
	char                *result;
	m0_bcount_t          len;
	int                  i;
	struct m0_ext        range[] = {
		{ .e_start = 8, .e_end = 9 },
		{ .e_start = 3, .e_end = 4 },
		{ .e_start = 2, .e_end = 5 },
		{ .e_start = 1, .e_end = 2 },
		{ .e_start = 6, .e_end = 7 },
	};

	/*
	 * Bit string is (since we are little-endian):
	 * 1110000011100000...
	 */
	memset(buffer, 7, sizeof(buffer));
	M0_SET0(&mask);
	rc = m0_dix_imask_init(&mask, range, ARRAY_SIZE(range));
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_imask_apply(buffer, sizeof(buffer), &mask,
				(void*)&result, &len);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(result != NULL);
	M0_UT_ASSERT(len == 12);
	for (i = 0; i < len; i++) {
		char val = get_bit(result, i);

		if (i == 0 || i == 1 || i == 4 || i == 8 || i == 9)
			M0_UT_ASSERT(val == 1);
		else
			M0_UT_ASSERT(val == 0);
	}
	m0_dix_imask_fini(&mask);
	m0_free(result);
}

static void imask_empty(void)
{
	struct m0_dix_imask  mask;
	int                  rc;
	char                 buffer[200];
	char                *result;
	m0_bcount_t          len;

	M0_SET0(&mask);
	rc = m0_dix_imask_init(&mask, NULL, 0);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_imask_apply(buffer, sizeof(buffer), &mask,
				(void*)&result, &len);
	M0_UT_ASSERT(rc == 0);
	m0_dix_imask_fini(&mask);
	M0_UT_ASSERT(result == NULL);
	M0_UT_ASSERT(len == 0);
}

static void imask_infini(void)
{
	struct m0_dix_imask  mask;
	int                  rc;
	char                 buffer[200];
	char                *result;
	m0_bcount_t          len;
	int                  i;
	struct m0_ext        range[] = {
		{ .e_start = 8,  .e_end = 9 },
		{ .e_start = 16, .e_end = IMASK_INF},
		{ .e_start = 2,  .e_end = 5 },
		{ .e_start = 8,  .e_end = IMASK_INF },
	};
	memset(buffer, 0xff, sizeof(buffer));
	M0_SET0(&mask);
	rc = m0_dix_imask_init(&mask, range, ARRAY_SIZE(range));
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_imask_apply(buffer, sizeof(buffer), &mask,
				(void*)&result, &len);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(result != NULL);
	M0_UT_ASSERT(len == 2 + sizeof(buffer) * 8 - 16 + 4 +
		     sizeof(buffer) * 8 - 8);
	for (i = 0; i < len; i++) {
		char val = get_bit(result, i);

		M0_UT_ASSERT(val == 1);
	}
	m0_dix_imask_fini(&mask);
	m0_free(result);
}

static void imask_short(void)
{
	struct m0_dix_imask  mask;
	int                  rc;
	char                 buffer[10];
	char                *result;
	m0_bcount_t          len;
	int                  i;
	struct m0_ext  range[] = {
		{ .e_start = 8, .e_end = 600 },
	};

	memset(buffer, 0xff, sizeof(buffer));
	M0_SET0(&mask);
	rc = m0_dix_imask_init(&mask, range, ARRAY_SIZE(range));
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_imask_apply(buffer, sizeof(buffer), &mask,
				(void*)&result, &len);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(result != NULL);
	M0_UT_ASSERT(len == sizeof(buffer) * 8 - range[0].e_start);
	for (i = 0; i < len; i++) {
		char val = get_bit(result, i);

		M0_UT_ASSERT(val == 1);
	}
	m0_dix_imask_fini(&mask);
	m0_free(result);
}

static void imask_invalid(void)
{
	struct m0_dix_imask  mask;
	int                  rc;
	char                 buffer[10];
	char                *result;
	m0_bcount_t          len;
	struct m0_ext        range[] = {
		{ .e_start = 100, .e_end = 600 },
	};

	memset(buffer, 0xff, sizeof(buffer));
	M0_SET0(&mask);
	rc = m0_dix_imask_init(&mask, range, ARRAY_SIZE(range));
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_imask_apply(buffer, sizeof(buffer), &mask,
				 (void*)&result, &len);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(result == NULL);
	M0_UT_ASSERT(len == 0);
	m0_dix_imask_fini(&mask);
}

static void layout_check(struct m0_dix_linst *dli)
{
	uint32_t      W;
	struct m0_buf key = M0_BUF_INIT0;
	m0_bcount_t   key_len = 200;
	uint64_t      unit;
	uint64_t      id1;
	uint64_t      id2;
	int           rc;

	rc = m0_buf_alloc(&key, key_len);
	M0_UT_ASSERT(rc == 0);
	memset(key.b_addr, 7, key_len);
	W = dli->li_pl->pl_attr.pa_N + 2 * dli->li_pl->pl_attr.pa_K;
	for (unit = 0; unit < W; ++unit) {
		m0_dix_target(dli, unit, &key, &id1);
		m0_dix_target(dli, unit, &key, &id2);
		M0_UT_ASSERT(id1 == id2);
	}
	m0_buf_free(&key);
}

static void layout_create(struct m0_layout_domain *domain,
			  struct m0_pool_version  *pver)
{
	int rc;

	rc = m0_layout_init_by_pver(domain, pver, NULL);
	M0_UT_ASSERT(rc == 0);
}

void pdclust_map(void)
{
	struct m0_pool_version     pool_ver;
	uint32_t                   cache_nr;
	uint64_t                  *cache_len;
	struct m0_pool             pool;
	struct m0_fid              fid;
	struct m0_layout_domain    domain;
	uint64_t                   id;
	int                        rc;
	struct m0_dix_linst        dli;
	struct m0_dix_ldesc        dld;
	struct m0_ext              range[] = {{.e_start = 0, .e_end = 100}};

	fid = DFID(0,1);
	rc = m0_pool_init(&pool, &M0_FID_TINIT('o', 0, 1));
	M0_UT_ASSERT(rc == 0);
	rc = m0_layout_domain_init(&domain);
	M0_UT_ASSERT(rc == 0);
	rc = m0_layout_standard_types_register(&domain);
	M0_UT_ASSERT(rc == 0);
	/* Init pool_ver. */
	cache_nr = 1;
	M0_ALLOC_ARR(cache_len, cache_nr);
	M0_UT_ASSERT(cache_len != NULL);
	pool_ver.pv_id = M0_FID_TINIT('v', 1, 1);
	pool_ver.pv_fd_tree.ft_cache_info.fci_nr   = cache_nr;
	pool_ver.pv_fd_tree.ft_cache_info.fci_info = cache_len;
	pool_ver.pv_attr.pa_N = DIX_M0T1FS_LAYOUT_N;
	pool_ver.pv_attr.pa_K = DIX_M0T1FS_LAYOUT_K;
	pool_ver.pv_attr.pa_P = DIX_M0T1FS_LAYOUT_P;
	cache_len[0] = DIX_M0T1FS_LAYOUT_P;
	layout_create(&domain, &pool_ver);
	/* Init layout. */
	m0_dix_ldesc_init(&dld, range, ARRAY_SIZE(range), HASH_FNC_CITY,
			   &pool_ver.pv_id);
	id = m0_pool_version2layout_id(&pool_ver.pv_id, M0_DEFAULT_LAYOUT_ID);
	rc = m0_dix_layout_init(&dli, &domain, &fid, id, &pool_ver, &dld);
	M0_UT_ASSERT(rc == 0);
	layout_check(&dli);
	m0_dix_layout_fini(&dli);
	m0_dix_ldesc_fini(&dld);
	m0_layout_domain_cleanup(&domain);
	m0_layout_standard_types_unregister(&domain);
	m0_layout_domain_fini(&domain);
	m0_pool_fini(&pool);
	m0_free(cache_len);
}

void meta_val_encdec(void)
{
	struct m0_fid       fid  = DFID(0,1);
	struct m0_fid       fid2 = DFID(0,100);
	struct m0_fid       pfid = DFID(0,1);
	struct m0_ext       range[] = {
		{.e_start = 0,   .e_end = 100},
		{.e_start = 110, .e_end = 200},
		{.e_start = 300, .e_end = 400},
	};
	int                 rc;
	struct m0_bufvec    bv;
	struct m0_dix_ldesc dld;
	struct m0_dix_ldesc dld2;

	rc = m0_dix_ldesc_init(&dld, range, ARRAY_SIZE(range),
			       HASH_FNC_CITY, &pfid);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix__meta_val_enc(&fid, &dld, 1, &bv);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix__meta_val_dec(&bv, &fid2, &dld2, 1);
	m0_bufvec_free(&bv);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_fid_eq(&fid, &fid2));
	M0_UT_ASSERT(dld2.ld_hash_fnc == HASH_FNC_CITY);
	M0_UT_ASSERT(dld.ld_hash_fnc == dld2.ld_hash_fnc);
	M0_UT_ASSERT(m0_fid_eq(&dld.ld_pver, &dld2.ld_pver));
	M0_UT_ASSERT(dld.ld_imask.im_nr == dld2.ld_imask.im_nr);
	M0_UT_ASSERT(m0_forall(i, ARRAY_SIZE(range),
		     dld.ld_imask.im_range[i].e_start ==
		     dld2.ld_imask.im_range[i].e_start &&
		     dld.ld_imask.im_range[i].e_end ==
		     dld2.ld_imask.im_range[i].e_end));
	m0_dix_ldesc_fini(&dld);
	m0_dix_ldesc_fini(&dld2);
}

void meta_val_encdec_n(void)
{
	struct m0_fid       fid[COUNT];
	struct m0_fid       fid2[COUNT];
	struct m0_ext       range[] = {
		{.e_start = 0,   .e_end = 100},
		{.e_start = 110, .e_end = 200},
		{.e_start = 300, .e_end = 400},
	};
	int                 rc;
	struct m0_bufvec    bv;
	struct m0_dix_ldesc dld[COUNT];
	struct m0_dix_ldesc dld2[COUNT];
	int                 i;

	for(i = 0; i < ARRAY_SIZE(fid); i++) {
		struct m0_fid f = DFID(1,i);

		fid[i]  = DFID(0,i+1);
		fid2[i] = DFID(1,i);
		rc = m0_dix_ldesc_init(&dld[i], range, ARRAY_SIZE(range),
				       HASH_FNC_CITY, &f);
		M0_UT_ASSERT(rc == 0);
	}
	rc = m0_dix__meta_val_enc(fid, dld, ARRAY_SIZE(fid), &bv);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix__meta_val_dec(&bv, fid2, dld2, ARRAY_SIZE(fid));
	m0_bufvec_free(&bv);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < ARRAY_SIZE(fid); i++) {
		M0_UT_ASSERT(m0_fid_eq(&fid[i], &fid[i]));
		M0_UT_ASSERT(dld2[i].ld_hash_fnc == HASH_FNC_CITY);
		M0_UT_ASSERT(dld[i].ld_hash_fnc == dld2[i].ld_hash_fnc);
		M0_UT_ASSERT(m0_fid_eq(&dld[i].ld_pver, &dld2[i].ld_pver));
		M0_UT_ASSERT(dld[i].ld_imask.im_nr == dld2[i].ld_imask.im_nr);
		M0_UT_ASSERT(m0_forall(j, ARRAY_SIZE(range),
			     dld[i].ld_imask.im_range[j].e_start ==
			     dld2[i].ld_imask.im_range[j].e_start &&
			     dld[i].ld_imask.im_range[j].e_end ==
			     dld2[i].ld_imask.im_range[j].e_end));
		m0_dix_ldesc_fini(&dld[i]);
		m0_dix_ldesc_fini(&dld2[i]);
	}
}

void layout_encdec(void)
{
	struct m0_ext        range[] = {
		{.e_start = 0,   .e_end = 100},
		{.e_start = 110, .e_end = 200},
		{.e_start = 300, .e_end = 400},
	};
	int                  rc;
	struct m0_dix_ldesc  dld1;
	struct m0_dix_ldesc  dld2;
	struct m0_dix_ldesc *pdld;
	struct m0_bufvec     key;
	struct m0_bufvec     val;
	uint64_t             lid1 = 1010101;
	uint64_t             lid2;
	struct m0_fid        fid = DFID(0,1);
	struct m0_fid        fid1 = DFID(0,100);
	struct m0_fid        fid2 = DFID(0,101);
	struct m0_dix_layout dlay1;
	struct m0_dix_layout dlay2;

	/*
	 * Encode/decode values that are used to work with 'layout-descriptor'
	 * index.
	 */
	rc = m0_dix_ldesc_init(&dld1, range, ARRAY_SIZE(range),
			       HASH_FNC_CITY, &fid);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix__ldesc_vals_enc(&lid1, &dld1, 1, &key, &val);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix__ldesc_vals_dec(&key, &val, &lid2, &dld2, 1);
	M0_UT_ASSERT(rc == 0);
	m0_bufvec_free(&key);
	m0_bufvec_free(&val);
	M0_UT_ASSERT(lid1 == lid2);
	M0_UT_ASSERT(m0_fid_eq(&dld1.ld_pver, &dld2.ld_pver));
	M0_UT_ASSERT(dld2.ld_hash_fnc == HASH_FNC_CITY);
	M0_UT_ASSERT(dld1.ld_hash_fnc == dld2.ld_hash_fnc);
	M0_UT_ASSERT(dld1.ld_imask.im_nr == dld2.ld_imask.im_nr);
	M0_UT_ASSERT(m0_forall(i, ARRAY_SIZE(range),
		     dld1.ld_imask.im_range[i].e_start ==
		     dld2.ld_imask.im_range[i].e_start &&
		     dld1.ld_imask.im_range[i].e_end ==
		     dld2.ld_imask.im_range[i].e_end));
	m0_dix_ldesc_fini(&dld1);
	m0_dix_ldesc_fini(&dld2);

	/* Encode/decode values that are used to work with 'layout' index. */
	dlay1.dl_type = DIX_LTYPE_ID;
	dlay1.u.dl_id = lid1;
	rc = m0_dix__layout_vals_enc(&fid1, &dlay1, 1, &key, &val);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix__layout_vals_dec(&key, &val, &fid2, &dlay2, 1);
	M0_UT_ASSERT(rc == 0);
	m0_bufvec_free(&key);
	m0_bufvec_free(&val);
	M0_UT_ASSERT(m0_fid_eq(&fid1, &fid2));
	M0_UT_ASSERT(dlay1.dl_type = dlay2.dl_type);
	M0_UT_ASSERT(dlay2.dl_type = DIX_LTYPE_ID);

	rc = m0_dix_ldesc_init(&dld1, range, ARRAY_SIZE(range),
			       HASH_FNC_FNV1, &fid);
	M0_UT_ASSERT(rc == 0);
	dlay1.dl_type = DIX_LTYPE_DESCR;
	m0_dix_ldesc_copy(&dlay1.u.dl_desc, &dld1);
	rc = m0_dix__layout_vals_enc(&fid1, &dlay1, 1, &key, &val);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix__layout_vals_dec(&key, &val, &fid2, &dlay2, 1);
	M0_UT_ASSERT(rc == 0);
	m0_bufvec_free(&key);
	m0_bufvec_free(&val);
	M0_UT_ASSERT(m0_fid_eq(&fid1, &fid2));
	M0_UT_ASSERT(dlay1.dl_type = dlay2.dl_type);
	M0_UT_ASSERT(dlay2.dl_type = DIX_LTYPE_DESCR);
	pdld = &dlay2.u.dl_desc;
	M0_UT_ASSERT(pdld->ld_hash_fnc == HASH_FNC_FNV1);
	M0_UT_ASSERT(pdld->ld_hash_fnc == dld1.ld_hash_fnc);
	M0_UT_ASSERT(pdld->ld_imask.im_nr == dld1.ld_imask.im_nr);
	M0_UT_ASSERT(m0_forall(i, ARRAY_SIZE(range),
		     dld1.ld_imask.im_range[i].e_start ==
		     pdld->ld_imask.im_range[i].e_start &&
		     dld1.ld_imask.im_range[i].e_end ==
		     pdld->ld_imask.im_range[i].e_end));
	m0_dix_ldesc_fini(&dlay1.u.dl_desc);
	m0_dix_ldesc_fini(&dlay2.u.dl_desc);
	m0_dix_ldesc_fini(&dld1);
}

static uint64_t dix_key(uint32_t seq_num)
{
	return seq_num;
}

static uint64_t dix_val(uint32_t seq_num)
{
	return 100 + seq_num;
}

static void dix__vals_check(struct dix_rep_arr *rep,
			    uint32_t            first,
			    uint32_t            last)
{
	uint32_t i;
	uint32_t count = last - first + 1;
	uint64_t key;
	uint64_t val;

	M0_UT_ASSERT(rep->dra_nr == count);
	for (i = 0; i < count; i++) {
		M0_UT_ASSERT(rep->dra_rep[i].dre_rc == 0);
		key = *(uint64_t *)rep->dra_rep[i].dre_key.b_addr;
		val = *(uint64_t *)rep->dra_rep[i].dre_val.b_addr;
		M0_UT_ASSERT(key == dix_key(i));
		M0_UT_ASSERT(val == dix_val(i + first * 100));
	}
}

static void dix_vals_check(struct dix_rep_arr *rep, uint32_t count)
{
	dix__vals_check(rep, 0, count - 1);
}

static void dix_index_init(struct m0_dix *index, uint32_t id)
{
	struct m0_ext        range = {.e_start = 0, .e_end = IMASK_INF};
	struct m0_dix_ldesc *ldesc = &index->dd_layout.u.dl_desc;
	int                  rc;

	rc = m0_dix_ldesc_init(ldesc, &range, 1, HASH_FNC_CITY,
			       &dix_ut_cctx.cl_pver);
	M0_UT_ASSERT(rc == 0);
	index->dd_layout.dl_type = DIX_LTYPE_DESCR;
	index->dd_fid            = DFID(1, id);
}

static void dix_index_fini(struct m0_dix *index)
{
	m0_dix_fini(index);
}

static void dix__kv_alloc_and_fill(struct m0_bufvec *keys,
				   struct m0_bufvec *vals,
				   uint32_t          first,
				   uint32_t          last)
{
	uint32_t i;
	uint32_t count = last - first + 1;
	int      rc;

	rc = m0_bufvec_alloc(keys, count, sizeof (uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(vals, count, sizeof (uint64_t));
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < count; i++) {
		*(uint64_t *)keys->ov_buf[i] = dix_key(i);
		*(uint64_t *)vals->ov_buf[i] = dix_val(i + first * 100);
	}
}

static void dix_kv_alloc_and_fill(struct m0_bufvec *keys,
				  struct m0_bufvec *vals,
				  uint32_t          count)
{
	dix__kv_alloc_and_fill(keys, vals, 0, count - 1);
}

static void dix_kv_destroy(struct m0_bufvec *keys,
			   struct m0_bufvec *vals)
{
	m0_bufvec_free(keys);
	m0_bufvec_free(vals);
}

static void dix_rep_free(struct dix_rep_arr *rep)
{
	uint32_t i;

	for (i = 0; i < rep->dra_nr; i++) {
		if (rep->dra_rep[i].dre_rc == 0) {
			m0_buf_free(&rep->dra_rep[i].dre_key);
			m0_buf_free(&rep->dra_rep[i].dre_val);
		}
	}
	m0_free(rep->dra_rep);
}

static int dix_client_init(struct cl_ctx *cctx, const char *cl_ep_addr,
			   const char *srv_ep_addr, const char* dbname,
			   struct m0_net_xprt *xprt)
{
	int                        rc;
	struct m0_rpc_client_ctx  *cl_rpc_ctx;
	struct m0_conf_filesystem *fs;
	struct m0_confc_args      *confc_args;
	struct m0_pools_common    *pc = &cctx->cl_pools_common;

	M0_PRE(cctx != NULL && cl_ep_addr != NULL && srv_ep_addr != NULL &&
	       dbname != NULL && xprt != NULL);

	/* Initialise layout domain for DIX client. */
	rc = m0_layout_domain_init(&cctx->cl_ldom);
	M0_UT_ASSERT(rc == 0);
	rc = m0_layout_standard_types_register(&cctx->cl_ldom);
	M0_UT_ASSERT(rc == 0);

	/* Start RPC client. */
	rc = m0_net_domain_init(&cctx->cl_ndom, xprt);
	M0_UT_ASSERT(rc == 0);
	cl_rpc_ctx = &cctx->cl_rpc_ctx;
	cl_rpc_ctx->rcx_net_dom            = &cctx->cl_ndom;
	cl_rpc_ctx->rcx_local_addr         = cl_ep_addr;
	cl_rpc_ctx->rcx_remote_addr        = srv_ep_addr;
	cl_rpc_ctx->rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT;
	cl_rpc_ctx->rcx_fid                = &g_process_fid;
	/*
	 * The value is used to set a number of buffers
	 * (m0_rpc_net_buffer_pool_setup()) and queue_len
	 * (m0_rpc_machine_init()) during m0_rpc_client_start(). Default value
	 * is M0_NET_TM_RECV_QUEUE_DEF_LEN which equals to 2. Need to set a
	 * larger value. 10 is enough.
	 */
	cl_rpc_ctx->rcx_recv_queue_min_length = RECV_QUEUE_LEN;

	rc = m0_rpc_client_start(cl_rpc_ctx);
	M0_UT_ASSERT(rc == 0);

	/*
	 * Initialise pools common structure, it's necessary for normal DIX
	 * client functioning.
	 */
	rc = m0_fid_sscanf(ut_profile,
			   &cl_rpc_ctx->rcx_reqh.rh_profile);
	M0_UT_ASSERT(rc == 0);
	confc_args = &(struct m0_confc_args) {
		.ca_profile = ut_profile,
		.ca_rmach   = &cl_rpc_ctx->rcx_rpc_machine,
		.ca_group   = m0_locality0_get()->lo_grp
	};
	rc = m0_reqh_conf_setup(&cl_rpc_ctx->rcx_reqh, confc_args);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&cl_rpc_ctx->rcx_reqh.rh_rconfc,
				  &cl_rpc_ctx->rcx_reqh.rh_profile);
	M0_UT_ASSERT(rc == 0);
	rc = m0_conf_fs_get(&cl_rpc_ctx->rcx_reqh.rh_profile,
			    m0_reqh2confc(&cl_rpc_ctx->rcx_reqh), &fs);
	M0_UT_ASSERT(rc == 0);
	rc = m0_pools_common_init(pc, &cl_rpc_ctx->rcx_rpc_machine, fs);
	M0_UT_ASSERT(rc == 0);
	rc = m0_pools_setup(pc, fs, NULL, NULL, NULL);
	M0_UT_ASSERT(rc == 0);
	rc = m0_pools_service_ctx_create(pc, fs);
	M0_UT_ASSERT(rc == 0);
	/* Wait until all services are connected. */
	m0_pools_common_service_ctx_connect_sync(pc);
	/*
	 * Unfortunately pools common structure uses layout domain from request
	 * handler, so it's necessary to initialise it. Please note, that DIX
	 * client uses another one (cctx->cl_ldom).
	 */
	rc = m0_layout_domain_init(&cl_rpc_ctx->rcx_reqh.rh_ldom);
	M0_UT_ASSERT(rc == 0);
	rc = m0_layout_standard_types_register(&cl_rpc_ctx->rcx_reqh.rh_ldom);
	M0_UT_ASSERT(rc == 0);
	rc = m0_pool_versions_setup(pc, fs, NULL, NULL, NULL);
	M0_UT_ASSERT(rc == 0);
	m0_confc_close(&fs->cf_obj);
	return rc;
}

static void dix_client_fini(struct cl_ctx *cctx)
{
	struct m0_pools_common  *pc = &cctx->cl_pools_common;
	struct m0_layout_domain *rpc_client_ldom;
	int                      rc;

	rpc_client_ldom = &cctx->cl_rpc_ctx.rcx_reqh.rh_ldom;
	m0_pool_versions_destroy(pc);
	m0_layout_domain_cleanup(rpc_client_ldom);
	m0_layout_standard_types_unregister(rpc_client_ldom);
	m0_layout_domain_fini(rpc_client_ldom);
	m0_pools_service_ctx_destroy(pc);
	m0_pools_destroy(pc);
	m0_pools_common_fini(pc);

	m0_rconfc_stop_sync(&cctx->cl_rpc_ctx.rcx_reqh.rh_rconfc);
	m0_rconfc_fini(&cctx->cl_rpc_ctx.rcx_reqh.rh_rconfc);
	rc = m0_rpc_client_stop(&cctx->cl_rpc_ctx);
	M0_UT_ASSERT(rc == 0);
	m0_layout_domain_cleanup(&cctx->cl_ldom);
	m0_layout_standard_types_unregister(&cctx->cl_ldom);
	m0_layout_domain_fini(&cctx->cl_ldom);
	m0_net_domain_fini(&cctx->cl_ndom);
}

static void dixc_ut_init(struct m0_rpc_server_ctx *sctx,
			 struct cl_ctx            *cctx)
{
	int rc;

	M0_SET0(&sctx->rsx_mero_ctx);
	m0_fi_enable_once("m0_rpc_machine_init", "bulk_cutoff_4K");
	rc = m0_rpc_server_start(sctx);
	M0_UT_ASSERT(rc == 0);
	rc = dix_client_init(cctx, cl_ep_addrs[0],
			     srv_ep_addrs[0], cdbnames[0],
			     cs_xprts[0]);
	M0_UT_ASSERT(rc == 0);
}

static void dixc_ut_fini(struct m0_rpc_server_ctx *sctx,
			 struct cl_ctx            *cctx)
{
	dix_client_fini(cctx);
	m0_rpc_server_stop(sctx);
}

static int ut_pver_find(struct m0_reqh *reqh, struct m0_fid *out)
{
	struct m0_conf_filesystem *fs;
	int                        rc;

	rc = m0_conf_fs_get(&reqh->rh_profile, m0_reqh2confc(reqh), &fs);
	if (rc != 0)
		return M0_ERR(rc);
	*out = fs->cf_imeta_pver;
	m0_confc_close(&fs->cf_obj);
	return 0;
}

static void ut_service_init(void)
{
	struct m0_dix_cli *cli = &dix_ut_cctx.cl_cli;
	struct m0_ext      range[] = {
		{ .e_start = 0, .e_end = IMASK_INF },
	};
	int                rc;

	dixc_ut_init(&dix_ut_sctx, &dix_ut_cctx);

	dix_ut_cctx.cl_grp = m0_locality0_get()->lo_grp;

	rc = ut_pver_find(&dix_ut_cctx.cl_rpc_ctx.rcx_reqh,
			  &dix_ut_cctx.cl_pver);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_cli_init(cli,
			     dix_ut_cctx.cl_grp,
			     &dix_ut_cctx.cl_pools_common,
			     &dix_ut_cctx.cl_ldom,
			     &dix_ut_cctx.cl_pver);
	M0_UT_ASSERT(rc == 0);
	m0_dix_cli_bootstrap_lock(cli);
	layout_create(&dix_ut_cctx.cl_ldom, dix_ut_cctx.cl_cli.dx_pver);

	/* Create meta indices (root, layout, layout-descr). */
	rc = m0_dix_ldesc_init(&dix_ut_cctx.cl_dld1, range, ARRAY_SIZE(range),
			       HASH_FNC_CITY, &dix_ut_cctx.cl_pver);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_ldesc_init(&dix_ut_cctx.cl_dld2, range, ARRAY_SIZE(range),
			       HASH_FNC_CITY, &dix_ut_cctx.cl_pver);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_meta_create(&dix_ut_cctx.cl_cli,
				dix_ut_cctx.cl_grp,
				&dix_ut_cctx.cl_dld1,
				&dix_ut_cctx.cl_dld2);
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_cli_start_sync(&dix_ut_cctx.cl_cli);
	M0_UT_ASSERT(rc == 0);
	m0_dix_ldesc_fini(&dix_ut_cctx.cl_dld1);
	m0_dix_ldesc_fini(&dix_ut_cctx.cl_dld2);
}

static void ut_service_fini(void)
{
	m0_dix_cli_lock(&dix_ut_cctx.cl_cli);
	m0_dix_cli_stop(&dix_ut_cctx.cl_cli);
	m0_dix_cli_unlock(&dix_ut_cctx.cl_cli);
	m0_dix_cli_fini_lock(&dix_ut_cctx.cl_cli);
	dixc_ut_fini(&dix_ut_sctx, &dix_ut_cctx);
}

static void dix_meta_create(void)
{
	ut_service_init();
	ut_service_fini();
}

static int dix_common_idx_op(const struct m0_dix *indices, uint32_t indices_nr,
			     enum ut_dix_req_type type)
{
	struct m0_dix_req req;
	int               rc;
	int               ret;
	int               i;

	m0_dix_req_init(&req, &dix_ut_cctx.cl_cli, dix_ut_cctx.cl_grp);
	m0_dix_req_lock(&req);
	switch (type) {
	case REQ_CREATE:
		rc = m0_dix_create(&req, indices, indices_nr, NULL);
		break;
	case REQ_DELETE:
		rc = m0_dix_delete(&req, indices, indices_nr, NULL);
		break;
	case REQ_LOOKUP:
		rc = m0_dix_cctgs_lookup(&req, indices, indices_nr);
		break;
	default:
		M0_IMPOSSIBLE("Unknown req type %u", type);
	}
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_req_wait(&req, M0_BITS(DIXREQ_FINAL, DIXREQ_FAILURE),
			      M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	ret = m0_dix_generic_rc(&req);
	if (ret == 0)
		for (i = 0; i < m0_dix_req_nr(&req); i++) {
			ret = m0_dix_item_rc(&req, i);
			if (ret != 0)
				break;
		}
	m0_dix_req_unlock(&req);
	m0_dix_req_fini_lock(&req);
	return ret;
}

static int dix_common_rec_op(const struct m0_dix    *index,
			     const struct m0_bufvec *keys,
			     struct m0_bufvec       *vals,
			     const uint32_t         *recs_nr,
			     uint32_t                flags,
			     struct dix_rep_arr     *rep,
			     enum ut_dix_req_type    type)
{
	struct m0_dix_req req;
	int               rc;
	int               i;
	int               k = 0;

	m0_dix_req_init(&req, &dix_ut_cctx.cl_cli, dix_ut_cctx.cl_grp);
	m0_dix_req_lock(&req);
	switch (type) {
	case REQ_PUT:
		rc = m0_dix_put(&req, index, keys, vals, NULL, flags);
		break;
	case REQ_DEL:
		rc = m0_dix_del(&req, index, keys, NULL);
		break;
	case REQ_GET:
		rc = m0_dix_get(&req, index, keys);
		break;
	case REQ_NEXT:
		rc = m0_dix_next(&req, index, keys, recs_nr);
		break;
	default:
		M0_IMPOSSIBLE("Unknown req type %u", type);
		break;
	}
	M0_UT_ASSERT(rc == 0);
	rc = m0_dix_req_wait(&req, M0_BITS(DIXREQ_FINAL, DIXREQ_FAILURE),
			     M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	m0_dix_req_unlock(&req);
	rc = m0_dix_generic_rc(&req);
	if (rc != 0) {
		rep->dra_nr = 0;
		rep->dra_rep = NULL;
		goto out;
	}
	if (type == REQ_NEXT)
		rep->dra_nr = m0_reduce(i, keys->ov_vec.v_nr,
				0, + (m0_dix_item_rc(&req, i) ?
					0 : m0_dix_next_rep_nr(&req, i)));
	else
		rep->dra_nr = m0_dix_req_nr(&req);
	if (rep->dra_nr == 0) {
		rep->dra_rep = NULL;
		rc = M0_ERR(-ENOENT);
		goto out;
	}
	M0_ALLOC_ARR(rep->dra_rep, rep->dra_nr);
	M0_UT_ASSERT(rep->dra_rep != NULL);
	switch (type) {
	case REQ_PUT:
	case REQ_DEL:
		for (i = 0; i < rep->dra_nr; i++)
			rep->dra_rep[i].dre_rc =
				m0_dix_item_rc(&req, i);
		break;
	case REQ_GET:
		for (i = 0; i < rep->dra_nr; i++) {
			struct m0_dix_get_reply get_rep;

			m0_dix_get_rep(&req, i, &get_rep);
			rep->dra_rep[i].dre_rc = get_rep.dgr_rc;
			if (rep->dra_rep[i].dre_rc != 0)
				continue;
			m0_buf_copy(&rep->dra_rep[i].dre_key,
				    &(struct m0_buf) {
					.b_addr = keys->ov_buf[i],
					.b_nob  = keys->ov_vec.v_count[i]
				     });
			m0_buf_copy(&rep->dra_rep[i].dre_val,
				    &get_rep.dgr_val);
		}
		break;
	case REQ_NEXT:
		for (i = 0; i < keys->ov_vec.v_nr; i++) {
			struct m0_dix_next_reply nrep;
			uint32_t                 j;
			int                      rc2;

			rc2 = m0_dix_item_rc(&req, i);
			if (rc2 != 0) {
				rep->dra_rep[k++].dre_rc = rc2;
				continue;
			}
			for (j = 0; j < m0_dix_next_rep_nr(&req, i); j++) {
				m0_dix_next_rep(&req, i, j, &nrep);
				rep->dra_rep[k].dre_rc = 0;
				m0_buf_copy(&rep->dra_rep[k].dre_key,
					    &nrep.dnr_key);
				m0_buf_copy(&rep->dra_rep[k].dre_val,
					    &nrep.dnr_val);
				k++;
			}
		}
		break;
	default:
		M0_IMPOSSIBLE("Unknown req type");
	}
out:
	m0_dix_req_fini_lock(&req);
	return rc;
}

static int dix_ut_put(const struct m0_dix    *index,
		      const struct m0_bufvec *keys,
		      struct m0_bufvec       *vals,
		      uint32_t                flags,
		      struct dix_rep_arr     *rep)
{
	return dix_common_rec_op(index, keys, vals, NULL, flags, rep, REQ_PUT);
}

static int dix_ut_get(const struct m0_dix    *index,
		      const struct m0_bufvec *keys,
		      struct dix_rep_arr     *rep)
{
	return dix_common_rec_op(index, keys, NULL, NULL, 0, rep, REQ_GET);
}

static int dix_ut_del(const struct m0_dix    *index,
		      const struct m0_bufvec *keys,
		      struct dix_rep_arr     *rep)
{
	return dix_common_rec_op(index, keys, NULL, NULL, 0, rep, REQ_DEL);
}

static int dix_ut_next(const struct m0_dix    *index,
		       const struct m0_bufvec *start_keys,
		       const uint32_t         *recs_nr,
		       struct dix_rep_arr     *rep)
{
	return dix_common_rec_op(index, start_keys, NULL, recs_nr, 0, rep,
				 REQ_NEXT);
}

static void dix_index_create_and_fill(const struct m0_dix    *index,
				      const struct m0_bufvec *keys,
				      struct m0_bufvec       *vals)
{
	struct dix_rep_arr rep;
	int                rc;

	rc = dix_common_idx_op(index, 1, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);
	rc = dix_ut_put(index, keys, vals, 0, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == 0));
	dix_rep_free(&rep);
}

static void dix_create(void)
{
	struct m0_dix indices[COUNT_INDEX];
	uint32_t      indices_nr = ARRAY_SIZE(indices);
	int           i;
	int           rc;

	ut_service_init();
	for (i = 0; i < indices_nr; i++)
		dix_index_init(&indices[i], i);
	rc = dix_common_idx_op(indices, indices_nr, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < indices_nr; i++)
		dix_index_fini(&indices[i]);
	ut_service_fini();
}

static void dix_delete(void)
{
	struct m0_dix indices[COUNT_INDEX];
	uint32_t      indices_nr = ARRAY_SIZE(indices);
	int           i;
	int           rc;

	ut_service_init();
	for (i = 0; i < indices_nr; i++)
		dix_index_init(&indices[i], i);
	rc = dix_common_idx_op(indices, indices_nr, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);
	rc = dix_common_idx_op(indices, indices_nr, REQ_DELETE);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < indices_nr; i++)
		dix_index_fini(&indices[i]);
	ut_service_fini();
}

static int dix_list_op(const struct m0_fid *start_fid,
		       uint32_t             indices_nr,
		       struct m0_fid       *out_fids,
		       uint32_t            *out_fids_nr)
{
	struct m0_dix_meta_req mreq;
	struct m0_clink        clink;
	uint32_t               i;
	int                    rc;

	m0_dix_meta_req_init(&mreq, &dix_ut_cctx.cl_cli, dix_ut_cctx.cl_grp);
	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&mreq.dmr_chan, &clink);
	m0_dix_meta_lock(&mreq);
	rc = m0_dix_index_list(&mreq, start_fid, indices_nr);
	m0_dix_meta_unlock(&mreq);
	M0_UT_ASSERT(rc == 0);
	m0_chan_wait(&clink);
	rc = m0_dix_meta_generic_rc(&mreq) ?:
	     m0_dix_meta_item_rc(&mreq, 0);
	if (rc == 0) {
		*out_fids_nr = m0_dix_index_list_rep_nr(&mreq);
		for (i = 0; i < *out_fids_nr; i++)
			m0_dix_index_list_rep(&mreq, i, &out_fids[i]);
	}
	m0_clink_del_lock(&clink);
	m0_dix_meta_req_fini_lock(&mreq);
	return rc;
}

static void dix_list(void)
{
	enum {
		LIST_INDEX_REQ_NR = 100,
	};

	struct m0_dix indices[COUNT_INDEX];
	uint32_t      indices_nr = ARRAY_SIZE(indices);
	struct m0_fid start_fid;
	struct m0_fid res[LIST_INDEX_REQ_NR];
	uint32_t      res_nr = 0;
	int           i;
	int           rc;

	ut_service_init();
	for (i = 0; i < indices_nr; i++)
		dix_index_init(&indices[i], i);
	rc = dix_common_idx_op(indices, indices_nr, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);

	/* List indices from the beginning. */
	start_fid = M0_FID_INIT(0, 0);
	rc = dix_list_op(&start_fid, LIST_INDEX_REQ_NR, res, &res_nr);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(res_nr == indices_nr);
	M0_UT_ASSERT(m0_forall(i, indices_nr,
			       m0_fid_eq(&res[i], &DFID(1, i))));

	for (i = 0; i < indices_nr; i++)
		dix_index_fini(&indices[i]);
	ut_service_fini();
}

static void dix_cctgs_lookup(void)
{
	struct m0_dix indices[COUNT_INDEX];
	uint32_t      indices_nr = ARRAY_SIZE(indices);
	struct m0_dix lookup_indices[COUNT_INDEX];
	struct m0_dix unknown_indices[COUNT_INDEX];
	int           i;
	int           rc;

	ut_service_init();
	for (i = 0; i < indices_nr; i++)
		dix_index_init(&indices[i], i);
	rc = dix_common_idx_op(indices, indices_nr, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < indices_nr; i++) {
		lookup_indices[i] = (struct m0_dix) { .dd_fid = DFID(1, i) };
		unknown_indices[i] =
			(struct m0_dix) { .dd_fid = DFID(1, 100 + i) };
	}
	/* Lookup existing indices. */
	rc = dix_common_idx_op(lookup_indices, indices_nr, REQ_LOOKUP);
	M0_UT_ASSERT(rc == 0);
	/* Lookup unknown indices. */
	rc = dix_common_idx_op(unknown_indices, indices_nr, REQ_LOOKUP);
	M0_UT_ASSERT(rc == -ENOENT);
	/* Delete all indices. */
	rc = dix_common_idx_op(indices, indices_nr, REQ_DELETE);
	M0_UT_ASSERT(rc == 0);
	/* Try to lookup deleted items. */
	rc = dix_common_idx_op(lookup_indices, indices_nr, REQ_LOOKUP);
	M0_UT_ASSERT(rc == -ENOENT);
	for (i = 0; i < indices_nr; i++)
		dix_index_fini(&indices[i]);
	ut_service_fini();
}

static void dix_put(void)
{
	struct m0_dix      index;
	struct m0_bufvec   keys;
	struct m0_bufvec   vals;
	struct dix_rep_arr rep;
	int                rc;

	ut_service_init();
	dix_index_init(&index, 1);
	dix_kv_alloc_and_fill(&keys, &vals, COUNT);
	rc = dix_common_idx_op(&index, 1, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);
	rc = dix_ut_put(&index, &keys, &vals, 0, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == 0));
	dix_rep_free(&rep);
	dix_kv_destroy(&keys, &vals);
	dix_index_fini(&index);
	ut_service_fini();
}

static void dix_put_overwrite(void)
{
	struct m0_dix      index;
	struct m0_bufvec   keys;
	struct m0_bufvec   vals;
	struct dix_rep_arr rep;
	uint32_t           recs_nr = COUNT;
	struct m0_bufvec   start_key;
	int                rc;

	ut_service_init();
	dix_index_init(&index, 1);
	dix_kv_alloc_and_fill(&keys, &vals, COUNT);
	rc = dix_common_idx_op(&index, 1, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);
	/* If there are no records, than they should be created. */
	rc = dix_ut_put(&index, &keys, &vals, COF_OVERWRITE, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == 0));
	dix_rep_free(&rep);
	rc = dix_ut_get(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	dix_vals_check(&rep, COUNT);
	dix_rep_free(&rep);
	dix_kv_destroy(&keys, &vals);

	/* If there are records, than they should be overwritten. */
	dix__kv_alloc_and_fill(&keys, &vals, 10, 10 + COUNT - 1);
	rc = dix_ut_put(&index, &keys, &vals, COF_OVERWRITE, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == 0));
	dix_rep_free(&rep);
	rc = dix_ut_get(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	dix__vals_check(&rep, 10, 10 + COUNT - 1);
	dix_rep_free(&rep);

	/* Removing records should clear the index. */
	rc = dix_ut_del(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == 0));
	dix_rep_free(&rep);

	m0_bufvec_alloc(&start_key, 1, sizeof(uint64_t));
	*(uint64_t *)start_key.ov_buf[0] = 0;
	rc = dix_ut_next(&index, &start_key, &recs_nr, &rep);
	M0_UT_ASSERT(rc == -ENOENT);
	dix_rep_free(&rep);
	dix_kv_destroy(&keys, &vals);

	dix_index_fini(&index);
	ut_service_fini();
}

static void dix_get(void)
{
	struct m0_dix      index;
	struct m0_bufvec   keys;
	struct m0_bufvec   vals;
	struct m0_bufvec   ret_vals;
	struct dix_rep_arr rep;
	int                rc;

	ut_service_init();
	dix_index_init(&index, 1);
	dix_kv_alloc_and_fill(&keys, &vals, COUNT);
	dix_index_create_and_fill(&index, &keys, &vals);
	/* Get values by existing keys. */
	rc = m0_bufvec_alloc(&ret_vals, COUNT, sizeof (uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = dix_ut_get(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	dix_vals_check(&rep, COUNT);
	dix_rep_free(&rep);
	/* Get values by non-existing keys. */
	rc = dix_ut_del(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	dix_rep_free(&rep);
	rc = dix_ut_get(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == -ENOENT));
	dix_rep_free(&rep);
	m0_bufvec_free(&ret_vals);
	dix_kv_destroy(&keys, &vals);
	dix_index_fini(&index);
	ut_service_fini();
}

static void dix_get_resend(void)
{
	struct m0_dix      index;
	struct m0_bufvec   keys;
	struct m0_bufvec   vals;
	struct dix_rep_arr rep;
	int                rc;

	ut_service_init();
	dix_index_init(&index, 1);
	dix_kv_alloc_and_fill(&keys, &vals, COUNT);
	dix_index_create_and_fill(&index, &keys, &vals);
	/*
	 * Imitate one failure during GET operation, the record should be
	 * re-requested from parity unit.
	 */
	m0_fi_enable_once("cas_place", "place_fail");
	rc = dix_ut_get(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	dix_vals_check(&rep, COUNT);
	dix_rep_free(&rep);
	/*
	 * Imitate many failures during GET operation, so retrieval from data
	 * and parity unit locations fails.
	 */
	m0_fi_enable("cas_place", "place_fail");
	rc = dix_ut_get(&index, &keys, &rep);
	M0_UT_ASSERT(rc == -EHOSTUNREACH);
	dix_rep_free(&rep);
	m0_fi_disable("cas_place", "place_fail");

	dix_kv_destroy(&keys, &vals);
	dix_index_fini(&index);
	ut_service_fini();
}

static void dix_next(void)
{
	struct m0_dix      index;
	struct m0_bufvec   keys;
	struct m0_bufvec   next_keys;
	struct m0_bufvec   vals;
	struct dix_rep_arr rep;
	uint32_t           recs_nr[COUNT/2];
	int                i;
	int                rc;

	ut_service_init();
	dix_index_init(&index, 1);
	dix_kv_alloc_and_fill(&keys, &vals, COUNT);
	dix_index_create_and_fill(&index, &keys, &vals);
	/* Get values by operation NEXT for existing keys. */
	rc = m0_bufvec_alloc(&next_keys, COUNT/2, sizeof (uint64_t));
	M0_UT_ASSERT(rc == 0);
	/* Request NEXT for all keys. */
	for (i = 0; i < COUNT/2; i++) {
		*(uint64_t *)next_keys.ov_buf[i] = dix_key(2 * i);
		recs_nr[i]                       = 2;
	}
	rc = dix_ut_next(&index, &next_keys, recs_nr, &rep);
	M0_UT_ASSERT(rc == 0);
	dix_vals_check(&rep, COUNT);
	dix_rep_free(&rep);
	dix_kv_destroy(&keys, &vals);
	dix_index_fini(&index);
	ut_service_fini();
}

static void dix_del(void)
{
	struct m0_dix      index;
	struct m0_bufvec   keys;
	struct m0_bufvec   vals;
	struct dix_rep_arr rep;
	int                rc;

	ut_service_init();
	dix_index_init(&index, 1);
	dix_kv_alloc_and_fill(&keys, &vals, COUNT);
	rc = dix_common_idx_op(&index, 1, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);
	rc = dix_ut_del(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == -ENOENT));
	dix_rep_free(&rep);
	rc = dix_ut_put(&index, &keys, &vals, 0, &rep);
	M0_UT_ASSERT(rc == 0);
	dix_rep_free(&rep);
	rc = dix_ut_del(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == 0));
	dix_rep_free(&rep);
	rc = dix_ut_del(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == -ENOENT));
	dix_rep_free(&rep);
	dix_kv_destroy(&keys, &vals);
	dix_index_fini(&index);
	ut_service_fini();
}

static void dix_null_value(void)
{
	struct m0_dix      index;
	struct m0_bufvec   keys;
	struct m0_bufvec   vals;
	struct m0_bufvec   start_key;
	uint32_t           recs_nr = COUNT;
	struct dix_rep_arr rep;
	uint64_t           key;
	int                i;
	int                rc;

	ut_service_init();
	dix_index_init(&index, 1);
	rc = m0_bufvec_alloc(&keys, COUNT, sizeof (uint64_t));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_empty_alloc(&vals, COUNT);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < keys.ov_vec.v_nr; i++)
		*(uint64_t *)keys.ov_buf[i] = dix_key(i);
	rc = dix_common_idx_op(&index, 1, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);
	/* Put records with empty values. */
	rc = dix_ut_put(&index, &keys, &vals, 0, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == 0));
	dix_rep_free(&rep);
	/* Get records using GET request. */
	rc = dix_ut_get(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < keys.ov_vec.v_nr; i++) {
		M0_UT_ASSERT(rep.dra_rep[i].dre_rc == 0);
		key = *(uint64_t *)rep.dra_rep[i].dre_key.b_addr;
		M0_UT_ASSERT(key == dix_key(i));
		M0_UT_ASSERT(rep.dra_rep[i].dre_val.b_nob == 0);
		M0_UT_ASSERT(rep.dra_rep[i].dre_val.b_addr == NULL);
	}
	dix_rep_free(&rep);

	/* Get records using NEXT request. */
	m0_bufvec_alloc(&start_key, 1, sizeof(uint64_t));
	*(uint64_t *)start_key.ov_buf[0] = dix_key(0);
	rc = dix_ut_next(&index, &start_key, &recs_nr, &rep);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < keys.ov_vec.v_nr; i++) {
		M0_UT_ASSERT(rep.dra_rep[i].dre_rc == 0);
		key = *(uint64_t *)rep.dra_rep[i].dre_key.b_addr;
		M0_UT_ASSERT(key == dix_key(i));
		M0_UT_ASSERT(rep.dra_rep[i].dre_val.b_nob == 0);
		M0_UT_ASSERT(rep.dra_rep[i].dre_val.b_addr == NULL);
	}
	dix_rep_free(&rep);
	m0_bufvec_free(&start_key);

	/* Delete records. */
	rc = dix_ut_del(&index, &keys, &rep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc == 0));
	dix_rep_free(&rep);

	dix_kv_destroy(&keys, &vals);
	dix_index_fini(&index);
	ut_service_fini();
}

static void local_failures(void)
{
	struct m0_dix      index;
	struct m0_bufvec   keys;
	struct m0_bufvec   vals;
	struct dix_rep_arr rep;
	int                rc;

	ut_service_init();
	dix_index_init(&index, 1);
	dix_kv_alloc_and_fill(&keys, &vals, COUNT);
	rc = dix_common_idx_op(&index, 1, REQ_CREATE);
	M0_UT_ASSERT(rc == 0);
	/*
	 * Only two CAS requests can be sent successfully, but N + K = 3, so
	 * no record will be successfully put to all component catalogues.
	 */
	m0_fi_enable_off_n_on_m("cas_req_replied_cb", "send-failure", 2, 3);
	rc = dix_ut_put(&index, &keys, &vals, 0, &rep);
	m0_fi_disable("cas_req_replied_cb", "send-failure");
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rep.dra_nr == COUNT);
	M0_UT_ASSERT(m0_forall(i, COUNT, rep.dra_rep[i].dre_rc != 0));
	dix_rep_free(&rep);
	dix_kv_destroy(&keys, &vals);
	dix_index_fini(&index);
	ut_service_fini();
}

enum {
	CASE_1,
	CASE_2,
	CASE_3,
	CASE_4,
};

static void keys_alloc(struct m0_bufvec *cas_reps,
		       struct m0_bufvec *dix_reps)
{
	int                       rc;
	int                       i;
	int                       j;
	struct m0_bufvec         *reps;
	struct m0_cas_next_reply *crep;
	struct m0_dix_next_reply *drep;

	for (i = 0; i < cas_reps->ov_vec.v_nr; i++ ) {
		reps = cas_reps->ov_buf[i];
		for (j = 0; j < reps->ov_vec.v_nr; j++) {
			crep = (struct m0_cas_next_reply *)reps->ov_buf[j];
			M0_SET0(crep);
			rc = m0_buf_alloc(&crep->cnp_key, sizeof(uint64_t));
			M0_UT_ASSERT(rc == 0);
			rc = m0_buf_alloc(&crep->cnp_val, sizeof(uint64_t));
			M0_UT_ASSERT(rc == 0);
		}
	}
	for (i = 0; i < dix_reps->ov_vec.v_nr; i++ ) {
		reps = dix_reps->ov_buf[i];
		for (j = 0; j < reps->ov_vec.v_nr; j++) {
			drep = (struct m0_dix_next_reply *)reps->ov_buf[j];
			M0_SET0(drep);
			rc = m0_buf_alloc(&drep->dnr_key, sizeof(uint64_t));
			M0_UT_ASSERT(rc == 0);
			rc = m0_buf_alloc(&drep->dnr_val, sizeof(uint64_t));
			M0_UT_ASSERT(rc == 0);
		}
	}

}

static void crep_val_set(struct m0_bufvec *reps, uint32_t idx, uint64_t val)
{
	struct m0_cas_next_reply *crep;

	crep = (struct m0_cas_next_reply *)reps->ov_buf[idx];
	*(uint64_t *)crep->cnp_key.b_addr = val;
	*(uint64_t *)crep->cnp_val.b_addr = *(uint64_t *)crep->cnp_key.b_addr;
}

static void drep_val_set(struct m0_bufvec *reps, uint32_t idx, uint64_t val)
{
	struct m0_dix_next_reply *drep;

	drep = (struct m0_dix_next_reply *)reps->ov_buf[idx];
	*(uint64_t *)drep->dnr_key.b_addr = val;
	*(uint64_t *)drep->dnr_val.b_addr = *(uint64_t *)drep->dnr_key.b_addr;
}

/*
 * keys[] = {1, 4};
 * nrs[]  = {4, 5};
 * arr1[] = {1,  3, 4,   7,   4, 7, 100};
 * arr2[] = {NO, 8, 101, 102};
 * arr3[] = {1,  2, 3,   5,   6, 7, 10, 101};
 *  Result must be:
 *  start key "1" (cnt 4): 1 2 3 4
 *  start key "4" (cnt 5): 4 6 7 8 10
 */
static int case_1_data(struct m0_bufvec *cas_reps,
		       struct m0_bufvec *dix_reps,
		       uint32_t         **recs_nr,
		       struct m0_bufvec *start_keys,
		       uint32_t         *ctx_nr)
{
	int               rc;
	uint32_t          start_keys_nr = 2;
	struct m0_bufvec *reps;

	*ctx_nr = 3;
	/* Allocate array with start keys. */
	rc = m0_bufvec_alloc(start_keys, start_keys_nr, sizeof (uint64_t));
	M0_UT_ASSERT(rc == 0);
	/* Allocate recs_nr arrays. */
	M0_ALLOC_ARR(*recs_nr, start_keys_nr);
	M0_UT_ASSERT(*recs_nr != NULL);
	/*
	 * Allocate cas_reply. There are three arrays with items - simulate
	 * CAS service responses from three CAS services.
	 */
	rc = m0_bufvec_alloc(cas_reps, *ctx_nr, sizeof (struct m0_bufvec));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(cas_reps->ov_buf[0], 7,
			     sizeof (struct m0_cas_next_reply));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(cas_reps->ov_buf[1], 4,
			     sizeof (struct m0_cas_next_reply));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(cas_reps->ov_buf[2], 8,
			     sizeof (struct m0_cas_next_reply));
	M0_UT_ASSERT(rc == 0);
	/* Allocate dix_reply - entities for results. */
	rc = m0_bufvec_alloc(dix_reps, start_keys_nr,
			     sizeof (struct m0_bufvec));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(dix_reps->ov_buf[0], 4,
			     sizeof (struct m0_dix_next_reply));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(dix_reps->ov_buf[1], 5,
			     sizeof (struct m0_dix_next_reply));
	M0_UT_ASSERT(rc == 0);
	/* Allocate key and val in CAS and DIX reply. */
	keys_alloc(cas_reps, dix_reps);
	/* Put data into recs_nr. */
	(*recs_nr)[0] = 4;
	(*recs_nr)[1] = 5;

	/* Put data into start_keys. */
	*(uint64_t *)start_keys->ov_buf[0] = 1;
	*(uint64_t *)start_keys->ov_buf[1] = 4;

	/* Put data into CAS relpy. */
	reps = cas_reps->ov_buf[0];
	crep_val_set(reps, 0, 1);
	crep_val_set(reps, 1, 3);
	crep_val_set(reps, 2, 4);
	crep_val_set(reps, 3, 7);
	crep_val_set(reps, 4, 4);
	crep_val_set(reps, 5, 7);
	crep_val_set(reps, 6, 100);

	reps = cas_reps->ov_buf[1];
	((struct m0_cas_next_reply *)reps->ov_buf[0])->cnp_rc = -ENOENT;
	crep_val_set(reps, 1, 8);
	crep_val_set(reps, 2, 101);
	crep_val_set(reps, 3, 102);

	reps = cas_reps->ov_buf[2];
	crep_val_set(reps, 0, 1);
	crep_val_set(reps, 1, 2);
	crep_val_set(reps, 2, 3);
	crep_val_set(reps, 3, 5);
	crep_val_set(reps, 4, 6);
	crep_val_set(reps, 5, 7);
	crep_val_set(reps, 6, 10);
	crep_val_set(reps, 7, 101);

	/* Put data into DIX relpy. */
	reps = dix_reps->ov_buf[0];
	drep_val_set(reps, 0, 1);
	drep_val_set(reps, 1, 2);
	drep_val_set(reps, 2, 3);
	drep_val_set(reps, 3, 4);

	reps = dix_reps->ov_buf[1];
	drep_val_set(reps, 0, 4);
	drep_val_set(reps, 1, 6);
	drep_val_set(reps, 2, 7);
	drep_val_set(reps, 3, 8);
	drep_val_set(reps, 4, 10);
	return 0;
}

/*
 * int keys[] = {1, 10};
 * int nrs[]  = {3, 5};
 * int arr1[] = {1, 3, 4,  100, 110, 120, 130};
 * int arr2[] = {2, 3, 5,  10,  11,  200, 210, 400};
 * int arr3[] = {1, 2, 5,   6,   7,   10, 101, 200};
 * Result must be:
 *  start key "1" (cnt 3) : 1 2 3
 *  start key "10" (cnt 5): 6 7 10 11 100
 */
static int case_2_data(struct m0_bufvec *cas_reps,
		       struct m0_bufvec *dix_reps,
		       uint32_t         **recs_nr,
		       struct m0_bufvec *start_keys,
		       uint32_t         *ctx_nr)
{
	int               rc;
	uint32_t          start_keys_nr = 2;
	struct m0_bufvec *reps;

	*ctx_nr = 3;
	/* Allocate array with start keys. */
	rc = m0_bufvec_alloc(start_keys, start_keys_nr, sizeof (uint64_t));
	M0_UT_ASSERT(rc == 0);
	/* Allocate recs_nr arrays. */
	M0_ALLOC_ARR(*recs_nr, start_keys_nr);
	M0_UT_ASSERT(*recs_nr != NULL);
	/*
	 * Allocate cas_reply. There are three arrays with items - simulate
	 * CAS service responses from three CAS services.
	 */
	rc = m0_bufvec_alloc(cas_reps, *ctx_nr, sizeof (struct m0_bufvec));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(cas_reps->ov_buf[0], 7,
			     sizeof (struct m0_cas_next_reply));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(cas_reps->ov_buf[1], 8,
			     sizeof (struct m0_cas_next_reply));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(cas_reps->ov_buf[2], 8,
			     sizeof (struct m0_cas_next_reply));
	M0_UT_ASSERT(rc == 0);
	/* Allocate dix_reply - entities for results. */
	rc = m0_bufvec_alloc(dix_reps, start_keys_nr,
			     sizeof (struct m0_bufvec));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(dix_reps->ov_buf[0], 3,
			     sizeof (struct m0_dix_next_reply));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(dix_reps->ov_buf[1], 5,
			     sizeof (struct m0_dix_next_reply));
	M0_UT_ASSERT(rc == 0);
	/* Allocate key and val in CAS and DIX reply. */
	keys_alloc(cas_reps, dix_reps);
	/* Put data into recs_nr. */
	(*recs_nr)[0] = 3;
	(*recs_nr)[1] = 5;

	/* Put data into start_keys. */
	*(uint64_t *)start_keys->ov_buf[0] = 1;
	*(uint64_t *)start_keys->ov_buf[1] = 10;

	/* Put data into CAS relpy. */
	reps = cas_reps->ov_buf[0];
	crep_val_set(reps, 0, 1);
	crep_val_set(reps, 1, 3);
	crep_val_set(reps, 2, 4);
	crep_val_set(reps, 3, 100);
	crep_val_set(reps, 4, 110);
	crep_val_set(reps, 5, 120);
	crep_val_set(reps, 6, 130);

	reps = cas_reps->ov_buf[1];
	crep_val_set(reps, 0, 2);
	crep_val_set(reps, 1, 3);
	crep_val_set(reps, 2, 5);
	crep_val_set(reps, 3, 10);
	crep_val_set(reps, 4, 11);
	crep_val_set(reps, 5, 200);
	crep_val_set(reps, 6, 210);
	crep_val_set(reps, 7, 400);

	reps = cas_reps->ov_buf[2];
	crep_val_set(reps, 0, 1);
	crep_val_set(reps, 1, 2);
	crep_val_set(reps, 2, 5);
	crep_val_set(reps, 3, 6);
	crep_val_set(reps, 4, 7);
	crep_val_set(reps, 5, 10);
	crep_val_set(reps, 6, 101);
	crep_val_set(reps, 7, 200);

	/* Put data into DIX relpy. */
	reps = dix_reps->ov_buf[0];
	drep_val_set(reps, 0, 1);
	drep_val_set(reps, 1, 2);
	drep_val_set(reps, 2, 3);

	reps = dix_reps->ov_buf[1];
	drep_val_set(reps, 0, 6);
	drep_val_set(reps, 1, 7);
	drep_val_set(reps, 2, 10);
	drep_val_set(reps, 3, 11);
	drep_val_set(reps, 4, 100);
	return 0;
}

/*
 * int keys[] = {1, 7};
 * int nrs[]  = {3, 5};
 * int arr1[] = {1, 3, 4,  100, 110, 120};
 * int arr2[] = {2, 3, 5,  110};
 * int arr3[] = {1, 2, 6,  7,   100};
 *  Result must be:
 *  start key "1" (cnt 3) : 1 2 3
 *  start key "10" (cnt 5): 7 100 110 120
 */
static int case_3_data(struct m0_bufvec *cas_reps,
		       struct m0_bufvec *dix_reps,
		       uint32_t         **recs_nr,
		       struct m0_bufvec *start_keys,
		       uint32_t         *ctx_nr)
{
	int               rc;
	uint32_t          start_keys_nr = 2;
	struct m0_bufvec *reps;

	*ctx_nr = 3;
	/* Allocate array with start keys. */
	rc = m0_bufvec_alloc(start_keys, start_keys_nr, sizeof (uint64_t));
	M0_UT_ASSERT(rc == 0);
	/* Allocate recs_nr arrays. */
	M0_ALLOC_ARR(*recs_nr, start_keys_nr);
	M0_UT_ASSERT(*recs_nr != NULL);
	/*
	 * Allocate cas_reply. There are three arrays with items - simulate
	 * CAS service responses from three CAS services.
	 */
	rc = m0_bufvec_alloc(cas_reps, *ctx_nr, sizeof (struct m0_bufvec));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(cas_reps->ov_buf[0], 6,
			     sizeof (struct m0_cas_next_reply));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(cas_reps->ov_buf[1], 4,
			     sizeof (struct m0_cas_next_reply));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(cas_reps->ov_buf[2], 5,
			     sizeof (struct m0_cas_next_reply));
	M0_UT_ASSERT(rc == 0);
	/* Allocate dix_reply - entities for results. */
	rc = m0_bufvec_alloc(dix_reps, start_keys_nr,
			     sizeof (struct m0_bufvec));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(dix_reps->ov_buf[0], 3,
			     sizeof (struct m0_dix_next_reply));
	M0_UT_ASSERT(rc == 0);
	rc = m0_bufvec_alloc(dix_reps->ov_buf[1], 4,
			     sizeof (struct m0_dix_next_reply));
	M0_UT_ASSERT(rc == 0);
	/* Allocate key and val in CAS and DIX reply. */
	keys_alloc(cas_reps, dix_reps);
	/* Put data into recs_nr. */
	(*recs_nr)[0] = 3;
	(*recs_nr)[1] = 5;

	/* Put data into start_keys. */
	*(uint64_t *)start_keys->ov_buf[0] = 1;
	*(uint64_t *)start_keys->ov_buf[1] = 7;

	/* Put data into CAS relpy. */
	reps = cas_reps->ov_buf[0];
	crep_val_set(reps, 0, 1);
	crep_val_set(reps, 1, 3);
	crep_val_set(reps, 2, 4);
	crep_val_set(reps, 3, 100);
	crep_val_set(reps, 4, 110);
	crep_val_set(reps, 5, 120);

	reps = cas_reps->ov_buf[1];
	crep_val_set(reps, 0, 2);
	crep_val_set(reps, 1, 3);
	crep_val_set(reps, 2, 5);
	crep_val_set(reps, 3, 110);

	reps = cas_reps->ov_buf[2];
	crep_val_set(reps, 0, 1);
	crep_val_set(reps, 1, 2);
	crep_val_set(reps, 2, 6);
	crep_val_set(reps, 3, 7);
	crep_val_set(reps, 4, 100);

	/* Put data into DIX relpy. */
	reps = dix_reps->ov_buf[0];
	drep_val_set(reps, 0, 1);
	drep_val_set(reps, 1, 2);
	drep_val_set(reps, 2, 3);

	reps = dix_reps->ov_buf[1];
	drep_val_set(reps, 0, 7);
	drep_val_set(reps, 1, 100);
	drep_val_set(reps, 2, 110);
	drep_val_set(reps, 3, 120);
	return 0;
}

/*
 * int keys[] = {1, 3, 5, 7, 9 };
 * int nrs[]  = {2, 2, 2, 2, 2 };
 * int arr1[] = {1, 2, 3, 4, 5, 6, 7 ,8 ,9, 10};
 * int arr2[] = {1, 2, 3, 4, 5, 6, 7 ,8 ,9, 10};
 * int arr3[] = {1, 2, 3, 4, 5, 6, 7 ,8 ,9, 10};
 * Result must be:
 *  1 : 1 2
 *  3 : 3 4
 *  5 : 5 6
 *  7 : 7 8
 *  9 : 9 10
 */
static int case_4_data(struct m0_bufvec *cas_reps,
		       struct m0_bufvec *dix_reps,
		       uint32_t         **recs_nr,
		       struct m0_bufvec *start_keys,
		       uint32_t         *ctx_nr)
{
	int               rc;
	int               i;
	int               j;
	uint32_t          start_keys_nr = 5;
	struct m0_bufvec *reps;

	*ctx_nr = 3;
	/* Allocate array with start keys. */
	rc = m0_bufvec_alloc(start_keys, start_keys_nr, sizeof (uint64_t));
	M0_UT_ASSERT(rc == 0);
	/* Allocate recs_nr arrays. */
	M0_ALLOC_ARR(*recs_nr, start_keys_nr);
	M0_UT_ASSERT(*recs_nr != NULL);
	/*
	 * Allocate cas_reply. There are three arrays with items - simulate
	 * CAS service responses from three CAS services.
	 */
	rc = m0_bufvec_alloc(cas_reps, *ctx_nr, sizeof (struct m0_bufvec));
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < *ctx_nr; i++) {
		rc = m0_bufvec_alloc(cas_reps->ov_buf[i], 10,
				     sizeof (struct m0_cas_next_reply));
		M0_UT_ASSERT(rc == 0);
	}
	/* Allocate dix_reply - entities for results. */
	rc = m0_bufvec_alloc(dix_reps, start_keys_nr,
			     sizeof (struct m0_bufvec));
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < start_keys_nr; i++) {
		rc = m0_bufvec_alloc(dix_reps->ov_buf[i], 2,
				     sizeof (struct m0_dix_next_reply));
		M0_UT_ASSERT(rc == 0);
	}
	/* Allocate key and val in CAS and DIX reply. */
	keys_alloc(cas_reps, dix_reps);
	/* Put data into recs_nr. */
	for (i = 0; i < start_keys_nr; i++)
		(*recs_nr)[i] = 2;

	/* Put data into start_keys. */
	*(uint64_t *)start_keys->ov_buf[0] = 1;
	*(uint64_t *)start_keys->ov_buf[1] = 3;
	*(uint64_t *)start_keys->ov_buf[2] = 5;
	*(uint64_t *)start_keys->ov_buf[3] = 7;
	*(uint64_t *)start_keys->ov_buf[4] = 9;

	/* Put data into CAS relpy. */
	for (i = 0; i < *ctx_nr; i++) {
		reps = cas_reps->ov_buf[i];
		for (j = 0; j < 10; j++)
			crep_val_set(reps, j, j + 1);
	}

	/* Put data into DIX relpy. */
	reps = dix_reps->ov_buf[0];
	drep_val_set(reps, 0, 1);
	drep_val_set(reps, 1, 2);

	reps = dix_reps->ov_buf[1];
	drep_val_set(reps, 0, 3);
	drep_val_set(reps, 1, 4);

	reps = dix_reps->ov_buf[2];
	drep_val_set(reps, 0, 5);
	drep_val_set(reps, 1, 6);

	reps = dix_reps->ov_buf[3];
	drep_val_set(reps, 0, 7);
	drep_val_set(reps, 1, 8);

	reps = dix_reps->ov_buf[4];
	drep_val_set(reps, 0, 9);
	drep_val_set(reps, 1, 10);
	return 0;
}

static int dix_rep_cmp(struct m0_dix_next_reply *a, struct m0_dix_next_reply *b)
{
	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL)
		return -1;
	if (b == NULL)
		return 1;
	return memcmp(a->dnr_key.b_addr, b->dnr_key.b_addr,
		      min64(a->dnr_key.b_nob, b->dnr_key.b_nob)) ?:
		M0_3WAY(a->dnr_key.b_nob, b->dnr_key.b_nob);
}

static void case_data_free(struct m0_bufvec *cas_reps,
			   struct m0_bufvec *dix_reps,
			   uint32_t         *recs_nr,
			   struct m0_bufvec *start_keys)
{
	int                       i;
	int                       j;
	struct m0_bufvec         *reps;
	struct m0_cas_next_reply *crep;
	struct m0_dix_next_reply *drep;

	m0_free(recs_nr);
	for (i = 0; i < cas_reps->ov_vec.v_nr; i++) {
		reps = cas_reps->ov_buf[i];
		for (j = 0; j < reps->ov_vec.v_nr; j++) {
			crep = (struct m0_cas_next_reply *)reps->ov_buf[j];
			m0_buf_free(&crep->cnp_key);
			m0_buf_free(&crep->cnp_val);
		}
		m0_bufvec_free(cas_reps->ov_buf[i]);
	}
	m0_bufvec_free(cas_reps);
	for (i = 0; i < dix_reps->ov_vec.v_nr; i++) {
		reps = dix_reps->ov_buf[i];
		for (j = 0; j < reps->ov_vec.v_nr; j++) {
			drep = (struct m0_dix_next_reply *)reps->ov_buf[j];
			m0_buf_free(&drep->dnr_key);
			m0_buf_free(&drep->dnr_val);
		}
		m0_bufvec_free(dix_reps->ov_buf[i]);
	}
	m0_bufvec_free(dix_reps);
	m0_bufvec_free(start_keys);
}

/* cas_reps - bufvec[ctx_nr] is an array of m0_cas_next_reply. */
static int (*cases[]) (struct m0_bufvec *cas_reps,
		       struct m0_bufvec *dix_reps,
		       uint32_t         **recs_nr,
		       struct m0_bufvec *start_keys,
		       uint32_t         *ctx_nr) = {
	[CASE_1] = case_1_data,
	[CASE_2] = case_2_data,
	[CASE_3] = case_3_data,
	[CASE_4] = case_4_data,
};

void static results_check(struct m0_dix_req *req, struct m0_bufvec *dix_reps)
{
	uint64_t                  key_idx;
	uint64_t                  val_idx;
	uint64_t                  rep_nr;
	struct m0_dix_next_reply *drep;
	struct m0_dix_next_reply  rep;
	struct m0_bufvec         *reps;

	for (key_idx = 0; key_idx < dix_reps->ov_vec.v_nr; key_idx++) {
		rep_nr = m0_dix_next_rep_nr(req, key_idx);
		reps   = (struct m0_bufvec *)dix_reps->ov_buf[key_idx];
		for (val_idx = 0; val_idx < rep_nr; val_idx++) {
			drep = reps->ov_buf[val_idx];
			m0_dix_next_rep(req, key_idx, val_idx, &rep);
			M0_UT_ASSERT(dix_rep_cmp(drep, &rep) == 0);
		}
	}
}

void next_merge(void)
{
	struct m0_dix_next_resultset *rs;
	struct m0_dix_req             req;
	struct m0_bufvec              cas_reps;
	struct m0_bufvec              dix_reps;
	struct m0_bufvec              start_keys;
	int                           rc;
	struct m0_dix_next_sort_ctx  *ctx;
	uint32_t                      ctx_id;
	uint32_t                      key_id;
	uint32_t                      ctx_nr;
	uint32_t                     *recs_nr;
	uint32_t                      start_keys_nr;
	int                           it;

	m0_fi_enable("m0_dix_next_result_prepare", "mock_data_load");
	m0_fi_enable("sc_result_add", "mock_data_load");
	m0_fi_enable("m0_dix_req_fini", "mock_data_load");
	m0_fi_enable("m0_dix_rs_fini", "mock_data_load");
	for (it = 0; it < ARRAY_SIZE(cases); it++) {
		/* Create test data. */
		cases[it](&cas_reps, &dix_reps, &recs_nr,
			  &start_keys, &ctx_nr);
		req.dr_recs_nr  = recs_nr;
		start_keys_nr   = req.dr_items_nr = start_keys.ov_vec.v_nr;
		rs              = &req.dr_rs;
		rc = m0_dix_rs_init(rs, start_keys_nr, ctx_nr);
		M0_UT_ASSERT(rc == 0);
		for (ctx_id = 0; ctx_id < ctx_nr; ctx_id++) {
			struct m0_cas_next_reply *crep;
			struct m0_bufvec         *creps;

			ctx   = &rs->nrs_sctx_arr.sca_ctx[ctx_id];
			/* Array with cas reps for ctx_id. */
			creps = cas_reps.ov_buf[ctx_id];
			ctx->sc_reps_nr = creps->ov_vec.v_nr;
			M0_ALLOC_ARR(ctx->sc_reps, ctx->sc_reps_nr);
			for (key_id = 0; key_id < ctx->sc_reps_nr; key_id++) {
				crep = (struct m0_cas_next_reply *)
							  creps->ov_buf[key_id];
				ctx->sc_reps[key_id] = *crep;
			}
		}
		rc = m0_dix_next_result_prepare(&req);
		M0_UT_ASSERT(rc == 0);
		/* Check results. */
		results_check(&req, &dix_reps);
		m0_dix_rs_fini(rs);
		case_data_free(&cas_reps, &dix_reps, recs_nr, &start_keys);
	}
	m0_fi_disable("m0_dix_next_result_prepare", "mock_data_load");
	m0_fi_disable("sc_result_add", "mock_data_load");
	m0_fi_disable("m0_dix_req_fini", "mock_data_load");
	m0_fi_disable("m0_dix_rs_fini", "mock_data_load");
}

struct m0_ut_suite dix_client_ut = {
	.ts_name   = "dix-client",
	.ts_owners = "Leonid",
	.ts_init   = NULL,
	.ts_fini   = NULL,
	.ts_tests  = {
		{ "imask",                  imask,                   "Leonid" },
		{ "imask-apply",            imask_apply,             "Leonid" },
		{ "imask-empty",            imask_empty,             "Leonid" },
		{ "imask-infini",           imask_infini,            "Leonid" },
		{ "imask-short",            imask_short,             "Leonid" },
		{ "imask-invalid",          imask_invalid,           "Leonid" },
		{ "pdclust-map",            pdclust_map,             "Leonid" },
		{ "meta-val-encdec",        meta_val_encdec,         "Leonid" },
		{ "meta-val-encdec-n",      meta_val_encdec_n,       "Leonid" },
		{ "layout-encdec",          layout_encdec,           "Leonid" },
		{ "meta-create",            dix_meta_create,         "Leonid" },
		{ "create",                 dix_create,              "Leonid" },
		{ "delete",                 dix_delete,              "Leonid" },
		{ "list",                   dix_list,                "Leonid" },
		{ "put",                    dix_put,                 "Leonid" },
		{ "put-overwrite",          dix_put_overwrite,       "Egor"   },
		{ "get",                    dix_get,                 "Leonid" },
		{ "get-resend",             dix_get_resend,          "Egor"   },
		{ "next",                   dix_next,                "Leonid" },
		{ "del",                    dix_del,                 "Leonid" },
		{ "null-value",             dix_null_value,          "Egor"   },
		{ "cctgs-lookup",           dix_cctgs_lookup,        "Leonid" },
		{ "local-failures",         local_failures,          "Egor"   },
		{ "next-merge",             next_merge,              "Leonid" },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM

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