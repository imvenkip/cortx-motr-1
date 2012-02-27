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
#include <unistd.h>   /* getpagesize */
#include <fcntl.h>    /* open, O_RDWR|O_CREAT|O_TRUNC */
#include <sys/mman.h> /* mmap */

#include "lib/arith.h"
#include "lib/memory.h"
#include "lib/trace.h"

/**
   @addtogroup trace

   <b>User-space c2_trace_parse() implementation.</b>

   @{
 */

static int logfd;

static const char sys_kern_randvspace_fname[] = "/proc/sys/kernel/randomize_va_space";


int c2_arch_trace_init()
{
	FILE *f;
	char buf[80];
	int res;
	int val;

	f = fopen(sys_kern_randvspace_fname, "r");
	if (f == NULL)
		err(EX_OSFILE, "open(\"%s\")", sys_kern_randvspace_fname);

	res = fscanf(f, "%d", &val);
	if (res != 1)
		err(EX_IOERR, "fread(\"%s\")", sys_kern_randvspace_fname);

	if (val != 0) {
		fprintf(stderr, "System configuration ERROR: "
		   "kernel.randomize_va_space should be set to 0.\n");
		errno = -EINVAL;
		goto out;
	}

	sprintf(buf, "c2.trace.%u", (unsigned)getpid());
	logfd = open(buf, O_RDWR|O_CREAT|O_TRUNC, 0700);
	if (logfd == -1)
		err(EX_CANTCREAT, "open(\"%s\")", buf);

	errno = posix_fallocate(logfd, 0, c2_logbufsize);
	if (errno != 0)
		err(EX_CANTCREAT, "fallocate(\"%s\", %u)", buf, c2_logbufsize);

	c2_logbuf = mmap(NULL, c2_logbufsize, PROT_WRITE,
		      MAP_SHARED, logfd, 0);
	if (c2_logbuf == MAP_FAILED)
		err(EX_OSERR, "mmap(\"%s\")", buf);

out:
	if (f != NULL)
		fclose(f);

	return -errno;
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
 */
int c2_trace_parse(void)
{
	struct c2_trace_rec_header   trh;
	const struct c2_trace_descr *td;
	unsigned                     pos = 0;
	unsigned                     nr;
	unsigned                     n2r;

	printf("   no   |    tstamp     |   stack ptr    |        func        |        src        | sz|narg\n");
	printf("-------------------------------------------------------------------------------------------\n");

	while (!feof(stdin)) {
		char *buf = NULL;

		pos = align(8, pos); /* At the beginning of a record */

		/* Find the complete record */
		do {
			nr = fread(&trh.trh_magic, 1, sizeof trh.trh_magic, stdin);
			if (nr != sizeof trh.trh_magic) {
				C2_ASSERT(feof(stdin));
				return 0;
			}
			pos += nr;
		} while (trh.trh_magic != C2_TRACE_MAGIC);

		/* Now we might have complete record */
		n2r = sizeof trh - sizeof trh.trh_magic;
		nr = fread(&trh.trh_sp, 1, n2r, stdin);
		C2_ASSERT(nr == n2r);
		pos += nr;

		td = trh.trh_descr;

		buf = c2_alloc(td->td_size);
		C2_ASSERT(buf != NULL);

		nr = fread(buf, 1, td->td_size, stdin);
		C2_ASSERT(nr == td->td_size);
		pos += nr;

		c2_trace_record_print(&trh, buf);

		if (buf)
			c2_free(buf);
	}
	return 0;
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
