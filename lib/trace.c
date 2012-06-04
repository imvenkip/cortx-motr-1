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

#ifdef __KERNEL__
#include "lib/cdefs.h" /* CHAR_BIT */
#include <linux/ctype.h> /* tolower */
#else
#include <limits.h> /* CHAR_BIT */
#include <ctype.h> /* tolower */
#endif

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

/**
 * This buffer is used for early trace records issued before real buffer is
 * initialized by c2_trace_init().
 */
static char bootbuf[4096];
void      *c2_logbuf     = bootbuf;
uint32_t   c2_logbufsize = sizeof bootbuf;

unsigned long c2_trace_immediate_mask = 0;
C2_BASSERT(sizeof(c2_trace_immediate_mask) == 8);

static uint32_t           bufmask;
static struct c2_atomic64 cur;

#undef C2_TRACE_SUBSYS
#define C2_TRACE_SUBSYS(name, value) [value] = #name,
/** The array of subsystem names */
static const char *trace_subsys_str[] = {
	C2_TRACE_SUBSYSTEMS
};

extern int  c2_arch_trace_init(void);
extern void c2_arch_trace_fini(void);

int c2_trace_init(void)
{
	int psize;

	c2_atomic64_set(&cur, 0);

	C2_ASSERT(c2_is_po2(C2_TRACE_BUFSIZE));
	c2_logbufsize = C2_TRACE_BUFSIZE;
	bufmask = c2_logbufsize - 1;

	psize = c2_pagesize_get();
	C2_ASSERT((c2_logbufsize % psize) == 0);

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
	uint32_t header_len;
	uint32_t record_len;
	uint32_t pos_in_buf;
	uint32_t endpos_in_buf;
	uint64_t pos;
	uint64_t endpos;
	struct c2_trace_rec_header *header;
	register unsigned long sp asm ("sp"); /* stack pointer */

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
			memset(c2_logbuf + pos_in_buf, 0,
			       c2_logbufsize - pos_in_buf);
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
	if (C2_TRACE_IMMEDIATE_DEBUG &&
	    (td->td_subsys & c2_trace_immediate_mask))
		c2_trace_record_print(header, body);
}

static char *subsys_str(uint64_t subsys, char *buf)
{
	int i;
	char *s = buf;

	*s++ = '<';
	for (i = 0; i < ARRAY_SIZE(trace_subsys_str); i++, subsys >>= 1)
		*s++ = (subsys & 1) ? trace_subsys_str[i][0] :
		              tolower(trace_subsys_str[i][0]);
	*s++ = '>';
	*s = '\0';

	return buf;
}

void
c2_trace_record_print(const struct c2_trace_rec_header *trh, const void *buf)
{
	int i;
	const struct c2_trace_descr *td = trh->trh_descr;
	union {
		uint8_t  v8;
		uint16_t v16;
		uint32_t v32;
		uint64_t v64;
	} v[C2_TRACE_ARGC_MAX];
	char subsys_map_str[sizeof(uint64_t) * CHAR_BIT + 3];

	c2_console_printf("%8.8llu %15.15llu %5.5x %-18s %-20s "
			  "%15s:%-3i\n\t",
			  (unsigned long long)trh->trh_no,
			  (unsigned long long)trh->trh_timestamp,
			  (unsigned) (trh->trh_sp & 0xfffff),
			  subsys_str(td->td_subsys, subsys_map_str),
			  td->td_func, td->td_file, td->td_line);

	for (i = 0; i < td->td_nr; ++i) {
		const char *addr;

		addr = buf + td->td_offset[i];
		switch (td->td_sizeof[i]) {
		case 0:
			break;
		case 1:
			v[i].v8 = *(uint8_t *)addr;
			break;
		case 2:
			v[i].v16 = *(uint16_t *)addr;
			break;
		case 4:
			v[i].v32 = *(uint32_t *)addr;
			break;
		case 8:
			v[i].v64 = *(uint64_t *)addr;
			break;
		default:
			C2_IMPOSSIBLE("sizeof");
		}
	}
	c2_console_printf(td->td_fmt, v[0], v[1], v[2], v[3], v[4], v[5], v[6],
			  v[7], v[8]);
	c2_console_printf("\n");
}


void c2_console_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	c2_console_vprintf(fmt, ap);
	va_end(ap);
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
