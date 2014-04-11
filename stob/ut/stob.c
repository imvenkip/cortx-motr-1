/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dmytro Podgornyi <dmytro_podgornyi@xyratex.com>
 * Original creation date: 5-Mar-2014
 */

#include "be/ut/helper.h"	/* m0_be_ut_backend, m0_be_ut_seg */
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/semaphore.h"
#include "module/instance.h"	/* m0_get */
#include "ut/ut.h"		/* M0_UT_ASSERT */
#include "ut/stob.h"		/* m0_ut_stob_linux_get */
#include "ut/threads.h"

#include "stob/ad.h"		/* m0_stob_ad_cfg_make */
#include "stob/domain.h"
#include "stob/stob.h"

enum {
	M0_STOB_UT_STOB_NR          = 0x04,
	M0_STOB_UT_THREADS_PER_STOB = 0x04,
	M0_STOB_UT_THREAD_NR        = M0_STOB_UT_STOB_NR *
				      M0_STOB_UT_THREADS_PER_STOB,
};

enum {
	LOCATION_SIZE = 64,
	SEG_SIZE      = 1 << 20,
};

struct stob_ut_ctx {
	struct m0_mutex       *su_lock;
	struct m0_semaphore   *su_destroy_sem;
	struct m0_stob_domain *su_domain;
	uint64_t               su_stob_key;
	int                    su_users_nr;
	const char            *su_cfg;
};

static void stob_ut_stob_thread(void *param)
{
	struct m0_be_ut_backend *ut_be;
	struct stob_ut_ctx      *ctx = (struct stob_ut_ctx *)param;
	struct m0_stob          *stob;
	bool                     stob_creator = false;
	int                      rc;
	int                      i;

	rc = m0_stob_find_by_key(ctx->su_domain, ctx->su_stob_key, &stob);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(stob != NULL);

	m0_mutex_lock(ctx->su_lock);
	M0_UT_ASSERT(M0_IN(m0_stob_state_get(stob),
			   (CSS_UNKNOWN, CSS_EXISTS)));
	if (m0_stob_state_get(stob) == CSS_UNKNOWN) {
		rc = m0_stob_locate(stob);
		M0_UT_ASSERT(rc == 0);
	}
	if (m0_stob_state_get(stob) == CSS_NOENT) {
		rc = m0_ut_stob_create(stob, ctx->su_cfg);
		M0_UT_ASSERT(rc == 0);
		stob_creator = true;
	}
	M0_UT_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);
	m0_mutex_unlock(ctx->su_lock);

	/* Reference counting */
	m0_stob_get(stob);
	m0_stob_put(stob);

	if (!stob_creator) {
		m0_stob_put(stob);
		m0_semaphore_up(ctx->su_destroy_sem);
	} else {
		for (i = 0; i < ctx->su_users_nr - 1; ++i)
			m0_semaphore_down(ctx->su_destroy_sem);
		rc = m0_ut_stob_destroy(stob);
		M0_UT_ASSERT(rc == 0);
	}

	/*
	 * m0_ut_stob_create() makes implicit m0_be_ut_backend_sm_group_lookup()
	 * therefore we need to call m0_be_ut_backend_thread_exit() for every
	 * thread that has called the helper. Otherwise, m0_be_ut_backend_fini()
	 * fails because of unlocked thread's sm group.
	 * Simplify this task and call the exit function for all threads.
	 */
	ut_be = m0_get()->i_be_ut_backend;
	if (ut_be != NULL) {
		(void)m0_be_ut_backend_sm_group_lookup(ut_be);
		m0_be_ut_backend_thread_exit(ut_be);
	}
}

M0_UT_THREADS_DEFINE(stob_ut_stob, stob_ut_stob_thread);

static void stob_ut_stob_multi(const char *location,
			       const char *dom_cfg,
			       const char *stob_cfg,
			       int thread_nr,
			       int stob_nr)
{
	struct stob_ut_ctx    *ctxs;
	struct m0_mutex       *stob_mtxs;
	struct m0_semaphore   *destroy_sems;
	struct m0_stob_domain *dom;
	uint64_t               dom_key = 0xba5ec0de;
	int                    rc;
	int                    i;

	M0_UT_ASSERT(stob_nr <= thread_nr);
	/* simplification */
	M0_UT_ASSERT(thread_nr % stob_nr == 0);

	M0_ALLOC_ARR(ctxs, thread_nr);
	M0_UT_ASSERT(ctxs != NULL);
	M0_ALLOC_ARR(stob_mtxs, stob_nr);
	M0_UT_ASSERT(stob_mtxs != NULL);
	M0_ALLOC_ARR(destroy_sems, stob_nr);
	M0_UT_ASSERT(destroy_sems != NULL);

	rc = m0_stob_domain_create(location, NULL, dom_key, dom_cfg, &dom);
	M0_UT_ASSERT(rc == 0);

	for (i = 0; i < stob_nr; ++i) {
		m0_mutex_init(&stob_mtxs[i]);
		m0_semaphore_init(&destroy_sems[i], 0);
	}
	for (i = 0; i < thread_nr; ++i) {
		ctxs[i] = (struct stob_ut_ctx) {
			.su_lock        = &stob_mtxs[i % stob_nr],
			.su_destroy_sem = &destroy_sems[i % stob_nr],
			.su_users_nr    = thread_nr / stob_nr,
			.su_stob_key    = i % stob_nr + 1,
			.su_domain      = dom,
			.su_cfg  = stob_cfg,
		};
	}

	M0_UT_THREADS_START(stob_ut_stob, thread_nr, ctxs);
	M0_UT_THREADS_STOP(stob_ut_stob);

	for (i = 0; i < stob_nr; ++i)
		m0_mutex_fini(&stob_mtxs[i]);
	m0_free(stob_mtxs);
	m0_free(ctxs);

	m0_stob_domain_destroy(dom);
}

static void stob_ut_stob_single(const char *location,
				const char *dom_cfg,
				const char *stob_cfg)
{
	struct m0_stob_domain *dom;
	struct m0_stob        *stob;
	struct m0_stob        *stob2;
	struct m0_fid          fid;
	uint64_t               dom_key = 0xba5ec0de;
	uint64_t               stob_key = 0xc0deba5e;
	int                    rc;

	rc = m0_stob_domain_create(location, NULL, dom_key, dom_cfg, &dom);
	M0_UT_ASSERT(rc == 0);

	rc = m0_stob_find_by_key(dom, stob_key, &stob);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(stob != NULL);
	M0_UT_ASSERT(m0_stob_state_get(stob) == CSS_UNKNOWN);

	rc = m0_stob_locate(stob);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_stob_state_get(stob) == CSS_NOENT);

	rc = m0_ut_stob_create(stob, stob_cfg);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);
	M0_UT_ASSERT(m0_stob_key_get(stob) == stob_key);
	rc = m0_ut_stob_create(stob, stob_cfg);
	M0_UT_ASSERT(rc == -EEXIST);

	rc = m0_stob_lookup_by_key(dom, stob_key, &stob2);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(stob2 == stob);
	m0_stob_put(stob2);

	m0_stob_get(stob);
	m0_stob_put(stob);

	fid = *m0_stob_fid_get(stob);

	m0_stob_put(stob);
	m0_stob_domain_fini(dom);

	rc = m0_stob_domain_init(location, NULL, &dom);
	M0_UT_ASSERT(rc == 0);
	rc = m0_stob_find(&fid, &stob);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(stob != NULL);
	M0_UT_ASSERT(m0_stob_state_get(stob) == CSS_UNKNOWN);
	rc = m0_stob_locate(stob);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);
	M0_UT_ASSERT(m0_stob_key_get(stob) == stob_key);

	rc = m0_stob_lookup(&fid, &stob2);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(stob2 == stob);
	m0_stob_put(stob2);

	rc = m0_ut_stob_destroy(stob);
	M0_UT_ASSERT(rc == 0);
	rc = m0_stob_domain_destroy(dom);
	M0_UT_ASSERT(rc == 0);
}

void m0_stob_ut_stob_null(void)
{
	M0_CASSERT(M0_STOB_UT_THREAD_NR % M0_STOB_UT_STOB_NR == 0);
	stob_ut_stob_single("nullstob:path", NULL, NULL);
	stob_ut_stob_multi("nullstob:path", NULL, NULL, M0_STOB_UT_THREAD_NR,
			   M0_STOB_UT_STOB_NR);
}

#ifndef __KERNEL__
void m0_stob_ut_stob_linux(void)
{
	stob_ut_stob_single("linuxstob:./__s", NULL, NULL);
	stob_ut_stob_multi("linuxstob:./__s", NULL, NULL, M0_STOB_UT_THREAD_NR,
			   M0_STOB_UT_STOB_NR);
}

void m0_stob_ut_stob_ad(void)
{
	struct m0_be_ut_backend  ut_be = {};
	struct m0_be_ut_seg      ut_seg = {};
	struct m0_be_seg        *seg;
	struct m0_stob          *stob;
	struct m0_sm_group      *grp;
	char                    *dom_cfg;
	char                     location[LOCATION_SIZE];
	int                      rc;

	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, SEG_SIZE);
	m0_be_ut_seg_allocator_init(&ut_seg, &ut_be);
	seg = &ut_seg.bus_seg;
	grp = m0_be_ut_backend_sm_group_lookup(&ut_be);
	rc  = m0_be_seg_dict_create_grp(seg, grp);
	M0_ASSERT(rc == 0);
	stob = m0_ut_stob_linux_get();
	M0_UT_ASSERT(stob != NULL);

	rc = snprintf(location, sizeof(location), "adstob:seg=%p,1234", seg);
	M0_UT_ASSERT(rc < sizeof(location));
	m0_stob_ad_cfg_make(&dom_cfg, seg, m0_stob_fid_get(stob));
	M0_UT_ASSERT(dom_cfg != NULL);

	stob_ut_stob_single(location, dom_cfg, NULL);
	stob_ut_stob_multi(location, dom_cfg, NULL, M0_STOB_UT_THREAD_NR,
			   M0_STOB_UT_STOB_NR);

	m0_free(dom_cfg);
	m0_ut_stob_put(stob, true);
	m0_be_seg_dict_destroy_grp(seg, grp);
	/* m0_be_ut_seg_allocator_fini(&ut_seg, &ut_be); */ /* XXX */
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);

}
#endif

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
