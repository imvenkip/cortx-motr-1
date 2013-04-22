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
 * Original author: Nachiket Sahasrabudhe <Nachiket_Sahasrabudhe@xyratex.com>
 * Original creation date: 19/07/2012
 */

#include <setjmp.h>        /* setjmp() and longjmp() */

#include "lib/thread.h"
#include "lib/misc.h"      /* M0_SET0 */
#include "lib/errno.h"     /* errno */
#include "lib/assert.h"    /* m0_panic */

/**
   @addtogroup cookie
   @{
 */

static pthread_key_t addr_check_key;

/**
 * Signal handler for SIGSEGV.
 */
static void sigsegv(int sig)
{
	jmp_buf *buf;

	buf = pthread_getspecific(addr_check_key);
	if (buf != NULL)
		longjmp(*buf, 1);
	else
		m0_panic("sigsegv", "unknown", "unknown", 0);
}

/**
 * Checks the validity of an address by dereferencing the same. Occurrence of
 * an error in case of an invalid address gets handled by the
 * function sigsegv().
 */
M0_INTERNAL bool m0_arch_addr_is_sane(const void *addr)
{
	jmp_buf           buf;
	volatile uint64_t dummy;
	int               ret;
	bool              result;

	ret = pthread_setspecific(addr_check_key, &buf);
	M0_ASSERT(ret == 0);
	ret = setjmp(buf);
	if (ret == 0) {
		dummy = *(uint64_t *)addr;
		result = true;
	} else
		result = false;
	ret = pthread_setspecific(addr_check_key, NULL);
	M0_ASSERT(ret == 0);
	return result;
}

/**
 * Sets up the signal handler for SIGSEGV to the function sigsegv.
 * Creates a pthread_key to be used in the function m0_arch_addr_is_sane.
 */
M0_INTERNAL int m0_arch_cookie_global_init(void)
{
	int		 ret;
	struct sigaction sa_sigsegv;

	M0_SET0(&sa_sigsegv);
	sa_sigsegv.sa_handler = sigsegv;
	sa_sigsegv.sa_flags = SA_NODEFER;
	ret = sigemptyset(&sa_sigsegv.sa_mask);
	if (ret == -1)
		return -errno;
	ret = sigaction(SIGSEGV, &sa_sigsegv, NULL);
	if (ret == -1)
		return -errno;
	return -pthread_key_create(&addr_check_key, NULL);
}

/**
 * Sets the signal handler for SIGSEGV to the default signal handler. Deletes
 * pthread_key.
 */
M0_INTERNAL void m0_arch_cookie_global_fini(void)
{
	struct sigaction sa_sigsegv;
	int              ret;

	M0_SET0(&sa_sigsegv);
	sa_sigsegv.sa_handler = SIG_DFL;
	ret = sigemptyset(&sa_sigsegv.sa_mask);
	M0_ASSERT(ret == 0);
	ret = sigaction(SIGSEGV, &sa_sigsegv, NULL);
	M0_ASSERT(ret == 0);
	ret = pthread_key_delete(addr_check_key);
	M0_ASSERT(ret == 0);
}

/** @} end of cookie group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
