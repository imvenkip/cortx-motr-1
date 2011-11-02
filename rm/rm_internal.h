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

