#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>

#include <lib/memory.h>
#include <lib/atomic.h>
#include <lib/assert.h>
#include <lib/queue.h>
#include <lib/arith.h>
#include <lib/thread.h>

#include "linux.h"
#include "linux_internal.h"

/**
   @addtogroup stoblinux

   <b>Linux stob adieu</b>

   adieu implementation for Linux stob is based on Linux specific asynchronous
   IO interfaces: io_{setup,destroy,submit,cancel,getevents}().

   @see http://www.kernel.org/doc/man-pages/online/pages/man2/io_setup.2.html

   @{
 */

struct ioq_qev {
	struct iocb           iq_iocb;
	struct c2_queue_link  iq_linkage;
	struct c2_stob_io    *iq_io;
};

struct linux_stob_io {
	uint64_t           si_nr;
	uint64_t           si_done;
	struct ioq_qev    *si_qev;
};

static struct ioq_qev *ioq_queue_get   (struct linux_domain *ldom);
static void            ioq_queue_put   (struct linux_domain *ldom, 
					struct ioq_qev *qev);
static void            ioq_queue_submit(struct linux_domain *ldom);
static void            ioq_queue_lock  (struct linux_domain *ldom);
static void            ioq_queue_unlock(struct linux_domain *ldom);

static const struct c2_stob_io_op linux_stob_io_op;

int linux_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io)
{
	struct linux_stob_io *lio;
	int                   result;

	C2_PRE(io->si_state == SIS_IDLE);

	C2_ALLOC_PTR(lio);
	if (lio != NULL) {
		io->si_stob_private = lio;
		io->si_op = &linux_stob_io_op;
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

static void linux_stob_io_fini(struct linux_stob_io *lio)
{
	c2_free(lio->si_qev);
	lio->si_qev = NULL;
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

	C2_PRE(io->si_obj->so_domain->sd_type == &linux_stob_type);

	/* prefix fragments execution mode is not yet supported */
	C2_ASSERT((io->si_flags & SIF_PREFIX) == 0);
	C2_PRE(c2_vec_count(&io->si_user.div_vec.ov_vec) > 0);

	c2_vec_cursor_init(&src, &io->si_user.div_vec.ov_vec);
	c2_vec_cursor_init(&dst, &io->si_stob.ov_vec);

	frags = 0;
	do {
		frag_size = min_t(c2_bcount_t, c2_vec_cursor_step(&src),
				  c2_vec_cursor_step(&dst));
		C2_ASSERT(frag_size > 0);
		frags++;
		eosrc = c2_vec_cursor_move(&src, frag_size);
		eodst = c2_vec_cursor_move(&dst, frag_size);
		C2_ASSERT(eosrc == eodst);
	} while (!eosrc);

	c2_vec_cursor_init(&src, &io->si_user.div_vec.ov_vec);
	c2_vec_cursor_init(&dst, &io->si_stob.ov_vec);

	lio->si_nr   = frags;
	lio->si_done = 0;
	C2_ALLOC_ARR(lio->si_qev, frags);

	if (lio->si_qev != NULL) {
		for (i = 0; i < frags; ++i) {
			void        *buf;
			c2_bindex_t  off;
			struct iocb *iocb;

			frag_size = min_t(c2_bcount_t, c2_vec_cursor_step(&src),
					  c2_vec_cursor_step(&dst));
			if (frag_size > (size_t)~0ULL) {
				result = -EOVERFLOW;
				break;
			}

			buf = io->si_user.div_vec.ov_buf[src.vc_seg] + 
				src.vc_offset;
			off = io->si_stob.ov_index[dst.vc_seg] + dst.vc_offset;

			iocb = &lio->si_qev[i].iq_iocb;
			memset(iocb, 0, sizeof *iocb);

			iocb->aio_fildes = lstob->sl_fd;
			iocb->u.c.buf    = buf;
			iocb->u.c.nbytes = frag_size;
			iocb->u.c.offset = off;

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

			c2_vec_cursor_move(&src, frag_size);
			c2_vec_cursor_move(&dst, frag_size);
			lio->si_qev[i].iq_io = io;
		}
		if (result == 0) {
			ioq_queue_lock(ldom);
			for (i = 0; i < frags; ++i)
				ioq_queue_put(ldom, &lio->si_qev[i]);
			ioq_queue_unlock(ldom);
			ioq_queue_submit(ldom);
		}
	} else
		result = -ENOMEM;

	if (result != 0)
		linux_stob_io_fini(lio);
	return result;
}

static void linux_stob_io_cancel(struct c2_stob_io *io)
{
}

static const struct c2_stob_io_op linux_stob_io_op = {
	.sio_launch  = linux_stob_io_launch,
	.sio_cancel  = linux_stob_io_cancel
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
				/* XXX log error */
				printf("io_submit: %i", put);
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

static void ioq_complete(struct linux_domain *ldom, struct ioq_qev *qev,
			 unsigned long res, unsigned long res2)
{
	struct c2_stob_io    *io;
	struct linux_stob_io *lio;
	struct linux_stob    *lstob;
	bool done;
	int  i;

	C2_ASSERT(!c2_queue_link_is_in(&qev->iq_linkage));
	C2_ASSERT(qev->iq_io->si_obj->so_domain == &ldom->sdl_base);

	io    = qev->iq_io;
	lio   = io->si_stob_private;
	lstob = stob2linux(io->si_obj);

	C2_ASSERT(io->si_state == SIS_BUSY);

	/*
	 * XXX per io lock can be used here.
	 */
	ioq_queue_lock(ldom);
	C2_ASSERT(lio->si_done < lio->si_nr);
	if (res > 0)
		qev->iq_io->si_count += res;
	else if (res < 0 && qev->iq_io->si_rc == 0)
		qev->iq_io->si_rc = res;
	++lio->si_done;
	done = lio->si_done == lio->si_nr;
	ioq_queue_unlock(ldom);

	if (done) {
		for (i = 0; i < lio->si_nr; ++i) {
			C2_ASSERT(!c2_queue_link_is_in
				  (&lio->si_qev[i].iq_linkage));
		}
		io->si_state = SIS_IDLE;
		c2_chan_broadcast(&io->si_wait);
	}
}

const static struct timespec ioq_timeout_default = {
	.tv_sec  = 1,
	.tv_nsec = 0
};

static void ioq_thread(struct linux_domain *ldom)
{
	int got;
	int i;
	struct io_event evout[IOQ_BATCH_OUT_SIZE];
	struct timespec ioq_timeout;

	while (!ldom->ioq_shutdown) {
		ioq_timeout = ioq_timeout_default;
		got = io_getevents(ldom->ioq_ctx, 1, ARRAY_SIZE(evout), 
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
		if (got < 0) {
			/* XXX log error */
			printf("io_getevents: %i\n", got);
		}

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
						NULL, &ioq_thread, ldom);
			if (result != 0)
				break;
		}
	}
	if (result != 0)
		linux_domain_io_fini(dom);
	return result;
}

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
