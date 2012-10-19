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

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/atomic.h"
#include "lib/assert.h"
#include "lib/queue.h"
#include "lib/arith.h"
#include "lib/thread.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_IOSERVICE
#include "lib/trace.h"
#include "addb/addb.h"

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
	/** Linkage to a per-domain admission queue
	    (linux_domain::ioq_queue). */
	struct c2_queue_link  iq_linkage;
	struct c2_stob_io    *iq_io;
};

/**
   Linux adieu specific part of generic c2_stob_io structure.
 */
struct linux_stob_io {
	/** Number of fragments in this adieu request. */
	uint64_t           si_nr;
	/** Number of completed fragments. */
	uint64_t           si_done;
	/** Array of fragments. */
	struct ioq_qev    *si_qev;
	/** Mutex serialising processing of completion events for this IO. */
	struct c2_mutex    si_endlock;
};

static struct ioq_qev *ioq_queue_get   (struct linux_domain *ldom);
static void            ioq_queue_put   (struct linux_domain *ldom,
					struct ioq_qev *qev);
static void            ioq_queue_submit(struct linux_domain *ldom);
static void            ioq_queue_lock  (struct linux_domain *ldom);
static void            ioq_queue_unlock(struct linux_domain *ldom);

static const struct c2_stob_io_op linux_stob_io_op;

static const struct c2_addb_loc adieu_addb_loc = {
	.al_name = "linux-adieu"
};

enum {
	/*
	 * Alignment for direct-IO.
	 *
	 * According to open(2) manpage: "Under Linux 2.6, alignment to
	 * 512-byte boundaries suffices".
	 *
	 * Don't use these constants directly, use LINUX_DOM_XXX macros
	 * instead (see below), because they take into account
	 * linux_domain.use_directio flag which is set in runtime.
	 */
	LINUX_BSHIFT = 12, /* pow(2, 12) == 4096 */
	LINUX_BSIZE  = 1 << LINUX_BSHIFT,
	LINUX_BMASK  = LINUX_BSIZE - 1
};

#define LINUX_DOM_BSHIFT(ldom) ({				\
	struct linux_domain *_ldom = (ldom);			\
	_ldom->use_directio ? LINUX_BSHIFT : 0 ;		\
})

#define LINUX_DOM_BSIZE(ldom) ({				\
	struct linux_domain *_ldom = (ldom);			\
	_ldom->use_directio ? LINUX_BSIZE : 0 ;			\
})

#define LINUX_DOM_BMASK(ldom) ({				\
	struct linux_domain *_ldom = (ldom);			\
	_ldom->use_directio ? LINUX_BMASK : 0 ;		\
})

#define ADDB_GLOBAL_ADD(name, rc)					\
C2_ADDB_ADD(&adieu_addb_ctx, &adieu_addb_loc, c2_addb_func_fail, (name), (rc))

#define ADDB_ADD(obj, ev, ...)	\
C2_ADDB_ADD(&(obj)->so_addb, &adieu_addb_loc, ev , ## __VA_ARGS__)

#define ADDB_CALL(obj, name, rc)	\
C2_ADDB_ADD(&(obj)->so_addb, &adieu_addb_loc, c2_addb_func_fail, (name), (rc))

int linux_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io)
{
	struct linux_stob_io *lio;
	int                   result;

	C2_PRE(io->si_state == SIS_IDLE);

	C2_ALLOC_PTR(lio);
	if (lio != NULL) {
		io->si_stob_private = lio;
		io->si_op = &linux_stob_io_op;
		c2_mutex_init(&lio->si_endlock);
		result = 0;
	} else {
		ADDB_ADD(stob, c2_addb_oom);
		result = -ENOMEM;
	}
	return result;
}

static void linux_stob_io_release(struct linux_stob_io *lio)
{
	c2_free(lio->si_qev);
	lio->si_qev = NULL;
}

static void linux_stob_io_fini(struct c2_stob_io *io)
{
	struct linux_stob_io *lio = io->si_stob_private;

	linux_stob_io_release(lio);
	c2_mutex_fini(&lio->si_endlock);
	c2_free(lio);
}

/**
   Launch asynchronous IO.

   @li calculate how many fragments IO operation has;

   @li allocate ioq_qev array and fill it with fragments;

   @li queue all fragments and submit as many as possible;
 */
static int linux_stob_io_launch(struct c2_stob_io *io)
{
	struct linux_stob    *lstob  = stob2linux(io->si_obj);
	struct linux_domain  *ldom   = domain2linux(io->si_obj->so_domain);
	struct linux_stob_io *lio    = io->si_stob_private;
	struct c2_vec_cursor  src;
	struct c2_vec_cursor  dst;
	uint32_t              frags;
	c2_bcount_t           frag_size;
	int                   result = 0;
	int                   i;
	bool                  eosrc;
	bool                  eodst;

	C2_PRE(io->si_obj->so_domain->sd_type == &c2_linux_stob_type);

	/* prefix fragments execution mode is not yet supported */
	C2_ASSERT((io->si_flags & SIF_PREFIX) == 0);
	C2_PRE(c2_vec_count(&io->si_user.ov_vec) > 0);

	c2_vec_cursor_init(&src, &io->si_user.ov_vec);
	c2_vec_cursor_init(&dst, &io->si_stob.iv_vec);

	frags = 0;
	do {
		frag_size = min_check(c2_vec_cursor_step(&src),
				      c2_vec_cursor_step(&dst));
		C2_ASSERT(frag_size > 0);
		frags++;
		eosrc = c2_vec_cursor_move(&src, frag_size);
		eodst = c2_vec_cursor_move(&dst, frag_size);
		C2_ASSERT(eosrc == eodst);
	} while (!eosrc);

	c2_vec_cursor_init(&src, &io->si_user.ov_vec);
	c2_vec_cursor_init(&dst, &io->si_stob.iv_vec);

	lio->si_nr   = frags;
	lio->si_done = 0;
	C2_ALLOC_ARR(lio->si_qev, frags);

	if (lio->si_qev != NULL) {
		for (i = 0; i < frags; ++i) {
			void        *buf;
			c2_bindex_t  off;
			struct iocb *iocb;

			frag_size = min_check(c2_vec_cursor_step(&src),
					      c2_vec_cursor_step(&dst));
			if (frag_size > (size_t)~0ULL) {
				ADDB_CALL(io->si_obj, "frag_overflow",
					  frag_size);
				result = -EOVERFLOW;
				break;
			}

			buf = io->si_user.ov_buf[src.vc_seg] +
				src.vc_offset;
			off = io->si_stob.iv_index[dst.vc_seg] + dst.vc_offset;

			iocb = &lio->si_qev[i].iq_iocb;
			C2_SET0(iocb);

			iocb->aio_fildes = lstob->sl_fd;
			iocb->u.c.buf    = c2_stob_addr_open(buf,
						LINUX_DOM_BSHIFT(ldom));
			iocb->u.c.nbytes = frag_size << LINUX_DOM_BSHIFT(ldom);
			iocb->u.c.offset = off       << LINUX_DOM_BSHIFT(ldom);

			switch (io->si_opcode) {
			case SIO_READ:
				iocb->aio_lio_opcode = IO_CMD_PREAD;
				break;
			case SIO_WRITE:
				iocb->aio_lio_opcode = IO_CMD_PWRITE;
				break;
			default:
				C2_ASSERT(0);
			}
			C2_LOG(C2_DEBUG, "frag=%d op=%d sz=%d, off=%d",
				i, io->si_opcode, (int)frag_size, (int)off);

			c2_vec_cursor_move(&src, frag_size);
			c2_vec_cursor_move(&dst, frag_size);
			lio->si_qev[i].iq_io = io;
			c2_queue_link_init(&lio->si_qev[i].iq_linkage);
		}
		if (result == 0) {
			ioq_queue_lock(ldom);
			for (i = 0; i < frags; ++i)
				ioq_queue_put(ldom, &lio->si_qev[i]);
			ioq_queue_unlock(ldom);
			ioq_queue_submit(ldom);
		}
	} else {
		ADDB_ADD(io->si_obj, c2_addb_oom);
		result = -ENOMEM;
	}

	if (result != 0)
		linux_stob_io_release(lio);
	return result;
}

static const struct c2_stob_io_op linux_stob_io_op = {
	.sio_launch  = linux_stob_io_launch,
	.sio_fini    = linux_stob_io_fini
};

/**
   An implementation of c2_stob_op::sop_lock() method.
 */
void linux_stob_io_lock(struct c2_stob *stob)
{
}

/**
   An implementation of c2_stob_op::sop_unlock() method.
 */
void linux_stob_io_unlock(struct c2_stob *stob)
{
}

/**
   An implementation of c2_stob_op::sop_is_locked() method.
 */
bool linux_stob_io_is_locked(const struct c2_stob *stob)
{
	return true;
}

/**
   An implementation of c2_stob_op::sop_block_shift() method.
 */
uint32_t linux_stob_block_shift(const struct c2_stob *stob)
{
	struct linux_domain *ldom;

	ldom  = domain2linux(stob->so_domain);
	return LINUX_DOM_BSHIFT(ldom);
}

/**
   An implementation of c2_stob_domain_op::sdo_block_shift() method.
 */
uint32_t linux_stob_domain_block_shift(struct c2_stob_domain *sdomain)
{
	struct linux_domain *ldom;

	ldom  = domain2linux(sdomain);
	return LINUX_DOM_BSHIFT(ldom);
}

/**
   Removes an element from the (non-empty) admission queue and returns it.
 */
static struct ioq_qev *ioq_queue_get(struct linux_domain *ldom)
{
	struct c2_queue_link *head;

	C2_ASSERT(!c2_queue_is_empty(&ldom->ioq_queue));
	C2_ASSERT(c2_mutex_is_locked(&ldom->ioq_lock));

	head = c2_queue_get(&ldom->ioq_queue);
	ldom->ioq_queued--;
	C2_ASSERT(ldom->ioq_queued == c2_queue_length(&ldom->ioq_queue));
	return container_of(head, struct ioq_qev, iq_linkage);
}

/**
   Adds an element to the admission queue.
 */
static void ioq_queue_put(struct linux_domain *ldom,
			  struct ioq_qev *qev)
{
	C2_ASSERT(!c2_queue_link_is_in(&qev->iq_linkage));
	C2_ASSERT(c2_mutex_is_locked(&ldom->ioq_lock));
	C2_ASSERT(qev->iq_io->si_obj->so_domain == &ldom->sdl_base);

	c2_queue_put(&ldom->ioq_queue, &qev->iq_linkage);
	ldom->ioq_queued++;
	C2_ASSERT(ldom->ioq_queued == c2_queue_length(&ldom->ioq_queue));
}

static void ioq_queue_lock(struct linux_domain *ldom)
{
	c2_mutex_lock(&ldom->ioq_lock);
}

static void ioq_queue_unlock(struct linux_domain *ldom)
{
	c2_mutex_unlock(&ldom->ioq_lock);
}

/**
   Transfers fragments from the admission queue to the ring buffer in batches
   until the ring buffer is full.
 */
static void ioq_queue_submit(struct linux_domain *ldom)
{
	int got;
	int put;
	int i;

	struct ioq_qev  *qev[IOQ_BATCH_IN_SIZE];
	struct iocb    *evin[IOQ_BATCH_IN_SIZE];

	do {
		ioq_queue_lock(ldom);
		got = min32(ldom->ioq_queued,
			    min32(ldom->ioq_avail, ARRAY_SIZE(evin)));
		for (i = 0; i < got; ++i) {
			qev[i] = ioq_queue_get(ldom);
			evin[i] = &qev[i]->iq_iocb;
		}
		ldom->ioq_avail -= got;
		ioq_queue_unlock(ldom);

		if (got > 0) {
			put = io_submit(ldom->ioq_ctx, got, evin);

			ioq_queue_lock(ldom);
			if (put < 0) {
				ADDB_GLOBAL_ADD("io_submit", put);
				put = 0;
			}
			for (i = put; i < got; ++i)
				ioq_queue_put(ldom, qev[i]);
			ldom->ioq_avail += got - put;
			C2_ASSERT(ldom->ioq_avail <= IOQ_RING_SIZE);
			ioq_queue_unlock(ldom);
		}
	} while (got > 0);
}

/**
   Handles AIO completion event from the ring buffer.

   When all fragments of a certain adieu request have completed, signals
   c2_stob_io::si_wait.
 */
static void ioq_complete(struct linux_domain *ldom, struct ioq_qev *qev,
			 long res, long res2)
{
	struct c2_stob_io    *io;
	struct linux_stob_io *lio;
	bool done;

	C2_ASSERT(!c2_queue_link_is_in(&qev->iq_linkage));
	C2_ASSERT(qev->iq_io->si_obj->so_domain == &ldom->sdl_base);

	io  = qev->iq_io;
	lio = io->si_stob_private;

	C2_ASSERT(io->si_state == SIS_BUSY);

	c2_mutex_lock(&lio->si_endlock);
	C2_ASSERT(lio->si_done < lio->si_nr);
	if (res > 0) {
		if ((res & LINUX_DOM_BMASK(ldom)) != 0) {
			ADDB_CALL(io->si_obj, "partial transfer", res);
			res = -EIO;
		} else
			qev->iq_io->si_count += res >> LINUX_DOM_BSHIFT(ldom);
	}

	if (res < 0 && qev->iq_io->si_rc == 0)
		qev->iq_io->si_rc = res;
	++lio->si_done;
	C2_LOG(C2_DEBUG, "done=%d res=%d si_rc=%d", (int)lio->si_done, (int)res,
	       (int)qev->iq_io->si_rc);
	done = lio->si_done == lio->si_nr;
	c2_mutex_unlock(&lio->si_endlock);

	if (done) {
		C2_ASSERT(c2_forall(i, lio->si_nr,
		          !c2_queue_link_is_in(&lio->si_qev[i].iq_linkage)));
		linux_stob_io_release(lio);
		io->si_state = SIS_IDLE;
		c2_chan_broadcast(&io->si_wait);
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
	int i;
	struct io_event evout[IOQ_BATCH_OUT_SIZE];
	struct timespec ioq_timeout;

	while (!ldom->ioq_shutdown) {
		ioq_timeout = ioq_timeout_default;
		got = raw_io_getevents(ldom->ioq_ctx, 1, ARRAY_SIZE(evout),
				       evout, &ioq_timeout);
		ioq_queue_lock(ldom);
		if (got > 0)
			ldom->ioq_avail += got;
		C2_ASSERT(ldom->ioq_avail <= IOQ_RING_SIZE);
		ioq_queue_unlock(ldom);

		for (i = 0; i < got; ++i) {
			struct ioq_qev  *qev;
			struct io_event *iev;

			iev = &evout[i];
			qev = container_of(iev->obj, struct ioq_qev, iq_iocb);
			C2_ASSERT(!c2_queue_link_is_in(&qev->iq_linkage));
			ioq_complete(ldom, qev, iev->res, iev->res2);
		}
		if (got < 0 && got != -EINTR)
			ADDB_GLOBAL_ADD("io_getevents", got);

		ioq_queue_submit(ldom);
	}
}

void linux_domain_io_fini(struct c2_stob_domain *dom)
{
	int i;
	struct linux_domain *ldom;

	ldom = domain2linux(dom);

	ldom->ioq_shutdown = true;
	for (i = 0; i < ARRAY_SIZE(ldom->ioq); ++i) {
		if (ldom->ioq[i].t_func != NULL)
			c2_thread_join(&ldom->ioq[i]);
	}
	if (ldom->ioq_ctx != NULL)
		io_destroy(ldom->ioq_ctx);
	c2_queue_fini(&ldom->ioq_queue);
	c2_mutex_fini(&ldom->ioq_lock);
}

int linux_domain_io_init(struct c2_stob_domain *dom)
{
	int                          result;
	int                          i;
	struct linux_domain *ldom;

	ldom = domain2linux(dom);
	ldom->ioq_shutdown = false;
	ldom->ioq_ctx      = NULL;
	ldom->ioq_avail    = IOQ_RING_SIZE;
	ldom->ioq_queued   = 0;

	memset(ldom->ioq, 0, sizeof ldom->ioq);
	c2_queue_init(&ldom->ioq_queue);
	c2_mutex_init(&ldom->ioq_lock);

	result = io_setup(IOQ_RING_SIZE, &ldom->ioq_ctx);
	if (result == 0) {
		for (i = 0; i < ARRAY_SIZE(ldom->ioq); ++i) {
			result = C2_THREAD_INIT(&ldom->ioq[i],
						struct linux_domain *,
						NULL, &ioq_thread, ldom,
						"ioq_thread%d", i);
			if (result != 0)
				break;
		}
	} else
		ADDB_GLOBAL_ADD("io_setup", result);
	if (result != 0)
		linux_domain_io_fini(dom);
	return result;
}

#undef C2_TRACE_SUBSYSTEM
#undef ADDB_GLOBAL_ADD
#undef ADDB_ADD
#undef ADDB_CALL

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
