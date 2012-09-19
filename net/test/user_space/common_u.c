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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 09/15/2012
 */

#include <string.h>			/* strlen */

#include "lib/misc.h"			/* C2_SET0 */
#include "lib/assert.h"			/* C2_ASSERT */
#include "lib/memory.h"			/* c2_alloc */
#include "net/lnet/lnet.h"		/* c2_net_lnet_ifaces_get */
#include "net/net.h"			/* c2_net_domain */

#include "net/test/user_space/common_u.h"

/**
   @defgroup NetTestUCommonInternals Test Node user-space program
   @ingroup NetTestInternals

   @see @ref net-test

   @{
 */

char *c2_net_test_u_str_copy(const char *str)
{
	size_t  len = strlen(str) + 1;
	char   *copy = c2_alloc(len);

	C2_ASSERT(copy != NULL);
	return strncpy(copy, str, len);
}

void c2_net_test_u_str_free(char *str)
{
	c2_free(str);
}

void c2_net_test_u_print_error(const char *s, int code)
{
	if (code != 0)
		PRINT("%s, error %d: %s\n", s, code, strerror(-code));
}

void c2_net_test_u_print_s(const char *fmt, const char *str)
{
	PRINT(fmt, str == NULL ? "NULL" : str);
}

void c2_net_test_u_print_time(char *name, c2_time_t time)
{
	uint64_t ns = c2_time_nanoseconds(time);

	PRINT("%s\t= %lus", name, c2_time_seconds(time));
	if (ns != 0)
		PRINT(" %luns", ns);
	PRINT("\n");
}

void c2_net_test_u_lnet_info(void)
{
	struct c2_net_domain  dom;
	int		      rc;
	char * const	     *nidstrs;
	int		      i;

	rc = c2_net_xprt_init(&c2_net_lnet_xprt);
	C2_ASSERT(rc == 0);
	C2_SET0(&dom);
	rc = c2_net_domain_init(&dom, &c2_net_lnet_xprt);
	C2_ASSERT(rc == 0);

	PRINT("c2_net_domain_get_max_buffer_size()         = %lu\n",
	      c2_net_domain_get_max_buffer_size(&dom));
	PRINT("c2_net_domain_get_max_buffer_segments()     = %i\n",
	      c2_net_domain_get_max_buffer_segments(&dom));
	PRINT("c2_net_domain_get_max_buffer_segment_size() = %lu\n",
	      c2_net_domain_get_max_buffer_segment_size(&dom));
	rc = c2_net_lnet_ifaces_get(&dom, &nidstrs);
	if (rc != 0) {
		PRINT("c2_net_lnet_ifaces_get() failed.\n");
	} else {
		PRINT("List of LNET interfaces:\n");
		for (i = 0; nidstrs != NULL && nidstrs[i] != NULL; ++i)
			PRINT("NID %d:\t%s\n", i, nidstrs[i]);
		PRINT("End of list.\n");
		c2_net_lnet_ifaces_put(&dom, &nidstrs);
	}

	c2_net_domain_fini(&dom);
	c2_net_xprt_fini(&c2_net_lnet_xprt);
}

/**
   @} end of NetTestUCommonInternals group
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
