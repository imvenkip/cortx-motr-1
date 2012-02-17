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

#include "lib/errno.h"
#include "lib/atomic.h"
#include "lib/arith.h" /* c2_align */
#include "lib/misc.h"
#include "lib/memory.h" /* c2_pagesize_get */
#include "lib/trace.h"

/**
 * @addtogroup trace
 *
 * <b>Tracing facilities implementation.</b>
 *
 * Trace entries are placed in a largish buffer backed up by a memory mapped
 * file. Buffer space allocation is controlled by a single atomic variable
 * (cur).
 *
 * Trace entries contain pointers from the process address space. To interpret
 * them, c2_trace_parse() must be called in the same binary. See utils/ut_main.c
 * for example.
 *
 * @note things like address space layout randomization might break this
 * implementation.
 *
 * @{
 */

/* single buffer for now */
void *c2_logbuf = NULL;

static uint32_t           bufmask;
static struct c2_atomic64 cur;

extern int  c2_arch_trace_init(void);
extern void c2_arch_trace_fini(void);

int c2_trace_init(void)
{
	int psize;

	C2_ASSERT(c2_logbuf == NULL);

	c2_atomic64_set(&cur, 0);
	bufmask  = C2_TRACE_BUFSIZE - 1;

	psize = c2_pagesize_get();
	C2_ASSERT((C2_TRACE_BUFSIZE % psize) == 0);

	return c2_arch_trace_init();
}

void c2_trace_fini(void)
{
	c2_arch_trace_fini();
	c2_logbuf = NULL;
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

	C2_ASSERT(c2_logbuf != NULL);
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
			memset(c2_logbuf + pos_in_buf, 0, C2_TRACE_BUFSIZE - pos_in_buf);
			memset(c2_logbuf, 0, endpos_in_buf);
		} else
			break;
	}

	header                = c2_logbuf + pos_in_buf;
	header->trh_magic     = 0;
	header->trh_no        = pos;
	header->trh_sp        = sp;
	header->trh_timestamp = rdtsc();
	header->trh_descr     = td;
	memcpy((void*)header + header_len, body, td->td_size);
	/** @todo put memory barrier here before writing the magic */
	header->trh_magic = C2_TRACE_MAGIC;     
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
