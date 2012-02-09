/* -*- C -*- */
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

#ifndef __COLIBRI_STOB_LINUX_INTERNAL_H__
#define __COLIBRI_STOB_LINUX_INTERNAL_H__

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
	bool linux_setup;

	/**
	 *  Controls whether to use O_DIRECT flag for open(2).
	 *  Can be set with c2_linux_stob_setup().
	 *  Initial value is set to 'false' in linux_stob_type_domain_locate().
	 */
	bool use_directio;

	struct c2_stob_domain sdl_base;
	/**
	   parent directory to hold the objects.
	 */
	char             sdl_path[MAXPATHLEN];

	/** List of all existing c2_stob's. */
	struct c2_tl     sdl_object;

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
   stob based on Linux file system and block devices
 */
struct linux_stob {
	struct c2_stob		sl_stob;

	/** fd from returned open(2) */
	int			sl_fd;
	/** File mode as returned by stat(2) */
	mode_t			sl_mode;

	struct c2_tlink		sl_linkage;
	uint64_t		sl_magix;
};

static inline struct linux_stob *stob2linux(struct c2_stob *stob)
{
	return container_of(stob, struct linux_stob, sl_stob);
}

static inline struct linux_domain *domain2linux(struct c2_stob_domain *dom)
{
	return container_of(dom, struct linux_domain, sdl_base);
}

int      linux_stob_io_init     (struct c2_stob *stob, struct c2_stob_io *io);
void     linux_stob_io_lock     (struct c2_stob *stob);
void     linux_stob_io_unlock   (struct c2_stob *stob);
bool     linux_stob_io_is_locked(const struct c2_stob *stob);
uint32_t linux_stob_block_shift (const struct c2_stob *stob);
void     linux_domain_io_fini   (struct c2_stob_domain *dom);
int      linux_domain_io_init   (struct c2_stob_domain *dom);

uint32_t linux_stob_domain_block_shift(struct c2_stob_domain *sdomain);
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
