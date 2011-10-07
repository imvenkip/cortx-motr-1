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
 * Original author: Nathan Rutman <Nathan_Rutman@xyratex.com>
 * Original creation date: 11/17/2010
 */

#include "lib/bitstring.h"
#include "lib/arith.h"      /* C2_3WAY */


void *c2_bitstring_buf_get(struct c2_bitstring *c)
{
	return c->b_data;
}
C2_EXPORTED(c2_bitstring_buf_get);

uint32_t c2_bitstring_len_get(const struct c2_bitstring *c)
{
	return c->b_len;
}
C2_EXPORTED(c2_bitstring_len_get);

void c2_bitstring_len_set(struct c2_bitstring *c, uint32_t len)
{
	c->b_len = len;
}
C2_EXPORTED(c2_bitstring_len_set);

/**
   String-like compare: alphanumeric for the length of the shortest string.
   Shorter strings precede longer strings.
   Strings may contain embedded NULLs.
 */
int c2_bitstring_cmp(const struct c2_bitstring *c1,
                     const struct c2_bitstring *c2)
{
        /* Compare the bytes as unsigned */
        const unsigned char *s1 = (const unsigned char *)c1->b_data;
        const unsigned char *s2 = (const unsigned char *)c2->b_data;
        uint32_t pos = 1, min_len;
        int rc;

        min_len = min_check(c1->b_len, c2->b_len);
        if (min_len == 0)
                return 0;

        /* Find the first differing char */
        while (*s1 == *s2 && pos < min_len) {
                s1++;
                s2++;
                pos++;
        }

        if ((rc = C2_3WAY(*s1, *s2)))
                return rc;

        /* Everything matches through the shortest string, so compare length */
        return C2_3WAY(c1->b_len, c2->b_len);
}
C2_EXPORTED(c2_bitstring_cmp);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
