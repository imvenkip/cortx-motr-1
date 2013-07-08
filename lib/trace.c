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
#  include <linux/ctype.h>  /* tolower */
#  include <linux/sched.h>  /* current->pid */
#else
#  include <limits.h>       /* CHAR_BIT */
#  include <ctype.h>        /* tolower */
#  include <sys/types.h>
#  include <unistd.h>       /* getpid */
#endif
#include "lib/errno.h"
#include "lib/atomic.h"
#include "lib/arith.h"  /* m0_align */
#include "lib/misc.h"   /* m0_short_file_name, string.h */
#include "lib/memory.h" /* m0_pagesize_get */
#include "lib/string.h" /* m0_strdup */
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
M0_EXPORTED(m0_trace_immediate_mask);
M0_BASSERT(sizeof(m0_trace_immediate_mask) == 8);

unsigned int m0_trace_print_context = M0_TRACE_PCTX_SHORT;
M0_EXPORTED(m0_trace_print_context);

unsigned int m0_trace_level         = M0_WARN | M0_ERROR | M0_FATAL;
M0_EXPORTED(m0_trace_level);

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

#define NULL_STRING_STUB  "(null)"

static uint32_t calc_string_data_size(const struct m0_trace_descr *td,
				      const void *body)
{
	int       i;
	uint32_t  total_size = 0;

	for (i = 0; i < td->td_nr; ++i)
		if (td->td_isstr[i]) {
			char *s = *(char**)((char*)body + td->td_offset[i]) ?:
				  NULL_STRING_STUB;
			total_size += strlen(s) + 1;
		}

	return total_size;
}

static void copy_string_data(char *body, const struct m0_trace_descr *td)
{
	int   i;
	char *dst_str = body + m0_align(td->td_size, M0_TRACE_REC_ALIGN);

	for (i = 0; i < td->td_nr; ++i)
		if (td->td_isstr[i]) {
			char *src_str = *(char**)(body + td->td_offset[i]) ?:
					NULL_STRING_STUB;
			size_t str_len = strlen(src_str);
			memcpy(dst_str, src_str, str_len + 1);
			dst_str += str_len + 1;
		}
}

M0_INTERNAL void m0_trace_allot(const struct m0_trace_descr *td,
				const void *body)
{
	uint32_t  header_len;
	uint32_t  record_len;
	uint32_t  pos_in_buf;
	uint32_t  endpos_in_buf;
	uint64_t  pos;
	uint64_t  endpos;
	uint32_t  str_data_size;
	void     *body_in_buf;
	struct m0_trace_rec_header *header;
	register unsigned long sp asm("sp"); /* stack pointer */

	/*
	 * Allocate space in trace buffer to store trace record header
	 * (header_len bytes) and record payload (record_len bytes).
	 *
	 * Record and payload always start at 8-byte (M0_TRACE_REC_ALIGN)
	 * aligned address.
	 *
	 * Record payload consists of a body (printf arguments) and optional
	 * string data section, which starts immediately after body at the
	 * nearest 8-byte (M0_TRACE_REC_ALIGN) aligned address.
	 *
	 * String data section contains copies of data, pointed by any char*
	 * arguments, present in body.
	 *
	 * First free byte in the trace buffer is at "cur" offset. Note, that
	 * cur is not wrapped to 0 when the end of the buffer is reached (that
	 * would require additional synchronization between contending threads).
	 */

	header_len    = m0_align(sizeof *header, M0_TRACE_REC_ALIGN);
	str_data_size = calc_string_data_size(td, body);
	record_len    = header_len + m0_align(td->td_size, M0_TRACE_REC_ALIGN) +
			m0_align(str_data_size, M0_TRACE_REC_ALIGN);

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

	m0_trace_update_stats(record_len);

	header                = m0_logbuf + pos_in_buf;
	header->trh_magic     = 0;
#ifdef __KERNEL__
	header->trh_pid       = current->pid;
#else
	header->trh_pid       = getpid();
#endif
	header->trh_no        = pos;
	header->trh_sp        = sp;
	header->trh_timestamp = rdtsc();
	header->trh_descr     = td;
	header->trh_string_data_size = str_data_size;
	header->trh_record_size = record_len;
	body_in_buf           = (char*)header + header_len;

	memcpy(body_in_buf, body, td->td_size);

	if (str_data_size > 0)
		copy_string_data(body_in_buf, td);

	/** @todo put memory barrier here before writing the magic */
	header->trh_magic = M0_TRACE_MAGIC;

#ifdef ENABLE_IMMEDIATE_TRACE
	if ( (td->td_subsys & m0_trace_immediate_mask ||
	      td->td_level & (M0_WARN|M0_ERROR|M0_FATAL)) &&
	     td->td_level & m0_trace_level )
		m0_trace_record_print(header, body_in_buf);
#endif
}
M0_EXPORTED(m0_trace_allot);

M0_INTERNAL const char *m0_trace_subsys_name(uint64_t subsys)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trace_subsys_str); i++, subsys >>= 1)
		if (subsys & 1)
			return trace_subsys_str[i];

	return NULL;
}
M0_EXPORTED(m0_trace_subsys_name);

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

M0_INTERNAL const char *m0_trace_level_name(enum m0_trace_level level)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trace_levels); i++)
		if (level == trace_levels[i].level)
			return trace_levels[i].name;

	return NULL;
}
M0_EXPORTED(m0_trace_level_name);

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
M0_EXPORTED(m0_trace_parse_trace_level);

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

M0_INTERNAL int m0_trace_set_print_context(const char *ctx_name)
{
	enum m0_trace_print_context ctx;

	if (ctx_name == NULL)
		return 0;

	ctx = m0_trace_parse_trace_print_context(ctx_name);
	if (ctx == M0_TRACE_PCTX_INVALID)
		return -EINVAL;

	m0_trace_print_context = ctx;
#ifdef __KERNEL__
	pr_info("Mero trace print context: %s\n", ctx_name);
#endif
	return 0;
}
M0_EXPORTED(m0_trace_set_print_context);

M0_INTERNAL int m0_trace_set_level(const char *level_str)
{
	unsigned int  level;
	char         *level_str_copy;

	if (level_str == NULL)
		return 0;

	level_str_copy = m0_strdup(level_str);
	if (level_str_copy == NULL)
		return -ENOMEM;

	level = m0_trace_parse_trace_level(level_str_copy);
	m0_free(level_str_copy);

	if (level == M0_NONE) {
		m0_console_printf("mero: incorrect trace level specification,"
			" it should be in form of 'level[+][,level[+]]'"
			" where 'level' is one of call|debug|info|notice|warn|"
			"error|fatal");
		return -EINVAL;
	}

	m0_trace_level = level;
#ifdef __KERNEL__
	pr_info("Mero trace level: %s\n", level_str);
#endif
	return 0;
}
M0_EXPORTED(m0_trace_set_level);

M0_INTERNAL void *m0_trace_get_logbuf_addr(void)
{
	return m0_logbuf;
}
M0_EXPORTED(m0_trace_get_logbuf_addr);

M0_INTERNAL uint32_t m0_trace_get_logbuf_size(void)
{
	return m0_logbufsize;
}
M0_EXPORTED(m0_trace_get_logbuf_size);

M0_INTERNAL uint64_t m0_trace_get_logbuf_pos(void)
{
	return m0_atomic64_get(&cur);
}
M0_EXPORTED(m0_trace_get_logbuf_pos);

M0_INTERNAL void m0_trace_print_subsystems(void)
{
	int i;

	m0_console_printf("# YAML\n");
	m0_console_printf("---\n");
	m0_console_printf("trace_subsystems:\n");

	for (i = 0; i < ARRAY_SIZE(trace_subsys_str); i++)
		m0_console_printf("    - %s\n", trace_subsys_str[i]);
}

M0_INTERNAL void m0_trace_unpack_args(const struct m0_trace_rec_header *trh,
				      m0_trace_rec_args_t args,
				      const void *buf)
{
	int         i;
	size_t      total_str_len = 0;
	const char *str_data = NULL;
	const struct m0_trace_descr *td = trh->trh_descr;

	if (trh->trh_string_data_size != 0)
		str_data = (char*)buf + m0_align(td->td_size, M0_TRACE_REC_ALIGN);

	for (i = 0; i < td->td_nr; ++i) {
		const char *addr;

		addr = buf + td->td_offset[i];
		switch (td->td_sizeof[i]) {
		case 0:
			break;
		case 1:
			args[i].v8 = *(uint8_t *)addr;
			break;
		case 2:
			args[i].v16 = *(uint16_t *)addr;
			break;
		case 4:
			args[i].v32 = *(uint32_t *)addr;
			break;
		case 8:
			args[i].v64 = *(uint64_t *)addr;
			break;
		default:
			M0_IMPOSSIBLE("sizeof");
		}

		if (td->td_isstr[i]) {
			size_t str_len = strlen(str_data);
			total_str_len += str_len + 1;

			if (total_str_len > trh->trh_string_data_size)
				m0_arch_panic("trace record string data is invalid",
					      __func__, __FILE__, __LINE__);

			args[i].v64 = (uint64_t)str_data;
			str_data += str_len + 1;
		}
	}
}

M0_INTERNAL void
m0_trace_record_print(const struct m0_trace_rec_header *trh, const void *buf)
{
	const struct m0_trace_descr *td = trh->trh_descr;
	m0_trace_rec_args_t          args;

	/* there are 64 possible subsystems, so we need one byte for each
	 * subsystem name plus two bytes for '<' and '>' symbols around them and
	 * one byte for '\0' and the end */
	char subsys_map_str[sizeof(uint64_t) * CHAR_BIT + 3];

	m0_trace_unpack_args(trh, args, buf);

	if (m0_trace_print_context == M0_TRACE_PCTX_FULL) {
		m0_console_printf("%5.5u %8.8llu %15.15llu %5.5x %-18s %-7s "
				  "%-20s %s:%-3i\n\t",
				  trh->trh_pid,
				  (unsigned long long)trh->trh_no,
				  (unsigned long long)trh->trh_timestamp,
				  (unsigned) (trh->trh_sp & 0xfffff),
				  subsys_str(td->td_subsys, subsys_map_str),
				  m0_trace_level_name(td->td_level),
				  td->td_func, m0_short_file_name(td->td_file),
				  td->td_line);
	}

	if (m0_trace_print_context == M0_TRACE_PCTX_SHORT)
		m0_console_printf("mero: %6s : [%s:%i:%s] ",
				  m0_trace_level_name(td->td_level),
				  m0_short_file_name(td->td_file),
				  td->td_line, td->td_func);
	else if (m0_trace_print_context == M0_TRACE_PCTX_FUNC ||
		 (m0_trace_print_context == M0_TRACE_PCTX_NONE &&
		  (td->td_level == M0_CALL || td->td_level == M0_NOTICE)))
		m0_console_printf("mero: %s: ", td->td_func);
	else /* td->td_level == M0_TRACE_PCTX_NONE
		|| td->td_level == M0_TRACE_PCTX_FULL */
		m0_console_printf("mero: ");

	m0_console_printf(td->td_fmt, args[0], args[1], args[2], args[3],
				      args[4], args[5], args[6], args[7],
				      args[8]);

	if (td->td_fmt[strlen(td->td_fmt) - 1] != '\n')
		m0_console_printf("\n");
}

M0_INTERNAL void m0_console_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	m0_console_vprintf(fmt, ap);
	va_end(ap);
}

/*
 * escape any occurrence of single-quote characters (') inside string by
 * duplicating them, for example "it's a pen" becomes "it''s pen"
 */
static int escape_yaml_str(char *str, size_t max_size)
{
	size_t   str_len    = strlen(str);
	ssize_t  free_space = max_size - str_len;
	char    *p          = str;;

	if (free_space < 0)
		return -EINVAL;

	while ((p = strchr(p, '\'')) != NULL)
	{
		if (--free_space < 0)
			return -ENOMEM;
		memmove(p + 1, p, str_len - (p - str) + 1);
		*p = '\'';
		p += 2;
	}

	return 0;
}

M0_INTERNAL
int  m0_trace_record_print_yaml(char *outbuf, size_t outbuf_size,
				const struct m0_trace_rec_header *trh,
				const void *tr_body, bool yaml_stream_mode)
{
	const struct m0_trace_descr *td = trh->trh_descr;
	m0_trace_rec_args_t          args;
	const char                  *td_fmt;
	static char                  msg_buf[8 * 1024]; /* 8 KB */
	size_t                       outbuf_used = 0;
	int                          rc;

	msg_buf[0] = '\0';
	m0_trace_unpack_args(trh, args, tr_body);

	td_fmt = yaml_stream_mode ? "---\n"
				    "record_num: %" PRIu64 "\n"
				    "timestamp:  %" PRIu64 "\n"
				    "pid:        %u\n"
				    "stack_addr: %" PRIx64 "\n"
				    "subsystem:  %s\n"
				    "level:      %s\n"
				    "func:       %s\n"
				    "file:       %s\n"
				    "line:       %u\n"
				    "msg:        '"

				  : "  - record_num: %" PRIu64 "\n"
				    "    timestamp:  %" PRIu64 "\n"
				    "    pid:        %u\n"
				    "    stack_addr: %" PRIx64 "\n"
				    "    subsystem:  %s\n"
				    "    level:      %s\n"
				    "    func:       %s\n"
				    "    file:       %s\n"
				    "    line:       %u\n"
				    "    msg:        '";

	outbuf_used += snprintf(outbuf, outbuf_size, td_fmt,
				trh->trh_no,
				trh->trh_timestamp,
				trh->trh_pid,
				/* TODO: add comment why mask is needed */
				(trh->trh_sp & 0xfffff),
				m0_trace_subsys_name(td->td_subsys),
				m0_trace_level_name(td->td_level),
				td->td_func,
				td->td_file,
				td->td_line);
	if (outbuf_used >= outbuf_size)
		return -ENOBUFS;

	rc = snprintf(msg_buf, sizeof msg_buf, td->td_fmt, args[0], args[1],
		      args[2], args[3], args[4], args[5], args[6], args[7],
		      args[8]);
	if (rc > sizeof msg_buf)
		m0_console_printf("mero: %s: 'msg' is too big and has been"
				  " truncated to %zu bytes",
				  __func__, sizeof msg_buf);

	rc = escape_yaml_str(msg_buf, sizeof msg_buf);
	if (rc != 0)
		m0_console_printf("mero: %s: failed to escape single quote"
				  " characters in msg: %s", __func__, msg_buf);

	outbuf_used += snprintf(outbuf + outbuf_used, outbuf_size - outbuf_used,
				"%s'\n", msg_buf);
	if (outbuf_used >= outbuf_size)
		return -ENOBUFS;

	return 0;
}
M0_EXPORTED(m0_trace_record_print_yaml);

M0_INTERNAL const struct m0_trace_rec_header *m0_trace_get_last_record(void)
{
	char *curptr = (char*)m0_logbuf +
				m0_trace_get_logbuf_pos() % m0_logbufsize;
	char *p = curptr;

	while (p - (char*)m0_logbuf > sizeof (struct m0_trace_rec_header)) {
		p -= M0_TRACE_REC_ALIGN;
		if (*((uint64_t*)p) == M0_TRACE_MAGIC)
			return (const struct m0_trace_rec_header*)p;
	}

	p = (char*)m0_logbuf + m0_logbufsize;

	while (p >= curptr) {
		p -= M0_TRACE_REC_ALIGN;
		if (*((uint64_t*)p) == M0_TRACE_MAGIC)
			return (const struct m0_trace_rec_header*)p;
	}

	return NULL;
}
M0_EXPORTED(m0_trace_get_last_record);

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
