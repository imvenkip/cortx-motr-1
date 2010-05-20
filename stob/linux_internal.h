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

#include "lib/thread.h"

#include "stob.h"

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

enum {
	IOQ_NR_THREADS     = 8,
	IOQ_RING_SIZE      = 1024,
	IOQ_BATCH_IN_SIZE  = 8,
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
	   in Objects/LOXXXXXX
	*/
	char             sdl_path[MAXPATHLEN];

	/** List of all existing c2_stob's. */
	struct c2_list   sdl_object;

        DB_ENV          *sdl_dbenv;
        DB              *sdl_mapping;
        u_int32_t        sdl_dbenv_flags;
        u_int32_t        sdl_db_flags;
        u_int32_t        sdl_txn_flags;
        u_int32_t        sdl_cache_size;
        u_int32_t        sdl_nr_thread;
        u_int32_t        sdl_recsize;
        int              sdl_direct_db;

	bool             ioq_shutdown;
	io_context_t     ioq_ctx;
	int              ioq_avail;
	int              ioq_queued;
	struct c2_thread ioq[IOQ_NR_THREADS];

	struct c2_mutex  ioq_lock;
	struct c2_queue  ioq_queue;
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
