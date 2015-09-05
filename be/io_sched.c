/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 5-Sep-2015
 */


/**
 * @addtogroup be
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/io_sched.h"

#include "be/op.h"              /* m0_be_op */
#include "be/io.h"              /* m0_be_io_launch */

#include "mero/magic.h"         /* M0_BE_LOG_IO_MAGIC */


/* m0_be_io_sched::bis_ios */
M0_TL_DESCR_DEFINE(sched_io, "be log scheduler IOs", static,
		   struct m0_be_io, bio_sched_link, bio_sched_magic,
		   M0_BE_IO_SCHED_MAGIC, M0_BE_IO_SCHED_HEAD_MAGIC);
M0_TL_DEFINE(sched_io, static, struct m0_be_io);


M0_INTERNAL int m0_be_io_sched_init(struct m0_be_io_sched     *sched,
				    struct m0_be_io_sched_cfg *cfg)
{
	if (cfg != NULL)
		sched->bis_cfg = *cfg;
	m0_mutex_init(&sched->bis_lock);
	sched_io_tlist_init(&sched->bis_ios);
	sched->bis_io_in_progress = false;

	return 0;
}

M0_INTERNAL void m0_be_io_sched_fini(struct m0_be_io_sched *sched)
{
	sched_io_tlist_fini(&sched->bis_ios);
	m0_mutex_fini(&sched->bis_lock);
}

M0_INTERNAL void m0_be_io_sched_lock(struct m0_be_io_sched *sched)
{
	m0_mutex_lock(&sched->bis_lock);
}

M0_INTERNAL void m0_be_io_sched_unlock(struct m0_be_io_sched *sched)
{
	m0_mutex_unlock(&sched->bis_lock);
}

M0_INTERNAL bool m0_be_io_sched_is_locked(struct m0_be_io_sched *sched)
{
	return m0_mutex_is_locked(&sched->bis_lock);
}

static void be_io_sched_launch_next(struct m0_be_io_sched *sched)
{
	struct m0_be_io *io;

	M0_PRE(m0_be_io_sched_is_locked(sched));

	if (!sched->bis_io_in_progress) {
		io = sched_io_tlist_head(&sched->bis_ios);
		if (io != NULL) {
			sched->bis_io_in_progress = true;
			m0_be_io_launch(io, &io->bio_sched_op);
		}
	}
}

static void be_io_sched_launch_next_locked(struct m0_be_io_sched *sched)
{
	m0_be_io_sched_lock(sched);
	be_io_sched_launch_next(sched);
	m0_be_io_sched_unlock(sched);
}

static void be_io_sched_cb(struct m0_be_op *op, void *param)
{
	struct m0_be_io       *io    = param;
	struct m0_be_io_sched *sched = io->bio_sched;

	M0_LOG(M0_DEBUG, "sched=%p io=%p", sched, io);

	m0_be_io_sched_lock(sched);
	sched_io_tlink_del_fini(io);
	m0_be_op_fini(&io->bio_sched_op);
	sched->bis_io_in_progress = false;
	m0_be_io_sched_unlock(sched);

	be_io_sched_launch_next_locked(sched);
}

M0_INTERNAL void m0_be_io_sched_add(struct m0_be_io_sched *sched,
				    struct m0_be_io       *io,
				    struct m0_be_op       *op)
{
	M0_LOG(M0_DEBUG, "sched=%p io=%p op=%p "
	       "m0_be_io_size(io)=%"PRIu64,
	       sched, io, op, m0_be_io_size(io));

	M0_PRE(m0_be_io_sched_is_locked(sched));
	M0_PRE(!m0_be_io_is_empty(io));

	io->bio_sched = sched;
	sched_io_tlink_init_at_tail(io, &sched->bis_ios);
	m0_be_op_init(&io->bio_sched_op);
	m0_be_op_callback_set(&io->bio_sched_op, &be_io_sched_cb,
			      io, M0_BOS_GC);
	m0_be_op_set_add(op, &io->bio_sched_op);
	be_io_sched_launch_next(sched);
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
