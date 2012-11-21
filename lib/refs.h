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
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 04/08/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_REFS_H__
#define __COLIBRI_LIB_REFS_H__

#include "cdefs.h"
#include "atomic.h"

/**
 routines for handling generic reference counted objects
*/

struct c2_ref {
	/**
	 number references to object
	 */
	struct c2_atomic64	ref_cnt;
	/**
	  ponter to destructor
	  @param ref pointer to reference object
	*/
	void (*release) (struct c2_ref *ref);
};

/**
 constructor for init reference counted protection

 @param ref pointer to c2_ref object
 @param init_num initial references on object
 @param release destructor function for the object
*/
void c2_ref_init(struct c2_ref *ref, int init_num,
		void (*release) (struct c2_ref *ref));

/**
 take one reference to the object

 @param ref pointer to c2_ref object

 @return none
 */
C2_INTERNAL void c2_ref_get(struct c2_ref *ref);

/**
 release one reference from the object.
 if function will release last rererence, destructor will called.

 @param ref pointer to c2_ref object

 @return none
*/
C2_INTERNAL void c2_ref_put(struct c2_ref *ref);

/**
 Read the current value of the reference count from the c2_ref object

 @param ref pointer to c2_ref object

 @return current value of the reference count
 */
C2_INTERNAL int64_t c2_ref_read(const struct c2_ref *ref);
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
