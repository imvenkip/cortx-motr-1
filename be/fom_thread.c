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

static void be_fom_thread_func(struct m0_be_fom_thread *fth)
{
	struct m0_sm_group *grp = &fth->fth_loc.fl_group;
	struct m0_fom      *fom = fth->fth_fom;
	bool                done = false;
	int                 rc;

	/* This thread is not supposed to exit before the fom finishes */
	while (!done) {
		m0_chan_wait(&grp->s_clink);
		m0_sm_group_lock(grp);
		if (m0_semaphore_trydown(&fth->fth_wakeup_sem)) {
			do {
				M0_ASSERT(m0_fom_phase(fom) !=
					  M0_FOM_PHASE_FINISH);
				rc = fom->fo_ops->fo_tick(fom);
			} while (rc == M0_FSO_AGAIN);
			M0_ASSERT(rc == M0_FSO_WAIT);
			if (m0_fom_phase(fom) == M0_FOM_PHASE_FINISH) {
				fom->fo_ops->fo_fini(fom);
				done = true;
			}
		}
		m0_sm_group_unlock(grp);
	}
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
	fom->fo_loc = &fth->fth_loc;
	m0_fom_sm_init(fom);
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
	m0_sm_state_set(&fom->fo_sm_state, M0_FOS_FINISH);
	m0_sm_fini(&fom->fo_sm_phase);
	m0_sm_fini(&fom->fo_sm_state);
	m0_sm_group_unlock(&fth->fth_loc.fl_group);
	m0_semaphore_fini(&fth->fth_wakeup_sem);
	m0_sm_group_fini(&fth->fth_loc.fl_group);
	M0_LEAVE("fth=%p", fth);
}

M0_INTERNAL void m0_be_fom_thread_wakeup(struct m0_be_fom_thread *fth)
{
	M0_ENTRY("fth=%p", fth);
	m0_semaphore_up(&fth->fth_wakeup_sem);
	m0_clink_signal(&fth->fth_fom->fo_loc->fl_group.s_clink);
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
