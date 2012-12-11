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

#include "lib/misc.h"		/* M0_SET0 */
#include "lib/memory.h"		/* M0_ALLOC_ARR */
#include "lib/errno.h"		/* ENOMEM */

#include "net/test/slist.h"

/**
   @defgroup NetTestSListInternals String List
   @ingroup NetTestInternals

   @{
 */

enum {
	/** NT_SLIST @todo move to lib/magic.h */
	SLIST_SERIALIZE_MAGIC =  0x5453494C535F544E,
};

static bool slist_alloc(struct m0_net_test_slist *slist,
		        size_t string_nr,
		        size_t arr_len)
{
	M0_ALLOC_ARR(slist->ntsl_list, string_nr);
	if (slist->ntsl_list != NULL) {
		M0_ALLOC_ARR(slist->ntsl_str, arr_len);
		if (slist->ntsl_str == NULL)
			m0_free(slist->ntsl_list);
	}
	return slist->ntsl_list != NULL && slist->ntsl_str != NULL;
}

static void slist_free(struct m0_net_test_slist *slist)
{
	m0_free(slist->ntsl_list);
	m0_free(slist->ntsl_str);
}

int m0_net_test_slist_init(struct m0_net_test_slist *slist,
			   const char *str,
			   char delim)
{
	char  *str1;
	size_t len = 0;
	size_t i = 0;
	bool   allocated;

	M0_PRE(slist != NULL);
	M0_PRE(str   != NULL);
	M0_PRE(delim != '\0');

	M0_SET0(slist);

	for (len = 0; str[len] != '\0'; ++len)
		slist->ntsl_nr += str[len] == delim;

	if (len != 0) {
		slist->ntsl_nr++;

		allocated = slist_alloc(slist, slist->ntsl_nr, len + 1);
		if (!allocated)
			return -ENOMEM;

		strncpy(slist->ntsl_str, str, len + 1);
		str1 = slist->ntsl_str;
		slist->ntsl_list[i++] = str1;
		for (; *str1 != '\0'; ++str1)
			if (*str1 == delim) {
				*str1 = '\0';
				slist->ntsl_list[i++] = str1 + 1;
			}
	}
	M0_POST(m0_net_test_slist_invariant(slist));
	return 0;
}

bool m0_net_test_slist_invariant(const struct m0_net_test_slist *slist)
{
	size_t i;

	if (slist == NULL)
		return false;
	if (slist->ntsl_nr == 0)
		return true;
	if (slist->ntsl_list == NULL)
		return false;
	if (slist->ntsl_str == NULL)
		return false;

	/* check all pointers in ntsl_list */
	if (slist->ntsl_list[0] != slist->ntsl_str)
		return false;
	for (i = 1; i < slist->ntsl_nr; ++i)
		if (slist->ntsl_list[i - 1] >= slist->ntsl_list[i] ||
		    slist->ntsl_list[i] <= slist->ntsl_str)
			return false;
	return true;
}

void m0_net_test_slist_fini(struct m0_net_test_slist *slist)
{
	M0_PRE(m0_net_test_slist_invariant(slist));

	if (slist->ntsl_nr > 0)
		slist_free(slist);
	M0_SET0(slist);
}

bool m0_net_test_slist_unique(const struct m0_net_test_slist *slist)
{
	size_t i;
	size_t j;

	M0_PRE(m0_net_test_slist_invariant(slist));

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

static m0_bcount_t slist_encode(struct m0_net_test_slist *slist,
				struct m0_bufvec *bv,
				m0_bcount_t offset)
{
	struct slist_params sp;
	m0_bcount_t	    len;
	m0_bcount_t	    len_total;

	sp.sp_nr    = slist->ntsl_nr;
	sp.sp_len   = slist->ntsl_nr == 0 ? 0 :
		      slist->ntsl_list[slist->ntsl_nr - 1] -
		      slist->ntsl_list[0] +
		      strlen(slist->ntsl_list[slist->ntsl_nr - 1]) + 1;
	sp.sp_magic = SLIST_SERIALIZE_MAGIC;

	len_total = m0_net_test_serialize(M0_NET_TEST_SERIALIZE, &sp,
				          USE_TYPE_DESCR(slist_params),
					  bv, offset);
	if (len_total == 0 || slist->ntsl_nr == 0)
		return len_total;

	len = m0_net_test_serialize_data(M0_NET_TEST_SERIALIZE, slist->ntsl_str,
					 sp.sp_len, true,
					 bv, offset + len_total);
	return len == 0 ? 0 : len_total + len;
}

static m0_bcount_t slist_decode(struct m0_net_test_slist *slist,
				struct m0_bufvec *bv,
				m0_bcount_t offset)
{
	struct slist_params sp;
	m0_bcount_t	    len;
	m0_bcount_t	    len_total;
	size_t		    i;
	bool		    allocated;


	len_total = m0_net_test_serialize(M0_NET_TEST_DESERIALIZE, &sp,
					  USE_TYPE_DESCR(slist_params),
					  bv, offset);
	if (len_total == 0 || sp.sp_magic != SLIST_SERIALIZE_MAGIC)
		return 0;

	M0_SET0(slist);
	slist->ntsl_nr = sp.sp_nr;
	/* zero-size string list */
	if (slist->ntsl_nr == 0)
		return len_total;

	allocated = slist_alloc(slist, sp.sp_nr, sp.sp_len + 1);
	if (!allocated)
		return 0;

	len = m0_net_test_serialize_data(M0_NET_TEST_DESERIALIZE, slist->ntsl_str,
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

m0_bcount_t m0_net_test_slist_serialize(enum m0_net_test_serialize_op op,
					struct m0_net_test_slist *slist,
					struct m0_bufvec *bv,
					m0_bcount_t offset)
{
	M0_PRE(slist != NULL);
	M0_PRE(op == M0_NET_TEST_SERIALIZE || op == M0_NET_TEST_DESERIALIZE);

	return op == M0_NET_TEST_SERIALIZE ? slist_encode(slist, bv, offset) :
					     slist_decode(slist, bv, offset);
}

/**
   @} end of NetTestSListInternals group
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
