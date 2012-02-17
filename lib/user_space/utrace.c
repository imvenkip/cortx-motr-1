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
#include <unistd.h>   /* getpagesize */
#include <fcntl.h>    /* open, O_RDWR|O_CREAT|O_TRUNC */
#include <sys/mman.h> /* mmap */
#include <sys/syscall.h>
#include <linux/sysctl.h>

#include "lib/arith.h"
#include "lib/memory.h"
#include "lib/trace.h"

/**
   @addtogroup trace

   <b>User-space c2_trace_parse() implementation.</b>

   @{
 */

static int read_count;
static int logfd;

extern void *c2_logbuf;

int c2_arch_trace_init()
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
		return -EINVAL;
	}

	errno = 0;
	logfd = open("c2.trace", O_RDWR|O_CREAT|O_TRUNC, 0700);
	if (logfd != -1) {
		if (ftruncate(logfd, C2_TRACE_BUFSIZE) == 0) {
			c2_logbuf = mmap(NULL, C2_TRACE_BUFSIZE, PROT_WRITE,
				      MAP_SHARED, logfd, 0);
			if (c2_logbuf == MAP_FAILED)
				perror("mmap");
		}
	} else {
		perror("open");
	}

	return -errno;
}

void c2_arch_trace_fini(void)
{
	munmap(c2_logbuf, C2_TRACE_BUFSIZE);
	close(logfd);
}


static void align(unsigned align)
{
	int pos = read_count ? read_count - 1 : 0;
	C2_ASSERT(c2_is_po2(align));
	while (!feof(stdin) && (pos & (align - 1))) {
		getchar();
		pos++;
	}
	read_count = pos + 1;
}

/**
 * Parse log buffer supplied at stdin.
 */
int c2_trace_parse(void)
{
	struct c2_trace_rec_header   trh;
	const struct c2_trace_descr *td;
	int                          nr, n2r;

	read_count = 0;

	printf("  no   |    tstamp     |   stack ptr    |        func        |        src        | sz|narg\n");
	printf("------------------------------------------------------------------------------------------\n");

	while (!feof(stdin)) {
		char *buf = NULL;

		align(8); /* At the beginning of a record */

		/* Find the complete record */
		do {
			nr = fread(&trh.trh_magic, 1, sizeof trh.trh_magic, stdin);
			if (nr != sizeof trh.trh_magic) {
				C2_ASSERT(feof(stdin));
				return 0;
			}
			read_count += nr;
		} while (trh.trh_magic != C2_TRACE_MAGIC);

		/* Now we might have complete record */
		n2r = sizeof trh - sizeof trh.trh_magic;
		nr = fread(&trh.trh_sp, 1, n2r, stdin);
		C2_ASSERT(nr == n2r);
		read_count += nr;

		td = trh.trh_descr;

		buf = c2_alloc(td->td_size);
		C2_ASSERT(buf != NULL);

		nr = fread(buf, 1, td->td_size, stdin);
		C2_ASSERT(nr == td->td_size);
		read_count += nr;

		c2_trace_print_record(&trh, buf);

		if (buf)
			c2_free(buf);
		align(8);
	}
	return 0;
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
