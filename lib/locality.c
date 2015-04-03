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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 04-Jun-2013
 */

/**
 * @addtogroup locality
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_LIB
#include "lib/trace.h"
#include "sm/sm.h"
#include "lib/chan.h"
#include "lib/arith.h"                    /* max_check */
#include "lib/errno.h"                    /* ENOMEM */
#include "lib/thread.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "lib/locality.h"
#include "module/instance.h"
#include "fop/fom.h"
#include "reqh/reqh.h"

M0_LOCKERS_DEFINE(M0_INTERNAL, m0_locality, lo_lockers);

struct chore_local {
	struct m0_locality_chore *lo_chore;
	void                     *lo_data;
	m0_time_t                 lo_last;
	struct m0_tlink           lo_linkage;
	uint64_t                  lo_magix;
};

struct locality_global {
	struct m0_locality    lg_fallback;
	struct m0_sm_group    lg_grp;
	struct m0_thread      lg_ast_thread;
	bool                  lg_shutdown;
	struct m0_fom_domain *lg_dom;

	struct m0_mutex       lg_lock;
	struct m0_tl          lg_chore;
};

enum { M0_CHORE_L_MAGIC, M0_CHORE_L_HEAD_MAGIC,
       M0_CHORES_G_MAGIC, M0_CHORES_G_HEAD_MAGIC };

M0_TL_DESCR_DEFINE(chore_l, "chores-local", static, struct chore_local,
		   lo_linkage, lo_magix,
		   M0_CHORE_L_MAGIC, M0_CHORE_L_HEAD_MAGIC);
M0_TL_DEFINE(chore_l, static, struct chore_local);

M0_TL_DESCR_DEFINE(chores_g, "chores-global", static, struct m0_locality_chore,
		   lc_linkage, lc_magix,
		   M0_CHORES_G_MAGIC, M0_CHORES_G_HEAD_MAGIC);
M0_TL_DEFINE(chores_g, static, struct m0_locality_chore);

static struct locality_global *loc_glob(void);
static int                     loc_nr(void);

static void chore_del(struct m0_locality *loc, struct m0_locality_chore *chore);
static int  chore_add(struct m0_locality *loc, struct m0_locality_chore *chore);
static void chore_post(struct m0_locality *loc, struct m0_locality_chore *chore,
		       void (*cb)(struct m0_sm_group *, struct m0_sm_ast *));
static int  chore_add_all(struct m0_locality_chore *chore);
static void chore_del_all(struct m0_locality_chore *chore);
static void chore_add_cb(struct m0_sm_group *grp, struct m0_sm_ast *ast);
static void chore_del_cb(struct m0_sm_group *grp, struct m0_sm_ast *ast);

#define LOC_FOR(idx_var, loc_var)					\
do {									\
	int idx_var;							\
									\
	for (idx_var = 0; idx_var < loc_nr(); ++idx_var) {		\
		struct m0_locality *loc_var = m0_locality_get(idx_var);

#define LOC_ENDFOR } } while (0)

#define CHORES_FOR(chvar)						\
do {									\
	struct m0_locality_chore *chvar;				\
	struct m0_tl             *__chlist = &loc_glob()->lg_chore;	\
	m0_tl_for(chores_g, __chlist, chvar)

#define CHORES_ENDFOR m0_tl_endfor; } while (0)

M0_INTERNAL void m0_locality_init(struct m0_locality *loc,
				  struct m0_sm_group *grp,
				  struct m0_reqh *reqh, size_t idx)
{
	M0_PRE(M0_IS0(loc));
	*loc = (struct m0_locality) {
		.lo_grp  = grp,
		.lo_reqh = reqh,
		.lo_idx  = idx
	};
	m0_locality_lockers_init(loc);
	chore_l_tlist_init(&loc->lo_chores);
}

M0_INTERNAL void m0_locality_fini(struct m0_locality *loc)
{
	chore_l_tlist_fini(&loc->lo_chores);
	m0_locality_lockers_fini(loc);
}

M0_INTERNAL struct m0_locality *m0_locality_here(void)
{
	return m0_locality_get(m0_processor_id_get());
}

M0_INTERNAL struct m0_locality *m0_locality_get(uint64_t value)
{
	struct locality_global *glob = loc_glob();
	struct m0_fom_domain   *dom  = glob->lg_dom;

	if (dom != NULL) {
		struct m0_fom_locality *floc;
		int                     idx = value % dom->fd_localities_nr;

		floc = dom->fd_localities[idx];
		M0_ASSERT(m0_bitmap_get(&floc->fl_processors, idx));
		return &floc->fl_locality;
	} else
		return &glob->lg_fallback;
}

M0_INTERNAL struct m0_locality *m0_locality0_get(void)
{
	return &loc_glob()->lg_fallback;
}

M0_INTERNAL int m0_locality_dom_set(struct m0_fom_domain *dom)
{
	struct locality_global *glob   = loc_glob();
	int                     result = 0;

	if (glob->lg_dom == NULL) {
		glob->lg_dom = dom;
		CHORES_FOR(chore) {
			result = chore_add_all(chore);
			if (result != 0) {
				CHORES_FOR(scan) {
					if (scan == chore)
						break;
					chore_del_all(chore);
				} CHORES_ENDFOR;
				glob->lg_dom = NULL;
			}
		} CHORES_ENDFOR;
	}
	return result;
}

M0_INTERNAL void m0_locality_dom_clear(struct m0_fom_domain *dom)
{
	struct locality_global *glob = loc_glob();

	if (dom == glob->lg_dom) {
		CHORES_FOR(chore) {
			chore_del_all(chore);
		} CHORES_ENDFOR;
		glob->lg_dom = NULL;
	}
}

static void locs_ast_handler(void *__unused)
{
	struct locality_global *glob = loc_glob();
	struct m0_sm_group     *grp  = &glob->lg_grp;

	while (!glob->lg_shutdown) {
		m0_chan_wait(&grp->s_clink);
		m0_sm_group_lock(grp);
		m0_sm_asts_run(grp);
		if (glob->lg_dom == NULL)
			m0_locality_chores_run(&glob->lg_fallback);
		m0_sm_group_unlock(grp);
	}
}

static int ast_thread_init(void *__unused)
{
	return 0;
}

M0_INTERNAL int m0_localities_init(void)
{
	struct locality_global *glob;

	M0_ALLOC_PTR(glob);
	if (glob != NULL) {
		m0_mutex_init(&glob->lg_lock);
		chores_g_tlist_init(&glob->lg_chore);
		m0_sm_group_init(&glob->lg_grp);
		m0_locality_init(&glob->lg_fallback, &glob->lg_grp, NULL, 0);
		m0_get()->i_moddata[M0_MODULE_LOCALITY] = glob;
		/*
		 * Start fall-back ast processing thread. Dummy init function
		 * (ast_thread_init()) is used to guarantee that the thread has
		 * started by the time M0_THREAD_INIT() returns. This is needed
		 * to make intialisation order deterministic.
		 */
		return M0_THREAD_INIT(&glob->lg_ast_thread, void *,
				      &ast_thread_init,
				      &locs_ast_handler, NULL,
				      "m0_fallback_ast");
	} else
		return M0_ERR(-ENOMEM);
}

M0_INTERNAL void m0_localities_fini(void)
{
	struct locality_global *glob = loc_glob();

	M0_PRE(glob->lg_dom == NULL);
	M0_PRE(chores_g_tlist_is_empty(&glob->lg_chore));

	glob->lg_shutdown = true;
	m0_clink_signal(&glob->lg_grp.s_clink);
	m0_thread_join(&glob->lg_ast_thread);
	m0_thread_fini(&glob->lg_ast_thread);
	m0_locality_fini(&glob->lg_fallback);
	m0_sm_group_fini(&glob->lg_grp);
	chores_g_tlist_fini(&glob->lg_chore);
	m0_mutex_fini(&glob->lg_lock);
}

/*
 * Chores interface.
 */

int m0_locality_chore_init(struct m0_locality_chore *chore,
			   const struct m0_locality_chore_ops *ops,
			   void *datum, m0_time_t interval,
			   size_t datasize)
{
	struct locality_global *glob = loc_glob();
	int                     result;

	M0_PRE(M0_IS0(chore));

	chore->lc_ops = ops;
	chore->lc_datum = datum;
	chore->lc_interval = interval;
	chore->lc_datasize = datasize;
	m0_mutex_init(&chore->lc_lock);
	m0_chan_init(&chore->lc_signal, &chore->lc_lock);
	m0_mutex_lock(&glob->lg_lock);
	chores_g_tlink_init_at_tail(chore, &glob->lg_chore);
	m0_mutex_unlock(&glob->lg_lock);
	result = chore_add_all(chore);
	if (result != 0)
		m0_locality_chore_fini(chore);
	return result;
}

void m0_locality_chore_fini(struct m0_locality_chore *chore)
{
	if (chore->lc_active > 0)
		chore_del_all(chore);
	m0_chan_fini_lock(&chore->lc_signal);
	chores_g_tlink_del_fini(chore);
	m0_mutex_fini(&chore->lc_lock);
}

M0_INTERNAL void m0_locality_chores_run(struct m0_locality *locality)
{
	struct chore_local     *chloc;
	struct locality_global *glob = loc_glob();

	M0_PRE(m0_sm_group_is_locked(locality->lo_grp));

	if (locality->lo_reqh != NULL &&
	    &locality->lo_reqh->rh_fom_dom != glob->lg_dom)
		/*
		 * Multiple request handler are running. Run chores only in the
		 * first one.
		 */
		return;

	M0_ASSERT(locality == m0_locality_here());

	m0_tl_for(chore_l, &locality->lo_chores, chloc) {
		struct m0_locality_chore *chore = chloc->lo_chore;

		if (chore->lc_interval == 0 ||
		    m0_time_is_in_past(m0_time_add(chloc->lo_last,
						   chore->lc_interval))) {
			chore->lc_ops->co_tick(chore,
					       locality, chloc->lo_data);
			chloc->lo_last = m0_time_now();
		}
	} m0_tl_endfor;
}

static struct locality_global *loc_glob(void)
{
	return m0_get()->i_moddata[M0_MODULE_LOCALITY];
}

static int loc_nr(void)
{
	struct locality_global *glob = loc_glob();

	return glob->lg_dom != NULL ? glob->lg_dom->fd_localities_nr : 1;
}

static int chore_add_all(struct m0_locality_chore *chore)
{
	int result;

	M0_PRE(chore->lc_active == 0);

	LOC_FOR(i, loc) {
		result = chore_add(loc, chore);
		if (result != 0) {
			LOC_FOR(i, scan) {
				if (scan == loc)
					break;
				chore_del(scan, chore);
			} LOC_ENDFOR;
			break;
		}
	} LOC_ENDFOR;
	M0_POST((result == 0) == (chore->lc_active == loc_nr()));
	M0_POST(M0_IN(chore->lc_active, (0, loc_nr())));
	return result;
}

static void chore_del_all(struct m0_locality_chore *chore)
{
	M0_POST(chore->lc_active == loc_nr());
	LOC_FOR(i, loc) {
		chore_del(loc, chore);
	} LOC_ENDFOR;
	M0_POST(chore->lc_active == 0);
}

static void chore_post(struct m0_locality *loc, struct m0_locality_chore *chore,
		       void (*cb)(struct m0_sm_group *, struct m0_sm_ast *))
{
	struct m0_sm_ast *ast = &chore->lc_ast;
	struct m0_clink   clink;

	M0_PRE(ast->sa_next == NULL);
	ast->sa_cb = cb;
	ast->sa_datum = loc;
	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&chore->lc_signal, &clink);
	m0_sm_ast_post(loc->lo_grp, &chore->lc_ast);
	m0_chan_wait(&clink);
	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
}

static int chore_add(struct m0_locality *loc, struct m0_locality_chore *chore)
{
	chore_post(loc, chore, &chore_add_cb);
	return chore->lc_rc;
}

static void chore_del(struct m0_locality *loc, struct m0_locality_chore *chore)
{
	chore_post(loc, chore, &chore_del_cb);
}

static void chore_add_cb(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_locality_chore *chore = M0_AMB(chore, ast, lc_ast);
	struct m0_locality       *loc   = ast->sa_datum;
	struct chore_local       *chloc;
	int                       result;

	M0_PRE(loc == m0_locality_here());
	M0_PRE(m0_tl_forall(chore_l, scan, &loc->lo_chores,
			    scan->lo_chore != chore));

	chloc = m0_alloc(sizeof *chloc + chore->lc_datasize);
	if (chloc != NULL) {
		chloc->lo_chore = chore;
		chloc->lo_data  = chloc + 1;
		result = chore->lc_ops->co_enter(chore, loc, chloc->lo_data);
		if (result == 0) {
			chore_l_tlink_init_at_tail(chloc, &loc->lo_chores);
			chore->lc_active++;
		} else
			m0_free(chloc);
	} else
		result = M0_ERR(-ENOMEM);
	chore->lc_rc = result;
	m0_chan_signal_lock(&chore->lc_signal);
}

static void chore_del_cb(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_locality_chore *chore = M0_AMB(chore, ast, lc_ast);
	struct m0_locality       *loc   = ast->sa_datum;
	struct chore_local       *chloc;

	chloc = m0_tl_find(chore_l, scan, &loc->lo_chores,
			   scan->lo_chore == chore);
	M0_PRE(chloc != NULL);
	chore->lc_ops->co_leave(chore, loc, chloc->lo_data);
	chore_l_tlink_del_fini(chloc);
	chore->lc_active--;
	m0_free(chloc);
	m0_chan_signal_lock(&chore->lc_signal);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of locality group */

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
