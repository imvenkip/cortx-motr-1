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

#include <limits.h>
#include <sys/uio.h>

#include "lib/misc.h"   /* M0_SET0 */
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/atomic.h"
#include "lib/assert.h"
#include "lib/queue.h"
#include "lib/arith.h"
#include "lib/thread.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_STOB
#include "lib/trace.h"
#include "addb/addb.h"

#include "stob/stob_addb.h"
#include "linux.h"
#include "linux_internal.h"
#include "linux_getevents.h"

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
   linux_domain::ioq_queue) where it is held until there is enough space in the
   AIO ring buffer (linux_domain::ioq_ctx). Placing a fragment into the ring
   buffer (ioq_queue_submit()) means that kernel AIO is launched for it. When IO
   completes, the kernel delivers an IO completion event via the ring buffer.

   A number (IOQ_NR_THREADS by default) of worker adieu threads is created for
   each storage object domain. These threads are implementing admission control
   and completion notification, they

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
   linux_domain::ioq_mutex.

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

/**
   AIO fragment.

   A ioq_qev is created for each fragment of original adieu request (see
   linux_stob_io_launch()).
 */
struct ioq_qev {
	struct iocb           iq_iocb;
	m0_bcount_t           iq_nbytes;
	/** Linkage to a per-domain admission queue
	    (linux_domain::ioq_queue). */
	struct m0_queue_link  iq_linkage;
	struct m0_stob_io    *iq_io;
};

/**
   Linux adieu specific part of generic m0_stob_io structure.
 */
struct linux_stob_io {
	/** Number of fragments in this adieu request. */
	uint32_t           si_nr;
	/** Number of completed fragments. */
	struct m0_atomic64 si_done;
	/** Number of completed bytes. */
	struct m0_atomic64 si_bdone;
	/** Array of fragments. */
	struct ioq_qev    *si_qev;
};

static struct ioq_qev *ioq_queue_get   (struct linux_domain *ldom);
static void            ioq_queue_put   (struct linux_domain *ldom,
					struct ioq_qev *qev);
static void            ioq_queue_submit(struct linux_domain *ldom);
static void            ioq_queue_lock  (struct linux_domain *ldom);
static void            ioq_queue_unlock(struct linux_domain *ldom);

static const struct m0_stob_io_op linux_stob_io_op;

enum {
	/*
	 * Alignment for direct-IO.
	 *
	 * According to open(2) manpage: "Under Linux 2.6, alignment to
	 * 512-byte boundaries suffices".
	 *
	 * Don't use these constants directly, use LINUX_DOM_XXX macros
	 * instead (see below), because they take into account
	 * linux_domain.sdl_use_directio flag which is set in runtime.
	 */
	LINUX_BSHIFT = 12, /* pow(2, 12) == 4096 */
	LINUX_BSIZE  = 1 << LINUX_BSHIFT,
	LINUX_BMASK  = LINUX_BSIZE - 1
};

#define LINUX_DOM_BSHIFT(ldom) ({				\
	struct linux_domain *_ldom = (ldom);			\
	_ldom->sdl_use_directio ? LINUX_BSHIFT : 0 ;		\
})

#define LINUX_DOM_BSIZE(ldom) ({				\
	struct linux_domain *_ldom = (ldom);			\
	_ldom->sdl_use_directio ? LINUX_BSIZE : 0 ;		\
})

#define LINUX_DOM_BMASK(ldom) ({				\
	struct linux_domain *_ldom = (ldom);			\
	_ldom->sdl_use_directio ? LINUX_BMASK : 0 ;		\
})

M0_INTERNAL int linux_stob_io_init(struct m0_stob *stob, struct m0_stob_io *io)
{
	struct linux_stob_io *lio;
	int                   result;

	M0_PRE(io->si_state == SIS_IDLE);

	M0_ALLOC_PTR(lio);
	if (lio != NULL) {
		io->si_stob_private = lio;
		io->si_op = &linux_stob_io_op;
		result = 0;
	} else {
		M0_STOB_OOM(LAD_STOB_IO_INIT);
		result = -ENOMEM;
	}
	return result;
}

static void linux_stob_io_release(struct linux_stob_io *lio)
{
	if (lio->si_qev != NULL)
		m0_free(lio->si_qev->iq_iocb.u.c.buf);
	m0_free(lio->si_qev);
	lio->si_qev = NULL;
}

static void linux_stob_io_fini(struct m0_stob_io *io)
{
	struct linux_stob_io *lio = io->si_stob_private;

	linux_stob_io_release(lio);
	m0_free(lio);
}

/**
   Launch asynchronous IO.

   @li calculate how many fragments IO operation has;

   @li allocate ioq_qev array and fill it with fragments;

   @li queue all fragments and submit as many as possible;
 */
static int linux_stob_io_launch(struct m0_stob_io *io)
{
	struct linux_stob    *lstob  = stob2linux(io->si_obj);
	struct linux_domain  *ldom   = domain2linux(io->si_obj->so_domain);
	struct linux_stob_io *lio    = io->si_stob_private;
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

	M0_PRE(io->si_obj->so_domain->sd_type == &m0_linux_stob_type);

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
	M0_LOG(M0_DEBUG, "chunks=%d frags=%d si_nr=%d", chunks, frags, lio->si_nr);
	m0_atomic64_set(&lio->si_done, 0);
	m0_atomic64_set(&lio->si_bdone, 0);
	M0_ALLOC_ARR(lio->si_qev, lio->si_nr);
	M0_ALLOC_ARR(iov, frags);
	qev = lio->si_qev;
	if (qev == NULL || iov == NULL) {
		M0_STOB_OOM(LAD_STOB_IO_LAUNCH_2);
		result = -ENOMEM;
	}

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
		iocb->u.v.offset = off << LINUX_DOM_BSHIFT(ldom);

		switch (io->si_opcode) {
		case SIO_READ:
			iocb->aio_lio_opcode = IO_CMD_PREADV;
			break;
		case SIO_WRITE:
			iocb->aio_lio_opcode = IO_CMD_PWRITEV;
			break;
		default:
			M0_ASSERT(0);
		}

		for (i = 0; i < iocb->u.v.nr; ++i) {
			void        *buf;
			m0_bindex_t  off;

			buf = io->si_user.ov_buf[src.vc_seg] + src.vc_offset;
			off = io->si_stob.iv_index[dst.vc_seg] + dst.vc_offset;

			M0_LOG(M0_DEBUG, "p_off=%lx off=%lx sz=%lx",
				(unsigned long)prev_off, (unsigned long)off,
				(unsigned long)frag_size);
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
						LINUX_DOM_BSHIFT(ldom));
			iov->iov_len  = frag_size << LINUX_DOM_BSHIFT(ldom);
			chunk_size += frag_size;

			m0_vec_cursor_move(&src, frag_size);
			m0_vec_cursor_move(&dst, frag_size);
			++iov;
		}
		M0_LOG(M0_DEBUG, "["U128X_F"] %2d: frags=%d op=%d off=%lx sz=%lx",
		       U128_P(&io->si_obj->so_id.si_bits),
		       (int)(qev - lio->si_qev), i, io->si_opcode,
		       (unsigned long)off, (unsigned long)chunk_size);
		if (result == 0) {
			iocb->u.v.nr = i;
			qev->iq_nbytes = chunk_size << LINUX_DOM_BSHIFT(ldom);

			ioq_queue_lock(ldom);
			ioq_queue_put(ldom, qev);
			ioq_queue_unlock(ldom);

			frags -= i;
			if (frags == 0)
				break;

			++qev;
			M0_ASSERT(qev - lio->si_qev < lio->si_nr);
		}
	}
	lio->si_nr = ++qev - lio->si_qev;

	if (result != 0)
		linux_stob_io_release(lio);
	else
		ioq_queue_submit(ldom);

	return result;
}

static const struct m0_stob_io_op linux_stob_io_op = {
	.sio_launch  = linux_stob_io_launch,
	.sio_fini    = linux_stob_io_fini
};

/**
   An implementation of m0_stob_op::sop_block_shift() method.
 */
M0_INTERNAL uint32_t linux_stob_block_shift(const struct m0_stob *stob)
{
	struct linux_domain *ldom;

	ldom  = domain2linux(stob->so_domain);
	return LINUX_DOM_BSHIFT(ldom);
}

/**
   Removes an element from the (non-empty) admission queue and returns it.
 */
static struct ioq_qev *ioq_queue_get(struct linux_domain *ldom)
{
	struct m0_queue_link *head;

	M0_ASSERT(!m0_queue_is_empty(&ldom->ioq_queue));
	M0_ASSERT(m0_mutex_is_locked(&ldom->ioq_lock));

	head = m0_queue_get(&ldom->ioq_queue);
	ldom->ioq_queued--;
	M0_ASSERT_EX(ldom->ioq_queued == m0_queue_length(&ldom->ioq_queue));
	return container_of(head, struct ioq_qev, iq_linkage);
}

/**
   Adds an element to the admission queue.
 */
static void ioq_queue_put(struct linux_domain *ldom,
			  struct ioq_qev *qev)
{
	M0_ASSERT(!m0_queue_link_is_in(&qev->iq_linkage));
	M0_ASSERT(m0_mutex_is_locked(&ldom->ioq_lock));
	M0_ASSERT(qev->iq_io->si_obj->so_domain == &ldom->sdl_base);

	m0_queue_put(&ldom->ioq_queue, &qev->iq_linkage);
	ldom->ioq_queued++;
	M0_ASSERT_EX(ldom->ioq_queued == m0_queue_length(&ldom->ioq_queue));
}

static void ioq_queue_lock(struct linux_domain *ldom)
{
	m0_mutex_lock(&ldom->ioq_lock);
}

static void ioq_queue_unlock(struct linux_domain *ldom)
{
	m0_mutex_unlock(&ldom->ioq_lock);
}

/**
   Transfers fragments from the admission queue to the ring buffer in batches
   until the ring buffer is full.
 */
static void ioq_queue_submit(struct linux_domain *ldom)
{
	int got;
	int put;
	int avail;
	int i;

	struct ioq_qev  *qev[IOQ_BATCH_IN_SIZE];
	struct iocb    *evin[IOQ_BATCH_IN_SIZE];

	do {
		avail = m0_atomic64_get(&ldom->ioq_avail);

		ioq_queue_lock(ldom);
		got = min32(ldom->ioq_queued, min32(avail, ARRAY_SIZE(evin)));
		for (i = 0; i < got; ++i) {
			qev[i] = ioq_queue_get(ldom);
			evin[i] = &qev[i]->iq_iocb;
		}
		ioq_queue_unlock(ldom);

		if (got > 0) {
			m0_atomic64_sub(&ldom->ioq_avail, got);

			put = io_submit(ldom->ioq_ctx, got, evin);

			if (put < 0) {
				M0_STOB_FUNC_FAIL(LAD_IOQ_SUBMIT, put);
				put = 0;
			}

			ioq_queue_lock(ldom);
			for (i = put; i < got; ++i)
				ioq_queue_put(ldom, qev[i]);
			ioq_queue_unlock(ldom);

			if (got > put)
				m0_atomic64_add(&ldom->ioq_avail, got - put);
		}
	} while (got > 0);
}

/**
   Handles AIO completion event from the ring buffer.

   When all fragments of a certain adieu request have completed, signals
   m0_stob_io::si_wait.
 */
static void ioq_complete(struct linux_domain *ldom, struct ioq_qev *qev,
			 long res, long res2)
{
	struct m0_stob_io    *io   = qev->iq_io;
	struct linux_stob_io *lio  = io->si_stob_private;
	struct iocb          *iocb = &qev->iq_iocb;
	uint32_t              done;

	M0_ASSERT(!m0_queue_link_is_in(&qev->iq_linkage));
	M0_ASSERT(io->si_obj->so_domain == &ldom->sdl_base);
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
		if ((res & LINUX_DOM_BMASK(ldom)) != 0) {
			M0_STOB_FUNC_FAIL(LAD_IOQ_COMPLETE, -EIO);
			res = -EIO;
		} else
			m0_atomic64_add(&lio->si_bdone, res);
	}

	if (res < 0 && io->si_rc == 0)
		io->si_rc = res;

	/*
	 * The position of this operation is critical:
	 * all threads must complete the above code until
	 * some of them finds here out that all frags are done.
	 */
	done = m0_atomic64_add_return(&lio->si_done, 1);

	if (done == lio->si_nr) {
		m0_bcount_t bdone = m0_atomic64_get(&lio->si_bdone);

		M0_LOG(M0_DEBUG, "["U128X_F"] nr=%d sz=%lx si_rc=%d",
		       U128_P(&io->si_obj->so_id.si_bits),
		       done, (unsigned long)bdone, (int)io->si_rc);
		M0_ASSERT(m0_forall(i, lio->si_nr,
		          !m0_queue_link_is_in(&lio->si_qev[i].iq_linkage)));
		io->si_count = bdone >> LINUX_DOM_BSHIFT(ldom);
		linux_stob_io_release(lio);
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
static void ioq_thread(struct linux_domain *ldom)
{
	int got;
	int avail;
	int i;
	struct io_event evout[IOQ_BATCH_OUT_SIZE];
	struct timespec ioq_timeout;

	while (!ldom->ioq_shutdown) {
		ioq_timeout = ioq_timeout_default;
		got = raw_io_getevents(ldom->ioq_ctx, 1, ARRAY_SIZE(evout),
				       evout, &ioq_timeout);
		M0_LOG(M0_DEBUG, "got=%d", got);
		if (got > 0) {
			avail = m0_atomic64_add_return(&ldom->ioq_avail, got);
			M0_ASSERT(avail <= IOQ_RING_SIZE);
		}

		for (i = 0; i < got; ++i) {
			struct ioq_qev  *qev;
			struct io_event *iev;

			iev = &evout[i];
			qev = container_of(iev->obj, struct ioq_qev, iq_iocb);
			M0_ASSERT(!m0_queue_link_is_in(&qev->iq_linkage));
			ioq_complete(ldom, qev, iev->res, iev->res2);
		}
		if (got < 0 && got != -EINTR)
			M0_STOB_FUNC_FAIL(LAD_IOQ_THREAD, got);

		ioq_queue_submit(ldom);
	}
}

M0_INTERNAL void linux_domain_io_fini(struct m0_stob_domain *dom)
{
	int i;
	struct linux_domain *ldom;

	ldom = domain2linux(dom);

	ldom->ioq_shutdown = true;
	for (i = 0; i < ARRAY_SIZE(ldom->ioq); ++i) {
		if (ldom->ioq[i].t_func != NULL)
			m0_thread_join(&ldom->ioq[i]);
	}
	if (ldom->ioq_ctx != NULL)
		io_destroy(ldom->ioq_ctx);
	m0_queue_fini(&ldom->ioq_queue);
	m0_mutex_fini(&ldom->ioq_lock);
}

M0_INTERNAL int linux_domain_io_init(struct m0_stob_domain *dom)
{
	int                          result;
	int                          i;
	struct linux_domain *ldom;

	ldom = domain2linux(dom);
	ldom->ioq_shutdown = false;
	ldom->ioq_ctx      = NULL;
	m0_atomic64_set(&ldom->ioq_avail, IOQ_RING_SIZE);
	ldom->ioq_queued   = 0;

	memset(ldom->ioq, 0, sizeof ldom->ioq);
	m0_queue_init(&ldom->ioq_queue);
	m0_mutex_init(&ldom->ioq_lock);

	result = io_setup(IOQ_RING_SIZE, &ldom->ioq_ctx);
	if (result == 0) {
		for (i = 0; i < ARRAY_SIZE(ldom->ioq); ++i) {
			result = M0_THREAD_INIT(&ldom->ioq[i],
						struct linux_domain *,
						NULL, &ioq_thread, ldom,
						"ioq_thread%d", i);
			if (result != 0)
				break;
		}
	} else
		M0_STOB_FUNC_FAIL(LAD_DOM_IO_INIT, result);
	if (result != 0)
		linux_domain_io_fini(dom);
	return result;
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
