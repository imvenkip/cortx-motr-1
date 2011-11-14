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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 09/15/2011
 */
#ifndef __COLIBRI_RM_RM_INTERNAL_H__
#define __COLIBRI_RM_RM_INTERNAL_H__

/**
   Sticks a tracking pin on @right. When @right is released, the all incoming
   requests that stuck pins into it are notified.
*/
int pin_add(struct c2_rm_incoming *in, struct c2_rm_right *right);

/**
   Makes another copy of right struct.
*/
int right_copy(struct c2_rm_right *dest, const struct c2_rm_right *src);

/**
   Called when an outgoing request completes (possibly with an error, like a
   timeout).
*/
void c2_rm_outgoing_complete(struct c2_rm_outgoing *og, int32_t rc);

/** Returns true iff rights are equal. */
bool right_eq(const struct c2_rm_right *r0, const struct c2_rm_right *r1);

/**
   @name RM lists.
 */

/** @{ */

C2_TL_DESCR_DECLARE(res, extern);
C2_TL_DECLARE(res, , struct c2_rm_resource);

C2_TL_DESCR_DECLARE(ur, extern);
C2_TL_DECLARE(ur, , struct c2_rm_right);

C2_TL_DESCR_DECLARE(pr, extern);
C2_TL_DECLARE(pr, , struct c2_rm_pin);

C2_TL_DESCR_DECLARE(pi, extern);
C2_TL_DECLARE(pi, , struct c2_rm_pin);

/**
   Execute "expr" against all rights lists in a given owner.
 */
#define RM_OWNER_LISTS_FOR(owner, expr)					\
({									\
	struct c2_rm_owner *__o = (owner);				\
	int                 __i;					\
	int                 __j;					\
									\
	(expr)(&__o->ro_borrowed);					\
	(expr)(&__o->ro_sublet);					\
									\
	for (__i = 0; __i < ARRAY_SIZE(__o->ro_owned); __i++)		\
		(expr)(&__o->ro_owned[__i]);				\
									\
	for (__i = 0; __i < ARRAY_SIZE(__o->ro_incoming); __i++) {	\
		for (__j = 0; __j < ARRAY_SIZE(__o->ro_incoming[__i]); __j++) \
			(expr)(&__o->ro_incoming[__i][__j]);		\
	}								\
									\
	for (__i = 0; __i < ARRAY_SIZE(__o->ro_outgoing); __i++)	\
		(expr)(&__o->ro_outgoing[__i]);				\
})

/** @} end of RM lists. */

/* __COLIBRI_RM_RM_INTERNAL_H__ */
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

