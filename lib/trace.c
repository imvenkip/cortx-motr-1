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
 * Original creation date: 08/12/2010
 */

#ifndef __KERNEL__
#  include <string.h>   /* memset */
#  include <stdio.h>
#  include <unistd.h>   /* getpagesize */
#  include <fcntl.h>    /* open, O_RDWR|O_CREAT|O_TRUNC */
#  include <sys/mman.h> /* mmap */
#  include <sys/syscall.h>
#  include <linux/sysctl.h>
#else
#  include <linux/slab.h>
#  include <linux/sched.h>
#endif

#include "lib/errno.h"
#include "lib/atomic.h"
#include "lib/arith.h" /* c2_align */
#include "lib/trace.h"

/**
   @addtogroup trace

   <b>Tracing facilities implementation.</b>

   Trace entries are placed in a largish buffer backed up by a memory mapped
   file. Buffer space allocation is controlled by a single atomic variable
   (cur).

   Trace entries contain pointers from the process address space. To interpret
   them, c2_trace_parse() must be called in the same binary. See utils/ut_main.c
   for example.

   @note things like address space layout randomization might break this
   implementation.

   @{
 */

/* single buffer for now */
static void              *logbuf = NULL;
static uint32_t           bufsize;
static uint32_t           bufmask;
static struct c2_atomic64 cur;
#ifndef __KERNEL__
static int                logfd;
#endif

enum {
	BUFSHIFT = 10 + 12, /* 4MB log buffer */
	BUFSIZE  = 1 << BUFSHIFT
};

int c2_trace_init(void)
{
	int psize;

	C2_ASSERT(logbuf == NULL);
#ifndef __KERNEL__
	{
		int name[] = { CTL_KERN, KERN_RANDOMIZE };
		struct __sysctl_args args;
		int val;
		size_t val_sz = sizeof val;

		memset(&args, 0, sizeof args);
		args.name = name;
		args.nlen = ARRAY_SIZE(name);
		args.oldval  = &val;
		args.oldlenp = &val_sz;

		if (syscall(SYS__sysctl, &args) == -1) {
			perror("_sysctl");
			return -errno;
		}

		if (val != 0) {
			fprintf(stderr, "System configuration ERROR: "
			   "kernel.randomize_va_space should be set to 0.\n");
			return -1;
		}
	}
#endif

	c2_atomic64_set(&cur, 0);
	bufsize  = BUFSIZE;
	bufmask  = bufsize - 1;

#ifdef __KERNEL__
	psize = PAGE_SIZE;
#else
	psize = getpagesize();
#endif
	C2_ASSERT((BUFSIZE % psize) == 0);

#ifndef __KERNEL__
	errno = 0;
	logfd = open("c2.trace", O_RDWR|O_CREAT|O_TRUNC, 0700);
	if (logfd != -1) {
		if (ftruncate(logfd, BUFSIZE) == 0) {
			logbuf = mmap(NULL, BUFSIZE, PROT_WRITE,
				      MAP_SHARED, logfd, 0);
			if (logbuf == MAP_FAILED)
				perror("mmap");
		}
	} else {
		perror("open");
	}

	return -errno;
#else
	logbuf = kzalloc(BUFSIZE, GFP_KERNEL);

	return logbuf ? 0 : -ENOMEM;
#endif
}

void c2_trace_fini(void)
{
#ifdef __KERNEL__
	kfree(logbuf);
#else
	munmap(logbuf, bufsize);
	close(logfd);
#endif
	logbuf = NULL;
}


/*
 * XXX x86_64 version.
 */
static inline uint64_t rdtsc(void)
{
	uint32_t count_hi;
	uint32_t count_lo;

	__asm__ __volatile__("rdtsc" : "=a"(count_lo), "=d"(count_hi));

	return ((uint64_t)count_lo) | (((uint64_t)count_hi) << 32);
}

void c2_trace_allot(const struct c2_trace_descr *td, const void *body)
{
	uint32_t header_len, record_len;
	uint32_t pos_in_buf, endpos_in_buf;
	uint64_t pos, endpos;
	struct c2_trace_rec_header *header;
	register unsigned long sp asm ("sp");

	C2_ASSERT(logbuf != NULL);
	/*
	 * Allocate space in trace buffer to store trace record header
	 * (header_len bytes) and record payload (record_len bytes).
	 *
	 * Record and payload always start at 8-byte aligned address.
	 *
	 * First free byte in the trace buffer is at "cur" offset. Note, that
	 * cur is not wrapped to 0 when the end of the buffer is reached (that
	 * would require additional synchronization between contending threads).
	 */

	header_len = c2_align(sizeof *header, 8);
	record_len = header_len + c2_align(td->td_size, 8);

	while (1) {
		endpos = c2_atomic64_add_return(&cur, record_len);
		pos    = endpos - record_len;
		pos_in_buf = pos & bufmask;
		endpos_in_buf = endpos & bufmask;
		/*
		 * The record should not cross the buffer.
		 */
		if (pos_in_buf > endpos_in_buf && endpos_in_buf) {
			memset(logbuf + pos_in_buf, 0, bufsize - pos_in_buf);
			memset(logbuf, 0, endpos_in_buf);
		} else
			break;
	}

	header                = logbuf + pos_in_buf;
	header->trh_magic     = 0;
	header->trh_no        = pos;
	header->trh_sp        = sp;
	header->trh_timestamp = rdtsc();
	header->trh_descr     = td;
	memcpy((void*)header + header_len, body, td->td_size);
	/** @todo put memory barrier here before writing the magic */
	header->trh_magic = MAGIC;     
}

/** @} end of trace group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
