/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_ITERATOR_H__
#define __COLIBRI_FOP_FOP_ITERATOR_H__

#include "lib/types.h"                  /* uint64_t */
#include "lib/list.h"
#include "fop/fop.h"

/**
   @addtogroup fop

   <b>Fop iterators.</b>

   Consult the HLD, referenced below, for the overall purpose of fop iterators.

   In the course of fop processing, the request handler code has to execute a
   number of formulaic actions, like fetching the objects involved in the
   operation from the storage, locking the objects, authorization and
   authentication checks. With the help of fop iterators these actions can be
   executed by the generic part of request handler (as opposed to duplicating
   them in every fop-type specific code). To this end, fop iterators provide an
   interface to walk over fop fields, that satisfy particular criterion. For
   example, "fop-object" iterator, traverses all the fields that identify file
   system objects. The generic request handler code loops over fields yielded by
   the iterator, executing corresponding actions on them.

   Concurrency control.

   ...

   @note "Fop iterator" is systematically abbreviated to "fit" in the symbol
   names.

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfM2MyZzc5Z2Rx

   @{
*/

/**
   Fop iterator type defines a set of fop fields returned by the iterators of
   this type.

   There are several instances of this data-type:

       - c2_fop_object_iterator: for iterators yielding fop fields identifying
         the objects affected by the operation;

       - c2_fop_user_iterator: for iterators yielding the fields identifying the
         users authorizing the operation;

       - c2_fop_capability_iterator: for iterators yielding the fields
         containing the capabilities authenticating the operation.

   And possibly a few others.
 */
struct c2_fit_type {
	/** A human readable name of the iterator. */
	const char    *fit_name;
	/** 
	    A list of all watches for this iterator type. 

	    @see c2_fit_watch::fif_linkage
	*/
	struct c2_list fit_watch;
	/**
	   A list of per-field modifiers.

	   @see c2_fit_mod::fm_linkage
	 */
	struct c2_list fit_mod;
	/**
	   An index assigned to this fop iterator type within a global array of
	   fop iterator types.
	 */
	int            fit_index;
};

/**
   A fop iterator watch is a type of fop field that iterator "watches
   for". Whenever a field of this type is added to a fop type (usually early
   during system initialization or when a new fop type is dynamically
   registered), a fop iterator type is notified.
 */
struct c2_fit_watch {
	/** A fop field type that the iterator type watches for. */
	struct c2_fop_field_type *fif_field;
	/** 
	    A collection of bit-flags associated with this watch.

	    The interpretation of these is entirely up to the iterator type. For
	    example, fop-object iterator type might store there bits indicating
	    what kind of a lock is to be taken on the object identified by the
	    field.
	*/
	uint64_t                  fif_bits;
	/**
	   Linkage into a list of watches hanging off of fop iterator type.

	   @see c2_fit_type::fit_watch
	 */
	struct c2_list_link       fif_linkage;
};

struct c2_fit_mod {
	struct c2_fop_type  *fm_type;
	struct c2_fop_field *fm_field;
	uint64_t             fm_bits_add;
	uint64_t             fm_bits_sub;
	struct c2_list_link  fm_linkage;
};

void c2_fop_itype_init(struct c2_fit_type *itype);
void c2_fop_itype_fini(struct c2_fit_type *itype);

/**
   Adds a watch to a fop iterator type.

   @post itype->fit_watch contains the watch
 */
void c2_fop_itype_add (struct c2_fit_type *itype, struct c2_fit_watch *watch);

/**
   Adds a per-field modifier to a fop iterator type.

   @pre itype->fit_watch contains a watch for mod->fm_field->ff_type
 */
void c2_fop_itype_mod (struct c2_fit_type *itype, struct c2_fit_mod *mod);

extern struct c2_fit_type c2_fop_object_iterator;
extern struct c2_fit_type c2_fop_user_iterator;
extern struct c2_fit_type c2_fop_capability_iterator;

struct c2_fit {
	struct c2_fop      *fi_fop;
	struct c2_fit_type *fi_type;
};

/**
   Information returned by a fop iterator on each iteration.

   @note The term "yield" is from the CLU programming language, which pioneered
   explicit first-class iterators.
 */
struct c2_fit_yield {
	/** Field instance. */
	struct c2_fop_field_instance fy_val;
	/** Cumulative bits for the field instance. */
	uint64_t                     fy_bits;
};

/**
   Initialize the iterator of a given type for a given fop.
 */
void c2_fit_init(struct c2_fit *it, struct c2_fit_type *itype, struct c2_fop *);
void c2_fit_fini(struct c2_fit *it);

/**
   Fill in the yield with the next matching field instance.

   Returns +1 on success (yield is populated), 0 when the iteration is over and
   a negative errno value on other error.

   The return values are selected to enable the following idiom:

   @code
   while ((result = c2_fit_yield(&it, &yield)) > 0) {
           // use yield.
   }

   // result contains either 0 or negative errno.
   @endcode
 */
int c2_fit_yield(struct c2_fit *it, struct c2_fit_yield *yield);

/**
   Synchronizes the 
 */
void c2_fit_sync(void);

/** @} end of fop group */

/* __COLIBRI_FOP_FOP_ITERATOR_H__ */
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
