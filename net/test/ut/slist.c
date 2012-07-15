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
 * Original creation date: 06/06/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/ut.h"		/* C2_UT_ASSERT */
#include "lib/types.h"		/* c2_bcount_t */
#include "lib/vec.h"		/* c2_bufvec */

#include "net/test/slist.h"

enum {
	NET_TEST_SLIST_NR     = 64,
	NET_TEST_SLIST_BV_LEN = NET_TEST_SLIST_NR * 3 + 0x100,
};

static void slist_check(struct c2_net_test_slist *slist, int nr, char *buf)
{
	int i;
	int rc;

	/* check number of string in string list */
	C2_UT_ASSERT(slist->ntsl_nr == nr + 1);
	C2_UT_ASSERT(slist->ntsl_list != NULL);
	/* for every string in the list */
	for (i = 0; i < nr; ++i) {
		/* check the string content */
		rc = memcmp(slist->ntsl_list[i], &buf[i * 3], 2);
		C2_UT_ASSERT(rc == 0);
		/* check the string size */
		rc = strlen(slist->ntsl_list[i]);
		C2_UT_ASSERT(rc == 2);
	}
}

void c2_net_test_slist_ut(void)
{
	struct c2_net_test_slist slist;
	static char		 buf[NET_TEST_SLIST_NR * 3];
	int			 i;
	int			 rc;
	bool			 rc_bool;
	static char		 bv_buf[NET_TEST_SLIST_BV_LEN];
	void			*bv_addr = bv_buf;
	c2_bcount_t		 bv_len = NET_TEST_SLIST_BV_LEN;
	struct c2_bufvec	 bv = C2_BUFVEC_INIT_BUF(&bv_addr, &bv_len);
	c2_bcount_t		 len;
	c2_bcount_t		 len2;


	/* NULL-string test */
	rc = c2_net_test_slist_init(&slist, NULL, ':');
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(slist.ntsl_nr == 0);
	c2_net_test_slist_fini(&slist);
	/* empty string test */
	rc = c2_net_test_slist_init(&slist, "", ':');
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(slist.ntsl_nr == 0);
	rc_bool = c2_net_test_slist_unique(&slist);
	C2_UT_ASSERT(rc_bool);
	len = c2_net_test_slist_serialize(C2_NET_TEST_SERIALIZE, &slist,
					  &bv, 0);
	C2_UT_ASSERT(len > 0);
	c2_net_test_slist_fini(&slist);
	len2 = c2_net_test_slist_serialize(C2_NET_TEST_DESERIALIZE, &slist,
					   &bv, 0);
	C2_UT_ASSERT(len2 == len);
	C2_UT_ASSERT(slist.ntsl_nr == 0);
	c2_net_test_slist_fini(&slist);
	/* one string test */
	rc = c2_net_test_slist_init(&slist, "asdf", ',');
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(slist.ntsl_nr == 1);
	C2_UT_ASSERT(slist.ntsl_list != NULL);
	C2_UT_ASSERT(slist.ntsl_list[0] != NULL);
	C2_UT_ASSERT(strncmp(slist.ntsl_list[0], "asdf", 5) == 0);
	rc_bool = c2_net_test_slist_unique(&slist);
	C2_UT_ASSERT(rc_bool);
	c2_net_test_slist_fini(&slist);
	/* one-of-strings-is-empty test */
	rc = c2_net_test_slist_init(&slist, "asdf,", ',');
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(slist.ntsl_nr == 2);
	C2_UT_ASSERT(slist.ntsl_list != NULL);
	for (i = 0; i < 2; ++i)
		C2_UT_ASSERT(slist.ntsl_list[i] != NULL);
	C2_UT_ASSERT(strncmp(slist.ntsl_list[0], "asdf", 5) == 0);
	C2_UT_ASSERT(strncmp(slist.ntsl_list[1], "",     1) == 0);
	rc_bool = c2_net_test_slist_unique(&slist);
	C2_UT_ASSERT(rc_bool);
	c2_net_test_slist_fini(&slist);
	/* many strings test */
	/* fill string with some pattern (for example, "01,12,23,34\0") */
	for (i = 0; i < NET_TEST_SLIST_NR; ++i) {
		buf[i * 3]     = '0' + i % 10;
		buf[i * 3 + 1] = '0' + (i + 1) % 10;
		buf[i * 3 + 2] = ',';
	}
	/* run test for every string number in [0, NET_TEST_SLIST_NR) */
	for (i = 0; i < NET_TEST_SLIST_NR; ++i) {
		/* cut the line */
		buf[i * 3 + 2] = '\0';
		/* alloc slist */
		rc = c2_net_test_slist_init(&slist, buf, ',');
		C2_UT_ASSERT(rc == 0);
		rc_bool = c2_net_test_slist_unique(&slist);
		C2_UT_ASSERT(!rc_bool ^ (i < 10));
		slist_check(&slist, i, buf);
		/* serialize string list to buffer */
		len = c2_net_test_slist_serialize(C2_NET_TEST_SERIALIZE, &slist,
						  &bv, 0);
		C2_UT_ASSERT(len > 0);
		/* free slist */
		c2_net_test_slist_fini(&slist);
		/* deserialize string slist from buffer */
		/* alloc slist */
		len2 = c2_net_test_slist_serialize(C2_NET_TEST_DESERIALIZE,
						   &slist, &bv, 0);
		C2_UT_ASSERT(len2 == len);
		slist_check(&slist, i, buf);
		/* free slist */
		c2_net_test_slist_fini(&slist);
		/* restore line delimiter */
		buf[i * 3 + 2] = ',';
	}
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
