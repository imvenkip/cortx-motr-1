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

#include <sys/mman.h> /* mmap */
#include <time.h>
#include <unistd.h>   /* getpagesize */
#include <fcntl.h>    /* open, O_RDWR|O_CREAT|O_TRUNC */
#include <string.h>   /* memset */
#include <stdio.h>    /* getchar */
#include <ctype.h>    /* isspace */

#include "lib/errno.h"
#include "lib/atomic.h"
#include "lib/cdefs.h"
#include "lib/arith.h" /* c2_align */
#include "lib/trace.h"

/**
   @addtogroup trace

   <b>User space trace implementation.</b>

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
static void              *logbuf;
static uint32_t           bufshift;
static uint32_t           bufsize;
static uint32_t           bufmask;
static int                logfd;
static struct c2_atomic64 cur;

enum {
	MAGIC = 0xc0de1eafacc01adeULL,
};

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

void *c2_trace_allot(const struct c2_trace_descr *td)
{
	uint32_t                    record_len;
	uint32_t                    header_len;
	struct c2_trace_rec_header *header;
	uint64_t                    pos;
	uint64_t                    endpos;
	uint32_t                    pos_in_buf;

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
		/*
		 * If allocated space crosses buffer end, zero allocated parts
		 * of the buffer and allocate anew. Allocated space remains lost
		 * until the buffer is wrapped over again.
		 */
		if ((pos >> bufshift) != (endpos >> bufshift)) {
			memset(logbuf + pos_in_buf, 0, bufsize - pos_in_buf);
			memset(logbuf, 0, endpos & bufmask);
		} else
			break;
	}

	header                = logbuf + pos_in_buf;
	header->thr_magic     = MAGIC;
	/* select thr_no so that it increases over time but never 0. */
	header->thr_no        = (pos & ((1ULL << 62) - 1)) + 1;
	header->trh_timestamp = rdtsc();
	header->trh_descr     = td;
	return ((void *)header) + header_len;
}

static void align(unsigned align)
{
	C2_ASSERT(c2_is_po2(align));
	while (ftell(stdin) & (align - 1))
		getchar();
}

static void trace_decl(const char *decl)
{
	void skip(void) {
		while (isspace(*decl))
			decl++;
	}

	bool gotmatch(const char *keyword) {
		if (!strncmp(decl, keyword, strlen(keyword))) {
			decl += strlen(keyword);
			return true;
		} else
			return false;
	}

	void field(int size, const char *fmt, int alignment) {
		union {
			uint16_t v16;
			uint32_t v32;
			uint64_t v64;
		} val;
		int  nr;

		align(alignment);
		nr = fread(&val, size, 1, stdin);
		C2_ASSERT(nr == 1);

		skip();
		/* skip field name */
		while (('A' <= toupper(*decl) && toupper(*decl) <= 'Z') ||
		       ('0' <= *decl && *decl <= '9') ||
		       index("_$", *decl) != NULL)
			printf("%c", *decl++);

		printf(": ");
		printf(fmt, val);
		printf(" ");

		skip();
		C2_ASSERT(*decl == ';');
		decl++;
		skip();
	}

#define DECL(type, fmt)						\
	if (gotmatch(#type)) {					\
		field(sizeof(type), "%"fmt, __alignof__(type));	\
		continue;					\
	}

	skip();
	C2_ASSERT(*decl == '{');
	decl++;
	skip();
	while (*decl != '}') {
		while (gotmatch("const") || gotmatch("volatile") ||
		       gotmatch("unsigned"))
			skip();

		DECL(uint32_t, "u");
		DECL(uint16_t, "u");
		DECL(uint64_t, "lu");
		DECL(int32_t,  "d");
		DECL(int16_t,  "d");
		DECL(int64_t,  "ld");
	}
}

/**
   Parse log buffer supplied at stderr.

   When a trace record is defined by C2_TRACE_POINT(), a declaration of its
   format is stored in c2_trace_descr::td_decl as a NUL-terminated string
   containing C declaration. trace_decl() is an extremely crude ad-hoc parser
   for this string.
 */
int c2_trace_parse(void)
{
	uint64_t                     magic;
	uint64_t                     no;
	uint64_t                     timestamp;
	const struct c2_trace_descr *td;
	int                          nr;

	char int2ch(int x) {
		return x < 10 ? '0' + x : 'a' + x - 10;
	}

	while (!feof(stdin)) {
		/* At the beginning of a record */
		align(8);

		do {
			nr = fread(&magic, sizeof magic, 1, stdin);
			if (nr == 0) {
				C2_ASSERT(feof(stdin));
				return 0;
			}
		} while (magic != MAGIC);

		nr = fread(&no, sizeof no, 1, stdin);
		C2_ASSERT(nr == 1);

		nr = fread(&timestamp, sizeof timestamp, 1, stdin);
		C2_ASSERT(nr == 1);

		nr = fread(&td, sizeof td, 1, stdin);
		C2_ASSERT(nr == 1);

		printf("From Parser %10.10lu  %10.10lu  %15s %15s %4i %3.3i %s\n\t",
		       no, timestamp, td->td_func, td->td_file,
		       td->td_line, td->td_size, td->td_decl);
		align(8);
		trace_decl(td->td_decl);
		align(8);
		printf("\n");
	}
	return 0;
}

enum {
	BUFSHIFT = 10 + 12, /* 4MB log buffer */
	BUFSIZE  = 1 << BUFSHIFT
};

int c2_trace_init(void)
{
	int psize;

	c2_atomic64_set(&cur, 0);
	bufshift = BUFSHIFT;
	bufsize  = BUFSIZE;
	bufmask  = bufsize - 1;

	psize = getpagesize();
	C2_ASSERT((BUFSIZE % psize) == 0);

	logfd = open("c2.trace", O_RDWR|O_CREAT|O_TRUNC, 0700);
	if (logfd != -1) {
		if (ftruncate(logfd, BUFSIZE) == 0) {
			logbuf = mmap(NULL, BUFSIZE, PROT_WRITE,
				      MAP_SHARED, logfd, 0);
		}
	}
	return -errno;
}

void c2_trace_fini(void)
{
	munmap(logbuf, bufsize);
	close(logfd);
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
