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
#include "lib/arith.h"  /* m0_align */
#include "lib/misc.h"   /* m0_short_file_name */
#include "lib/memory.h" /* m0_pagesize_get */
#include "lib/trace.h"
#include "lib/trace_internal.h"

#include "mero/magic.h"

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
 * them, m0_trace_parse() must be called in the same binary. See utils/ut_main.c
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
 * initialized by m0_trace_init().
 */
static char      bootbuf[4096];
void            *m0_logbuf     = bootbuf;
uint32_t         m0_logbufsize = sizeof bootbuf;
static uint32_t  bufmask       = sizeof bootbuf - 1;

unsigned long m0_trace_immediate_mask = 0;
M0_BASSERT(sizeof(m0_trace_immediate_mask) == 8);

unsigned int m0_trace_print_context = M0_TRACE_PCTX_SHORT;
unsigned int m0_trace_level         = M0_WARN | M0_ERROR | M0_FATAL;

static struct m0_atomic64 cur;

#undef M0_TRACE_SUBSYS
#define M0_TRACE_SUBSYS(name, value) [value] = #name,
/** The array of subsystem names */
static const char *trace_subsys_str[] = {
	M0_TRACE_SUBSYSTEMS
};

/** The array of trace level names */
static struct {
	const char          *name;
	enum m0_trace_level  level;
} trace_levels[] = {
	[0] = { .name = "NONE",   .level = M0_NONE   },
	[1] = { .name = "FATAL",  .level = M0_FATAL  },
	[2] = { .name = "ERROR",  .level = M0_ERROR  },
	[3] = { .name = "WARN",   .level = M0_WARN   },
	[4] = { .name = "NOTICE", .level = M0_NOTICE },
	[5] = { .name = "INFO",   .level = M0_INFO   },
	[6] = { .name = "DEBUG",  .level = M0_DEBUG  },
	[7] = { .name = "CALL",   .level = M0_CALL   },
};

/** Array of trace print context names */
static const char *trace_print_ctx_str[] = {
	[M0_TRACE_PCTX_NONE]  = "none",
	[M0_TRACE_PCTX_FUNC]  = "func",
	[M0_TRACE_PCTX_SHORT] = "short",
	[M0_TRACE_PCTX_FULL]  = "full",
};

M0_INTERNAL int m0_arch_trace_init(uint32_t logbuf_size);
M0_INTERNAL void m0_arch_trace_fini(void);

M0_INTERNAL int m0_trace_init(void)
{
	int rc;

	M0_PRE((m0_logbufsize % m0_pagesize_get()) == 0);
	M0_PRE(m0_is_po2(M0_TRACE_BUFSIZE));

	m0_atomic64_set(&cur, 0);

	rc = m0_arch_trace_init(M0_TRACE_BUFSIZE);
	if (rc == 0)
		bufmask = m0_logbufsize - 1;

	M0_POST((m0_logbufsize % m0_pagesize_get()) == 0);

	return rc;
}

M0_INTERNAL void m0_trace_fini(void)
{
	m0_arch_trace_fini();
	m0_logbuf = NULL;
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

M0_INTERNAL void m0_trace_allot(const struct m0_trace_descr *td,
				const void *body)
{
	uint32_t header_len;
	uint32_t record_len;
	uint32_t pos_in_buf;
	uint32_t endpos_in_buf;
	uint64_t pos;
	uint64_t endpos;
	struct m0_trace_rec_header *header;
	register unsigned long sp asm("sp"); /* stack pointer */

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

	header_len = m0_align(sizeof *header, 8);
	record_len = header_len + m0_align(td->td_size, 8);

	while (1) {
		endpos = m0_atomic64_add_return(&cur, record_len);
		pos    = endpos - record_len;
		pos_in_buf = pos & bufmask;
		endpos_in_buf = endpos & bufmask;
		/*
		 * The record should not cross the buffer.
		 */
		if (pos_in_buf > endpos_in_buf && endpos_in_buf) {
			memset(m0_logbuf + pos_in_buf, 0,
			       m0_logbufsize - pos_in_buf);
			memset(m0_logbuf, 0, endpos_in_buf);
		} else
			break;
	}

	header                = m0_logbuf + pos_in_buf;
	header->trh_magic     = 0;
	header->trh_no        = pos;
	header->trh_sp        = sp;
	header->trh_timestamp = rdtsc();
	header->trh_descr     = td;
	memcpy((void*)header + header_len, body, td->td_size);
	/** @todo put memory barrier here before writing the magic */
	header->trh_magic = M0_TRACE_MAGIC;
	if (M0_TRACE_IMMEDIATE_DEBUG &&
	    (td->td_subsys & m0_trace_immediate_mask ||
	     td->td_level & (M0_WARN|M0_ERROR|M0_FATAL)) &&
	    td->td_level & m0_trace_level)
		m0_trace_record_print(header, body);
}
M0_EXPORTED(m0_trace_allot);

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

static inline char *uppercase(char *s)
{
	char *p;

	for (p = s; *p != '\0'; ++p)
		*p = toupper(*p);

	return s;
}

static inline char *lowercase(char *s)
{
	char *p;

	for (p = s; *p != '\0'; ++p)
		*p = tolower(*p);

	return s;
}

static unsigned long subsys_name_to_mask(char *subsys_name)
{
	int            i;
	unsigned long  mask;

	/* uppercase subsys_name to match names in trace_subsys_str array */
	uppercase(subsys_name);

	for (mask = 0, i = 0; i < ARRAY_SIZE(trace_subsys_str); i++)
		if (strcmp(subsys_name, trace_subsys_str[i]) == 0) {
			mask = 1 << i;
			break;
		}

	return mask;
}

/**
 * Produces a numeric bitmask from a comma-separated list of subsystem names.
 * If '!' is present at the beginning of list, then mask is inverted
 *
 * @param subsys_names comma-separated list of subsystem names with optional
 *                     '!' at the beginning
 *
 * @param ret_mask     a pointer to a variable where to store mask, the stored
 *                     value is valid only if no error is returned
 *
 * @return 0 on success
 * @return -EINVAL on failure
 */
M0_INTERNAL int
m0_trace_subsys_list_to_mask(char *subsys_names, unsigned long *ret_mask)
{
	char          *p;
	char          *subsys = subsys_names;
	unsigned long  mask;
	unsigned long  m;

	/*
	 * there can be an optional '!' symbol at the beginning of mask,
	 * skip it if it present
	 */
	subsys = subsys_names[0] == '!' ? subsys_names + 1 : subsys_names;

	/*
	 * a special pseudo-subsystem 'all' represents all available subsystems;
	 * it's valid only when it's the only subsystem in a list
	 */
	if (strcmp(subsys, "all") == 0 || strcmp(subsys, "ALL") == 0) {
		mask = ~0UL;
		goto out;
	}

	mask = 0;
	p = subsys;

	while (p != NULL) {
		p = strchr(subsys, ',');
		if (p != NULL)
			*p++ = '\0';
		m = subsys_name_to_mask(subsys);
		if (m == 0) {
			m0_console_printf("mero: failed to initialize trace"
					  " immediate mask: subsystem '%s' not"
					  " found\n", lowercase(subsys));
			return -EINVAL;
		}
		mask |= m;
		subsys = p;
	}
out:
	/* invert mask if there is '!' at the beginning */
	*ret_mask = subsys_names[0] == '!' ? ~mask : mask;

	return 0;
}

static const char *trace_level_name(enum m0_trace_level level)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trace_levels); i++)
		if (level == trace_levels[i].level)
			return trace_levels[i].name;

	return NULL;
}

static enum m0_trace_level trace_level_value(char *level_name)
{
	int i;

	/* uppercase level name to match names in trace_levels array */
	uppercase(level_name);

	for (i = 0; i < ARRAY_SIZE(trace_levels); i++) {
		if (strcmp(level_name, trace_levels[i].name) == 0)
			return trace_levels[i].level;

	}

	return M0_NONE;
}

static enum m0_trace_level trace_level_value_plus(char *level_name)
{
	enum m0_trace_level  level = M0_NONE;
	size_t               n = strlen(level_name);
	bool                 is_plus_level = false;

	if (level_name[n - 1] == '+') {
		level_name[n - 1] = '\0';
		is_plus_level = true;
	}

	level = trace_level_value(level_name);
	if (level == M0_NONE)
		return M0_NONE;

	/*
	 * enable requested level and all other levels with higher precedance if
	 * it's a "plus" level, otherwise just the requested level
	 */
	return is_plus_level ? level | (level - 1) : level;
}

/**
 * Parses textual trace level specification and returns a corresponding
 * m0_trace_level enum value.
 *
 * @param str textual trace level specification in form "level[+][,level[+]]",
 *            where level is one of "call|debug|info|warn|error|fatal",
 *            for example: 'warn+' or 'debug', 'trace,warn,error'
 *
 * @return m0_trace_level enum value, on success
 * @return M0_NONE on failure
 */
M0_INTERNAL enum m0_trace_level m0_trace_parse_trace_level(char *str)
{
	char                *level_str = str;
	char                *p = level_str;
	enum m0_trace_level  level = M0_NONE;
	enum m0_trace_level  l;

	while (p != NULL) {
		p = strchr(level_str, ',');
		if (p != NULL)
			*p++ = '\0';
		l = trace_level_value_plus(level_str);
		if (l == M0_NONE) {
			m0_console_printf("mero: failed to initialize trace"
					  " level: no such level '%s'\n",
					  lowercase(level_str));
			return M0_NONE;
		}
		level |= l;
		level_str = p;
	}

	return level;
}

M0_INTERNAL enum m0_trace_print_context
m0_trace_parse_trace_print_context(const char *ctx_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trace_print_ctx_str); ++i)
		if (strcmp(ctx_name, trace_print_ctx_str[i]) == 0)
			return i;

	m0_console_printf("mero: failed to initialize trace print context:"
			  " invalid value '%s'\n", ctx_name);

	return M0_TRACE_PCTX_INVALID;
}

M0_INTERNAL void m0_trace_print_subsystems(void)
{
	int i;

	m0_console_printf("# YAML\n");
	m0_console_printf("---\n");
	m0_console_printf("trace_subsystems:\n");

	for (i = 0; i < ARRAY_SIZE(trace_subsys_str); i++)
		m0_console_printf("    - %s\n", trace_subsys_str[i]);
}

M0_INTERNAL void
m0_trace_record_print(const struct m0_trace_rec_header *trh, const void *buf)
{
	int i;
	const struct m0_trace_descr *td = trh->trh_descr;
	union {
		uint8_t  v8;
		uint16_t v16;
		uint32_t v32;
		uint64_t v64;
	} v[M0_TRACE_ARGC_MAX];
	char subsys_map_str[sizeof(uint64_t) * CHAR_BIT + 3];

	if (m0_trace_print_context == M0_TRACE_PCTX_FULL) {
		m0_console_printf("%8.8llu %15.15llu %5.5x %-18s %-7s %-20s "
				  "%15s:%-3i\n\t",
				  (unsigned long long)trh->trh_no,
				  (unsigned long long)trh->trh_timestamp,
				  (unsigned) (trh->trh_sp & 0xfffff),
				  subsys_str(td->td_subsys, subsys_map_str),
				  trace_level_name(td->td_level),
				  td->td_func, m0_short_file_name(td->td_file),
				  td->td_line);
	}

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
			M0_IMPOSSIBLE("sizeof");
		}
	}

	if (m0_trace_print_context == M0_TRACE_PCTX_SHORT)
		m0_console_printf("mero: %6s : [%s:%i:%s] ",
				  trace_level_name(td->td_level),
				  m0_short_file_name(td->td_file),
				  td->td_line, td->td_func);
	else if (m0_trace_print_context == M0_TRACE_PCTX_FUNC ||
		 (m0_trace_print_context == M0_TRACE_PCTX_NONE &&
		  (td->td_level == M0_CALL || td->td_level == M0_NOTICE)))
		m0_console_printf("mero: %s: ", td->td_func);
	else /* td->td_level == M0_TRACE_PCTX_NONE */
		m0_console_printf("mero: ");

	m0_console_printf(td->td_fmt, v[0], v[1], v[2], v[3], v[4], v[5], v[6],
			  v[7], v[8]);
	m0_console_printf("\n");
}


M0_INTERNAL void m0_console_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	m0_console_vprintf(fmt, ap);
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
