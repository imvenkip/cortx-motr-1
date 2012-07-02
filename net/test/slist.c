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

#include "lib/misc.h"		/* C2_SET0 */
#include "lib/memory.h"		/* C2_ALLOC_ARR */
#include "lib/errno.h"		/* ENOMEM */

#include "net/test/slist.h"

enum {
	SLIST_XCODE_MAGIC =  0x5453494C535F544E, /* NT_SLIST */
};

static bool slist_alloc(struct c2_net_test_slist *slist,
		        size_t string_nr,
		        size_t arr_len)
{
	C2_ALLOC_ARR(slist->ntsl_list, string_nr);
	if (slist->ntsl_list != NULL) {
		C2_ALLOC_ARR(slist->ntsl_str, arr_len);
		if (slist->ntsl_str == NULL)
			c2_free(slist->ntsl_list);
	}
	return slist->ntsl_list != NULL && slist->ntsl_str != NULL;
}

static void slist_free(struct c2_net_test_slist *slist)
{
	c2_free(slist->ntsl_list);
	c2_free(slist->ntsl_str);
}

int c2_net_test_slist_init(struct c2_net_test_slist *slist,
			   char *str,
			   char delim)
{
	char  *str1;
	size_t len;
	size_t i = 0;

	C2_PRE(slist != NULL);
	C2_SET0(slist);

	if (str == NULL)
		return 0;
	len = strlen(str);
	if (len == 0)
		return 0;

	str1 = str;
	while (*str1 != '\0')
		slist->ntsl_nr += *str1++ == delim;
	slist->ntsl_nr++;

	if (!slist_alloc(slist, slist->ntsl_nr, len + 1))
		return -ENOMEM;

	strncpy(slist->ntsl_str, str, len + 1);
	str = slist->ntsl_str;
	slist->ntsl_list[i++] = str;
	for (; *str != '\0'; ++str)
		if (*str == delim) {
			*str = '\0';
			slist->ntsl_list[i++] = str + 1;
		}
	return 0;
}

void c2_net_test_slist_fini(struct c2_net_test_slist *slist)
{
	C2_PRE(slist != NULL);

	if (slist->ntsl_nr > 0)
		slist_free(slist);
	C2_SET0(slist);
}

bool c2_net_test_slist_unique(struct c2_net_test_slist *slist)
{
	uint32_t i, j;

	C2_PRE(slist != NULL);

	for (i = 0; i < slist->ntsl_nr; ++i)
		for (j = i + 1; j < slist->ntsl_nr; ++j)
			if (strcmp(slist->ntsl_list[i],
				   slist->ntsl_list[j]) == 0)
				return false;
	return true;
}

struct slist_params {
	size_t   sp_nr;		/**< number if strings in the list */
	size_t   sp_len;	/**< length of string array */
	uint64_t sp_magic;	/**< SLIST_XCODE_MAGIC */
};

TYPE_DESCR(slist_params) = {
	FIELD_DESCR(struct slist_params, sp_nr),
	FIELD_DESCR(struct slist_params, sp_len),
	FIELD_DESCR(struct slist_params, sp_magic),
};

static c2_bcount_t slist_encode(struct c2_net_test_slist *slist,
				struct c2_bufvec *bv,
				c2_bcount_t offset)
{
	struct slist_params sp;
	c2_bcount_t	    len;
	c2_bcount_t	    len_total;

	sp.sp_nr    = slist->ntsl_nr;
	sp.sp_len   = slist->ntsl_list[slist->ntsl_nr - 1] -
		      slist->ntsl_list[0] +
		      strlen(slist->ntsl_list[slist->ntsl_nr - 1]) + 1;
	sp.sp_magic = SLIST_XCODE_MAGIC;

	len_total = c2_net_test_xcode(C2_NET_TEST_ENCODE, &sp,
				      USE_TYPE_DESCR(slist_params), bv, offset);
	if (len_total == 0 || slist->ntsl_nr == 0)
		return len_total;

	len = c2_net_test_xcode_data(C2_NET_TEST_ENCODE, slist->ntsl_str,
				     sp.sp_len, true, bv, offset + len_total);
	return len == 0 ? 0 : len_total + len;
}

static c2_bcount_t slist_decode(struct c2_net_test_slist *slist,
				struct c2_bufvec *bv,
				c2_bcount_t offset)
{
	struct slist_params sp;
	c2_bcount_t	    len;
	c2_bcount_t	    len_total;
	size_t		    i;


	len_total = c2_net_test_xcode(C2_NET_TEST_DECODE, &sp,
				      USE_TYPE_DESCR(slist_params), bv, offset);
	if (len_total == 0 || sp.sp_magic != SLIST_XCODE_MAGIC)
		return 0;

	C2_SET0(slist);
	slist->ntsl_nr = sp.sp_nr;
	/* zero-size string list */
	if (slist->ntsl_nr == 0)
		return len_total;

	if (!slist_alloc(slist, sp.sp_nr, sp.sp_len + 1))
		return 0;

	len = c2_net_test_xcode_data(C2_NET_TEST_DECODE, slist->ntsl_str,
				     sp.sp_len, true, bv, offset + len_total);
	if (len == 0)
		goto failed;

	slist->ntsl_list[0] = slist->ntsl_str;
	/* additional check if received string doesn't contains '\0' */
	slist->ntsl_str[sp.sp_len] = '\0';
	for (i = 1; i < slist->ntsl_nr; ++i) {
		slist->ntsl_list[i] = slist->ntsl_list[i - 1] +
				      strlen(slist->ntsl_list[i - 1]) + 1;
		if (slist->ntsl_list[i] - slist->ntsl_list[0] >= sp.sp_len)
			goto failed;
	}

	return len + len_total;
failed:
	slist_free(slist);
	return 0;
}

c2_bcount_t c2_net_test_slist_xcode(enum c2_net_test_xcode_op op,
				    struct c2_net_test_slist *slist,
				    struct c2_bufvec *bv,
				    c2_bcount_t offset)
{
	C2_PRE(slist != NULL);
	C2_PRE(op == C2_NET_TEST_ENCODE || op == C2_NET_TEST_DECODE);

	return op == C2_NET_TEST_ENCODE ? slist_encode(slist, bv, offset) :
					  slist_decode(slist, bv, offset);
}

/**
   @} end NetTestCommandsInternals
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
