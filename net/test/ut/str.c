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
 * Original creation date: 09/03/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/ut.h"		/* C2_UT_ASSERT */
#include "lib/vec.h"		/* c2_bufvec */

#include "net/test/str.h"

enum {
	STR_BUF_LEN    = 0x100,
	STR_BUF_OFFSET = 42,
};

static void try_serialize(char *str)
{
	char		 buf[STR_BUF_LEN];
	void		*addr = buf;
	c2_bcount_t	 buf_len = STR_BUF_LEN;
	struct c2_bufvec bv = C2_BUFVEC_INIT_BUF(&addr, &buf_len);
	c2_bcount_t	 serialized_len;
	c2_bcount_t	 len;
	char		*str2;
	int		 str_len;
	int		 rc;

	serialized_len = c2_net_test_str_serialize(C2_NET_TEST_SERIALIZE,
						   &str, &bv, STR_BUF_OFFSET);
	C2_UT_ASSERT(serialized_len > 0);

	str2 = NULL;
	len = c2_net_test_str_serialize(C2_NET_TEST_DESERIALIZE,
					&str2, &bv, STR_BUF_OFFSET);
	C2_UT_ASSERT(len == serialized_len);

	str_len = strlen(str);
	rc = strncmp(str, str2, str_len + 1);
	C2_UT_ASSERT(rc == 0);
	c2_net_test_str_fini(&str2);
}

void c2_net_test_str_ut(void)
{
	try_serialize("");
	try_serialize("asdf");
	try_serialize("SGVsbG8sIHdvcmxkIQo=");
	try_serialize("0123456789!@#$%^&*()qwertyuiopasdfghjklzxcvbnm"
		      "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	try_serialize(__FILE__);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
