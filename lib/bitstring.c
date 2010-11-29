/* -*- C -*- */

#include "lib/bitstring.h"
#include "lib/arith.h"      /* C2_3WAY */


void *c2_bitstring_buf_get(struct c2_bitstring *c)
{
	return c->b_data;
}

uint32_t c2_bitstring_len_get(const struct c2_bitstring *c)
{
	return c->b_len;
}

void c2_bitstring_len_set(struct c2_bitstring *c, uint32_t len)
{
	c->b_len = len;
}

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


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
