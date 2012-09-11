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

#include <string.h>   /* memset */
#include <errno.h>
#include <err.h>
#include <sysexits.h>
#include <stdio.h>
#include <stdlib.h>   /* getenv, strtoul */
#include <unistd.h>   /* getpagesize */
#include <fcntl.h>    /* open, O_RDWR|O_CREAT|O_TRUNC */
#include <sys/mman.h> /* mmap */

#include "lib/arith.h"
#include "lib/memory.h"
#include "lib/trace.h"

#include "colibri/magic.h"

/**
   @addtogroup trace

   <b>User-space c2_trace_parse() implementation.</b>

   @{
 */

static int logfd;

static const char sys_kern_randvspace_fname[] =
	"/proc/sys/kernel/randomize_va_space";

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

static int logbuf_map()
{
	char buf[80];

	sprintf(buf, "c2.trace.%u", (unsigned)getpid());
	if ((logfd = open(buf, O_RDWR|O_CREAT|O_TRUNC, 0700)) == -1)
		warn("open(\"%s\")", buf);
	else if ((errno = posix_fallocate(logfd, 0, c2_logbufsize)) != 0)
		warn("fallocate(\"%s\", %u)", buf, c2_logbufsize);
	else if ((c2_logbuf = mmap(NULL, c2_logbufsize, PROT_WRITE,
                                   MAP_SHARED, logfd, 0)) == MAP_FAILED)
		warn("mmap(\"%s\")", buf);
	else
		memset(c2_logbuf, 0, c2_logbufsize);

	return -errno;
}

int c2_arch_trace_init()
{
	const char *mask;

	mask = getenv("C2_TRACE_IMMEDIATE_MASK");
	if (mask != NULL) {
		char *endp;

		c2_trace_immediate_mask = strtoul(mask, &endp, 0);
		if (errno != 0 || *endp != 0) {
			warn("strtoul(\"%s\"), setting mask to 0", mask);
			c2_trace_immediate_mask = 0;
		}
	}
	return randvspace_check() ?: logbuf_map();
}

void c2_arch_trace_fini(void)
{
	munmap(c2_logbuf, c2_logbufsize);
	close(logfd);
}


static unsigned align(unsigned align, unsigned pos)
{
	C2_ASSERT(c2_is_po2(align));
	while (!feof(stdin) && (pos & (align - 1))) {
		getchar();
		pos++;
	}
	return pos;
}

/**
 * Parse log buffer supplied at stdin.
 *
 * Returns sysexits.h error codes.
 */
int c2_trace_parse(void)
{
	struct c2_trace_rec_header   trh;
	const struct c2_trace_descr *td;
	unsigned                     pos = 0;
	unsigned                     nr;
	unsigned                     n2r;
	int                          size;

	printf("   no   |    tstamp     |stack|       subsys     |"
	       "        func        |        src        \n");
	printf("--------------------------------------------------"
	       "----------------------------------------\n");

	while (!feof(stdin)) {
		char *buf;

		pos = align(8, pos); /* At the beginning of a record */
		/* Find the complete record */
		do {
			nr = fread(&trh.trh_magic, 1,
				   sizeof trh.trh_magic, stdin);
			if (nr != sizeof trh.trh_magic) {
				if (!feof(stdin))
					errx(EX_DATAERR,
					     "Got %u bytes of magic", nr);
				return EX_OK;
			}
			pos += nr;
		} while (trh.trh_magic != C2_TRACE_MAGIC);

		/* Now we might have complete record */
		n2r = sizeof trh - sizeof trh.trh_magic;
		nr  = fread(&trh.trh_sp, 1, n2r, stdin);
		if (nr != n2r)
			errx(EX_DATAERR, "Got %u bytes of record (need %u)",
			     nr, n2r);
		pos += nr;
		td   = trh.trh_descr;
		size = td->td_size;
		buf  = c2_alloc(size);
		if (buf == NULL)
			err(EX_TEMPFAIL, "Cannot allocate %i bytes", size);
		nr = fread(buf, 1, size, stdin);
		if (nr != size)
			errx(EX_DATAERR, "Got %u bytes of data (need %i)",
			     nr, size);
		pos += nr;
		c2_trace_record_print(&trh, buf);
		c2_free(buf);
	}
	return EX_OK;
}

void c2_console_vprintf(const char *fmt, va_list args)
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
