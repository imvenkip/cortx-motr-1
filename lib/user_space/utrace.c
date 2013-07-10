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

#include <string.h>   /* memset, strlen */
#include <errno.h>
#include <err.h>
#include <sysexits.h>
#include <stdio.h>
#include <stdlib.h>   /* getenv, strtoul */
#include <unistd.h>   /* getpagesize */
#include <fcntl.h>    /* open, O_RDWR|O_CREAT|O_TRUNC */
#include <sys/mman.h> /* mmap */
#include <limits.h>   /* CHAR_BIT */

#include "lib/types.h"
#include "lib/arith.h"
#include "lib/memory.h"
#include "lib/trace.h"
#include "lib/trace_internal.h"

#include "mero/magic.h"

/**
   @addtogroup trace

   <b>User-space m0_trace_parse() implementation.</b>

   @{
 */

static int logfd;

static const char sys_kern_randvspace_fname[] =
	"/proc/sys/kernel/randomize_va_space";

static bool use_mmaped_buffer = true;

static int randvspace_check()
{
	int   val;
	int   result;
	FILE *f;

	if ((f = fopen(sys_kern_randvspace_fname, "r")) == NULL) {
		warn("open(\"%s\")", sys_kern_randvspace_fname);
		result = -errno;
	} else if (fscanf(f, "%d", &val) != 1) {
		warnx("fscanf(\"%s\")", sys_kern_randvspace_fname);
		result = -EINVAL;
	} else if (val != 0) {
		warnx("System configuration ERROR: "
		      "kernel.randomize_va_space should be set to 0.");
		result = -EINVAL;
	} else
		result = 0;

	if (f != NULL)
		fclose(f);

	return result;
}

static int logbuf_map(uint32_t logbuf_size)
{
	char buf[80];

	sprintf(buf, "m0.trace.%u", (unsigned)getpid());
	if ((logfd = open(buf, O_RDWR|O_CREAT|O_TRUNC, 0700)) == -1)
		warn("open(\"%s\")", buf);
	else if ((errno = posix_fallocate(logfd, 0, logbuf_size)) != 0)
		warn("fallocate(\"%s\", %u)", buf, logbuf_size);
	else if ((m0_logbuf = mmap(NULL, logbuf_size, PROT_WRITE,
                                   MAP_SHARED, logfd, 0)) == MAP_FAILED)
		warn("mmap(\"%s\")", buf);
	else {
		m0_logbufsize = logbuf_size;
		memset(m0_logbuf, 0, m0_logbufsize);
	}

	return -errno;
}

M0_INTERNAL int m0_trace_set_print_context(const char *ctx_name)
{
	if (ctx_name != NULL) {
		enum m0_trace_print_context ctx =
			m0_trace_parse_trace_print_context(ctx_name);

		if (ctx == M0_TRACE_PCTX_INVALID)
			return -EINVAL;

		m0_trace_print_context = ctx;
	}

	return 0;
}

M0_INTERNAL int m0_trace_set_immediate_mask(const char *mask)
{
	if (mask != NULL) {
		char *endp;

		m0_trace_immediate_mask = strtoul(mask, &endp, 0);

		/*
		 * if mask string fails to convert to a number cleanly, then
		 * assume that mask string contains a comma separated list of
		 * subsystem names, which we use to build a numeric mask
		 */
		if (errno != 0 || *endp != 0) {
			unsigned long  m = 0;
			int            rc;
			char          *s = strdup(mask);

			if (s == NULL)
				return -ENOMEM;

			rc = m0_trace_subsys_list_to_mask(s, &m);
			free(s);

			if (rc != 0)
				return rc;

			m0_trace_immediate_mask = m;
		}
	}

	return 0;
}

M0_INTERNAL int m0_trace_set_level(const char *level)
{
	if (level != NULL) {
		char *s = strdup(level);
		if (s == NULL)
			return -ENOMEM;
		m0_trace_level = m0_trace_parse_trace_level(s);
		free(s);
		if (m0_trace_level == M0_NONE)
			return -EINVAL;
	}

	return 0;
}

M0_INTERNAL void m0_trace_set_mmapped_buffer(bool val)
{
	use_mmaped_buffer = val;
}

M0_INTERNAL bool m0_trace_use_mmapped_buffer(void)
{
	return use_mmaped_buffer;
}

M0_INTERNAL int m0_arch_trace_init(uint32_t logbuf_size)
{
	int         rc;
	const char *var;

	var = getenv("M0_TRACE_IMMEDIATE_MASK");
	rc = m0_trace_set_immediate_mask(var);
	if (rc != 0)
		return rc;

	var = getenv("M0_TRACE_LEVEL");
	rc = m0_trace_set_level(var);
	if (rc != 0)
		return rc;

	var = getenv("M0_TRACE_PRINT_CONTEXT");
	rc = m0_trace_set_print_context(var);
	if (rc != 0)
		return rc;

	setlinebuf(stdout);

	rc = randvspace_check();
	if (rc != 0)
		return rc;

	return m0_trace_use_mmapped_buffer() ? logbuf_map(logbuf_size) : 0;
}

M0_INTERNAL void m0_arch_trace_fini(void)
{
	munmap(m0_logbuf, m0_logbufsize);
	close(logfd);
}


static unsigned align(FILE *file, unsigned align, unsigned pos)
{
	M0_ASSERT(m0_is_po2(align));
	while (!feof(file) && (pos & (align - 1))) {
		fgetc(file);
		pos++;
	}
	return pos;
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
void m0_trace_record_print_yaml(FILE *output_file,
				const struct m0_trace_rec_header *trh,
				const void *buf,
				bool stream_mode)
{
	const struct m0_trace_descr *td = trh->trh_descr;
	m0_trace_rec_args_t          args;
	const char                  *td_fmt;
	static char                  msg_buf[32 * 1024]; /* 32 KB */
	int                          rc;

	m0_trace_unpack_args(trh, args, buf);

	td_fmt = stream_mode ? "---\n"
			       "record_num: %" PRIu64 "\n"
			       "timestamp:  %" PRIu64 "\n"
			       "pid:        %u\n"
			       "stack_addr: %" PRIx64 "\n"
			       "subsystem:  %s\n"
			       "level:      %s\n"
			       "func:       %s\n"
			       "file:       %s\n"
			       "line:       %u\n"

			     : "  - record_num: %" PRIu64 "\n"
			       "    timestamp:  %" PRIu64 "\n"
			       "    pid:        %u\n"
			       "    stack_addr: %" PRIx64 "\n"
			       "    subsystem:  %s\n"
			       "    level:      %s\n"
			       "    func:       %s\n"
			       "    file:       %s\n"
			       "    line:       %u\n";

	fprintf(output_file, td_fmt,
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

	if (stream_mode)
		fprintf(output_file, "msg:        '");
	else
		fprintf(output_file, "    msg:        '");

	rc = snprintf(msg_buf, sizeof msg_buf, td->td_fmt, args[0], args[1],
		      args[2], args[3], args[4], args[5], args[6], args[7],
		      args[8]);
	if (rc > sizeof msg_buf)
		warnx("'msg' is too big and has been truncated to %zu bytes",
		      sizeof msg_buf);

	rc = escape_yaml_str(msg_buf, sizeof msg_buf);
	if (rc != 0)
		warnx("Failed to escape single quote characters in msg: %s",
		      msg_buf);

	fprintf(output_file, "%s'\n", msg_buf);
}

/**
 * Parse log buffer from file.
 *
 * Returns sysexits.h error codes.
 */
M0_INTERNAL int m0_trace_parse(FILE *trace_file, FILE *output_file,
			       bool yaml_stream_mode)
{
	struct m0_trace_rec_header   trh;
	const struct m0_trace_descr *td;
	unsigned                     pos = 0;
	unsigned                     nr;
	unsigned                     n2r;
	int                          size;

	if (!yaml_stream_mode)
		fprintf(output_file, "trace_records:\n");

	while (!feof(trace_file)) {
		char *buf;

		/* At the beginning of a record */
		pos = align(trace_file, M0_TRACE_REC_ALIGN, pos);

		/* Find the complete record */
		do {
			nr = fread(&trh.trh_magic, 1,
				   sizeof trh.trh_magic, trace_file);

			if (nr != sizeof trh.trh_magic) {
				if (!feof(trace_file)) {
					warnx("Got %u bytes of magic instead"
					      " of %zu", nr,
					      sizeof trh.trh_magic);
					return EX_DATAERR;
				}
				return EX_OK;
			}

			pos += nr;
		} while (trh.trh_magic != M0_TRACE_MAGIC);

		/* Now we might have complete record */
		n2r = sizeof trh - sizeof trh.trh_magic;
		nr  = fread(&trh.trh_sp, 1, n2r, trace_file);
		if (nr != n2r) {
			warnx("Got %u bytes of record (need %u)", nr, n2r);
			return EX_DATAERR;
		}
		pos += nr;

		td   = trh.trh_descr;
		if (td->td_magic != M0_TRACE_DESCR_MAGIC) {
			warnx("Invalid trace descriptor (most probably input"
			      " trace log was produced by different version of"
			      " Mero)");
			return EX_TEMPFAIL;
		}
		size = m0_align(td->td_size + trh.trh_string_data_size,
				M0_TRACE_REC_ALIGN);

		buf  = m0_alloc(size);
		if (buf == NULL) {
			warn("Failed to allocate %i bytes of memory", size);
			return EX_TEMPFAIL;
		}

		nr = fread(buf, 1, size, trace_file);
		if (nr != size) {
			warnx("Got %u bytes of data (need %i)", nr, size);
			return EX_DATAERR;
		}
		pos += nr;

		m0_trace_record_print_yaml(output_file, &trh, buf,
					   yaml_stream_mode);
		m0_free(buf);
	}
	return EX_OK;
}

M0_INTERNAL void m0_console_vprintf(const char *fmt, va_list args)
{
	vprintf(fmt, args);
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
