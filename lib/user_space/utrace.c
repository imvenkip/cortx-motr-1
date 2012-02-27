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
#include <stdio.h>
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

static int randvspace_check()
{
	int val;
	FILE *f;

	if ((f = fopen(sys_kern_randvspace_fname, "r")) == NULL) {
		warn("open(\"%s\")", sys_kern_randvspace_fname);
	} else if (fscanf(f, "%d", &val) != 1) {
		warnx("fscanf(\"%s\")", sys_kern_randvspace_fname);
		errno = EINVAL;
	} else if (val != 0) {
		warnx("System configuration ERROR: "
		      "kernel.randomize_va_space should be set to 0.");
		errno = EINVAL;
	}

	if (f != NULL)
		fclose(f);

	return -errno;
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

	return -errno;
}

int c2_arch_trace_init()
{
	int res;

	if ((res = randvspace_check()) != 0)
		return res;
	else if ((res = logbuf_map()) != 0)
		return res;
	else
		return 0;
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
