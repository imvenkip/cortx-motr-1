/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 01/29/2012
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>      /* dup, dup2 */

#include "ut/ioredirect.h"
#include "lib/assert.h"  /* C2_ASSERT */


void redirect_std_stream(FILE *std_stream, const char *path, int *fd,
			 fpos_t *pos, FILE **new_stream)
{
	/**
	 * This solution is based on the method described in the comp.lang.c
	 * FAQ list, Question 12.34: "Once I've used freopen, how can I get the
	 * original stdout (or stdin) back?"
	 *
	 * http://c-faq.com/stdio/undofreopen.html
	 * http://c-faq.com/stdio/rd.kirby.c
	 *
	 * It's not portable and will only work on systems which support dup(2)
	 * and dup2(2) system calls (these are supported in Linux).
	 */
	fflush(std_stream);
	fgetpos(std_stream, pos);
	*fd = dup(fileno(std_stream));
	C2_ASSERT(*fd != -1);
	*new_stream = freopen(path, "a+", std_stream);
	C2_ASSERT(new_stream != NULL);
}

void restore_std_stream(FILE *std_stream, int std_fd, int fd, fpos_t *pos)
{
	int result;

	/*
	 * see comment in redirect_std_stream() for detailed information about
	 * how to redirect and restore standard streams
	 */
	fflush(std_stream);
	result = dup2(fd, std_fd);
	C2_ASSERT(result != -1);
	close(fd);
	clearerr(std_stream);
	fsetpos(std_stream, pos);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
