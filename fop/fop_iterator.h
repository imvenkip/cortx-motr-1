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
 * Original creation date: 11/26/2010
 */

#pragma once

#ifndef __COLIBRI_FOP_FOP_ITERATOR_H__
#define __COLIBRI_FOP_FOP_ITERATOR_H__

#include "lib/types.h"                  /* uint64_t, size_t */
#include "lib/tlist.h"
#include "fop/fop_base.h"               /* c2_fop_field_instance */

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

   Fop iterators should not be used concurrently. It's up to user to provide
   serialization and to control liveness of underlying data-structures (fop,
   fop-type, iterator-type, etc.) while the iteration is in progress.

   @note "Fop iterator" is systematically abbreviated to "fit" in the symbol
   names.

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfM2MyZzc5Z2Rx

   @{
*/

/* import */
struct c2_fop;
struct c2_fop_type;
struct c2_fop_field;
struct c2_fop_field_type;

/* export */
struct c2_fit_type;
struct c2_fit_watch;
struct c2_fit_mod;
struct c2_fit;
struct c2_fit_yield;

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
	struct c2_tl   fit_watch;
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
	const struct c2_fop_field_type *fif_field;
	/**
	    A collection of bit-flags associated with this watch.

	    The interpretation of these is entirely up to the iterator type. For
	    example, fop-object iterator type might store there bits indicating
	    what kind of a lock is to be taken on the object identified by the
	    field.
	*/
	uint64_t                        fif_bits;
	/**
	   Linkage into a list of watches hanging off of fop iterator type.

	   @see c2_fit_type::fit_watch
	 */
	struct c2_tlink                 fif_linkage;
	/**
	   A list of per-field modifiers.

	   @see c2_fit_mod::fm_linkage
	 */
	struct c2_tl                    fif_mod;
	uint64_t                        fif_magix;
};

/**
   A per-field modifier for a field watched by an iterator type.

   Modifiers provide for a finer grained control over iteration than watches. A
   watch specifies that the iterator type returns all fields of a particular
   field type. All fields are returned with the same set of bit-flags. A
   modifier adjusts bits-flags on a per-field basis: it is possible to specify
   that a particular field in a particular fop type has its flags adjusted
   relative to what corresponding watch prescribes.

   A typical modifier use case is to specify that a parent directory argument of
   MKDIR operation should be locked for write, whereas parent directory argument
   of LOOKUP should be locked for read.
 */
struct c2_fit_mod {
	/**
	 * A field being watched.
	 */
	const struct c2_fop_field *fm_field;
	/**
	 * Bits added (|) to the watch bit flags.
	 */
	uint64_t                   fm_bits_add;
	/**
	 * Bits cleared (&~) from the watch bit flags.
	 */
	uint64_t                   fm_bits_sub;
	/**
	 * Linkage to the list of all modifiers for the watch.
	 *
	 * @see c2_fit_watch::fif_mod.
	 */
	struct c2_tlink            fm_linkage;
	uint64_t                   fm_magix;
};

void c2_fop_itype_init(struct c2_fit_type *itype);
void c2_fop_itype_fini(struct c2_fit_type *itype);

/**
   Adds a watch to a fop iterator type.

   @pre itype->fit_watch does not contains a watch for watch->fif_field
   @post itype->fit_watch contains the watch
 */
void c2_fop_itype_watch_add(struct c2_fit_type *itype,
			    struct c2_fit_watch *watch);

/**
   Adds a per-field modifier to a fop iterator watch.
 */
void c2_fop_itype_mod_add(struct c2_fit_watch *watch, struct c2_fit_mod *mod);

/**
   A helper function that allocates a per-field modifier for a field with the
   given name and adds it to the watch.
 */
int c2_fit_field_add(struct c2_fit_watch *watch,
		     struct c2_fop_type *ftype, const char *fname,
		     uint64_t add_bits, uint64_t sub_bits);

/**
   Fop cursor stack frame.

   Describes the state of iteration on a particular level of nesting.
 */
struct c2_fit_frame {
	/** Field type for the level. */
	struct c2_fop_field_type *ff_type;
	/** Position within the level. */
	size_t                    ff_pos;
	union {
		/**
		 * Element index reached by the cursor for FFA_SEQUENCE
		 * field.
		 */
		uint64_t          u_index;
		/**
		 * True iff union branch was already searched for on
		 * this level.
		 *
		 * @see c2_fit_yield()
		 */
		bool              u_branch;
	} ff_u;
};

enum {
	/** Maximal level of sub-fields nesting in a fop type. */
	C2_FOP_NESTING_MAX = 8
};

/**
   Fop iterator (cursor).

   A fop iterator moves through the various fields of a single fop, visiting the
   sub-fields recursively, if necessary. The tree of fields is similar to the
   tree of field types (as organized through c2_fop_field::ff_type::fft_child[]
   pointers), except that fields of FFA_SEQUENCE aggregation type come with an
   array of elements and fields of FFA_UNION aggregation type has only one of
   possible sub-fields.

   The iterator only moves through a subset of the fields. This subset consists
   of fields "watched" by this iterator type.

   @see c2_fit_watch
   @see c2_fit_yield
*/
struct c2_fit {
	/** fop instance being iterated through. */
	struct c2_fop      *fi_fop;
	/** fop iterator type */
	struct c2_fit_type *fi_type;
	/** stack of positions reached by the cursor, per nesting level. */
	struct c2_fit_frame fi_stack[C2_FOP_NESTING_MAX];
	/** stack depth. it->fi_stack[it->fi_depth] is the top of the stack. */
	int                 fi_depth;
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

   // result contains either 0 or a negative errno.
   @endcode
 */
int c2_fit_yield(struct c2_fit *it, struct c2_fit_yield *yield);

/*
  The following is not part of public interface.
 */

void c2_fits_init(void);
void c2_fits_fini(void);

/**
   Called from c2_fop_type_build() to construct fop-iterators related state for
   a newly built fop type.
 */
int c2_fop_field_type_fit(struct c2_fop_field_type *fieldt);

/*
 * Standard fop iterator types.
 */

/*
 * Fop object: enumerates file system objects referenced by a fop.
 */

struct c2_fid;

enum c2_fop_object_bitflags {
	C2_FOB_LOAD  = 1 << 0,
	C2_FOB_READ  = 1 << 1,
	C2_FOB_WRITE = 1 << 2
};

void c2_fop_object_init(const struct c2_fop_type_format *fid_type);
void c2_fop_object_fini(void);

void c2_fop_object_it_init (struct c2_fit *it, struct c2_fop *fop);
void c2_fop_object_it_fini (struct c2_fit *it);
int  c2_fop_object_it_yield(struct c2_fit *it,
			    struct c2_fid *fid, uint64_t *bits);

/*
 * Special FOP iterator interfaces
 */

void c2_fits_all_init(void);
void c2_fits_all_fini(void);

void c2_fop_it_reset(struct c2_fit *it);
void c2_fop_all_object_it_init(struct c2_fit *it, struct c2_fop *fop);
void c2_fop_all_object_it_fini(struct c2_fit *it);
int c2_fop_all_object_it_yield(struct c2_fit *it,
                               struct c2_fid *fid, uint64_t *bits);
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
