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
 * Original creation date: 5-Jun-2013
 */

#include <stdlib.h>		/* system */
#include <sys/stat.h>		/* mkdir */
#include <sys/types.h>		/* mkdir */
#include <pthread.h>		/* pthread_once */
#include <unistd.h>		/* syscall */
#include <sys/syscall.h>	/* syscall */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"
#include "lib/memory.h"		/* m0_alloc */
#include "lib/misc.h"		/* M0_BITS */
#include "lib/arith.h"		/* M0_CNT_INC */
#include "lib/errno.h"		/* ENOMEM */
#include "rpc/rpclib.h"		/* m0_rpc_server_start */
#include "net/net.h"		/* m0_net_xprt */
#include "module/instance.h"	/* m0 */

#include "ut/ast_thread.h"
#include "ut/stob.h"		/* m0_ut_stob_linux_get */
#include "be/ut/helper.h"	/* m0_be_ut_backend */
#include "be/tx_internal.h"	/* m0_be_tx__reg_area */
#include "be/seg0.h"            /* m0_be_0type_register */

enum {
	BE_UT_SEG_START_ADDR = 0x400000000000ULL,
};

struct m0_be_ut_sm_group_thread {
	struct m0_thread    sgt_thread;
	pid_t		    sgt_tid;
	struct m0_semaphore sgt_stop_sem;
	struct m0_sm_group  sgt_grp;
	bool		    sgt_lock_new;
};

struct be_ut_helper_struct {
	struct m0_net_xprt      *buh_net_xprt;
	struct m0_rpc_server_ctx buh_rpc_sctx;
	int			 buh_reqh_ref_cnt;
	pthread_once_t		 buh_once_control;
	struct m0_mutex		 buh_reqh_lock;
	struct m0_mutex		 buh_seg_lock;
	void			*buh_addr;
};

struct be_ut_helper_struct be_ut_helper = {
	/* because there is no m0_mutex static initializer */
	.buh_once_control = PTHREAD_ONCE_INIT,
};

static inline void be_ut_helper_fini(void)
{
	struct be_ut_helper_struct *h = &be_ut_helper;

	m0_mutex_fini(&h->buh_reqh_lock);
	m0_mutex_fini(&h->buh_seg_lock);
}

/* XXX call this function from m0_init()? */
static void be_ut_helper_init(void)
{
	struct be_ut_helper_struct *h = &be_ut_helper;

	h->buh_reqh_ref_cnt    = 0,
	h->buh_addr	       = (void *) BE_UT_SEG_START_ADDR,
	m0_mutex_init(&h->buh_seg_lock);
	m0_mutex_init(&h->buh_reqh_lock);
	atexit(&be_ut_helper_fini);	/* XXX REFACTORME */
}

static void be_ut_helper_init_once(void)
{
	int rc;

	rc = pthread_once(&be_ut_helper.buh_once_control, &be_ut_helper_init);
	M0_ASSERT(rc == 0);
}

static void *be_ut_seg_allocate_addr(struct be_ut_helper_struct *h,
				     m0_bcount_t size)
{
	void *addr;

	be_ut_helper_init_once();

	size = m0_align(size, m0_pagesize_get());

	m0_mutex_lock(&h->buh_seg_lock);
	addr	     = h->buh_addr;
	h->buh_addr += size;
	m0_mutex_unlock(&h->buh_seg_lock);

	return addr;
}

M0_INTERNAL struct m0_reqh *m0_be_ut_reqh_get(void)
{
	struct m0_reqh *reqh;
	int             result;

	struct be_ut_helper_struct *h = &be_ut_helper;
	be_ut_helper_init_once();
	M0_CNT_INC(h->buh_reqh_ref_cnt);
	M0_ALLOC_PTR(reqh);
        result = M0_REQH_INIT(reqh,
                              .rhia_dtm       = NULL,
                              .rhia_db        = NULL,
                              .rhia_mdstore   = NULL,
                              .rhia_fol       = NULL);
        M0_ASSERT(result == 0);
	return reqh;
}

M0_INTERNAL void m0_be_ut_reqh_put(struct m0_reqh *reqh)
{

	struct be_ut_helper_struct *h = &be_ut_helper;
	m0_reqh_fini(reqh);
	m0_free(reqh);
	M0_CNT_DEC(h->buh_reqh_ref_cnt);
}

static pid_t gettid_impl()
{
	return syscall(SYS_gettid);
}

static void be_ut_sm_group_thread_func(struct m0_be_ut_sm_group_thread *sgt)
{
	struct m0_sm_group *grp = &sgt->sgt_grp;

	while (!m0_semaphore_trydown(&sgt->sgt_stop_sem)) {
		m0_chan_wait(&grp->s_clink);
		m0_sm_group_lock(grp);
		m0_sm_asts_run(grp);
		m0_sm_group_unlock(grp);
	}
}

static int m0_be_ut_sm_group_thread_init(struct m0_be_ut_sm_group_thread **sgtp,
					 bool lock_new)
{
	struct m0_be_ut_sm_group_thread *sgt;
	int				 rc;

	M0_ALLOC_PTR(*sgtp);
	sgt = *sgtp;
	if (sgt != NULL) {
		sgt->sgt_tid = gettid_impl();
		sgt->sgt_lock_new = lock_new;

		m0_sm_group_init(&sgt->sgt_grp);
		m0_semaphore_init(&sgt->sgt_stop_sem, 0);
		rc = M0_THREAD_INIT(&sgt->sgt_thread,
				    struct m0_be_ut_sm_group_thread *, NULL,
				    &be_ut_sm_group_thread_func, sgt,
				    "be_ut sgt");
		if (rc == 0) {
			if (sgt->sgt_lock_new)
				m0_sm_group_lock(&sgt->sgt_grp);
		} else {
			m0_semaphore_fini(&sgt->sgt_stop_sem);
			m0_sm_group_fini(&sgt->sgt_grp);
			m0_free(sgt);
		}
	} else {
		rc = -ENOMEM;
	}
	return rc;
}

static void m0_be_ut_sm_group_thread_fini(struct m0_be_ut_sm_group_thread *sgt)
{
	int rc;

	m0_semaphore_up(&sgt->sgt_stop_sem);

	m0_clink_signal(&sgt->sgt_grp.s_clink);
	if (sgt->sgt_lock_new)
		m0_sm_group_unlock(&sgt->sgt_grp);

	rc = m0_thread_join(&sgt->sgt_thread);
	M0_ASSERT(rc == 0);
	m0_thread_fini(&sgt->sgt_thread);

	m0_semaphore_fini(&sgt->sgt_stop_sem);
	m0_sm_group_fini(&sgt->sgt_grp);
	m0_free(sgt);
}

void m0_be_ut_backend_cfg_default(struct m0_be_domain_cfg *cfg)
{
	struct m0_reqh *reqh = cfg->bc_engine.bec_group_fom_reqh;

	*cfg = (struct m0_be_domain_cfg) {
		.bc_engine = {
			.bec_group_nr	    = 1,
			.bec_log_size	    = 1 << 27,
			.bec_tx_size_max    = M0_BE_TX_CREDIT(1 << 18, 1 << 24),
			.bec_group_size_max = M0_BE_TX_CREDIT(1 << 18, 1 << 24),
			.bec_group_tx_max   = 20,
			.bec_log_replay	    = false,
			.bec_group_close_timeout = M0_TIME_ONE_MSEC,
			.bec_group_fom_reqh = reqh,
		},
		.bc_seg0_size = 1ULL << 20,
	};
}

static void ut_backend_init_cfg(struct m0_be_ut_backend *ut_be,
				struct m0_be_domain_cfg *cfg)
{
	int rc = 0;

	ut_be->but_sm_groups_unlocked = false;
	if (cfg == NULL) {
		m0_be_ut_backend_cfg_default(&ut_be->but_dom_cfg);
	} else {
		ut_be->but_dom_cfg = *cfg;
	}
	if (ut_be->but_dom_cfg.bc_engine.bec_group_fom_reqh == NULL) {
		ut_be->but_dom_cfg.bc_engine.bec_group_fom_reqh =
			m0_be_ut_reqh_get();
	}
	ut_be->but_dom_cfg.bc_engine.bec_log_stob = m0_ut_stob_linux_get();
	m0_mutex_init(&ut_be->but_sgt_lock);
	rc = m0_be_domain_start(&ut_be->but_dom, &ut_be->but_dom_cfg);
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
	if (rc != 0)
		m0_mutex_fini(&ut_be->but_sgt_lock);

	if (m0_get()->i_be_ut_backend != NULL) {
		m0_get()->i_be_ut_backend_save = m0_get()->i_be_ut_backend;
		m0_get()->i_be_ut_backend = NULL;
	} else if (m0_get()->i_be_ut_backend_save == NULL) {
		   m0_get()->i_be_ut_backend = ut_be;
	}
}

M0_INTERNAL void m0_be_ut_backend_init_cfg(struct m0_be_ut_backend *ut_be,
					   struct m0_be_domain_cfg *cfg)
{
	struct m0_be_0type *zt;

	/* XXX: Funny thing... Types have to be included into every domain so
	   link inside 0type does. Leaks here... */
	M0_ALLOC_PTR(zt);
	M0_ASSERT(zt != NULL);
	*zt = m0_be_ut_log0;

	(void)m0_be_domain_init(&ut_be->but_dom);
	m0_be_0type_register(&ut_be->but_dom, zt);
	ut_backend_init_cfg(ut_be, cfg);
}

void m0_be_ut_backend_init(struct m0_be_ut_backend *ut_be)
{
	m0_be_ut_backend_init_cfg(ut_be, NULL);
}

void m0_be_ut_backend_mkfs_init(struct m0_be_ut_backend *ut_be)
{
	(void)m0_be_domain_init(&ut_be->but_dom);
	m0_be_0type_register(&ut_be->but_dom, &m0_be_ut_log0);
	m0_be_0type_register(&ut_be->but_dom, &m0_be_ut_seg0);
	ut_backend_init_cfg(ut_be, NULL);
}

void m0_be_ut_backend_fini(struct m0_be_ut_backend *ut_be)
{
	if (m0_get()->i_be_ut_backend == ut_be ||
	    m0_get()->i_be_ut_backend_save == ut_be) {
		m0_get()->i_be_ut_backend = NULL;
		m0_get()->i_be_ut_backend_save = NULL;
	}
	m0_forall(i, ut_be->but_sgt_size,
		  m0_be_ut_sm_group_thread_fini(ut_be->but_sgt[i]), true);
	m0_free(ut_be->but_sgt);
	m0_be_domain_fini(&ut_be->but_dom);
	m0_mutex_fini(&ut_be->but_sgt_lock);
	if (be_ut_helper.buh_reqh_ref_cnt > 0)
		m0_be_ut_reqh_put(ut_be->but_dom_cfg.bc_engine.bec_group_fom_reqh);
	m0_ut_stob_put(ut_be->but_dom_cfg.bc_engine.bec_log_stob, true);
}

static void be_ut_sm_group_thread_add(struct m0_be_ut_backend *ut_be,
				      struct m0_be_ut_sm_group_thread *sgt)
{
	struct m0_be_ut_sm_group_thread **sgt_arr;
	size_t				  size = ut_be->but_sgt_size;

	M0_ALLOC_ARR(sgt_arr, size + 1);
	M0_ASSERT(sgt_arr != NULL);

	m0_forall(i, size, sgt_arr[i] = ut_be->but_sgt[i], true);
	sgt_arr[size] = sgt;

	m0_free(ut_be->but_sgt);
	ut_be->but_sgt = sgt_arr;
	++ut_be->but_sgt_size;
}

static size_t be_ut_backend_sm_group_find(struct m0_be_ut_backend *ut_be)
{
	size_t i;
	pid_t  tid = gettid_impl();

	for (i = 0; i < ut_be->but_sgt_size; ++i) {
		if (ut_be->but_sgt[i]->sgt_tid == tid)
			break;
	}
	return i;
}

static struct m0_sm_group *
be_ut_backend_sm_group_lookup(struct m0_be_ut_backend *ut_be, bool lock_new)
{
	struct m0_be_ut_sm_group_thread *sgt;
	struct m0_sm_group              *grp;
	pid_t                            tid = gettid_impl();
	unsigned                         i;
	int                              rc;

	m0_mutex_lock(&ut_be->but_sgt_lock);
	grp = NULL;
	for (i = 0; i < ut_be->but_sgt_size; ++i) {
		sgt = ut_be->but_sgt[i];
		if (sgt->sgt_tid == tid) {
			grp = &sgt->sgt_grp;
			break;
		}
	}
	if (grp == NULL) {
		rc = m0_be_ut_sm_group_thread_init(&sgt, lock_new);
		M0_ASSERT(rc == 0);
		be_ut_sm_group_thread_add(ut_be, sgt);
		grp = &sgt->sgt_grp;
	}
	m0_mutex_unlock(&ut_be->but_sgt_lock);
	M0_POST(grp != NULL);
	return grp;
}

struct m0_sm_group *
m0_be_ut_backend_sm_group_lookup(struct m0_be_ut_backend *ut_be)
{
	return be_ut_backend_sm_group_lookup(ut_be,
					     !ut_be->but_sm_groups_unlocked);
}

void m0_be_ut_backend_new_grp_lock_state_set(struct m0_be_ut_backend *ut_be,
					     bool unlocked_new)
{
	ut_be->but_sm_groups_unlocked = unlocked_new;
}

void m0_be_ut_backend_thread_exit(struct m0_be_ut_backend *ut_be)
{
	size_t index;
	size_t i;

	m0_mutex_lock(&ut_be->but_sgt_lock);
	index = be_ut_backend_sm_group_find(ut_be);
	if (index != ut_be->but_sgt_size) {
		m0_be_ut_sm_group_thread_fini(ut_be->but_sgt[index]);
		for (i = index + 1; i < ut_be->but_sgt_size; ++i)
			ut_be->but_sgt[i - 1] = ut_be->but_sgt[i];
		--ut_be->but_sgt_size;
	}
	m0_mutex_unlock(&ut_be->but_sgt_lock);
}

static void be_ut_tx_lock_if(struct m0_sm_group *grp,
		      struct m0_be_ut_backend *ut_be)
{
	if (ut_be->but_sm_groups_unlocked)
		m0_sm_group_lock(grp);
}

static void be_ut_tx_unlock_if(struct m0_sm_group *grp,
			struct m0_be_ut_backend *ut_be)
{
	if (ut_be->but_sm_groups_unlocked)
		m0_sm_group_unlock(grp);
}

void m0_be_ut_tx_init(struct m0_be_tx *tx, struct m0_be_ut_backend *ut_be)
{
	struct m0_sm_group *grp = m0_be_ut_backend_sm_group_lookup(ut_be);

	be_ut_tx_lock_if(grp, ut_be);
	m0_be_tx_init(tx, 0, &ut_be->but_dom, grp, NULL, NULL, NULL, NULL);
	be_ut_tx_unlock_if(grp, ut_be);
}

void m0_be_ut_seg_init(struct m0_be_ut_seg *ut_seg,
		       struct m0_be_ut_backend *ut_be,
		       m0_bcount_t size)
{
	struct be_ut_helper_struct *h = &be_ut_helper;
	int			    rc;

	m0_be_seg_init(&ut_seg->bus_seg, m0_ut_stob_linux_get(),
		       &ut_be->but_dom);
	rc = m0_be_seg_create(&ut_seg->bus_seg, size,
			      be_ut_seg_allocate_addr(h, size));
	M0_ASSERT(rc == 0);
	rc = m0_be_seg_open(&ut_seg->bus_seg);
	M0_ASSERT(rc == 0);

	ut_seg->bus_copy = NULL;
}

void m0_be_ut_seg_fini(struct m0_be_ut_seg *ut_seg)
{
	struct m0_stob *stob = ut_seg->bus_seg.bs_stob;
	int		rc;

	m0_free(ut_seg->bus_copy);

	m0_be_seg_close(&ut_seg->bus_seg);
	rc = m0_be_seg_destroy(&ut_seg->bus_seg);
	M0_ASSERT(rc == 0);
	m0_be_seg_fini(&ut_seg->bus_seg);

	m0_ut_stob_put(stob, false);
}

static void be_ut_data_save(const char *filename, m0_bcount_t size, void *addr)
{
	size_t	written;
	FILE   *f;

	f = fopen(filename, "w");
	written = fwrite(addr, size, 1, f);
	M0_ASSERT(written == 1);
	fclose(f);
}

void m0_be_ut_seg_check_persistence(struct m0_be_ut_seg *ut_seg)
{
	struct m0_be_seg *seg = &ut_seg->bus_seg;

	if (ut_seg->bus_copy == NULL) {
		ut_seg->bus_copy = m0_alloc(seg->bs_size);
		M0_ASSERT(ut_seg->bus_copy != NULL);
	}

	m0_be_seg__read(&M0_BE_REG_SEG(seg), ut_seg->bus_copy);

	if (memcmp(seg->bs_addr, ut_seg->bus_copy, seg->bs_size) != 0) {
		be_ut_data_save("stob.dat", seg->bs_size, ut_seg->bus_copy);
		be_ut_data_save("memory.dat", seg->bs_size, seg->bs_addr);
		M0_IMPOSSIBLE("Segment data differs from stob data");
	}
}

void m0_be_ut_seg_reload(struct m0_be_ut_seg *ut_seg)
{
	m0_be_seg_close(&ut_seg->bus_seg);
	m0_be_seg_open(&ut_seg->bus_seg);
}

static void be_ut_seg_allocator_initfini(struct m0_be_seg *seg,
					 struct m0_be_ut_backend *ut_be,
					 bool init)
{
	struct m0_be_tx_credit	credit = {};
	struct m0_be_allocator *a;
	struct m0_be_tx         tx;
	int                     rc;

	a = m0_be_seg_allocator(seg);
	if (ut_be != NULL) {
		m0_be_ut_tx_init(&tx, ut_be);
		be_ut_tx_lock_if(tx.t_sm.sm_grp, ut_be);
		m0_be_allocator_credit(a, init ? M0_BAO_CREATE : M0_BAO_DESTROY,
				       0, 0, &credit);
		m0_be_tx_prep(&tx, &credit);
		rc = m0_be_tx_open_sync(&tx);
		M0_ASSERT(rc == 0);
	}

	if (init) {
		rc = m0_be_allocator_init(a, seg);
		M0_ASSERT(rc == 0);
		rc = m0_be_allocator_create(a, ut_be == NULL ? NULL : &tx);
		M0_ASSERT(rc == 0);
	} else {
		m0_be_allocator_destroy(a, ut_be == NULL ? NULL : &tx);
		m0_be_allocator_fini(a);
	}

	if (ut_be != NULL) {
		m0_be_tx_close_sync(&tx);
		m0_be_tx_fini(&tx);
		be_ut_tx_unlock_if(tx.t_sm.sm_grp, ut_be);
	}
}

void m0_be_ut_seg_allocator_init(struct m0_be_ut_seg *ut_seg,
				 struct m0_be_ut_backend *ut_be)
{
	be_ut_seg_allocator_initfini(&ut_seg->bus_seg, ut_be, true);
	ut_seg->bus_allocator = m0_be_seg_allocator(&ut_seg->bus_seg);
}

void m0_be_ut_seg_allocator_fini(struct m0_be_ut_seg *ut_seg,
				 struct m0_be_ut_backend *ut_be)
{
	be_ut_seg_allocator_initfini(&ut_seg->bus_seg, ut_be, false);
}

void m0_be_ut__seg_allocator_init(struct m0_be_seg *seg,
				  struct m0_be_ut_backend *ut_be)
{
	be_ut_seg_allocator_initfini(seg, ut_be, true);
}

void m0_be_ut__seg_allocator_fini(struct m0_be_seg *seg,
				  struct m0_be_ut_backend *ut_be)
{
	be_ut_seg_allocator_initfini(seg, ut_be, false);
}

M0_INTERNAL void m0_be_ut_txc_init(struct m0_be_ut_txc *tc)
{
	M0_PRE(tc->butc_seg_copy.b_addr == NULL);
	M0_PRE(tc->butc_seg_copy.b_nob  == 0);
}

static void be_ut_txc_reg_d_apply(struct m0_be_ut_txc *tc,
				  const struct m0_be_reg_d *rd)
{
	const struct m0_be_reg *reg = &rd->rd_reg;

	M0_PRE(rd->rd_reg.br_seg != NULL);
	M0_PRE(m0_be_reg__invariant(reg));
	M0_PRE(m0_be_reg_offset(reg) + reg->br_size <= tc->butc_seg_copy.b_nob);

	memcpy(tc->butc_seg_copy.b_addr + m0_be_reg_offset(reg),
	       reg->br_addr, reg->br_size);
}

static void be_ut_txc_check_seg(struct m0_be_ut_txc *tc,
				const struct m0_be_seg *seg,
				struct m0_be_tx *tx)
{
	const char	      *seg_copy_filename = "seg_copy.dat";
	const char	      *seg_filename	 = "seg.dat";
	struct m0_be_reg_area *ra = m0_be_tx__reg_area(tx);
	struct m0_be_reg_d    *rd;
	bool		       res = true;

	M0_BE_REG_AREA_FORALL(ra, rd) {
		/*  one segment capturing is supported atm */
		if (seg == NULL)
			seg = rd->rd_reg.br_seg;
		if (rd->rd_reg.br_seg != seg) {
			res = false;
			M0_LOG(M0_FATAL, "tx = %p, seg = %p, reg_d seg = %p",
			       tx, seg, rd->rd_reg.br_seg);
		/* captured regions shouldn't be modified after capturing */
		} else if (memcmp(rd->rd_buf, rd->rd_reg.br_addr,
				  rd->rd_reg.br_size) != 0) {
			res = false;
			M0_LOG(M0_FATAL,
			       "segment modification wasn't captured: "
			       "seg %p: addr = %p, size = %lu; "
			       "reg_d %p: reg addr = %p, size = %lu; ",
			       seg, seg->bs_addr, seg->bs_size,
			       rd, rd->rd_reg.br_addr, rd->rd_reg.br_size);
		}
	}
	M0_ASSERT(seg->bs_size == tc->butc_seg_copy.b_nob);
	/* apply captured regions to saved copy of the segment */
	M0_BE_REG_AREA_FORALL(ra, rd) {
		be_ut_txc_reg_d_apply(tc, rd);
	}
	if (memcmp(seg->bs_addr, tc->butc_seg_copy.b_addr, seg->bs_size) != 0) {
		res = false;
		be_ut_data_save(seg_copy_filename, seg->bs_size,
				tc->butc_seg_copy.b_addr);
		be_ut_data_save(seg_filename, seg->bs_size, seg->bs_addr);
		M0_LOG(M0_FATAL, "in-memory segment data differs from captured "
		       "seg copy saved to %s, in-memory seg saved to %s",
		       seg_copy_filename, seg_filename);
	}
	M0_ASSERT_INFO(res,
		       "tx capturing check failed. "
		       "See M0_LOG(M0_FATAL, ...) for ut subsystem");
}

M0_INTERNAL void m0_be_ut_txc_start(struct m0_be_ut_txc *tc,
				    struct m0_be_tx *tx,
				    const struct m0_be_seg *seg)
{
	if (seg->bs_size != tc->butc_seg_copy.b_nob) {
		m0_buf_free(&tc->butc_seg_copy);
		m0_buf_init(&tc->butc_seg_copy,
			    m0_alloc(seg->bs_size), seg->bs_size);
		M0_ASSERT(tc->butc_seg_copy.b_addr != NULL);
	}
	memcpy(tc->butc_seg_copy.b_addr, seg->bs_addr, seg->bs_size);
	be_ut_txc_check_seg(tc, seg, tx);
}

M0_INTERNAL void m0_be_ut_txc_check(struct m0_be_ut_txc *tc,
				    struct m0_be_tx *tx)
{
	be_ut_txc_check_seg(tc, NULL, tx);
}

M0_INTERNAL void m0_be_ut_txc_fini(struct m0_be_ut_txc *tc)
{
	m0_buf_free(&tc->butc_seg_copy);
}

/* ------------------------------------------------------------------
 * XXX: 0types definitions, used for fake-mkfs.
 * have to be redeclared in mkfs utility.
 * ------------------------------------------------------------------ */

static int ut_log0_init(struct m0_be_domain *dom, const char *suffix,
			const struct m0_buf *data)
{
	const struct m0_be_engine_cfg *en_cfg = &dom->bd_cfg.bc_engine;
	struct m0_be_engine *en = m0_be_domain_engine(dom);

	M0_ENTRY();

	M0_ASSERT_INFO(en_cfg->bec_log_size >=
		       en_cfg->bec_group_size_max.tc_reg_size,
		       "Log size shouldn't be less than maximum group size: "
		       "log_size = %lu, group_size_max = %lu",
		       en_cfg->bec_log_size,
		       en_cfg->bec_group_size_max.tc_reg_size);
	M0_ASSERT_INFO(!en_cfg->bec_log_replay, "Recovery is not implemented");

	m0_be_log_init(&en->eng_log, en_cfg->bec_log_stob,
		       m0_be_engine_got_log_space_cb);
	return M0_RC(m0_be_log_create(&en->eng_log, en_cfg->bec_log_size));
}

static void ut_log0_fini(struct m0_be_domain *dom, const char *suffix,
			 const struct m0_buf *data)
{
	struct m0_be_engine *en = m0_be_domain_engine(dom);

	M0_ENTRY();
	m0_be_log_fini(&en->eng_log);
	M0_LEAVE();
}

static int ut_seg0_init(struct m0_be_domain *dom, const char *suffix,
			const struct m0_buf *data)
{
	struct be_ut_helper_struct *h = &be_ut_helper;
	struct m0_be_seg           *seg;
	uint64_t                    size = dom->bd_cfg.bc_seg0_size;
	int			    rc;

	M0_ALLOC_PTR(seg);
	M0_ASSERT(seg != NULL);

	m0_be_seg_init(seg, m0_ut_stob_linux_get_by_key(1043), dom);
	rc = m0_be_seg_create(seg, size, be_ut_seg_allocate_addr(h, size));
	M0_ASSERT(rc == 0);
	rc = m0_be_seg_open(seg);
	M0_ASSERT(rc == 0);

	M0_ASSERT(seg_tlist_is_empty(&dom->bd_seg_list));
	seg_tlist_add(&dom->bd_seg_list, seg);

	return rc;
}

static void ut_seg0_fini(struct m0_be_domain *dom, const char *suffix,
			 const struct m0_buf *data)
{
	struct m0_be_seg    *seg  = m0_be_domain_seg0_get(dom);
	struct m0_stob      *stob = seg->bs_stob;
	int		     rc;

	m0_be_seg_close(seg);
	rc = m0_be_seg_destroy(seg);
	M0_ASSERT(rc == 0);
	seg_tlist_del(seg);
	m0_be_seg_fini(seg);
	m0_free(seg);

	m0_ut_stob_put(stob, false);
}

struct m0_be_0type m0_be_ut_seg0 = {
	.b0_name = "M0_BE_MKFS:SEG0",
	.b0_init = ut_seg0_init,
	.b0_fini = ut_seg0_fini
};

struct m0_be_0type m0_be_ut_log0 = {
	.b0_name = "M0_BE_MKFS:LOG",
	.b0_init = ut_log0_init,
	.b0_fini = ut_log0_fini
};

static int seg0_init(struct m0_be_domain *d, const char *suffix,
		     const struct m0_buf *data)
{
	M0_ENTRY();
	M0_LEAVE();
	return 0;
}

static void seg0_fini(struct m0_be_domain *d, const char *suffix,
		      const struct m0_buf *data)
{
	M0_ENTRY();
	M0_LEAVE();
}

static int log0_init(struct m0_be_domain *dom, const char *suffix,
		     const struct m0_buf *data)
{
	struct m0_be_engine *en = m0_be_domain_engine(dom);
	struct m0_be_0type_log_opts *opts =
		(struct m0_be_0type_log_opts *) data->b_addr;
	struct m0_stob *log_stob;

	M0_ENTRY();

	log_stob = m0_ut_stob_linux_get_by_key(opts->lo_stob_key);
	M0_ASSERT(log_stob != NULL);

	m0_be_log_init(&en->eng_log, log_stob, m0_be_engine_got_log_space_cb);
	return M0_RC(m0_be_log_create(&en->eng_log, opts->lo_size));
}

static void log0_fini(struct m0_be_domain *dom, const char *suffix,
			 const struct m0_buf *data)
{
	struct m0_be_engine *en = m0_be_domain_engine(dom);

	M0_ENTRY();
	m0_be_log_fini(&en->eng_log);
	m0_ut_stob_put(en->eng_log.lg_store.ls_stob, false);
	M0_LEAVE();
}

struct m0_be_0type m0_be_log0 = {
	.b0_name = "M0_BE:LOG",
	.b0_init = log0_init,
	.b0_fini = log0_fini
};

struct m0_be_0type m0_be_seg0 = {
	.b0_name = "M0_BE:SEG0",
	.b0_init = seg0_init,
	.b0_fini = seg0_fini
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
