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
 * Original creation date: 1-Mar-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/log_sched.h"

#include "lib/memory.h" /* m0_alloc_aligned */
#include "lib/errno.h"  /* ENOMEM */

/**
 * @addtogroup be
 *
 * @{
 */

/* m0_be_log_sched::lsh_lios */
M0_TL_DESCR_DEFINE(sched_lio, "be log scheduler log IOs", static,
		   struct m0_be_log_io, lio_sched_linkage, lio_magic,
		   M0_BE_LOG_IO_MAGIC, M0_BE_LOG_IO_HEAD_MAGIC);
M0_TL_DEFINE(sched_lio, static, struct m0_be_log_io);


M0_INTERNAL int m0_be_log_sched_init(struct m0_be_log_sched     *sched,
				     struct m0_be_log_sched_cfg *cfg)
{
	if (cfg != NULL)
		sched->lsh_cfg = *cfg;
	m0_mutex_init(&sched->lsh_lock);
	sched_lio_tlist_init(&sched->lsh_lios);
	sched->lsh_io_in_progress = false;

	return 0;
}

M0_INTERNAL void m0_be_log_sched_fini(struct m0_be_log_sched *sched)
{
	sched_lio_tlist_fini(&sched->lsh_lios);
	m0_mutex_fini(&sched->lsh_lock);
}

M0_INTERNAL void m0_be_log_sched_lock(struct m0_be_log_sched *sched)
{
	m0_mutex_lock(&sched->lsh_lock);
}

M0_INTERNAL void m0_be_log_sched_unlock(struct m0_be_log_sched *sched)
{
	m0_mutex_unlock(&sched->lsh_lock);
}

M0_INTERNAL bool m0_be_log_sched_is_locked(struct m0_be_log_sched *sched)
{
	return m0_mutex_is_locked(&sched->lsh_lock);
}

static void be_log_sched_launch_next(struct m0_be_log_sched *sched)
{
	struct m0_be_log_io *lio;

	M0_PRE(m0_mutex_is_locked(&sched->lsh_lock));

	if (!sched->lsh_io_in_progress) {
		lio = sched_lio_tlist_head(&sched->lsh_lios);
		if (lio != NULL) {
			sched->lsh_io_in_progress = true;
			m0_be_io_launch(&lio->lio_be_io, &lio->lio_op);
		}
	}
}

static void be_log_sched_launch_next_locked(struct m0_be_log_sched *sched)
{
	m0_mutex_lock(&sched->lsh_lock);
	be_log_sched_launch_next(sched);
	m0_mutex_unlock(&sched->lsh_lock);
}

static void be_log_sched_cb(struct m0_be_op *op, void *param)
{
	struct m0_be_log_io    *lio   = param;
	struct m0_be_log_sched *sched = lio->lio_sched;

	m0_mutex_lock(&sched->lsh_lock);
	sched_lio_tlink_del_fini(lio);
	sched->lsh_io_in_progress = false;
	m0_mutex_unlock(&sched->lsh_lock);

	be_log_sched_launch_next_locked(sched);
}

M0_INTERNAL void m0_be_log_sched_add(struct m0_be_log_sched *sched,
				     struct m0_be_log_io    *lio,
				     struct m0_be_op        *op)
{
	M0_PRE(m0_mutex_is_locked(&sched->lsh_lock));
	M0_PRE(!m0_be_log_io_is_empty(lio));

	lio->lio_sched = sched;
	sched_lio_tlink_init_at_tail(lio, &sched->lsh_lios);
	m0_be_op_set_add(op, &lio->lio_op);
	be_log_sched_launch_next(sched);
}

M0_INTERNAL int m0_be_log_io_init(struct m0_be_log_io *lio)
{
	m0_be_op_init(&lio->lio_op);
	m0_be_op_callback_set(&lio->lio_op, &be_log_sched_cb, lio, M0_BOS_DONE);

	return 0;
}

M0_INTERNAL void m0_be_log_io_fini(struct m0_be_log_io *lio)
{
	m0_be_op_fini(&lio->lio_op);
}

M0_INTERNAL void m0_be_log_io_reset(struct m0_be_log_io *lio)
{
	lio->lio_buf_addr = NULL;
	lio->lio_buf_size = 0;
	lio->lio_bufvec   = M0_BUFVEC_INIT_BUF(NULL, NULL);
	m0_be_op_reset(&lio->lio_op);
	m0_be_io_reset(&lio->lio_be_io);
}

M0_INTERNAL int m0_be_log_io_allocate(struct m0_be_log_io    *lio,
				      struct m0_be_io_credit *iocred,
				      uint32_t                log_bshift)
{
	m0_bcount_t size  = iocred->bic_reg_size;
	void       *addr;
	int         rc;

	M0_PRE(m0_is_aligned(size, 1 << log_bshift));

	addr = m0_alloc_aligned(size, log_bshift);
	rc   = addr == NULL ? -ENOMEM : 0;
	if (rc != 0)
		goto err;
	rc = m0_be_io_init(&lio->lio_be_io);
	if (rc != 0)
		goto err_free;
	rc = m0_be_io_allocate(&lio->lio_be_io, iocred);
	if (rc != 0)
		goto err_lio_fini;

	lio->lio_buf        = M0_BUF_INIT(size, addr);
	lio->lio_log_bshift = log_bshift;

	return rc;

err_lio_fini:
	m0_be_io_fini(&lio->lio_be_io);
err_free:
	m0_free_aligned(addr, size, log_bshift);
err:
	return rc;
}

M0_INTERNAL void m0_be_log_io_deallocate(struct m0_be_log_io *lio)
{
	struct m0_buf *buf = &lio->lio_buf;

	m0_be_io_deallocate(&lio->lio_be_io);
	m0_be_io_fini(&lio->lio_be_io);
	m0_free_aligned(buf->b_addr, buf->b_nob, lio->lio_log_bshift);
}

M0_INTERNAL struct m0_bufvec *m0_be_log_io_bufvec(struct m0_be_log_io *lio)
{
	return &lio->lio_bufvec;
}

M0_INTERNAL struct m0_be_io *m0_be_log_io_be_io(struct m0_be_log_io *lio)
{
	return &lio->lio_be_io;
}

M0_INTERNAL void m0_be_log_io_user_data_set(struct m0_be_log_io *lio,
					    void                *data)
{
	lio->lio_user_data = data;
}

M0_INTERNAL void *m0_be_log_io_user_data(struct m0_be_log_io *lio)
{
	return lio->lio_user_data;
}

M0_INTERNAL bool m0_be_log_io_is_empty(struct m0_be_log_io *lio)
{
	return m0_be_io_is_empty(&lio->lio_be_io);
}

/** @} end of be group */
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
