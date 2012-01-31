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

#include <stdio.h>    /* getchar */

#include "lib/arith.h"
#include "lib/memory.h"
#include "lib/trace.h"

/**
   @addtogroup trace

   <b>User-space trace_parse() implementation.</b>

   @{
 */


static void align(unsigned align)
{
	C2_ASSERT(c2_is_po2(align));
	while (ftell(stdin) & (align - 1))
		getchar();
}

/**
 * Parse log buffer supplied at stdin.
 */
int c2_trace_parse(void)
{
	uint64_t                     magic;
	uint64_t                     no;
	uint64_t                     timestamp;
	const struct c2_trace_descr *td;
	int                          nr;
	int                          i;
	char                        *buf;
	union {
		uint8_t  v8;
		uint16_t v16;
		uint32_t v32;
		uint64_t v64;
	} v[C2_TRACE_ARGC_MAX];

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

		printf("%10.10lu  %10.10lu  %15s %15s %4i %3.3i %i\n\t",
		       no, timestamp, td->td_func, td->td_file,
		       td->td_line, td->td_size, td->td_nr);
		align(8);
		C2_ASSERT(td->td_nr <= C2_TRACE_ARGC_MAX);

		buf = c2_alloc(td->td_size);
		C2_ASSERT(buf != NULL);

		nr = fread(buf, 1, td->td_size, stdin);
		C2_ASSERT(nr == td->td_size);

		for (i = 0; i < td->td_nr; ++i) {
			char *addr;

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
		printf(td->td_fmt, v[0], v[1], v[2], v[3], v[4], v[5], v[6],
		       v[7], v[8]);

		c2_free(buf);
		align(8);
		printf("\n");
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
