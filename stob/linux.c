#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <libaio.h>
#include <errno.h>

#include "lib/memory.h"
#include "lib/atomic.h"
#include "lib/assert.h"
#include "lib/queue.h"
#include "lib/arith.h"
#include "lib/thread.h"
#include "linux.h"

/**
   @addtogroup stoblinux

   <b>Linux stob adieu</b>

   adieu implementation for Linux stob is based on Linux specific asynchronous
   IO interfaces: io_{setup,destroy,submit,cancel,getevents}().

   @see http://www.kernel.org/doc/man-pages/online/pages/man2/io_setup.2.html

   @{
 */

struct linux_stob {
	struct c2_stob  sl_stob;
	int             sl_fd;
	const char     *sl_path;
};

struct ioq_qev {
	struct iocb           iq_iocb;
	struct c2_queue_link  iq_linkage;
	struct c2_stob_io    *iq_io;
};

struct linux_stob_io {
	uint64_t           si_nr;
	struct c2_atomic64 si_done;
	struct ioq_qev    *si_qev;
};

static struct ioq_qev *ioq_queue_get   (void);
static void            ioq_queue_put   (struct ioq_qev *qev);
static void            ioq_queue_submit(void);
static void            ioq_queue_lock  (void);
static void            ioq_queue_unlock(void);

static const struct c2_stob_type linux_stob;

static struct linux_stob *stob2linux(struct c2_stob *stob)
{
	return container_of(stob, struct linux_stob, sl_stob);
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
	struct linux_stob_io *lio    = io->si_stob_private;
	struct c2_vec_cursor  src;
	struct c2_vec_cursor  dst;
	uint32_t              frags;
	c2_bcount_t           frag_size;
	int                   result = 0;
	int                   i;
	bool                  eosrc;
	bool                  eodst;

	C2_PRE(io->si_obj->so_type == &linux_stob);

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

	lio->si_nr = frags;
	c2_atomic64_set(&lio->si_done, 0);
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
			ioq_queue_lock();
			for (i = 0; i < frags; ++i)
				ioq_queue_put(&lio->si_qev[i]);
			ioq_queue_unlock();
			ioq_queue_submit();
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

#if 0
/**
  Linux stob init

  Init the Linux storage objects environment.
  Plain Linux file is used to represent an object for Linux type.
  Here we will use db4 to store the mapping from id to internal object
  representative. So, db4 connection and tales are initialized here.
*/
static int linux_stob_init(struct c2_stob *stob)
{
	/* connect to db4 here*/
	return 0;
}

static struct linux_stob *linux_stob_alloc(void)
{
	struct linux_stob *stob;

	C2_ALLOC_PTR(stob);
	return stob;
}
#endif

/**
  Linux stob fini

  Cleanup the environment. Here we cleanup the db4 connections.
*/
static void linux_stob_fini(struct c2_stob *stob)
{
}

#if 0
static void linux_stob_free(struct c2_stob *stob)
{
	struct linux_stob *lstob = stob2linux(stob);

	c2_free(lstob);
}

/**
  Create an object

  Create an object, establish the mapping from id to it in the db.
*/
static int linux_stob_create(struct c2_stob_id *id,
                             struct c2_stob_object **out)
{
	return 0;
}

/**
  Lookup an object with specified id

  Lookup an object with specified id in the mapping db.
*/
static int linux_stob_locate(struct c2_stob_id *id,
                             struct c2_stob_object **out)
{
	return 0;
}
#endif

static int linux_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io)
{
	C2_PRE(io->si_state == SIS_IDLE);

	io->si_op = &linux_stob_io_op;
	return 0;
}

/**
   An implementation of c2_stob_op::sop_lock() method.
 */
static void linux_stob_io_lock(struct c2_stob *stob)
{
}

/**
   An implementation of c2_stob_op::sop_unlock() method.
 */
static void linux_stob_io_unlock(struct c2_stob *stob)
{
}

/**
   An implementation of c2_stob_op::sop_is_locked() method.
 */
static bool linux_stob_io_is_locked(const struct c2_stob *stob)
{
	return true;
}

static const struct c2_stob_op linux_stob_op = {
	.sop_fini         = linux_stob_fini,
	.sop_io_init      = linux_stob_io_init,
	.sop_io_lock      = linux_stob_io_lock,
	.sop_io_unlock    = linux_stob_io_unlock,
	.sop_io_is_locked = linux_stob_io_is_locked,
};

enum {
	IOQ_NR_THREADS     = 8,
	IOQ_RING_SIZE      = 1024,
	IOQ_BATCH_IN_SIZE  = 8,
	IOQ_BATCH_OUT_SIZE = 8,
};

static bool         ioq_shutdown = false;
static io_context_t ioq_ctx      = NULL;
static int          ioq_avail    = IOQ_RING_SIZE;
static int          ioq_queued   = 0;

static struct c2_mutex ioq_lock;
static struct c2_queue ioq_queue;

static struct ioq_qev *ioq_queue_get(void)
{
	struct c2_queue_link *head;

	C2_ASSERT(!c2_queue_is_empty(&ioq_queue));
	C2_ASSERT(c2_mutex_is_locked(&ioq_lock));

	head = c2_queue_get(&ioq_queue);
	ioq_queued--;
	C2_ASSERT(ioq_queued == c2_queue_length(&ioq_queue));
	return container_of(head, struct ioq_qev, iq_linkage);
}

static void ioq_queue_put(struct ioq_qev *qev)
{
	C2_ASSERT(!c2_queue_link_is_in(&qev->iq_linkage));
	C2_ASSERT(c2_mutex_is_locked(&ioq_lock));

	c2_queue_put(&ioq_queue, &qev->iq_linkage);
	ioq_queued++;
	C2_ASSERT(ioq_queued == c2_queue_length(&ioq_queue));
}

static void ioq_queue_lock(void)
{
	c2_mutex_lock(&ioq_lock);
}

static void ioq_queue_unlock(void)
{
	c2_mutex_unlock(&ioq_lock);
}

static void ioq_queue_submit(void)
{
	int got;
	int put;
	int i;

	struct ioq_qev  *qev[IOQ_BATCH_IN_SIZE];
	struct iocb    *evin[IOQ_BATCH_IN_SIZE];

	do {
		ioq_queue_lock();
		got = min32(ioq_queued, min32(ioq_avail, ARRAY_SIZE(evin)));
		for (i = 0; i < got; ++i) {
			qev[i] = ioq_queue_get();
			evin[i] = &qev[i]->iq_iocb;
		}
		ioq_queue_unlock();

		if (got > 0) {
			put = io_submit(ioq_ctx, got, evin);

			if (put < 0) {
				; /* log error */
			} else if (put != got) {
				ioq_queue_lock();
				for (i = put; i < got; ++i)
					ioq_queue_put(qev[i]);
				ioq_queue_unlock();
			}
		}
	} while (got > 0);
}

static void ioq_complete(struct ioq_qev *qev)
{
	struct c2_stob_io    *io;
	struct linux_stob_io *lio;
	struct linux_stob    *lstob;
	bool done;
	int  i;

	C2_ASSERT(!c2_queue_link_is_in(&qev->iq_linkage));

	io    = qev->iq_io;
	lio   = io->si_stob_private;
	lstob = stob2linux(io->si_obj);

	C2_ASSERT(io->si_state == SIS_BUSY);
	C2_ASSERT(c2_atomic64_get(&lio->si_done) < lio->si_nr);
	done = c2_atomic64_add_return(&lio->si_done, 1) == lio->si_nr;

	if (done) {
		for (i = 0; i < lio->si_nr; ++i) {
			C2_ASSERT(!c2_queue_link_is_in
				  (&lio->si_qev[i].iq_linkage));
		}
		io->si_state = SIS_IDLE;
		c2_chan_broadcast(&io->si_wait);
	}
}

static struct timespec ioq_timeout = {
	.tv_sec  = 1,
	.tv_nsec = 0
};

static void ioq_thread(void *arg)
{
	int got;
	int i;

	struct io_event evout[IOQ_BATCH_OUT_SIZE];

	while (!ioq_shutdown) {
		got = io_getevents(ioq_ctx, 0, ARRAY_SIZE(evout), 
				   evout, &ioq_timeout);
		ioq_queue_lock();
		ioq_avail += got;
		C2_ASSERT(ioq_avail <= IOQ_RING_SIZE);
		ioq_queue_unlock();

		for (i = 0; i < got; ++i) {
			struct ioq_qev *qev;

			qev = container_of(evout[i].obj, struct ioq_qev,
					   iq_iocb);
			C2_ASSERT(!c2_queue_link_is_in(&qev->iq_linkage));
			ioq_complete(qev);
		}
		if (got < 0) {
			; /* log error */
		}

		ioq_queue_submit();
	}
}

static struct c2_thread ioq[IOQ_NR_THREADS];

static void linux_stob_type_fini_nr(int nr)
{
	ioq_shutdown = true;
	while (--nr > 0)
		c2_thread_join(&ioq[nr]);
	if (ioq_ctx != NULL)
		io_destroy(ioq_ctx);
	c2_queue_fini(&ioq_queue);
	c2_mutex_fini(&ioq_lock);
}

int linux_stob_type_init(struct c2_stob_type *stype)
{
	int result = 0;
	int i;

	c2_queue_init(&ioq_queue);
	c2_mutex_init(&ioq_lock);

	result = io_setup(IOQ_RING_SIZE, &ioq_ctx);
	if (result == 0) {
		for (i = 0; i < ARRAY_SIZE(ioq); ++i) {
			result = c2_thread_init(&ioq[i], ioq_thread, 
						(void *)(uint64_t)i);
			if (result != 0)
				break;
		}
	}
	if (result != 0)
		linux_stob_type_fini_nr(i);
	return result;
}

void linux_stob_type_fini(struct c2_stob_type *stype)
{
	linux_stob_type_fini_nr(ARRAY_SIZE(ioq));
}

static const struct c2_stob_type_op linux_stob_type_op = {
	.sto_init = linux_stob_type_init
};

static const struct c2_stob_type linux_stob = {
	.st_op    = &linux_stob_type_op,
	.st_name  = "linuxstob",
	.st_magic = 0xACC01ADE
};

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
