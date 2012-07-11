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
 * Original creation date: 06/28/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/** @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

#include "lib/misc.h"		/* C2_SET0 */
#include "lib/ut.h"		/* C2_UT_ASSERT */
#include "lib/vec.h"		/* C2_BUFVEC */

#include "net/test/serialize.h"

#ifndef __KERNEL__
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOGD(format, ...) do {} while (0)
#endif

enum {
	SERIALIZE_BUF_LEN = 0x100,
};

struct simple_struct {
	char  ss_c;
	short ss_s;
	int   ss_i;
	long  ss_l;
	void *ss_p;
};

/* simple_struct_descr */
TYPE_DESCR(simple_struct) = {
	FIELD_DESCR(struct simple_struct, ss_c),
	FIELD_DESCR(struct simple_struct, ss_s),
	FIELD_DESCR(struct simple_struct, ss_i),
	FIELD_DESCR(struct simple_struct, ss_l),
	FIELD_DESCR(struct simple_struct, ss_p),
};

c2_bcount_t simple_struct_serialize(enum c2_net_test_serialize_op op,
				    struct simple_struct *ss,
				    struct c2_bufvec *bv,
				    c2_bcount_t bv_offset)
{
	return c2_net_test_serialize(op, ss, USE_TYPE_DESCR(simple_struct),
				     bv, bv_offset);
}

void c2_net_test_serialize_ut(void)
{
	struct simple_struct ss;
	c2_bcount_t	     ss_encoded_len;
	c2_bcount_t	     rc_bcount;
	char		     buf[SERIALIZE_BUF_LEN];
	void		    *addr = buf;
	c2_bcount_t	     len = SERIALIZE_BUF_LEN;
	struct c2_bufvec     bv = C2_BUFVEC_INIT_BUF(&addr, &len);

	/* length of structure test */
	rc_bcount = simple_struct_serialize(C2_NET_TEST_SERIALIZE,
					    &ss, NULL, 0);
	C2_UT_ASSERT(rc_bcount > 0);

	/* simple encode-decode test */
	ss.ss_c = 1;
	ss.ss_s = 2;
	ss.ss_i = -1;
	ss.ss_l = -2;
	ss.ss_p = &ss;

	ss_encoded_len = simple_struct_serialize(C2_NET_TEST_SERIALIZE,
						 &ss, &bv, 0);
	C2_UT_ASSERT(ss_encoded_len > 0);

	C2_SET0(&ss);

	rc_bcount = simple_struct_serialize(C2_NET_TEST_DESERIALIZE,
					    &ss, &bv, 0);
	C2_UT_ASSERT(rc_bcount == ss_encoded_len);

	C2_UT_ASSERT(ss.ss_c == 1);
	C2_UT_ASSERT(ss.ss_s == 2);
	C2_UT_ASSERT(ss.ss_i == -1);
	C2_UT_ASSERT(ss.ss_l == -2);
	C2_UT_ASSERT(ss.ss_p == &ss);

	/* failure test */
	len = 1;
	rc_bcount = simple_struct_serialize(C2_NET_TEST_SERIALIZE, &ss, &bv, 0);
	C2_UT_ASSERT(rc_bcount == 0);
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
