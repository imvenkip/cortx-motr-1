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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 09/09/2010
 */

#ifndef __COLIBRI_FOL_LSN_H__
#define __COLIBRI_FOL_LSN_H__

/**
   @addtogroup fol

   Log sequence number (lsn) uniquely identifies a record in a fol.

   @{
 */

/**
   Log sequence number (lsn) uniquely identifies a record in a fol.

   lsn possesses two properties:

   @li a record with a given lsn can be found efficiently, and

   @li lsn of a dependent update is greater than the lsn of an update it depends
   upon.

   lsn should _never_ overflow, because other persistent file system tables
   (most notably object index) store lsns of arbitrarily old records, possibly
   long time truncated from the fol. It would be dangerous to allow such a
   reference to accidentally alias an unrelated record after lsn overflow. Are
   64 bits enough?

   Given 1M operations per second, a 64 bit counter overflows in 600000 years.
 */
typedef uint64_t c2_lsn_t;

enum {
	/** Invalid lsn value. Used to catch uninitialised lsns. */
	C2_LSN_INVALID,
	/** Non-existent lsn. This is used, for example, as a prevlsn, when
	    there is no previous operation on the object. */
	C2_LSN_NONE,
	/** LSN of a special dummy item always present in
	    c2_rpc_slot::sl_item_list. */
	C2_LSN_DUMMY_ITEM,
	C2_LSN_RESERVED_NR,
	/**
	    LSN of a special "anchor" record always present in the fol.
	 */
	C2_LSN_ANCHOR = C2_LSN_RESERVED_NR + 1
};

/** True iff the argument might be an lsn of an existing fol record. */
bool     c2_lsn_is_valid(c2_lsn_t lsn);
/** 3-way comparison (-1, 0, +1) of lsns, compatible with record
    dependencies. */
int      c2_lsn_cmp     (c2_lsn_t lsn0, c2_lsn_t lsn1);

/* __COLIBRI_FOL_LSN_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
