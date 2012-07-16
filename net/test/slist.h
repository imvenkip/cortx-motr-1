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

#include "lib/vec.h"		/* c2_bufvec */
#include "net/test/serialize.h"	/* c2_net_test_serialize_op */

/**
   @defgroup NetTestSListDFS String List
   @ingroup NetTestDFS

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
	size_t ntsl_nr;
	/**
	   Array of pointers to strings.
	 */
	char **ntsl_list;
	/**
	   Single array with NUL-separated strings (one after another).
	   ntsl_list contains the pointers to strings in this array.
	 */
	char  *ntsl_str;
};

/**
   Initialize a string list from a C string composed of individual sub-strings
   separated by a delimiter character.  The delimiter cannot be NUL and
   cannot be part of the sub-string.
   @pre slist != NULL
   @pre str != NULL
   @pre delim != NUL
   @post (result == 0) && c2_net_test_slist_invariant(slist)
 */
int c2_net_test_slist_init(struct c2_net_test_slist *slist,
			   const char *str,
			   char delim);
/**
   Finalize string list.
   @pre c2_net_test_slist_invariant(slist);
 */
void c2_net_test_slist_fini(struct c2_net_test_slist *slist);
bool c2_net_test_slist_invariant(const struct c2_net_test_slist *slist);

/**
   Is every string in list unique in this list.
   Time complexity - O(N*N), N - number of strings in the list.
   Two strings are equal if strcmp() returns 0.
   @return all strings in list are different.
   @pre c2_net_test_slist_invariant(slist);
 */
bool c2_net_test_slist_unique(const struct c2_net_test_slist *slist);

/**
   Serialize/deserialize string list to/from c2_bufvec.
   c2_net_test_slist_init() shall not be called for slist before
   c2_net_test_slist_serialize().
   c2_net_test_slist_fini() must be called for slist to free memory,
   allocated by c2_net_test_slist_serialize(C2_NET_TEST_DESERIALIZE, slist,...).
   @see c2_net_test_serialize().
 */
c2_bcount_t c2_net_test_slist_serialize(enum c2_net_test_serialize_op op,
					struct c2_net_test_slist *slist,
					struct c2_bufvec *bv,
					c2_bcount_t offset);

/**
   @} end of NetTestSListDFS group
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
