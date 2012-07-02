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

#include "lib/cdefs.h"		/* container_of */
#include "lib/types.h"		/* c2_bcount_t */
#include "lib/misc.h"		/* C2_SET0 */
#include "lib/memory.h"		/* c2_alloc */
#include "lib/errno.h"		/* ENOMEM */

#include "net/test/ntxcode.h"

/**
   @defgroup NetTestXCODEInternals Colibri Network Bencmark Xcode internals.

   @see
   @ref net-test

   @{
 */

/** Environment have LP64 data model */
C2_BASSERT(sizeof(long)   == 8);
C2_BASSERT(sizeof(void *) == 8);
C2_BASSERT(sizeof(int)    == 4);

enum {
	NTXCODE_BUF_LEN_MAX = 8, /**< @see net_test_xcode_data(). */
};

/**
   @return Environment is little-endian.
 */
static bool is_littl_endian_env(void)
{
	static const long one = 1;

	return *((char *) &one) == 1;
}

static void net_test_xcode_reorder(enum c2_net_test_xcode_op op,
				   char *buf,
				   char *data,
				   c2_bcount_t len)
{
	c2_bcount_t i;
	c2_bcount_t j;

	C2_PRE(len <= NTXCODE_BUF_LEN_MAX);

	for (i = 0; i < len; ++i) {
		j = is_littl_endian_env() ? i : len - 1 - i;

		if (op == C2_NET_TEST_ENCODE)
			buf[i] = data[j];
		else
			data[j] = buf[i];
	}
}

/**
   Encode/decode object field to buffer.
   Converts field to little-endian representation while encoding and
   reads field as little-endian from buffer while decoding
   iff plain_data == false.
   @param bv_length Total length of bv. This value is ignored if bv == NULL.
	            Must be equivalent to c2_vec_count(&bv->ov_vec).
   @pre data_len > 0
   @pre plain_data || data_len <= 8
   @see c2_net_test_xcode().
 */
static c2_bcount_t net_test_xcode_data(enum c2_net_test_xcode_op op,
				       void *data,
				       c2_bcount_t data_len,
				       bool plain_data,
				       struct c2_bufvec *bv,
				       c2_bcount_t bv_offset,
				       c2_bcount_t bv_length)
{
	struct c2_bufvec_cursor bv_cur;
	struct c2_bufvec_cursor data_cur;
	char			buf[NTXCODE_BUF_LEN_MAX];
	void		       *data_addr = plain_data ? data : buf;
	struct c2_bufvec	data_bv = C2_BUFVEC_INIT_BUF(&data_addr,
							     &data_len);
	c2_bcount_t		copied;

	C2_PRE(data_len > 0);
	C2_PRE(plain_data || data_len <= NTXCODE_BUF_LEN_MAX);

	/* is buffer is NULL and operation is 'encode' then return size */
	if (bv == NULL)
		return op == C2_NET_TEST_ENCODE ? data_len : 0;
	/* if buffer is not large enough then return 0 */
	if (bv_offset + data_len > bv_length)
		return 0;

	/*
	   Take care abount endiannes.
	   Store all endian-dependent data in little-endian format.
	 */
	if (!plain_data && op == C2_NET_TEST_ENCODE)
		net_test_xcode_reorder(op, buf, data, data_len);

	/* initialize cursors and copy data */
	c2_bufvec_cursor_init(&bv_cur, bv);
	c2_bufvec_cursor_move(&bv_cur, bv_offset);

	c2_bufvec_cursor_init(&data_cur, &data_bv);

	if (op == C2_NET_TEST_ENCODE)
		copied = c2_bufvec_cursor_copy(&bv_cur, &data_cur, data_len);
	else
		copied = c2_bufvec_cursor_copy(&data_cur, &bv_cur, data_len);
	C2_ASSERT(copied == data_len);

	/*
	   Take care abount endiannes.
	   Read all endian-dependent data from little-endian buffer.
	 */
	if (!plain_data && op == C2_NET_TEST_DECODE)
		net_test_xcode_reorder(op, buf, data, data_len);

	return data_len;
}

c2_bcount_t c2_net_test_xcode_data(enum c2_net_test_xcode_op op,
				   void *data,
				   c2_bcount_t data_len,
				   bool plain_data,
				   struct c2_bufvec *bv,
				   c2_bcount_t bv_offset)
{
	c2_bcount_t bv_length = bv == NULL ? 0 : c2_vec_count(&bv->ov_vec);

	if (!plain_data && data_len > NTXCODE_BUF_LEN_MAX)
		return 0;

	return net_test_xcode_data(op, data, data_len, plain_data,
				   bv, bv_offset, bv_length);
}

c2_bcount_t c2_net_test_xcode(enum c2_net_test_xcode_op op,
			      void *obj,
			      const struct c2_net_test_descr descr[],
			      size_t descr_nr,
			      struct c2_bufvec *bv,
			      c2_bcount_t bv_offset)
{
	size_t				i;
	const struct c2_net_test_descr *d_i;
	void			       *addr;
	c2_bcount_t			len_total = 0;
	c2_bcount_t			len_current = 0;
	c2_bcount_t			bv_length;

	C2_PRE(op == C2_NET_TEST_ENCODE || op == C2_NET_TEST_DECODE);
	C2_PRE(obj != NULL);
	C2_PRE(descr != NULL);

	bv_length = bv == NULL ? 0 : c2_vec_count(&bv->ov_vec);

	for (i = 0; i < descr_nr; ++i) {
		d_i = &descr[i];
		addr = &((char *) obj)[d_i->ntd_offset];
		len_current = net_test_xcode_data(op, addr, d_i->ntd_length,
						  d_i->ntd_plain_data,
					          bv, bv_offset + len_total,
						  bv_length);
		len_total += len_current;
		if (len_current == 0)
			break;
	}
	return len_current == 0 ? 0 : len_total;
}

/**
   @} end NetTestXCODEInternals
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
