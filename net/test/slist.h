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
 * Original author Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 06/28/2012
 */

#ifndef __NET_TEST_SLIST_H__
#define __NET_TEST_SLIST_H__

/**
   @defgroup NetTestSLIST Colibri Network Benchmark String List.

   @see
   @ref net-test

   @{
 */

/**
   String list.
 */
struct c2_net_test_slist {
	/**
	   Number of strings in the list. If it is 0, other fields are
	   not valid.
	 */
	int    ntsl_nr;
	/**
	   Array of pointers to strings.
	 */
	char **ntsl_list;
	/**
	   Single array with '\0'-separated strings (one after another).
	   ntsl_list contains the pointers to a strings in this array.
	 */
	char  *ntsl_str;
};

/**
   Initialize string list from a string and a delimiter.
   @todo document it.
 */
int c2_net_test_slist_init(struct c2_net_test_slist *slist,
			   char *str,
			   char delim);
void c2_net_test_slist_fini(struct c2_net_test_slist *slist);

/**
   Is every string in list unique in this list.
   Time complexity - O(N^2), N - number of strings in the list.
   @return all strings in list are different.
	   Two strings are equal if strcmp() returns 0.
 */
bool c2_net_test_slist_unique(struct c2_net_test_slist *slist);

/**
   @} end NetTestSLIST
 */

#endif /*  __NET_TEST_SLIST_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
