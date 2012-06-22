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
int pin_add(struct c2_rm_incoming *in, struct c2_rm_right *right, uint32_t flag);

/**
   Makes another copy of right struct.
*/
int right_copy(struct c2_rm_right *dest, const struct c2_rm_right *src);

/** Returns true iff rights are equal. */
bool right_eq(const struct c2_rm_right *r0, const struct c2_rm_right *r1);

/**
   @name rm-fop interface.

   Functions and data-structures used for interaction between RM core and RM fops
   (and fom)s.
 */

/** @{ */

/**
   Created by a creditor to process a borrow request.

   This is created when an incoming BORROW for is received. Typically, this
   structure will be embedded in an instance of the corresponding fom type.
 */
struct c2_rm_borrow_incoming {
	/* Incoming request of type RIT_BORROW. */
	struct c2_rm_incoming  bi_incoming;
	struct c2_rm_loan     *bi_loan;
};

/**
   Created by a debtor to process a revoke request.

   This is created when an incoming REVOKE request is received. Typically, this
   structure will be embedded in an instance of the corresponding fom type.
 */
struct c2_rm_revoke_incoming {
	/* Incoming request of type RIT_REVOKE. */
	struct c2_rm_incoming rv_incoming;
};

/**
   Moves credited rights from "owned" to "sublet" list.

   This is called by borrow fom code after successful processing of
   bor->bi_incoming.

   On success, this call transfers ownership of bor->bi_loan to the owner.

   @pre bor->bi_loan is initialised.
   @pre c2_mutex_is_locked(&owner->ro_lock)
   @pre in->rin_state == RI_SUCCESS
   @pre in->rin_type == RIT_BORROW
   @pre loan->rl_right.ri_owner == owner

   where "owner", "loan" and "in" are respective attributes of "bor".
 */
int c2_rm_borrow_commit(struct c2_rm_borrow_incoming *bor);

/**
   Removes revoked rights from "owned" and borrowed lists.

   Called by revoke fom code to complete processing of revoke.

   @pre c2_mutex_is_locked(&owner->ro_lock)
   @pre in->rin_state == RI_SUCCESS
   @pre in->rin_type == RIT_BORROW
 */
int c2_rm_revoke_commit(struct c2_rm_revoke_incoming *rvr);

/**
   Adds borrowed right to the "borrowed" and "owned" lists.

   This is called on receiving a reply to an outgoing BORROW fop.

   This function transfers ownership of the supplied "loan" structure to the
   owner.
 */
int c2_rm_borrow_done(struct c2_rm_outgoing *out, struct c2_rm_loan *loan);

/**
   Moves revoked right from "sublet" to "owned" list.

   This is called on receiving a reply to an outgoing REVOKE fop.
 */
int c2_rm_revoke_done(struct c2_rm_outgoing *out);

/**
   Returns a cookie for a given owner.
 */
void c2_rm_owner_cookie(const struct c2_rm_owner *o, struct c2_rm_cookie *cake);

/**
   Returns a cookie for a given loan.
 */
void c2_rm_loan_cookie(const struct c2_rm_loan *loan, struct c2_rm_cookie *cake);

/**
   Returns the owner corresponding to a given cookie, or NULL when the cookie is
   stale.
 */
struct c2_rm_owner *c2_rm_owner_find(const struct c2_rm_cookie *cake);

/**
   Returns the loan corresponding to a given cookie, or NULL when the cookie is
   stale.
 */
struct c2_rm_loan  *c2_rm_loan_find (const struct c2_rm_cookie *cake);

/**
   Returns the owner locally managing the rights for a given resource.

   This is used on the creditor side to identify an owner against which an
   incoming BORROW request is to be processed.
 */
int c2_rm_resource_owner_find(const struct c2_rm_resource *resource,
			      struct c2_rm_owner **owner);

/**
   Constructs and sends out an outgoing borrow request.

   Allocates c2_rm_outgoing, adds a pin from "in" to the outgoing request.
   Constructs and sends an outgoing borrow fop. Arranges
   c2_rm_outgoing_complete() to be called on fop reply or timeout.

   @see c2_rm_revoke_out().
 */
int c2_rm_borrow_out(struct c2_rm_incoming *in, struct c2_rm_right *right);

/**
   Constructs and sends out an outgoing revoke request.

   @see c2_rm_borrow_out().
 */
int c2_rm_revoke_out(struct c2_rm_incoming *in,
		     struct c2_rm_loan *loan, struct c2_rm_right *right);

/**
   Called when an outgoing request completes (possibly with an error, like a
   timeout).
*/
void c2_rm_outgoing_complete(struct c2_rm_outgoing *og);

#if 0
int outgoing_check(struct c2_rm_incoming *in, enum c2_rm_incoming_type inreq,
		   struct c2_rm_right *right, struct c2_rm_remote *remote);

/** @} end of rm-fop interface. */

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
#endif

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

