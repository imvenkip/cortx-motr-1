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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 11/05/2011
 */

#ifndef __COLIBRI_LIB_APTR_H__
#define __COLIBRI_LIB_APTR_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include <stddef.h>

/**
   Atomic pointer with automatic value changes counter.
 */
struct c2_aptr {
	// volatile void *ap_ptr;
	// volatile long ap_count;
	volatile char ap_buf[32];
};

/**
   Initialize atomic pointer to NULL value and 0 changes count.
 */
void c2_aptr_init(struct c2_aptr *ap);
void c2_aptr_fini(struct c2_aptr *ap);

void *c2_aptr_ptr(struct c2_aptr *ap);
long c2_aptr_count(struct c2_aptr *ap);

void c2_aptr_set(struct c2_aptr *ap, void *ptr, long count);

/**
   atomic block begin
   dst->ap_ptr = src->ap_ptr
   dst->ap_count = src->ap_count
   atomic block end
 */
void c2_aptr_copy(struct c2_aptr *dst, struct c2_aptr *src);

/**
   Compare two atomic pointers.
 */
bool c2_aptr_eq(struct c2_aptr *ap1, struct c2_aptr *ap2);

/**
   Atomic compare-and-swap primitive.
   dst and old_value must be different.

   atomic block begin
   if (dst->ap_ptr == old->ap_ptr &&
		dst->ap_count == src->ap_count)
	dst->ap_ptr = new_ptr
	dst->ap_count = new_count
	return true
   else
	return false
   atomic block end
 */
bool c2_aptr_cas(struct c2_aptr *dst, struct c2_aptr *old_value,
		void *new_ptr, long new_count);

/* __COLIBRI_LIB_APTR_H__ */
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
