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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 21-Jan-2012
 */

#ifndef __COLIBRI_LIB_BOB_H__
#define __COLIBRI_LIB_BOB_H__

/**
   @defgroup bob Branded objects

   @{
*/

/* import */
struct c2_xcode_type;
struct c2_tl_descr;

/* export */
struct c2_bob_type;

struct c2_bob_type {
	const char *bt_name;
	int         bt_magix_offset;
	uint64_t    bt_magix;
	bool      (*bt_check)(const void *bob);
};

void c2_bob_type_xcode_init(struct c2_bob_type *bt,
			    const struct c2_xcode_type *xt,
			    size_t magix_field, uint64_t magix);
void c2_bob_type_tlist_init(struct c2_bob_type *bt,
			    const struct c2_tl_descr *td);

void c2_bob_init(const struct c2_bob_type *bt, void *bob);
bool c2_bob_check(const struct c2_bob_type *bt, const void *bob);

#define C2_BOB_DEFINE(scope, bob_type, type)		\
scope void type ## _bob_init(const struct type *bob)	\
{							\
	c2_bob_init(bob_type, bob);			\
}							\
							\
scope bool type ## _bob_check(const struct type *bob)	\
{							\
	return c2_bob_check(bob_type, bob);		\
}

/** @} end of bob group */

/* __COLIBRI_LIB_BOB_H__ */
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
