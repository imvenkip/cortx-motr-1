/* -*- C -*- */

#ifndef __COLIBRI_STOB_LINUX_INTERNAL_H__
#define __COLIBRI_STOB_LINUX_INTERNAL_H__

/**
   @addtogroup stoblinux Storage object based on Linux specific file system
   interfaces.

   @see stob
   @{
 */

#include <db.h>
#include <libaio.h>

#include <lib/thread.h>

#include "stob.h"

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

enum {
	/** Default number of threads to create in a storage object domain. */
	IOQ_NR_THREADS     = 8,
	/** Default size of a ring buffer shared by adieu and the kernel. */
	IOQ_RING_SIZE      = 1024,
	/** Size of a batch in which requests are moved from the admission queue
	    to the ring buffer. */
	IOQ_BATCH_IN_SIZE  = 8,
	/** Size of a batch in which completion events are extracted from the
	    ring buffer. */
	IOQ_BATCH_OUT_SIZE = 8,
};

/**
   Stob domain for Linux type.
 */
struct linux_domain {
	struct c2_stob_domain sdl_base;
	/**
	   parent directory to hold the mapping db and objects.
	   Mapping db will be stored in map.db, and all objects will be stored
	   in o/AAAAAAAA.BBBBBBBB
	*/
	char             sdl_path[MAXPATHLEN];

	/** List of all existing c2_stob's. */
	struct c2_list   sdl_object;

        DB_ENV          *sdl_dbenv;
        DB              *sdl_mapping;
        uint32_t         sdl_dbenv_flags;
        uint32_t         sdl_db_flags;
        uint32_t         sdl_txn_flags;
        uint32_t         sdl_cache_size;
        uint32_t         sdl_nr_thread;
        uint32_t         sdl_recsize;
        int              sdl_direct_db;

	/** @name ioq Linux adieu fields. @{ */

	/** Set up when domain is being shut down. adieu worker threads
	    (ioq_thread()) check this field on each iteration. */
	bool             ioq_shutdown;
	/** 
	    Ring buffer shared between adieu and the kernel.
	    
	    It contains adieu request fragments currently being executed by the
	    kernel. The kernel delivers AIO completion events through this
	    buffer. */
	io_context_t     ioq_ctx;
	/** Free slots in the ring buffer. */
	int              ioq_avail;
	/** Used slots in the ring buffer. */
	int              ioq_queued;
	/** Worker threads. */
	struct c2_thread ioq[IOQ_NR_THREADS];

	/** Mutex protecting all ioq_ fields (except for the ring buffer that is
	    updated by the kernel asynchronously). */
	struct c2_mutex  ioq_lock;
	/** Admission queue where adieu request fragments are kept until there
	    is free space in the ring buffer.  */
	struct c2_queue  ioq_queue;

	/** *} end of ioq name */
};

/**
   stob based on Linux file system
 */
struct linux_stob {
	struct c2_stob      sl_stob;

	/** fd from returned open(2) */
	int                 sl_fd;
	struct c2_list_link sl_linkage;
};

static struct linux_stob *stob2linux(struct c2_stob *stob)
{
	return container_of(stob, struct linux_stob, sl_stob);
}

static struct linux_domain *domain2linux(struct c2_stob_domain *dom)
{
	return container_of(dom, struct linux_domain, sdl_base);
}

int  linux_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io);
void linux_stob_io_lock(struct c2_stob *stob);
void linux_stob_io_unlock(struct c2_stob *stob);
bool linux_stob_io_is_locked(const struct c2_stob *stob);
void linux_domain_io_fini(struct c2_stob_domain *dom);
int  linux_domain_io_init(struct c2_stob_domain *dom);

extern struct c2_addb_ctx adieu_addb_ctx;

/** @} end group stoblinux */

/* __COLIBRI_STOB_LINUX_INTERNAL_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
