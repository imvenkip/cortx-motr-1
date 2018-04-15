/* -*- C -*- */
/*
 * COPYRIGHT 2018 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 13-Apr-2018
 */

/**
 * @addtogroup be
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/fom_thread.h"

#include "lib/misc.h"   /* M0_SET0 */

/*
 * These lists are needed for m0_fom_invariant() to pass.
 * XXX copy-paste from fop/fom.c
 */

M0_TL_DESCR_DEFINE(ft_runq, "runq fom", static, struct m0_fom, fo_linkage,
		   fo_magic, M0_FOM_MAGIC, M0_FOM_RUNQ_MAGIC);
M0_TL_DEFINE(ft_runq, static, struct m0_fom);

M0_TL_DESCR_DEFINE(ft_wail, "wail fom", static, struct m0_fom, fo_linkage,
		   fo_magic, M0_FOM_MAGIC, M0_FOM_WAIL_MAGIC);
M0_TL_DEFINE(ft_wail, static, struct m0_fom);

static void be_fom_thread_clink_init(struct m0_be_fom_thread *fth)
{
	struct m0_fom_locality *loc   = &fth->fth_loc;
	struct m0_clink        *clink = &fth->fth_clink;

	m0_clink_init(clink, NULL);
	m0_clink_attach(clink, &loc->fl_group.s_clink, NULL);
	m0_clink_add(&loc->fl_runrun, clink);
}

static void be_fom_thread_clink_fini(struct m0_be_fom_thread *fth)
{
	struct m0_clink *clink = &fth->fth_clink;

	m0_clink_del(clink);
	m0_clink_fini(clink);
}

static void be_fom_thread_func(struct m0_be_fom_thread *fth)
{
	struct m0_sm_group *grp = &fth->fth_loc.fl_group;
	struct m0_fom      *fom = fth->fth_fom;
	struct m0_clink     clink;
	bool                done = false;
	int                 phase;
	int                 rc;

	M0_ENTRY("fth=%p grp=%p fom=%p", fth, grp, fom);
	/* This thread is not supposed to exit before the fom is finished */
	while (!done) {
		M0_SET0(&clink);
		m0_chan_wait(&fth->fth_clink);
		M0_LOG(M0_DEBUG, "signal caught");
		m0_sm_group_lock(grp);
		be_fom_thread_clink_fini(fth);
		be_fom_thread_clink_init(fth);
		if (!ft_runq_tlist_is_empty(&fth->fth_loc.fl_runq)) {
			M0_LOG(M0_DEBUG, "waking up");
			ft_runq_tlist_del(fom);
			M0_CNT_DEC(fth->fth_loc.fl_runq_nr);
			m0_sm_state_set(&fom->fo_sm_state, M0_FOS_RUNNING);
			do {
				phase = m0_fom_phase(fom);
				M0_ASSERT(phase != M0_FOM_PHASE_FINISH);
				rc = fom->fo_ops->fo_tick(fom);
				M0_ASSERT(M0_IN(rc,
						(M0_FSO_WAIT, M0_FSO_AGAIN)));
				M0_LOG(M0_DEBUG, "phase=%d rc=%s",
				       phase, rc == M0_FSO_WAIT ?
				       "M0_FSO_WAIT" : "M0_FSO_AGAIN");
			} while (rc == M0_FSO_AGAIN);
			M0_ASSERT(rc == M0_FSO_WAIT);
			if (m0_fom_phase(fom) == M0_FOM_PHASE_FINISH) {
				fom->fo_ops->fo_fini(fom);
				done = true;
			}
			m0_sm_state_set(&fom->fo_sm_state, M0_FOS_WAITING);
			M0_CNT_INC(fth->fth_loc.fl_wail_nr);
			ft_wail_tlist_add(&fth->fth_loc.fl_wail, fom);
		}
		m0_sm_group_unlock(grp);
	}
	M0_LEAVE("fth=%p grp=%p fom=%p", fth, grp, fom);
}

M0_INTERNAL int m0_be_fom_thread_init(struct m0_be_fom_thread *fth,
                                      struct m0_fom           *fom)
{
	int rc;

	M0_ENTRY("fth=%p fom=%p", fth, fom);
	M0_PRE(M0_IS0(fth));
	fth->fth_fom = fom;
	m0_sm_group_init(&fth->fth_loc.fl_group);
	m0_semaphore_init(&fth->fth_wakeup_sem, 0);
	ft_runq_tlist_init(&fth->fth_loc.fl_runq);
	ft_wail_tlist_init(&fth->fth_loc.fl_wail);
	ft_runq_tlink_init(fom);
	ft_wail_tlink_init_at(fom, &fth->fth_loc.fl_wail);
	fth->fth_loc.fl_wail_nr = 1;
	fth->fth_loc.fl_runq_nr = 0;
	fom->fo_loc = &fth->fth_loc;
	fom->fo_cb.fc_state = M0_FCS_DONE;
	m0_fom_sm_init(fom);
	m0_sm_group_lock(&fth->fth_loc.fl_group);
	m0_chan_init(&fth->fth_loc.fl_runrun, &fth->fth_loc.fl_group.s_lock);
	be_fom_thread_clink_init(fth);
	m0_sm_state_set(&fom->fo_sm_state, M0_FOS_READY);
	m0_sm_state_set(&fom->fo_sm_state, M0_FOS_RUNNING);
	m0_sm_state_set(&fom->fo_sm_state, M0_FOS_WAITING);
	m0_sm_group_unlock(&fth->fth_loc.fl_group);
	rc = M0_THREAD_INIT(&fth->fth_thread, struct m0_be_fom_thread *, NULL,
	                    &be_fom_thread_func, fth, "fom_thread_func");
	return M0_RC_INFO(rc, "fth=%p fom=%p", fth, fom);
}

M0_INTERNAL void m0_be_fom_thread_fini(struct m0_be_fom_thread *fth)
{
	struct m0_fom *fom = fth->fth_fom;
	int            rc;

	M0_ENTRY("fth=%p", fth);
	rc = m0_thread_join(&fth->fth_thread);
	M0_ASSERT_INFO(rc == 0, "rc=%d", rc);
	m0_thread_fini(&fth->fth_thread);
	m0_sm_group_lock(&fth->fth_loc.fl_group);
	if (fom->fo_sm_state.sm_state == M0_FOS_READY)
		m0_sm_state_set(&fom->fo_sm_state, M0_FOS_RUNNING);
	m0_sm_state_set(&fom->fo_sm_state, M0_FOS_FINISH);
	m0_sm_fini(&fom->fo_sm_phase);
	m0_sm_fini(&fom->fo_sm_state);
	be_fom_thread_clink_fini(fth);
	m0_chan_fini(&fth->fth_loc.fl_runrun);
	m0_sm_group_unlock(&fth->fth_loc.fl_group);
	ft_wail_tlist_del(fom);
	ft_wail_tlink_fini(fom);
	ft_runq_tlink_fini(fom);
	ft_wail_tlist_fini(&fth->fth_loc.fl_wail);
	ft_runq_tlist_fini(&fth->fth_loc.fl_runq);
	m0_semaphore_fini(&fth->fth_wakeup_sem);
	m0_sm_group_fini(&fth->fth_loc.fl_group);
	M0_LEAVE("fth=%p", fth);
}

M0_INTERNAL void m0_be_fom_thread_wakeup(struct m0_be_fom_thread *fth)
{
	M0_ENTRY("fth=%p", fth);
	m0_fom_wakeup(fth->fth_fom);
	/*
	m0_semaphore_up(&fth->fth_wakeup_sem);
	m0_clink_signal(&fth->fth_fom->fo_loc->fl_group.s_clink);
	*/
	M0_LEAVE("fth=%p", fth);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of be group */

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
