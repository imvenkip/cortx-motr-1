#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <db.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>


#include <libaio.h>

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

static struct c2_stob_type linux_stob_type;
static const struct c2_stob_op linux_stob_op;

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

static struct c2_stob_linux *stob2linux(struct c2_stob *stob)
{
	return container_of(stob, struct c2_stob_linux, sl_stob);
}

static struct c2_stob_domain_linux * domain2linux(struct c2_stob_domain *dom)
{
	return container_of(dom, struct c2_stob_domain_linux, sdl_base);
}

static void db_err(DB_ENV *dbenv, int rc, const char *msg)
{
	dbenv->err(dbenv, rc, msg);
	printf("%s: %s", msg, db_strerror(rc));
	return;
}

void mapping_db_fini(struct c2_stob_domain_linux *sdl)
{
	DB_ENV  *dbenv = sdl->sdl_dbenv;
	int 	 rc;
	
	if (dbenv) {
		rc = dbenv->log_flush(dbenv, NULL);
		if (sdl->sdl_mapping) {
			sdl->sdl_mapping->sync(sdl->sdl_mapping, 0);
			sdl->sdl_mapping->close(sdl->sdl_mapping, 0);
		}
		sdl->sdl_mapping = NULL;
		dbenv->close(dbenv, 0);
		sdl->sdl_dbenv = NULL;
	}
}


static int mapping_db_init(struct c2_stob_domain_linux *sdl)
{
	int         rc;
	DB_ENV 	   *dbenv;
	const char *backend_path = sdl->sdl_path;
	char 	    path[MAXPATHLEN];
	char	    *msg;

	rc = db_env_create(&dbenv, 0);
	if (rc != 0) {
		printf("db_env_create: %s", db_strerror(rc));
		return rc;
	}

	sdl->sdl_dbenv = dbenv;
	dbenv->set_errfile(dbenv, stderr);
	dbenv->set_errpfx(dbenv, "stob_linux");

	rc = dbenv->set_flags(dbenv, DB_TXN_NOSYNC, 1);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_flags(DB_TXN_NOSYNC)");
		mapping_db_fini(sdl);
		return rc;
	}

	if (sdl->sdl_direct_db) {
		rc = dbenv->set_flags(dbenv, DB_DIRECT_DB, 1);
		if (rc != 0) {
			db_err(dbenv, rc, "->set_flags(DB_DIRECT_DB)");
			mapping_db_fini(sdl);
			return rc;
		}
		rc = dbenv->log_set_config(dbenv, DB_LOG_DIRECT, 1);
		if (rc != 0) {
			db_err(dbenv, rc, "->log_set_config()");
			mapping_db_fini(sdl);
			return rc;
		}
	}

	rc = dbenv->set_lg_bsize(dbenv, 1024*1024*1024);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_lg_bsize()");
		mapping_db_fini(sdl);
		return rc;
	}

	rc = dbenv->log_set_config(dbenv, DB_LOG_AUTO_REMOVE, 1);
	if (rc != 0) {
		db_err(dbenv, rc, "->log_set_config(DB_LOG_AUTO_REMOVE)");
		mapping_db_fini(sdl);
		return rc;
	}

	rc = dbenv->set_cachesize(dbenv, 0, 1024 * 1024, 1);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_cachesize()");
		mapping_db_fini(sdl);
		return rc;
	}

	rc = dbenv->set_thread_count(dbenv, sdl->sdl_nr_thread);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_thread_count()");
		mapping_db_fini(sdl);
		return rc;
	}

#if 0
	dbenv->set_verbose(dbenv, DB_VERB_DEADLOCK, 1);
	dbenv->set_verbose(dbenv, DB_VERB_WAITSFOR, 1);
	dbenv->set_verbose(dbenv, DB_VERB_RECOVERY, 1);
	dbenv->set_verbose(dbenv, DB_VERB_FILEOPS, 1);
	dbenv->set_verbose(dbenv, DB_VERB_FILEOPS_ALL, 1);
#endif

	/* creating working directory */
	rc = mkdir(backend_path, 0700);
	if (rc != 0 && errno != EEXIST) {
		printf("mkdir(%s): %s", backend_path, strerror(errno));
		mapping_db_fini(sdl);
		return rc;
	}

	/* directory to hold objects */
	snprintf(path, MAXPATHLEN - 1, "%s/Objects", backend_path);
	rc = mkdir(path, 0700);
	if (rc != 0 && errno != EEXIST)
		printf("mkdir(%s): %s", path, strerror(errno));

	/* directory to hold mapping db */
	snprintf(path, MAXPATHLEN - 1, "%s/oi.db", backend_path);
	rc = mkdir(path, 0700);
	if (rc != 0 && errno != EEXIST)
		printf("mkdir(%s): %s", path, strerror(errno));

	snprintf(path, MAXPATHLEN - 1, "%s/oi.db/d", backend_path);
	mkdir(path, 0700);

	snprintf(path, MAXPATHLEN - 1, "%s/oi.db/d/o", backend_path);
	mkdir(path, 0700);

	snprintf(path, MAXPATHLEN - 1, "%s/oi.db/l", backend_path);
	mkdir(path, 0700);

	snprintf(path, MAXPATHLEN - 1, "%s/oi.db/t", backend_path);
	mkdir(path, 0700);

	rc = dbenv->set_tmp_dir(dbenv, "t");
	if (rc != 0)
		db_err(dbenv, rc, "->set_tmp_dir()");

	rc = dbenv->set_lg_dir(dbenv, "l");
	if (rc != 0)
		db_err(dbenv, rc, "->set_lg_dir()");

	rc = dbenv->set_data_dir(dbenv, "d");
	if (rc != 0)
		db_err(dbenv, rc, "->set_data_dir()");

	/* Open the environment with full transactional support. */
	sdl->sdl_dbenv_flags = DB_CREATE|DB_THREAD|DB_INIT_LOG|
	               	       DB_INIT_MPOOL|DB_INIT_TXN|DB_INIT_LOCK|
	                       DB_RECOVER;
	snprintf(path, MAXPATHLEN - 1, "%s/db4s.db", backend_path);
	rc = dbenv->open(dbenv, path, sdl->sdl_dbenv_flags, 0);
	if (rc != 0)
		db_err(dbenv, rc, "environment open");

	sdl->sdl_db_flags = DB_AUTO_COMMIT|DB_CREATE|
                            DB_THREAD|DB_TXN_NOSYNC|
	                    /*
	    		     * Both a data-base and a transaction
			     * must use "read uncommitted" to avoid
	    	             * dead-locks.
			     */
                             DB_READ_UNCOMMITTED;

	sdl->sdl_txn_flags = DB_READ_UNCOMMITTED|DB_TXN_NOSYNC;
	rc = db_create(&sdl->sdl_mapping, dbenv, 0);
	msg = "create";
	if (rc == 0) {
		rc = sdl->sdl_mapping->open(sdl->sdl_mapping, NULL, "o/oi", 
			               NULL, DB_BTREE, sdl->sdl_db_flags, 0664);
		msg = "open";
	}
	if (rc != 0) {
		dbenv->err(dbenv, rc, "database \"oi\": %s failure", msg);
		printf("database \"oi\": %s failure", msg);
	}
	return 0;
}

static int mapping_db_insert(struct c2_stob_domain_linux *sdl,
  		      	     const struct c2_stob_id *id,
		             const char *fname)
{
	DB_TXN *tx = NULL;
	DB_ENV *dbenv = sdl->sdl_dbenv;
	size_t  flen  = strlen(fname) + 1;
	int     rc;
	DBT     keyt;
	DBT     rect;

	memset(&keyt, 0, sizeof keyt);
	memset(&rect, 0, sizeof rect);

	keyt.data = (void*)id;
	keyt.size = sizeof *id;

	rect.data = (void*)fname;
	rect.size = flen;

	rc = dbenv->txn_begin(dbenv, NULL, &tx, sdl->sdl_txn_flags);
	if (tx == NULL) {
		db_err(dbenv, rc, "cannot start transaction");
		return rc;
	}

	rc = sdl->sdl_mapping->put(sdl->sdl_mapping, tx, &keyt, &rect, 0);
	if (rc == DB_LOCK_DEADLOCK)
		fprintf(stderr, "deadlock.\n");
	else if (rc != 0)
		db_err(dbenv, rc, "DB->put() cannot insert into database");

	if (rc == 0)
		rc = tx->commit(tx, 0);
	else
		rc = tx->abort(tx);
	if (rc != 0)
		db_err(dbenv, rc, "cannot commit/abort transaction");
	return rc;
}

static int mapping_db_lookup(struct c2_stob_domain_linux *sdl,
  		      	     const struct c2_stob_id *id,
		             char *fname, int maxflen)
{
	DB_ENV *dbenv = sdl->sdl_dbenv;
	size_t  flen  = maxflen;
	int     rc;
	DBT     keyt;
	DBT     rect;

	memset(&keyt, 0, sizeof keyt);
	memset(&rect, 0, sizeof rect);

	keyt.data = (void*)id;
	keyt.size = sizeof *id;

	rect.data = (void*)fname;
	rect.size = flen;

	rc = sdl->sdl_mapping->get(sdl->sdl_mapping, NULL, &keyt, &rect, 0);
	if (rc != 0)
		db_err(dbenv, rc, "DB->get() cannot get data from database");
	return rc;
}



/**
  Linux stob init
*/
static int linux_stob_init(struct c2_stob *stob)
{
	return 0;
}

/**
  Linux stob alloc
*/
static struct c2_stob_linux *linux_stob_alloc(void)
{
	struct c2_stob_linux *stob;

	C2_ALLOC_PTR(stob);
	return stob;
}

/**
  Linux stob fini
*/
static void linux_stob_fini(struct c2_stob *stob)
{
}

static void linux_stob_free(struct c2_stob *stob)
{
	struct c2_stob_linux *lstob = stob2linux(stob);

	c2_free(lstob);
}

static int stob_domain_linux_init(struct c2_stob_domain *self)
{
	int rc;
	struct c2_stob_domain_linux *sdl = domain2linux(self);

	rc = mapping_db_init(sdl);
	return rc;
}

static void stob_domain_linux_fini(struct c2_stob_domain *self)
{
	struct c2_stob_domain_linux *sdl = domain2linux(self);

	mapping_db_fini(sdl);
}

static struct c2_stob *stob_domain_linux_alloc(struct c2_stob_domain *d,
                                               struct c2_stob_id *id)
{
	struct c2_stob_linux *sl;

	sl = linux_stob_alloc();
	if (sl != NULL) {
		sl->sl_stob.so_op = &linux_stob_op;
		sl->sl_stob.so_id = *id;
		c2_list_link_init(&sl->sl_stob.so_linkage);
		c2_list_add(&d->sd_objects, &sl->sl_stob.so_linkage);
		return &sl->sl_stob;
	} else
		return NULL;
}

static void stob_domain_linux_free(struct c2_stob_domain *d,
				   struct c2_stob *o)
{
	c2_list_del(&o->so_linkage);
	linux_stob_free(o);	
}

/**
  Create an object

  Create an object on storage, establish the mapping from id to it in the db.
*/
static int stob_domain_linux_create(struct c2_stob_domain *d,
                                    struct c2_stob *o)
{
	struct c2_stob_domain_linux *sdl = container_of(d,
		 			   struct c2_stob_domain_linux, sdl_base);
	struct c2_stob_linux        *sl  = container_of(o, struct c2_stob_linux,
							sl_stob);	
	int fd;
	char *filename;

	sprintf(sl->sl_filename, "%s/Objects/LOXXXXXXXXXX", sdl->sdl_path);
	filename = mktemp(sl->sl_filename);

	if ((fd = open(filename, O_CREAT | O_RDWR)) > 0) {
		mapping_db_insert(sdl, &o->so_id, filename);
		sl->sl_fd = fd;
	}
	return 0;
}

/**
  Lookup an object with specified id

  Lookup an object with specified id in the mapping db.
*/
static int stob_domain_linux_locate(struct c2_stob_domain *d,
				    struct c2_stob *o)
{
	struct c2_stob_domain_linux *sdl = domain2linux(d);
	struct c2_stob_linux        *sl  = stob2linux(o);
	int rc;	

	memset(sl->sl_filename, 0, MAXPATHLEN);
	rc = mapping_db_lookup(sdl, &o->so_id, sl->sl_filename, MAXPATHLEN - 1);
	if (!rc) {
		sl->sl_fd = open(sl->sl_filename, O_RDWR);
		if (sl->sl_fd > 0)
			return 0;
		else
			return -EIO;
	} else
		return -ENOENT;
}

static const struct c2_stob_domain_op stob_domain_linux_op = {
	.sdo_init   = stob_domain_linux_init,
	.sdo_fini   = stob_domain_linux_fini,
	.sdo_alloc  = stob_domain_linux_alloc,
	.sdo_free   = stob_domain_linux_free,
	.sdo_create = stob_domain_linux_create,
	.sdo_locate = stob_domain_linux_locate

};

struct c2_stob_domain_linux sdl = {
	.sdl_base = {
		.sd_name = "stob_domain_linux_t1",
		.sd_ops  = &stob_domain_linux_op,
	},
	.sdl_path = "./",
};

static const struct c2_stob_io_op linux_stob_io_op;

static int linux_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io)
{
	C2_PRE(io->si_state == SIS_IDLE);

	io->si_op = &linux_stob_io_op;
	return 0;
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
	struct c2_stob_linux *lstob  = stob2linux(io->si_obj);
	struct linux_stob_io *lio    = io->si_stob_private;
	struct c2_vec_cursor  src;
	struct c2_vec_cursor  dst;
	uint32_t              frags;
	c2_bcount_t           frag_size;
	int                   result = 0;
	int                   i;
	bool                  eosrc;
	bool                  eodst;

	C2_PRE(io->si_obj->so_type == &linux_stob_type);

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
	.sop_init         = linux_stob_init,
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
	struct c2_stob_linux *lstob;
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

const static struct timespec ioq_timeout_default = {
	.tv_sec  = 1,
	.tv_nsec = 0
};

static void ioq_thread(int unused)
{
	int got;
	int i;

	struct io_event evout[IOQ_BATCH_OUT_SIZE];
	struct timespec ioq_timeout;

	while (!ioq_shutdown) {
		ioq_timeout = ioq_timeout_default;
		got = io_getevents(ioq_ctx, 1, ARRAY_SIZE(evout), 
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
			result = C2_THREAD_INIT(&ioq[i], 
						int, NULL, &ioq_thread, 0);
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

static struct c2_stob_type linux_stob_type = {
	.st_op    = &linux_stob_type_op,
	.st_name  = "linuxstob",
	.st_magic = 0xACC01ADE
};

int linux_stob_module_init(void)
{
	return linux_stob_type.st_op->sto_init(&linux_stob_type);
}

void linux_stob_module_fini(void)
{
	linux_stob_type.st_op->sto_fini(&linux_stob_type);
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
