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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 07-Mar-2016
 */


/**
 * @addtogroup cas
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CAS

#include "ut/ut.h"
#include "lib/misc.h"                     /* M0_SET0 */
#include "lib/finject.h"
#include "lib/semaphore.h"
#include "lib/byteorder.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "be/ut/helper.h"                 /* m0_be_ut_backend */

#include "cas/cas.h"

#define IFID(x, y) M0_FID_TINIT('i', (x), (y))
#define FIDBUF(fid) M0_BUF_INIT(sizeof *(fid), (fid))

enum { N = 4096 };

static struct m0_reqh          reqh;
static struct m0_be_ut_backend be;
static struct m0_be_seg       *seg0;
static struct m0_reqh_service *cas;
static struct m0_cas_rep       rep;
static struct m0_cas_rec       repv[N];
static struct m0_fid           ifid = IFID(2, 3);
static bool                    mt;

extern void (*cas__ut_cb_done)(struct m0_fom *fom);
extern void (*cas__ut_cb_fini)(struct m0_fom *fom);

static void cb_done(struct m0_fom *fom);
static void cb_fini(struct m0_fom *fom);

static void rep_clear(void)
{
	int i;

	rep.cgr_rc  = -EINVAL;
	rep.cgr_rep.cr_nr  = 0;
	rep.cgr_rep.cr_rec = repv;
	for (i = 0; i < ARRAY_SIZE(repv); ++i) {
		m0_buf_free(&repv[i].cr_key);
		m0_buf_free(&repv[i].cr_val);
		repv[i].cr_rc = -EINVAL;
	}
}

static void init(void)
{
	int result;

	/* Check validity of IFID definition. */
	M0_UT_ASSERT(m0_cas_index_fid_type.ft_id == 'i');
	M0_SET0(&reqh);
	M0_SET0(&be);
	m0_fi_enable("cas_in_ut", "ut");
	seg0 = m0_be_domain_seg0_get(&be.but_dom);
	result = M0_REQH_INIT(&reqh,
			      .rhia_db      = seg0,
			      .rhia_mdstore = (void *)1,
			      .rhia_fid     = &g_process_fid);
	M0_UT_ASSERT(result == 0);
	be.but_dom_cfg.bc_engine.bec_reqh = &reqh;
	m0_be_ut_backend_init(&be);
	result = m0_reqh_service_allocate(&cas, &m0_cas_service_type, NULL);
	M0_UT_ASSERT(result == 0);
	m0_reqh_service_init(cas, &reqh, NULL);
	m0_reqh_service_start(cas);
	m0_reqh_start(&reqh);
	cas__ut_cb_done = &cb_done;
	cas__ut_cb_fini = &cb_fini;
}

static void fini(void)
{
	m0_reqh_service_prepare_to_stop(cas);
	m0_reqh_service_stop(cas);
	m0_reqh_service_fini(cas);
	m0_be_ut_backend_fini(&be);
	m0_fi_disable("cas_in_ut", "ut");
	rep_clear();
	cas__ut_cb_done = NULL;
	cas__ut_cb_fini = NULL;
}

/**
 * "init-fini" test: initialise and finalise a cas service.
 */
static void init_fini(void)
{
	init();
	fini();
}

/**
 * Test service re-start with existing meta-index.
 */
static void reinit(void)
{
	int result;

	init();
	m0_reqh_service_prepare_to_stop(cas);
	m0_reqh_service_stop(cas);
	m0_reqh_service_fini(cas);
	result = m0_reqh_service_allocate(&cas, &m0_cas_service_type, NULL);
	M0_UT_ASSERT(result == 0);
	m0_reqh_service_init(cas, &reqh, NULL);
	m0_reqh_service_start(cas);
	fini();
}

static void fop_release(struct m0_ref *ref)
{
}

struct fopsem {
	struct m0_semaphore fs_end;
	struct m0_fop       fs_fop;
};

static void cb_done(struct m0_fom *fom)
{
	struct m0_cas_rep *reply = m0_fop_data(fom->fo_rep_fop);
	int                i;
	struct fopsem     *fs = M0_AMB(fs, fom->fo_fop, fs_fop);

	M0_UT_ASSERT(reply != NULL);
	M0_UT_ASSERT(reply->cgr_rep.cr_nr <= ARRAY_SIZE(repv));
	rep.cgr_rc         = reply->cgr_rc;
	rep.cgr_rep.cr_nr  = reply->cgr_rep.cr_nr;
	rep.cgr_rep.cr_rec = repv;
	for (i = 0; !mt && i < rep.cgr_rep.cr_nr; ++i) {
		struct m0_cas_rec *rec = &reply->cgr_rep.cr_rec[i];
		int                rc;

		repv[i].cr_hint = rec->cr_hint;
		repv[i].cr_rc   = rec->cr_rc;
		rc = m0_buf_copy(&repv[i].cr_key, &rec->cr_key);
		M0_UT_ASSERT(rc == 0);
		rc = m0_buf_copy(&repv[i].cr_val, &rec->cr_val);
		M0_UT_ASSERT(rc == 0);
	}
	m0_ref_put(&fom->fo_fop->f_ref);
	fom->fo_fop = NULL;
	m0_ref_put(&fom->fo_rep_fop->f_ref);
	m0_fop_release(&fom->fo_rep_fop->f_ref);
	fom->fo_rep_fop = NULL;
	{
		struct m0_tlink *link = &fom->fo_tx.tx_betx.t_engine_linkage;
		M0_ASSERT(ergo(link->t_link.ll_next != NULL,
			       !m0_list_link_is_in(&link->t_link)));
	}
	m0_semaphore_up(&fs->fs_end);
}

static void cb_fini(struct m0_fom *fom)
{
}

static void fop_submit(struct m0_fop_type *ft, struct m0_fid *index,
		       struct m0_cas_rec *rec)
{
	int              result;
	struct fopsem    fs;
	struct m0_cas_op op = {
		.cg_id  = { .ci_fid = *index },
		.cg_rec = { .cr_rec = rec }
	};

	M0_UT_ASSERT(cas__ut_cb_done == &cb_done);
	M0_UT_ASSERT(cas__ut_cb_fini == &cb_fini);
	while (rec[op.cg_rec.cr_nr].cr_rc != ~0ULL)
		++ op.cg_rec.cr_nr;
	m0_fop_init(&fs.fs_fop, ft, &op, &fop_release);
	fs.fs_fop.f_item.ri_rmachine = (void *)1;
	m0_semaphore_init(&fs.fs_end, 0);
	rep_clear();
	result = m0_reqh_fop_handle(&reqh, &fs.fs_fop);
	M0_UT_ASSERT(result == 0);
	m0_semaphore_down(&fs.fs_end);
	/**
	 * @note There is no need to finalise the locally allocated fop: rpc was
	 * never used, so there are no resources to free.
	 */
	m0_semaphore_fini(&fs.fs_end);
}

enum {
	BSET   = true,
	BUNSET = false,
	BANY   = 2
};

static bool rec_check(const struct m0_cas_rec *rec, int rc, int key, int val)
{
	return  ergo(rc  != BANY, rc == rec->cr_rc) &&
		ergo(key != BANY, m0_buf_is_set(&rec->cr_key) == key) &&
		ergo(val != BANY, m0_buf_is_set(&rec->cr_val) == val);
}

static bool rep_check(int recno, uint64_t rc, int key, int val)
{
	return rec_check(&rep.cgr_rep.cr_rec[recno], rc, key, val);
}

static void meta_submit(struct m0_fop_type *fopt, struct m0_fid *index)
{
	fop_submit(fopt, &m0_cas_meta_fid,
		   (struct m0_cas_rec[]) {
			   { .cr_key = FIDBUF(index) },
			   { .cr_rc = ~0ULL } });
	M0_UT_ASSERT(ergo(!mt, rep.cgr_rc == 0));
	M0_UT_ASSERT(ergo(!mt, rep.cgr_rep.cr_nr == 1));
}

/**
 * Test meta-lookup of a non-existent index.
 */
static void meta_lookup_none(void)
{
	init();
	meta_submit(&cas_get_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
	fini();
}

/**
 * Test meta-lookup of 2 non-existent indices.
 */
static void meta_lookup_2none(void)
{
	struct m0_fid nonce0 = IFID(2, 3);
	struct m0_fid nonce1 = IFID(2, 4);

	init();
	fop_submit(&cas_get_fopt, &m0_cas_meta_fid,
		   (struct m0_cas_rec[]) {
			   { .cr_key = FIDBUF(&nonce0) },
			   { .cr_key = FIDBUF(&nonce1) },
			   { .cr_rc = ~0ULL } });
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == 2);
	M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
	M0_UT_ASSERT(rep_check(1, -ENOENT, BUNSET, BUNSET));
	fini();
}

/**
 * Test meta-lookup of multiple non-existent indices.
 */
static void meta_lookup_Nnone(void)
{
	struct m0_fid     nonce[N]  = {};
	struct m0_cas_rec op[N + 1] = {};
	int               i;

	for (i = 0; i < ARRAY_SIZE(nonce); ++i) {
		nonce[i] = IFID(2, 3 + i);
		op[i].cr_key = FIDBUF(&nonce[i]);
	}
	op[i].cr_rc = ~0ULL;

	init();
	fop_submit(&cas_get_fopt, &m0_cas_meta_fid, op);
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == N);
	M0_UT_ASSERT(m0_forall(i, N, rep_check(i, -ENOENT, BUNSET, BUNSET)));
	fini();
}

/**
 * Test index creation.
 */
static void create(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	fini();
}

/**
 * Test index creation and index lookup.
 */
static void create_lookup(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	meta_submit(&cas_get_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	fini();
}

/**
 * Test index creation of the same index again.
 */
static void create_create(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, -EEXIST, BUNSET, BUNSET));
	meta_submit(&cas_get_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	fini();
}

/**
 * Test index deletion.
 */
static void create_delete(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	meta_submit(&cas_del_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	meta_submit(&cas_get_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
	fini();
}

/**
 * Test index deletion and re-creation.
 */
static void recreate(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	meta_submit(&cas_del_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	meta_submit(&cas_get_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	meta_submit(&cas_get_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	fini();
}

/**
 * Test that meta-cursor returns an existing index.
 */
static void meta_cur_1(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	fop_submit(&cas_cur_fopt, &m0_cas_meta_fid,
		   (struct m0_cas_rec[]) {
			   { .cr_key = FIDBUF(&ifid), .cr_rc = 1 },
			   { .cr_rc = ~0ULL } });
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == 1);
	M0_UT_ASSERT(rep_check(0, 1, BSET, BUNSET));
	M0_UT_ASSERT(m0_fid_eq(repv[0].cr_key.b_addr, &ifid));
	fini();
}

/**
 * Test that meta-cursor detects end of the tree.
 */
static void meta_cur_eot(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	fop_submit(&cas_cur_fopt, &m0_cas_meta_fid,
		   (struct m0_cas_rec[]) {
			   { .cr_key = FIDBUF(&ifid), .cr_rc = 2 },
			   { .cr_rc = ~0ULL } });
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == 2);
	M0_UT_ASSERT(rep_check(0, 1, BSET, BUNSET));
	M0_UT_ASSERT(rep_check(1, -ENOENT, BUNSET, BUNSET));
	M0_UT_ASSERT(m0_fid_eq(repv[0].cr_key.b_addr, &ifid));
	fini();
}

/**
 * Test meta-cursor empty iteration.
 */
static void meta_cur_0(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	fop_submit(&cas_cur_fopt, &m0_cas_meta_fid,
		   (struct m0_cas_rec[]) {
			   { .cr_key = FIDBUF(&ifid), .cr_rc = 0 },
			   { .cr_rc = ~0ULL } });
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == 0);
	fini();
}

/**
 * Test meta-cursor on empty meta-index.
 */
static void meta_cur_empty(void)
{
	init();
	fop_submit(&cas_cur_fopt, &m0_cas_meta_fid,
		   (struct m0_cas_rec[]) {
			   { .cr_key = FIDBUF(&ifid), .cr_rc = 1 },
			   { .cr_rc = ~0ULL } });
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == 1);
	M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
	fini();
}

/**
 * Test meta-cursor with non-existent starting point.
 */
static void meta_cur_none(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	fop_submit(&cas_cur_fopt, &m0_cas_meta_fid,
		   (struct m0_cas_rec[]) {
			   { .cr_key = FIDBUF(&IFID(8, 9)), .cr_rc = 3 },
			   { .cr_rc = ~0ULL } });
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == 3);
	M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
	M0_UT_ASSERT(rep_check(1, 0, BUNSET, BUNSET));
	M0_UT_ASSERT(rep_check(2, 0, BUNSET, BUNSET));
	fini();
}


/**
 * @todo add more, as more operations are supported.
 */
static struct m0_fop_type *ft[] = {
	&cas_put_fopt,
	&cas_get_fopt,
	&cas_del_fopt,
	&cas_cur_fopt
};

/**
 * Test random meta-operations.
 */
static void meta_random(void)
{
	enum { K = 10 };
	struct m0_fid     fid[K];
	struct m0_cas_rec op[K + 1];
	int               i;
	int               j;
	int               total;
	uint64_t          seed  = time(NULL)*time(NULL);

	init();
	for (i = 0; i < 50; ++i) {
		struct m0_fop_type *type = ft[m0_rnd64(&seed) % ARRAY_SIZE(ft)];

		memset(fid, 0, sizeof fid);
		memset(op, 0, sizeof op);
		total = 0;
		/*
		 * Keep number of operations in a fop small to avoid too large
		 * transactions.
		 */
		for (j = 0; j < K; ++j) {
			fid[j] = M0_FID_TINIT(m0_cas_index_fid_type.ft_id,
					      2, m0_rnd64(&seed) % 5);
			op[j].cr_key = FIDBUF(&fid[j]);
			if (type == &cas_cur_fopt) {
				int n = m0_rnd64(&seed) % 1000;
				if (total + n < ARRAY_SIZE(repv))
					total += op[j].cr_rc = n;
			}
		}
		op[j].cr_rc = ~0ULL;
		fop_submit(type, &m0_cas_meta_fid, op);
		M0_UT_ASSERT(rep.cgr_rc == 0);
		if (type != &cas_cur_fopt) {
			M0_UT_ASSERT(rep.cgr_rep.cr_nr == K);
			M0_UT_ASSERT(m0_forall(i, K, rep_check(i, BANY, BUNSET,
							       BUNSET)));
		} else {
			M0_UT_ASSERT(rep.cgr_rep.cr_nr == total);
		}
	}
	fini();
}

/**
 * Test garbage meta-operations.
 */
static void meta_garbage(void)
{
	enum { M = 16*1024 };
	uint64_t          buf[M + M];
	struct m0_cas_rec op[N + 1] = {};
	int               i;
	int               j;
	uint64_t          seed = time(NULL)*time(NULL);

	init();
	for (i = 0; i < ARRAY_SIZE(buf); ++i)
		buf[i] = m0_rnd64(&seed);
	for (i = 0; i < 200; ++i) {
		for (j = 0; j < 10; ++j) {
			m0_bcount_t size = m0_rnd64(&seed) % M;
			op[j].cr_key = M0_BUF_INIT(size,
						   buf + m0_rnd64(&seed) % M);
		}
		op[j].cr_rc = ~0ULL;
		fop_submit(ft[m0_rnd64(&seed) % ARRAY_SIZE(ft)],
			   &m0_cas_meta_fid, op);
		M0_UT_ASSERT(rep.cgr_rc == -EPROTO);
	}
	fini();
}

static void index_op_rc(struct m0_fop_type *ft, struct m0_fid *index,
			uint64_t key, uint64_t val, uint64_t rc)
{
	struct m0_buf no = M0_BUF_INIT(0, NULL);

	fop_submit(ft, index,
		   (struct m0_cas_rec[]) {
		   { .cr_key = key != 0 ? M0_BUF_INIT(sizeof key, &key) : no,
		     .cr_val = val != 0 ? M0_BUF_INIT(sizeof val, &val) : no,
		     .cr_rc  = rc },
		   { .cr_rc = ~0ULL } });
}

static void index_op(struct m0_fop_type *ft, struct m0_fid *index,
		     uint64_t key, uint64_t val)
{
	index_op_rc(ft, index, key, val, 0);
}

/**
 * Test insertion (in a non-meta index).
 */
static void insert(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	index_op(&cas_put_fopt, &ifid, 1, 2);
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == 1);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	fini();
}

/**
 * Test insert+lookup.
 */
static void insert_lookup(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	index_op(&cas_put_fopt, &ifid, 1, 2);
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == 1);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	index_op(&cas_get_fopt, &ifid, 1, 0);
	M0_UT_ASSERT(rep.cgr_rc == 0);
	M0_UT_ASSERT(rep.cgr_rep.cr_nr == 1);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BSET));
	M0_UT_ASSERT(rep.cgr_rep.cr_rec[0].cr_val.b_nob == sizeof (uint64_t));
	M0_UT_ASSERT(2 == *(uint64_t *)rep.cgr_rep.cr_rec[0].cr_val.b_addr);
	fini();
}

/**
 * Test insert+delete.
 */
static void insert_delete(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	index_op(&cas_put_fopt, &ifid, 1, 2);
	index_op(&cas_get_fopt, &ifid, 1, 0);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BSET));
	index_op(&cas_del_fopt, &ifid, 1, 0);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	index_op(&cas_get_fopt, &ifid, 1, 0);
	M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
	fini();
}

/**
 * Test lookup of a non-existing key
 */
static void lookup_none(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	index_op(&cas_put_fopt, &ifid, 1, 2);
	index_op(&cas_get_fopt, &ifid, 3, 0);
	M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
	fini();
}

/**
 * Test insert of an existing key
 */
static void insert_2(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	index_op(&cas_put_fopt, &ifid, 1, 2);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	index_op(&cas_put_fopt, &ifid, 1, 2);
	M0_UT_ASSERT(rep_check(0, -EEXIST, BUNSET, BUNSET));
	index_op(&cas_get_fopt, &ifid, 1, 0);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BSET));
	fini();
}

/**
 * Test delete of a non-existing key
 */
static void delete_2(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	index_op(&cas_del_fopt, &ifid, 1, 0);
	M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
	fini();
}

enum { INSERTS = 1500 };

#define CB(x) m0_byteorder_cpu_to_be64(x)
#define BC(x) m0_byteorder_be64_to_cpu(x)

static void insert_odd(struct m0_fid *index)
{
	int i;

	for (i = 1; i < INSERTS; i += 2) {
		/*
		 * Convert to big-endian to get predictable iteration order.
		 */
		index_op(&cas_put_fopt, index, CB(i), i*i);
		M0_UT_ASSERT(rep_check(0, 0, BUNSET, BUNSET));
	}
}

static void lookup_all(struct m0_fid *index)
{
	int i;

	for (i = 1; i < INSERTS; ++i) {
		index_op(&cas_get_fopt, index, CB(i), 0);
		if (i & 1) {
			M0_UT_ASSERT(rep_check(0, 0, BUNSET, BSET));
			M0_UT_ASSERT(repv[0].cr_val.b_nob == sizeof (uint64_t));
			M0_UT_ASSERT(*(uint64_t *)repv[0].cr_val.b_addr ==
				     i * i);
		} else {
			M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
		}
	}
}

/**
 * Test lookup of multiple values.
 */
static void lookup_N(void)
{
	init();
	meta_submit(&cas_put_fopt, &ifid);
	insert_odd(&ifid);
	lookup_all(&ifid);
	fini();
}

/**
 * Test lookup after restart.
 */
static void lookup_restart(void)
{
	int           result;

	init();
	meta_submit(&cas_put_fopt, &ifid);
	insert_odd(&ifid);
	lookup_all(&ifid);
	m0_reqh_service_prepare_to_stop(cas);
	m0_reqh_service_stop(cas);
	m0_reqh_service_fini(cas);
	result = m0_reqh_service_allocate(&cas, &m0_cas_service_type, NULL);
	M0_UT_ASSERT(result == 0);
	m0_reqh_service_init(cas, &reqh, NULL);
	m0_reqh_service_start(cas);
	lookup_all(&ifid);
	fini();
}

/**
 * Test iteration over multiple values (with restart).
 */
static void cur_N(void)
{
	int i;
	int result;

	init();
	meta_submit(&cas_put_fopt, &ifid);
	insert_odd(&ifid);
	m0_reqh_service_prepare_to_stop(cas);
	m0_reqh_service_stop(cas);
	m0_reqh_service_fini(cas);
	result = m0_reqh_service_allocate(&cas, &m0_cas_service_type, NULL);
	M0_UT_ASSERT(result == 0);
	m0_reqh_service_init(cas, &reqh, NULL);
	m0_reqh_service_start(cas);

	for (i = 1; i < INSERTS; ++i) {
		int j;
		int k;

		index_op_rc(&cas_cur_fopt, &ifid, CB(i), 0, INSERTS);
		if (!(i & 1)) {
			M0_UT_ASSERT(rep_check(0, -ENOENT, BUNSET, BUNSET));
			continue;
		}
		for (j = i, k = 0; j < INSERTS; j += 2, ++k) {
			struct m0_cas_rec *r = &repv[k];

			M0_UT_ASSERT(rep_check(k, k + 1, BSET, BSET));
			M0_UT_ASSERT(r->cr_val.b_nob == sizeof (uint64_t));
			M0_UT_ASSERT(*(uint64_t *)r->cr_val.b_addr == j * j);
			M0_UT_ASSERT(r->cr_key.b_nob == sizeof (uint64_t));
			M0_UT_ASSERT(*(uint64_t *)r->cr_key.b_addr == CB(j));
		}
		M0_UT_ASSERT(rep_check(k, -ENOENT, BUNSET, BUNSET));
		M0_UT_ASSERT(rep.cgr_rep.cr_nr == INSERTS);
	}
	fini();
}

static struct m0_thread t[8];

static void meta_mt_thread(int idx)
{
	uint64_t seed = time(NULL) * (idx + 6);
	int      i;

	M0_UT_ASSERT(0 <= idx && idx < ARRAY_SIZE(t));

	for (i = 0; i < 20; ++i) {
		meta_submit(ft[m0_rnd64(&seed) % ARRAY_SIZE(ft)],
			    &IFID(2, m0_rnd64(&seed) % 5));
		/*
		 * Cannot check anything: global rep and repv are corrupted.
		 */
	}
}

/**
 * Test multi-threaded meta-operations.
 */
static void meta_mt(void)
{
	int i;
	int result;

	init();
	mt = true;
	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		result = M0_THREAD_INIT(&t[i], int, NULL, &meta_mt_thread, i,
					"meta-mt-%i", i);
		M0_UT_ASSERT(result == 0);
	}
	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		m0_thread_join(&t[i]);
		m0_thread_fini(&t[i]);
	}
	mt = false;
	fini();
}

struct m0_ut_suite cas_service_ut = {
	.ts_name   = "cas-service",
	.ts_owners = "Nikita",
	.ts_init   = NULL,
	.ts_fini   = NULL,
	.ts_tests  = {
		{ "init-fini",               &init_fini,             "Nikita" },
		{ "re-init",                 &reinit,                "Nikita" },
		{ "meta-lookup-none",        &meta_lookup_none,      "Nikita" },
		{ "meta-lookup-2-none",      &meta_lookup_2none,     "Nikita" },
		{ "meta-lookup-N-none",      &meta_lookup_Nnone,     "Nikita" },
		{ "create",                  &create,                "Nikita" },
		{ "create-lookup",           &create_lookup,         "Nikita" },
		{ "create-create",           &create_create,         "Nikita" },
		{ "create-delete",           &create_delete,         "Nikita" },
		{ "recreate",                &recreate,              "Nikita" },
		{ "meta-cur-1",              &meta_cur_1,            "Nikita" },
		{ "meta-cur-0",              &meta_cur_0,            "Nikita" },
		{ "meta-cur-eot",            &meta_cur_eot,          "Nikita" },
		{ "meta-cur-empty",          &meta_cur_empty,        "Nikita" },
		{ "meta-cur-none",           &meta_cur_none,         "Nikita" },
		{ "meta-random",             &meta_random,           "Nikita" },
		{ "meta-garbage",            &meta_garbage,          "Nikita" },
		{ "insert",                  &insert,                "Nikita" },
		{ "insert-lookup",           &insert_lookup,         "Nikita" },
		{ "insert-delete",           &insert_delete,         "Nikita" },
		{ "lookup-none",             &lookup_none,           "Nikita" },
		{ "insert-2",                &insert_2,              "Nikita" },
		{ "delete-2",                &delete_2,              "Nikita" },
		{ "lookup-N",                &lookup_N,              "Nikita" },
		{ "lookup-restart",          &lookup_restart,        "Nikita" },
		{ "cur-N",                   &cur_N,                 "Nikita" },
		{ "meta-mt",                 &meta_mt,               "Nikita" },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM

/** @} end of cas group */

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
