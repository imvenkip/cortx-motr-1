/* -*- C -*- */

#include "lib/bitstring.h"


void *c2_bitstring_buf_get(struct c2_bitstring *c)
{
	return c->b_data;
}

uint32_t c2_bitstring_len_get(struct c2_bitstring *c)
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
        const char *s1 = c1->b_data;
        const char *s2 = c2->b_data;
        unsigned char uc1 = 0, uc2;
        int rc;

        /* end of compare */
        uc2 = c1->b_len < c2->b_len ? c1->b_len : c2->b_len;
        /* first diff */
        while (*s1 == *s2 && uc1 < uc2) {
                s1++;
                s2++;
                uc1++;
        }
        /* Compare the characters as unsigned char and
         return the difference.  */
        uc1 = (*(unsigned char *) s1);
        uc2 = (*(unsigned char *) s2);

        if ((rc = (uc1 < uc2) ? -1 : (uc1 > uc2)))
                return rc;

        /* Everything matches through the shortest string */
        return (c1->b_len < c2->b_len) ? -1 : (c1->b_len > c2->b_len);
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
