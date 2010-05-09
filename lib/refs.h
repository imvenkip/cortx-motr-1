/* -*- C -*- */

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
void c2_ref_get(struct c2_ref *ref);

/**
 release one reference from the object.
 if function will release last rererence, destructor will called.

 @param ref pointer to c2_ref object

 @return none
*/
void c2_ref_put(struct c2_ref *ref);

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
