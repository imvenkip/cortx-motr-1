/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#pragma once

#ifndef __MERO_STOB_LINUX_INTERNAL_H__
#define __MERO_STOB_LINUX_INTERNAL_H__

/**
   @addtogroup stoblinux Storage object based on Linux specific file system
   interfaces.

   @see stob
   @{
 */

#include <libaio.h>

#include "lib/queue.h"
#include "lib/tlist.h"
#include "lib/thread.h"
#include "stob/stob.h"
#include "stob/cache.h"

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
	/**
	   Set to true in linux_setup(). May be used in pre-conditions to
	   guarantee that the domain is fully initialized.
	 */
	bool                  sdl_linux_setup;

	/**
	 *  Controls whether to use O_DIRECT flag for open(2).
	 *  Can be set with m0_linux_stob_setup().
	 *  Initial value is set to 'false' in linux_stob_type_domain_locate().
	 */
	bool                  sdl_use_directio;

	struct m0_stob_domain sdl_base;
	/**
	   parent directory to hold the objects.
	 */
	char                  sdl_path[MAXPATHLEN];

	struct m0_stob_cache  sdl_cache;

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
	struct m0_thread ioq[IOQ_NR_THREADS];

	/** Mutex protecting all ioq_ fields (except for the ring buffer that is
	    updated by the kernel asynchronously). */
	struct m0_mutex  ioq_lock;
	/** Admission queue where adieu request fragments are kept until there
	    is free space in the ring buffer.  */
	struct m0_queue  ioq_queue;

	/** *} end of ioq name */
};

/**
   stob based on Linux file system and block devices
 */
struct linux_stob {
	struct m0_stob_cacheable sl_stob;

	/** fd from returned open(2) */
	int			 sl_fd;
	/** File mode as returned by stat(2) */
	mode_t			 sl_mode;

	struct m0_tlink		 sl_linkage;
	uint64_t		 sl_magix;
};

static inline struct linux_stob *stob2linux(struct m0_stob *stob)
{
	return container_of(stob, struct linux_stob, sl_stob.ca_stob);
}

static inline struct linux_domain *domain2linux(struct m0_stob_domain *dom)
{
	return container_of(dom, struct linux_domain, sdl_base);
}

M0_INTERNAL int linux_stob_io_init(struct m0_stob *stob, struct m0_stob_io *io);
M0_INTERNAL uint32_t linux_stob_block_shift(const struct m0_stob *stob);
M0_INTERNAL void linux_domain_io_fini(struct m0_stob_domain *dom);
M0_INTERNAL int linux_domain_io_init(struct m0_stob_domain *dom);

extern struct m0_addb_ctx adieu_addb_ctx;

/** @} end group stoblinux */

/* __MERO_STOB_LINUX_INTERNAL_H__ */
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
