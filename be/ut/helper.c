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
#include "stob/stob.h"		/* m0_stob_id */
#include "stob/linux.h"		/* m0_linux_stob_domain_locate */
#include "rpc/rpclib.h"		/* m0_rpc_server_start */
#include "net/net.h"		/* m0_net_xprt */

#include "ut/ast_thread.h"
#include "be/ut/helper.h"	/* m0_be_ut_backend */

#define BE_UT_H_STORAGE_DIR "./__seg_ut_stob"

#define REQH_EMU 1

enum {
	BE_UT_SEG_START_ADDR = 0x400000000000ULL,
	BE_UT_SEG_START_ID   = 42ULL,
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
	struct m0_stob_domain	*buh_stob_dom;
	void			*buh_addr;
	uint64_t		 buh_id;
	int			 buh_storage_ref_cnt;
};

struct be_ut_helper_struct be_ut_helper = {
	/* because there is no m0_mutex static initializer */
	.buh_once_control = PTHREAD_ONCE_INIT,
};


static void *be_ut_seg_allocate_addr(struct be_ut_helper_struct *h,
				     m0_bcount_t size)
{
	void *addr;

	size = m0_align(size, m0_pagesize_get());

	m0_mutex_lock(&h->buh_seg_lock);
	addr	     = h->buh_addr;
	h->buh_addr += size;
	m0_mutex_unlock(&h->buh_seg_lock);

	return addr;
}

static uint64_t be_ut_seg_allocate_id(struct be_ut_helper_struct *h)
{
	uint64_t id;

	m0_mutex_lock(&h->buh_seg_lock);
	id = h->buh_id++;
	m0_mutex_unlock(&h->buh_seg_lock);

	return id;
}

static inline void be_ut_helper_fini(void)
{
	struct be_ut_helper_struct *h = &be_ut_helper;

	M0_PRE(h->buh_stob_dom == NULL);
	M0_PRE(h->buh_storage_ref_cnt == 0);
	m0_mutex_fini(&h->buh_reqh_lock);
	m0_mutex_fini(&h->buh_seg_lock);
}

/* XXX call this function from m0_init()? */
static void be_ut_helper_init(void)
{
	struct be_ut_helper_struct *h = &be_ut_helper;

	h->buh_reqh_ref_cnt    = 0,
	h->buh_storage_ref_cnt = 0,
	h->buh_stob_dom	       = NULL,
	h->buh_addr	       = (void *) BE_UT_SEG_START_ADDR,
	h->buh_id	       = BE_UT_SEG_START_ID,
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

#ifndef REQH_EMU
struct m0_reqh *m0_be_ut_reqh_get(void)
{
	struct be_ut_helper_struct *h = &be_ut_helper;
	struct m0_reqh		   *reqh;
	int			    rc;
#define NAME(ext) "be-ut" ext
	static char		   *argv[] = {
		NAME(""), "-r", "-p", "-T", "linux", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", NAME("_addb.stob"), "-w", "10",
		"-e", "lnet:0@lo:12345:34:1", "-s", "be-tx-service"
	};

	be_ut_helper_init_once();

	m0_mutex_lock(&h->buh_reqh_lock);
	if (h->buh_reqh_ref_cnt == 0) {
		h->buh_net_xprt = &m0_net_lnet_xprt;
		h->buh_rpc_sctx = (struct m0_rpc_server_ctx) {
			.rsx_xprts         = &h->buh_net_xprt,
			.rsx_xprts_nr      = 1,
			.rsx_argv          = argv,
			.rsx_argc          = ARRAY_SIZE(argv),
			.rsx_log_file_name = NAME(".log"),
		};
#undef NAME
		rc = m0_net_xprt_init(h->buh_net_xprt);
		M0_ASSERT(rc == 0);
		rc = m0_rpc_server_start(&h->buh_rpc_sctx);
		M0_ASSERT(rc == 0);
	}
	M0_CNT_INC(h->buh_reqh_ref_cnt);
	reqh = m0_mero_to_rmach(&h->buh_rpc_sctx.rsx_mero_ctx)->rm_reqh;
	M0_ASSERT(reqh != NULL);
	m0_mutex_unlock(&h->buh_reqh_lock);

	return reqh;
}

void m0_be_ut_reqh_put(struct m0_reqh *reqh)
{
	struct be_ut_helper_struct *h = &be_ut_helper;
	struct m0_reqh		   *reqh2;

	m0_mutex_lock(&h->buh_reqh_lock);
	reqh2 = m0_mero_to_rmach(&h->buh_rpc_sctx.rsx_mero_ctx)->rm_reqh;
	M0_ASSERT(reqh == reqh2);
	M0_CNT_DEC(h->buh_reqh_ref_cnt);
	if (h->buh_reqh_ref_cnt == 0) {
		m0_rpc_server_stop(&h->buh_rpc_sctx);
		m0_net_xprt_fini(h->buh_net_xprt);
	}
	m0_mutex_unlock(&h->buh_reqh_lock);
}
#endif

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
				    struct m0_be_ut_sm_group_thread *,
				    NULL, &be_ut_sm_group_thread_func, sgt,
				    "%pbe_sm_group_thread", sgt);
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
	*cfg = (struct m0_be_domain_cfg) {
		.bc_engine = {
			.bec_group_nr = 1,
			.bec_log_size = 1 << 29,
			.bec_tx_size_max = M0_BE_TX_CREDIT_INIT(
				1 << 21, 1 << 27),
			.bec_group_size_max = M0_BE_TX_CREDIT_INIT(
				1 << 22, 1 << 28),
			.bec_group_tx_max = 20,
			.bec_log_replay = false,
		},
	};
}

void m0_be_ut_backend_init(struct m0_be_ut_backend *ut_be)
{
	int rc;

	M0_SET0(ut_be);
	ut_be->but_sm_groups_unlocked = false;
	m0_be_ut_backend_cfg_default(&ut_be->but_dom_cfg);
	ut_be->but_dom_cfg.bc_engine.bec_log_stob = m0_be_ut_stob_get(true);
#ifndef REQH_EMU
	/*
	 * XXX
	 * There is strange bug here. If m0_be_ut_stob_get() called before
	 * m0_be_ut_reqh_get(), then all tests pass. Otherwise tests fail
	 * with the following assertion failure:
	 * FATAL : [lib/assert.c:42:m0_panic] panic:
	 * m0_net__buffer_invariant(nb) nlx_xo_core_bev_to_net_bev()
	 * (/work/mero-backend/net/lnet/lnet_tm.c:292)
	 * Mero panic: m0_net__buffer_invariant(nb) at
	 * nlx_xo_core_bev_to_net_bev()
	 * /work/mero-backend/net/lnet/lnet_tm.c:292 (errno: 110) (last failed:
	 * none)
	 *
	 * This bug is similar to the one already encountered, and that bug was
	 * not fixed.
	 */
	ut_be->but_dom_cfg.bc_engine.bec_group_fom_reqh = m0_be_ut_reqh_get();
#endif
	m0_mutex_init(&ut_be->but_sgt_lock);
	rc = m0_be_domain_init(&ut_be->but_dom, &ut_be->but_dom_cfg);
	M0_ASSERT(rc == 0);

	if (rc != 0)
		m0_mutex_fini(&ut_be->but_sgt_lock);
}

void m0_be_ut_backend_fini(struct m0_be_ut_backend *ut_be)
{
	m0_forall(i, ut_be->but_sgt_size,
		  m0_be_ut_sm_group_thread_fini(ut_be->but_sgt[i]), true);
	m0_be_domain_fini(&ut_be->but_dom);
	m0_mutex_fini(&ut_be->but_sgt_lock);
#ifndef REQH_EMU
	m0_be_ut_reqh_put(ut_be->but_dom_cfg.bc_engine.bec_group_fom_reqh);
#endif
	m0_be_ut_stob_put(ut_be->but_dom_cfg.bc_engine.bec_log_stob, true);
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
	struct m0_be_ut_sm_group_thread	*sgt;
	struct m0_sm_group		*grp = NULL;
	pid_t				 tid = gettid_impl();
	int				 rc;

	m0_mutex_lock(&ut_be->but_sgt_lock);
	m0_forall(i, ut_be->but_sgt_size, sgt = ut_be->but_sgt[i],
		  grp = sgt->sgt_tid == tid ? &sgt->sgt_grp : NULL,
		  grp == NULL);
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

struct m0_stob *m0_be_ut_stob_get_by_id(uint64_t id, bool stob_create)
{
	struct be_ut_helper_struct *h = &be_ut_helper;
	struct m0_stob_id	    stob_id;
	struct m0_stob		   *stob;
	int			    rc;

	be_ut_helper_init_once();

	stob_id.si_bits = M0_UINT128(0, id);

	M0_ALLOC_PTR(stob);
	M0_ASSERT(stob != NULL);

	m0_mutex_lock(&h->buh_seg_lock);
	if (h->buh_storage_ref_cnt == 0) {
#if 0
		rc = system("rm -rf " BE_UT_H_STORAGE_DIR);
		M0_ASSERT(rc == 0);
#endif
		rc = mkdir(BE_UT_H_STORAGE_DIR, 0700);
		// M0_ASSERT(rc == 0);
		rc = mkdir(BE_UT_H_STORAGE_DIR "/o", 0700);
		// M0_ASSERT(rc == 0);

		M0_PRE(h->buh_stob_dom == NULL);
		rc = m0_linux_stob_domain_locate(BE_UT_H_STORAGE_DIR,
						 &h->buh_stob_dom);
		M0_ASSERT(rc == 0);
	}
	M0_CNT_INC(h->buh_storage_ref_cnt);

	if (stob_create) {
		rc = m0_stob_create_helper(h->buh_stob_dom, NULL, &stob_id,
					   &stob);
		M0_ASSERT(rc == 0);
	} else {
		m0_stob_init(stob, &stob_id, h->buh_stob_dom);
	}
	m0_mutex_unlock(&h->buh_seg_lock);
	return stob;
}

struct m0_stob *m0_be_ut_stob_get(bool stob_create)
{
	struct be_ut_helper_struct *h = &be_ut_helper;

	be_ut_helper_init_once();

	return m0_be_ut_stob_get_by_id(be_ut_seg_allocate_id(h), stob_create);
}

void m0_be_ut_stob_put(struct m0_stob *stob, bool stob_destroy)
{
	struct be_ut_helper_struct *h = &be_ut_helper;
	// int			    rc;

	m0_mutex_lock(&h->buh_seg_lock);
	if (stob_destroy)
		m0_stob_put(stob);

	M0_CNT_DEC(h->buh_storage_ref_cnt);
	if (h->buh_storage_ref_cnt == 0) {
		h->buh_stob_dom->sd_ops->sdo_fini(h->buh_stob_dom, NULL);
		h->buh_stob_dom = NULL;
		if (stob_destroy) {
#if 0
			rc = system("rm -rf " BE_UT_H_STORAGE_DIR);
			M0_ASSERT(rc == 0);
#endif
		}
	}
	m0_mutex_unlock(&h->buh_seg_lock);

	/* XXX memory leak here */
	/* m0_free(stob); */
}

void m0_be_ut_seg_init(struct m0_be_ut_seg *ut_seg,
		       struct m0_be_ut_backend *ut_be,
		       m0_bcount_t size)
{
	struct be_ut_helper_struct *h = &be_ut_helper;
	int			    rc;

	m0_be_seg_init(&ut_seg->bus_seg, m0_be_ut_stob_get(true),
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

	m0_be_ut_stob_put(stob, true);
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

static void be_ut_seg_allocator_initfini(struct m0_be_ut_seg *ut_seg,
					 struct m0_be_ut_backend *ut_be,
					 bool init)
{
	M0_BE_TX_CREDIT(credit);
	struct m0_be_allocator *a;
	struct m0_be_tx         tx;
	int                     rc;

	a = ut_seg->bus_allocator = &ut_seg->bus_seg.bs_allocator;

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
		rc = m0_be_allocator_init(a, &ut_seg->bus_seg);
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
	be_ut_seg_allocator_initfini(ut_seg, ut_be, true);
}

void m0_be_ut_seg_allocator_fini(struct m0_be_ut_seg *ut_seg,
				 struct m0_be_ut_backend *ut_be)
{
	be_ut_seg_allocator_initfini(ut_seg, ut_be, false);
}

#undef REQH_EMU

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
