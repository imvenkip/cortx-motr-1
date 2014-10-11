/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/21/2010
 */

#include "stob/ioq.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_STOB
#include "lib/trace.h"

#include <limits.h>			/* IOV_MAX */
#include <sys/uio.h>			/* iovec */

#include "ha/note.h"                    /* ha_note, ha_nvec */

#include "lib/misc.h"			/* M0_SET0 */
#include "lib/errno.h"			/* ENOMEM */
#include "lib/locality.h"
#include "lib/memory.h"			/* M0_ALLOC_PTR */

#include "reqh/reqh.h"                  /* m0_reqh */
#include "rpc/session.h"                /* m0_rpc_session */

#include "stob/stob_addb.h"		/* M0_STOB_OOM */
#include "stob/linux.h"			/* m0_stob_linux_container */
#include "stob/linux_getevents.h"	/* raw_io_getevents */
#include "stob/io.h"			/* m0_stob_io */

/**
   @addtogroup stoblinux

   <b>Linux stob adieu</b>

   adieu implementation for Linux stob is based on Linux specific asynchronous
   IO interfaces: io_{setup,destroy,submit,cancel,getevents}().

   IO admission control and queueing in Linux stob adieu are implemented on a
   storage object domain level, that is, each domain has its own set of queues,
   threads and thresholds.

   On a high level, adieu IO request is first split into fragments. A fragment
   is initially placed into a per-domain queue (admission queue,
   tmp_linux_domain::ioq_queue) where it is held until there is enough space in the
   AIO ring buffer (tmp_linux_domain::ioq_ctx). Placing a fragment into the ring
   buffer (ioq_queue_submit()) means that kernel AIO is launched for it. When IO
   completes, the kernel delivers an IO completion event via the ring buffer.

   A number (M0_STOB_IOQ_NR_THREADS by default) of worker adieu threads is
   created for each storage object domain. These threads are implementing
   admission control and completion notification, they

   @li listen for the AIO completion events on the in the ring buffer. When AIO
   is completed, worker thread signals completion event to AIO users;

   @li when space becomes available in the ring buffer, a worker thread moves
   some number of pending fragments from the admission queue to the ring buffer.

   Admission queue separate from the ring buffer is needed to

   @li be able to handle more pending fragments than a kernel can support and

   @li potentially do some pre-processing on the pending fragments (like
   elevator does).

   <b>Concurrency control</b>

   Per-domain data structures (queue, thresholds, etc.) are protected by
   tmp_linux_domain::ioq_mutex.

   Concurrency control for an individual adieu fragment is very simple: user is
   not allowed to touch it in SIS_BUSY state and io_getevents() exactly-once
   delivery semantics guarantee that there is no concurrency for busy->idle
   transition. This nice picture would break apart if IO cancellation were to be
   implemented, because it requires synchronization between user actions
   (cancellation) and ongoing IO in SIS_BUSY state.

   @todo use explicit state machine instead of ioq threads

   @see http://www.kernel.org/doc/man-pages/online/pages/man2/io_setup.2.html

   @{
 */

/* ---------------------------------------------------------------------- */

/**
   AIO fragment.

   A ioq_qev is created for each fragment of original adieu request (see
   tmp_linux_stob_io_launch()).
 */
struct ioq_qev {
	struct iocb           iq_iocb;
	m0_bcount_t           iq_nbytes;
	/** Linkage to a per-domain admission queue
	    (tmp_linux_domain::ioq_queue). */
	struct m0_queue_link  iq_linkage;
	struct m0_stob_io    *iq_io;
};

/**
   Linux adieu specific part of generic m0_stob_io structure.
 */
struct stob_linux_io {
	/** Number of fragments in this adieu request. */
	uint32_t           si_nr;
	/** Number of completed fragments. */
	struct m0_atomic64 si_done;
	/** Number of completed bytes. */
	struct m0_atomic64 si_bdone;
	/** Array of fragments. */
	struct ioq_qev    *si_qev;
	/** Main ioq struct */
	struct m0_stob_ioq *si_ioq;
};

static struct ioq_qev *ioq_queue_get   (struct m0_stob_ioq *ioq);
static void            ioq_queue_put   (struct m0_stob_ioq *ioq,
					struct ioq_qev *qev);
static void            ioq_queue_submit(struct m0_stob_ioq *ioq);
static void            ioq_queue_lock  (struct m0_stob_ioq *ioq);
static void            ioq_queue_unlock(struct m0_stob_ioq *ioq);

static const struct m0_stob_io_op stob_linux_io_op;

enum {
	/*
	 * Alignment for direct-IO.
	 *
	 * According to open(2) manpage: "Under Linux 2.6, alignment to
	 * 512-byte boundaries suffices".
	 */
	STOB_IOQ_BSHIFT = 12, /* pow(2, 12) == 4096 */
	STOB_IOQ_BSIZE	= 1 << STOB_IOQ_BSHIFT,
	STOB_IOQ_BMASK	= STOB_IOQ_BSIZE - 1
};

/**
 * Handles detection of drive IO error by signalling HA.
 */
static void io_err_callback(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_ha_note      note;
	struct m0_ha_nvec      nvec;
	struct m0_rpc_session *rpc_ssn;
	struct m0_stob_linux  *lstob;
	struct m0_stob_ioq    *ioq;

	M0_ENTRY();

	lstob = container_of(ast, struct m0_stob_linux, sl_ast);
	ioq = &container_of(&lstob->sl_stob.so_domain,
			   struct m0_stob_linux_domain, sld_dom)->sld_ioq;
	rpc_ssn = &m0_locality_here()->lo_reqh->rh_ha_rpc_session;
	m0_mutex_lock(&ioq->ioq_lock);
	if (rpc_ssn != NULL) {
		note.no_id    = *m0_stob_fid_get(&lstob->sl_stob);
		note.no_state = M0_NC_FAILED;
		nvec.nv_nr    = 1;
		nvec.nv_note  = &note;
		m0_ha_state_set(rpc_ssn, &nvec);
	}
	/* Release stob reference and mutex, mark AST completed. */
	ast->sa_cb = NULL;
	m0_mutex_unlock(&ioq->ioq_lock);
	m0_stob_put(&lstob->sl_stob);

	M0_LEAVE();
}

M0_INTERNAL int m0_stob_linux_io_init(struct m0_stob *stob,
				      struct m0_stob_io *io)
{
	struct m0_stob_linux *lstob = m0_stob_linux_container(stob);
	struct stob_linux_io *lio;
	int                   result;

	M0_PRE(io->si_state == SIS_IDLE);

	M0_ALLOC_PTR(lio);
	if (lio != NULL) {
		io->si_stob_private = lio;
		io->si_op = &stob_linux_io_op;
		lio->si_ioq = &lstob->sl_dom->sld_ioq;
		result = 0;
	} else {
		M0_STOB_OOM(LAD_STOB_IO_INIT);
		result = -ENOMEM;
	}
	return result;
}

static void stob_linux_io_release(struct stob_linux_io *lio)
{
	if (lio->si_qev != NULL)
		m0_free(lio->si_qev->iq_iocb.u.c.buf);
	m0_free0(&lio->si_qev);
}

static void stob_linux_io_fini(struct m0_stob_io *io)
{
	struct stob_linux_io *lio = io->si_stob_private;

	stob_linux_io_release(lio);
	m0_free(lio);
}

/**
   Launch asynchronous IO.

   @li calculate how many fragments IO operation has;

   @li allocate ioq_qev array and fill it with fragments;

   @li queue all fragments and submit as many as possible;
 */
static int stob_linux_io_launch(struct m0_stob_io *io)
{
	struct m0_stob_linux *lstob = m0_stob_linux_container(io->si_obj);
	struct stob_linux_io *lio   = io->si_stob_private;
	struct m0_stob_ioq   *ioq   = lio->si_ioq;
	struct ioq_qev       *qev;
	struct iovec         *iov;
	struct m0_vec_cursor  src;
	struct m0_vec_cursor  dst;
	uint32_t              frags = 0;
	uint32_t              chunks; /* contiguous stob chunks */
	m0_bcount_t           frag_size;
	int                   result = 0;
	int                   i;
	bool                  eosrc;
	bool                  eodst;
	int                   opcode;

	M0_PRE(M0_IN(io->si_opcode, (SIO_READ, SIO_WRITE)));
	/* prefix fragments execution mode is not yet supported */
	M0_ASSERT((io->si_flags & SIF_PREFIX) == 0);
	M0_PRE(!m0_vec_is_empty(&io->si_user.ov_vec));

	chunks = io->si_stob.iv_vec.v_nr;

	m0_vec_cursor_init(&src, &io->si_user.ov_vec);
	m0_vec_cursor_init(&dst, &io->si_stob.iv_vec);

	do {
		frag_size = min_check(m0_vec_cursor_step(&src),
				      m0_vec_cursor_step(&dst));
		M0_ASSERT(frag_size > 0);
		frags++;
		eosrc = m0_vec_cursor_move(&src, frag_size);
		eodst = m0_vec_cursor_move(&dst, frag_size);
		M0_ASSERT(eosrc == eodst);
	} while (!eosrc);

	m0_vec_cursor_init(&src, &io->si_user.ov_vec);
	m0_vec_cursor_init(&dst, &io->si_stob.iv_vec);

	lio->si_nr = max_check(frags / IOV_MAX + 1, chunks);
	M0_LOG(M0_DEBUG, "chunks=%d frags=%d si_nr=%d",
	       chunks, frags, lio->si_nr);
	m0_atomic64_set(&lio->si_done, 0);
	m0_atomic64_set(&lio->si_bdone, 0);
	M0_ALLOC_ARR(lio->si_qev, lio->si_nr);
	M0_ALLOC_ARR(iov, frags);
	qev = lio->si_qev;
	if (qev == NULL || iov == NULL) {
		M0_STOB_OOM(LAD_STOB_IO_LAUNCH_2);
		result = -ENOMEM;
	}
	opcode = io->si_opcode == SIO_READ ? IO_CMD_PREADV : IO_CMD_PWRITEV;

	ioq_queue_lock(ioq);
	while (result == 0) {
		struct iocb *iocb = &qev->iq_iocb;
		m0_bindex_t  off = io->si_stob.iv_index[dst.vc_seg] +
				   dst.vc_offset;
		m0_bindex_t  prev_off = ~0;
		m0_bcount_t  chunk_size = 0;

		qev->iq_io = io;
		m0_queue_link_init(&qev->iq_linkage);

		iocb->u.v.vec = iov;
		iocb->aio_fildes = lstob->sl_fd;
		iocb->u.v.nr = min32u(frags, IOV_MAX);
		iocb->u.v.offset = off << m0_stob_ioq_bshift(ioq);
		iocb->aio_lio_opcode = opcode;

		for (i = 0; i < iocb->u.v.nr; ++i) {
			void        *buf;
			m0_bindex_t  off;

			buf = io->si_user.ov_buf[src.vc_seg] + src.vc_offset;
			off = io->si_stob.iv_index[dst.vc_seg] + dst.vc_offset;

			if (prev_off != ~0 && prev_off + frag_size != off)
				break;
			prev_off = off;

			frag_size = min_check(m0_vec_cursor_step(&src),
					      m0_vec_cursor_step(&dst));
			if (frag_size > (size_t)~0ULL) {
				M0_STOB_FUNC_FAIL(LAD_STOB_IO_LAUNCH_1,
						  -EOVERFLOW);
				result = -EOVERFLOW;
				break;
			}

			iov->iov_base = m0_stob_addr_open(buf,
						m0_stob_ioq_bshift(ioq));
			iov->iov_len  = frag_size << m0_stob_ioq_bshift(ioq);
			chunk_size += frag_size;

			m0_vec_cursor_move(&src, frag_size);
			m0_vec_cursor_move(&dst, frag_size);
			++iov;
		}
		M0_LOG(M0_DEBUG, FID_F"(%p) %2d: frags=%d op=%d off=%lx sz=%lx"
				 ": rc = %d",
		       FID_P(m0_stob_fid_get(io->si_obj)), io,
		       (int)(qev - lio->si_qev), i, io->si_opcode,
		       (unsigned long)off, (unsigned long)chunk_size, result);
		if (result == 0) {
			iocb->u.v.nr = i;
			qev->iq_nbytes = chunk_size << m0_stob_ioq_bshift(ioq);

			ioq_queue_put(ioq, qev);

			frags -= i;
			if (frags == 0)
				break;

			++qev;
			M0_ASSERT(qev - lio->si_qev < lio->si_nr);
		}
	}
	lio->si_nr = ++qev - lio->si_qev;
	/* The lock should be held until all 'qev's are pushed into queue and
	 * the lio->si_nr is correctly updated. When this lock is released,
	 * these 'qev's may be submitted.
	 */
	ioq_queue_unlock(ioq);

	if (result != 0) {
		M0_LOG(M0_ERROR, "Launch op=%d io=%p failed: rc=%d",
				 io->si_opcode, io, result);
		stob_linux_io_release(lio);
	} else
		ioq_queue_submit(ioq);

	return result;
}

static const struct m0_stob_io_op stob_linux_io_op = {
	.sio_launch  = stob_linux_io_launch,
	.sio_fini    = stob_linux_io_fini
};

/**
   Removes an element from the (non-empty) admission queue and returns it.
 */
static struct ioq_qev *ioq_queue_get(struct m0_stob_ioq *ioq)
{
	struct m0_queue_link *head;

	M0_ASSERT(!m0_queue_is_empty(&ioq->ioq_queue));
	M0_ASSERT(m0_mutex_is_locked(&ioq->ioq_lock));

	head = m0_queue_get(&ioq->ioq_queue);
	ioq->ioq_queued--;
	M0_ASSERT_EX(ioq->ioq_queued == m0_queue_length(&ioq->ioq_queue));
	return container_of(head, struct ioq_qev, iq_linkage);
}

/**
   Adds an element to the admission queue.
 */
static void ioq_queue_put(struct m0_stob_ioq *ioq,
			  struct ioq_qev *qev)
{
	M0_ASSERT(!m0_queue_link_is_in(&qev->iq_linkage));
	M0_ASSERT(m0_mutex_is_locked(&ioq->ioq_lock));
	// M0_ASSERT(qev->iq_io->si_obj->so_domain == &ioq->sdl_base);

	m0_queue_put(&ioq->ioq_queue, &qev->iq_linkage);
	ioq->ioq_queued++;
	M0_ASSERT_EX(ioq->ioq_queued == m0_queue_length(&ioq->ioq_queue));
}

static void ioq_queue_lock(struct m0_stob_ioq *ioq)
{
	m0_mutex_lock(&ioq->ioq_lock);
}

static void ioq_queue_unlock(struct m0_stob_ioq *ioq)
{
	m0_mutex_unlock(&ioq->ioq_lock);
}

/**
   Transfers fragments from the admission queue to the ring buffer in batches
   until the ring buffer is full.
 */
static void ioq_queue_submit(struct m0_stob_ioq *ioq)
{
	int got;
	int put;
	int avail;
	int i;

	struct ioq_qev  *qev[M0_STOB_IOQ_BATCH_IN_SIZE];
	struct iocb    *evin[M0_STOB_IOQ_BATCH_IN_SIZE];

	do {
		ioq_queue_lock(ioq);
		avail = m0_atomic64_get(&ioq->ioq_avail);
		got = min32(ioq->ioq_queued, min32(avail, ARRAY_SIZE(evin)));
		m0_atomic64_sub(&ioq->ioq_avail, got);
		for (i = 0; i < got; ++i) {
			qev[i] = ioq_queue_get(ioq);
			evin[i] = &qev[i]->iq_iocb;
		}
		ioq_queue_unlock(ioq);

		if (got > 0) {
			put = io_submit(ioq->ioq_ctx, got, evin);

			if (put < 0) {
				M0_STOB_FUNC_FAIL(LAD_IOQ_SUBMIT, put);
				put = 0;
			}

			ioq_queue_lock(ioq);
			for (i = put; i < got; ++i)
				ioq_queue_put(ioq, qev[i]);
			ioq_queue_unlock(ioq);

			if (got > put)
				m0_atomic64_add(&ioq->ioq_avail, got - put);
		}
	} while (got > 0);
}

/**
 * Registers AST callback (io_err_callback) to handle IO error.
 */
static void ioq_io_error(struct m0_stob_ioq *ioq, struct ioq_qev *qev)
{
	struct m0_stob_io    *io    = qev->iq_io;
	struct m0_stob_linux *lstob = m0_stob_linux_container(io->si_obj);
	struct m0_sm_ast     *ast   = &lstob->sl_ast;

	m0_mutex_lock(&ioq->ioq_lock);
	if (ast->sa_cb != NULL) {
		ast->sa_cb = &io_err_callback;
		/*
		 * Acquire ref to stop the stob being freed before AST
		 * execution.
		 */
		m0_stob_get(&lstob->sl_stob);
		m0_sm_ast_post(m0_locality_here()->lo_grp, &lstob->sl_ast);
	} else {
		M0_LOG(M0_WARN,
		       "Repeated IO failures on "FID_F"; not reporting to HA.",
		       FID_P(m0_stob_fid_get(&lstob->sl_stob)));
	}
	m0_mutex_unlock(&ioq->ioq_lock);
}

/**
   Handles AIO completion event from the ring buffer.

   When all fragments of a certain adieu request have completed, signals
   m0_stob_io::si_wait.
 */
static void ioq_complete(struct m0_stob_ioq *ioq, struct ioq_qev *qev,
			 long res, long res2)
{
	struct m0_stob_io    *io   = qev->iq_io;
	struct stob_linux_io *lio  = io->si_stob_private;
	struct iocb          *iocb = &qev->iq_iocb;
	uint32_t              done;

	M0_ASSERT(!m0_queue_link_is_in(&qev->iq_linkage));
	M0_ASSERT(io->si_state == SIS_BUSY);

	done = m0_atomic64_get(&lio->si_done);
	M0_ASSERT(done < lio->si_nr);

	/* short read. */
	M0_LOG(M0_DEBUG, "res=%lx nbytes=%lx", (unsigned long)res,
					(unsigned long)qev->iq_nbytes);
	if (io->si_opcode == SIO_READ && res >= 0 && res < qev->iq_nbytes) {
		/* fill the rest of the user buffer with zeroes. */
		const struct iovec *iov = iocb->u.v.vec;
		int i;

		for (i = 0; i < iocb->u.v.nr; ++i) {
			if (iov->iov_len < res)
				res -= iov->iov_len;
			else if (res == 0)
				memset(iov->iov_base, 0, iov->iov_len);
			else {
				memset(iov->iov_base + res, 0,
							iov->iov_len - res);
				res = 0;
			}
		}
		res = qev->iq_nbytes;
	}

	if (res > 0) {
		if ((res & m0_stob_ioq_bmask(ioq)) != 0) {
			M0_STOB_FUNC_FAIL(LAD_IOQ_COMPLETE, -EIO);
			res = -EIO;
			ioq_io_error(ioq, qev);
		} else
			m0_atomic64_add(&lio->si_bdone, res);
	}

	if (res < 0 && io->si_rc == 0)
		io->si_rc = res;
	M0_ASSERT(io->si_rc == 0);

	/*
	 * The position of this operation is critical:
	 * all threads must complete the above code until
	 * some of them finds here out that all frags are done.
	 */
	done = m0_atomic64_add_return(&lio->si_done, 1);

	if (done == lio->si_nr) {
		m0_bcount_t bdone = m0_atomic64_get(&lio->si_bdone);

		M0_LOG(M0_DEBUG, FID_F" nr=%d sz=%lx si_rc=%d",
		       FID_P(m0_stob_fid_get(io->si_obj)),
		       done, (unsigned long)bdone, (int)io->si_rc);
		io->si_count = bdone >> m0_stob_ioq_bshift(ioq);
		stob_linux_io_release(lio);
		io->si_state = SIS_IDLE;
		m0_chan_broadcast_lock(&io->si_wait);
	}
}

static const struct timespec ioq_timeout_default = {
	.tv_sec  = 1,
	.tv_nsec = 0
};

/**
   Linux adieu worker thread.

   Listens to the completion events from the ring buffer. Delivers completion
   events to the users. Moves fragments from the admission queue to the ring
   buffer.
 */
static void stob_ioq_thread(struct m0_stob_ioq *ioq)
{
	int got;
	int avail;
	int i;
	struct io_event evout[M0_STOB_IOQ_BATCH_OUT_SIZE];
	struct timespec ioq_timeout;

	while (!ioq->ioq_shutdown) {
		ioq_timeout = ioq_timeout_default;
		got = raw_io_getevents(ioq->ioq_ctx, 1, ARRAY_SIZE(evout),
				       evout, &ioq_timeout);
		M0_LOG(M0_DEBUG, "got=%d", got);
		if (got > 0) {
			avail = m0_atomic64_add_return(&ioq->ioq_avail, got);
			M0_ASSERT(avail <= M0_STOB_IOQ_RING_SIZE);
		}

		for (i = 0; i < got; ++i) {
			struct ioq_qev  *qev;
			struct io_event *iev;

			iev = &evout[i];
			qev = container_of(iev->obj, struct ioq_qev, iq_iocb);
			M0_ASSERT(!m0_queue_link_is_in(&qev->iq_linkage));
			ioq_complete(ioq, qev, iev->res, iev->res2);
		}
		if (got < 0 && got != -EINTR)
			M0_STOB_FUNC_FAIL(LAD_IOQ_THREAD, got);

		ioq_queue_submit(ioq);
	}
}

M0_INTERNAL int m0_stob_ioq_init(struct m0_stob_ioq *ioq)
{
	int                          result;
	int                          i;

	ioq->ioq_shutdown = false;
	ioq->ioq_ctx      = NULL;
	m0_atomic64_set(&ioq->ioq_avail, M0_STOB_IOQ_RING_SIZE);
	ioq->ioq_queued   = 0;

	m0_queue_init(&ioq->ioq_queue);
	m0_mutex_init(&ioq->ioq_lock);

	result = io_setup(M0_STOB_IOQ_RING_SIZE, &ioq->ioq_ctx);
	if (result == 0) {
		for (i = 0; i < ARRAY_SIZE(ioq->ioq_thread); ++i) {
			result = M0_THREAD_INIT(&ioq->ioq_thread[i],
						struct m0_stob_ioq *,
						NULL, &stob_ioq_thread, ioq,
						"ioq_thread%d", i);
			if (result != 0)
				break;
			m0_stob_ioq_directio_setup(ioq, false);
		}
	} else
		M0_STOB_FUNC_FAIL(LAD_DOM_IO_INIT, result);
	if (result != 0)
		m0_stob_ioq_fini(ioq);
	return result;
}

M0_INTERNAL void m0_stob_ioq_fini(struct m0_stob_ioq *ioq)
{
	int i;

	ioq->ioq_shutdown = true;
	for (i = 0; i < ARRAY_SIZE(ioq->ioq_thread); ++i) {
		if (ioq->ioq_thread[i].t_func != NULL)
			m0_thread_join(&ioq->ioq_thread[i]);
	}
	if (ioq->ioq_ctx != NULL)
		io_destroy(ioq->ioq_ctx);
	m0_queue_fini(&ioq->ioq_queue);
	m0_mutex_fini(&ioq->ioq_lock);
}

M0_INTERNAL uint32_t m0_stob_ioq_bshift(struct m0_stob_ioq *ioq)
{
	return ioq->ioq_use_directio ? STOB_IOQ_BSHIFT : 0;
}

M0_INTERNAL m0_bcount_t m0_stob_ioq_bsize(struct m0_stob_ioq *ioq)
{
	return ioq->ioq_use_directio ? STOB_IOQ_BSIZE : 0;
}

M0_INTERNAL m0_bcount_t m0_stob_ioq_bmask(struct m0_stob_ioq *ioq)
{
	return ioq->ioq_use_directio ? STOB_IOQ_BMASK : 0;
}

M0_INTERNAL bool m0_stob_ioq_directio(struct m0_stob_ioq *ioq)
{
	return ioq->ioq_use_directio;
}

M0_INTERNAL void m0_stob_ioq_directio_setup(struct m0_stob_ioq *ioq,
					    bool use_directio)
{
	ioq->ioq_use_directio = use_directio;
}

#undef M0_TRACE_SUBSYSTEM

/** @} end group stoblinux */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
