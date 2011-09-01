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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/26/2011
 */

#ifndef __COLIBRI_LIB_TLIST_H__
#define __COLIBRI_LIB_TLIST_H__

#include "lib/list.h"
#include "lib/types.h"                    /* uint64_t */

/**
   @defgroup tlist Typed lists.

   Typed list module provides a double-linked list implementation that
   eliminates some chores and sources of errors typical for the "raw" c2_list
   interface.

   Typed list is implemented on top of c2_list and adds the following features:

       - a "list descriptor" (c2_tl_descr) object holding information about this
         list type, including its human readable name;

       - "magic" numbers embedded in list header and list links and checked by
         the code to catch corruptions;

       - automatic conversion to and from list links and ambient objects they
         are embedded to, obviating the need in container_of() and
         c2_list_entry() calls. In fact, links (c2_tlink) are not mentioned in
         tlist interface at all;

       - gdb (7.0) pretty-printer for lists (not yet implemented).

   tlist is a safe and more convenient alternative to c2_list. As a general
   rule, c2_list should be used only when performance is critical or some
   flexibility beyond what tlist provides are necessary.

   Similarly to c2_list, tlist is a purely algorithmic module: it deals with
   neither concurrency nor liveness nor with any similar issues that its callers
   are supposed to handle.

   To describe a typical tlist usage pattern, suppose that one wants a list of
   objects of type foo hanging off every object of type foo.

   First, two things have to be done:

   - "list link" has to be embedded in foo:

     @code
     struct foo {
             ...
	     // linkage into a list of foo-s hanging off bar::b_list
	     struct c2_tlink f_linkage;
	     ...
     };
     @endcode

   - then, a "list head" has to be embedded in bar:

     @code
     struct bar {
             ...
	     // list of foo-s, linked through foo::f_linkage
	     struct c2_tl b_list;
	     ...
     };
     @endcode

   - now, define a tlist type:

     @code
     static const struct c2_tl_descr foobar_list = {
             .td_name       = "foo-s of bar",
	     .td_offset     = offsetof(struct foo, f_linkage),
	     .td_head_magic = 0x666f6f6261726865 // "foobarhe"
     };
     @endcode

   This defines the simplest form of tlist without magic checking in list links
   (the magic embedded in a list head is checked anyway). To add magic checking,
   place a magic field in foo:

   @code
   struct foo {
           ...
	   uint64_t f_magic;
	   ...
   };

   static const struct c2_tl_descr foobar_list = {
           ...
	   .td_magic_offset = offsetof(struct foo, f_magic),
	   .td_link_magic   = 0x666f6f6261726c69 // "foobarli"
   };
   @endcode

   Magic field can be shared by multiple tlist links embedded in the same object
   and can be used for other sanity checking. In this case, all but last links
   must be finalised with a call to c2_tlist_fini0().

   Now, one can populate and manipulate foo-bar lists:

   @code
   struct bar  B;
   struct foo  F;
   struct foo *scan;

   c2_tlist_init(&B.b_list);
   c2_tlink_init(&F.f_linkage);

   c2_tlist_add(&foobar_list, &B.b_list, &F);
   C2_ASSERT(c2_tl_contains(&foobar_list, &B.b_list, &F));

   c2_tl_for(&foobar_list, &B.b_list, scan)
           C2_ASSERT(scan == &F);
   @endcode

   @note Differently from c2_list, tlist heads and links must be initialised
   before use, even when first usage overwrites the entity completely. This
   allows stronger checking in tlist manipulation functions.

   @{
 */

struct c2_tl_descr;
struct c2_tl;
struct c2_tlink;

/**
   An instance of this type must be defined for each "tlist type", specifically
   for each link embedded in an ambient type.

   @verbatim
			      ambient object
                          +  +-----------+  +
          td_magic_offset |  |           |  |
                          v  |           |  |
                             |-----------|  |
                             |link magic |  | td_offset
                             |-----------|  |
                             |           |  |
        head                 |           |  |
    +->+----------+          |           |  |
    |  |head magic|          |           |  v
    |  |----------|          |-----------|
    |  |        +----------->|link     +------------> . . . ---+
    |  +----------+          |-----------|                     |
    |                        |           |                     |
    |                        |           |                     |
    |                        |           |                     |
    |                        |           |                     |
    |                        +-----------+                     |
    |                                                          |
    |                                                          |
    +----------------------------------------------------------+

   @endverbatim
 */
struct c2_tl_descr {
	/** Human-readable list name, used for error messages. */
	const char *td_name;
	/** Offset of list link (c2_tlink) in the ambient object. */
	int         td_offset;
	/**
	    Offset of magic field in the ambient object.
	    This is used only when link magic checking is on.

	    @see c2_tl_descr::td_link_magic
	 */
	int         td_magic_offset;
	/**
	    Magic stored in c2_tl::t_magic and checked on all tlist
	    operations.
	 */
	uint64_t    td_head_magic;
	/**
	    Magic stored in an ambient object.

	    If this field is 0, link magic checking is disabled.
	 */
	uint64_t    td_link_magic;
};

/**
   tlist head.
 */
struct c2_tl {
	/**
	   Head magic. This is set to c2_tl::td_head_magic and verified by the
	   list invariant.
	 */
	uint64_t       t_magic;
	/** Underlying c2_list. */
	struct c2_list t_head;
};

/**
   tlist link.
 */
struct c2_tlink {
	/** Underlying c2_list link. */
	struct c2_list_link t_link;
};

void c2_tlist_init(const struct c2_tl_descr *d, struct c2_tl *list);
void c2_tlist_fini(const struct c2_tl_descr *d, struct c2_tl *list);

void c2_tlink_init(const struct c2_tl_descr *d, void *obj);

/**
   Finalises tlist link. If multiple links in the same ambient object share the
   magic field, only last link should be finalised by calling this function.

   @see c2_tlink_fini0()
 */
void c2_tlink_fini(const struct c2_tl_descr *d, void *obj);

/**
   Finalises one of multiple tlist links sharing the magic field.

   @see c2_tlink_fini()
 */
void c2_tlink_fini0(const struct c2_tl_descr *d, void *obj);

bool c2_tlist_invariant(const struct c2_tl_descr *d, const struct c2_tl *list);
bool c2_tlink_invariant(const struct c2_tl_descr *d, const void *obj);

bool   c2_tlist_is_empty(const struct c2_tl_descr *d, const struct c2_tl *list);
bool   c2_tlink_is_in   (const struct c2_tl_descr *d, const void *obj);

bool   c2_tlist_contains(const struct c2_tl_descr *d, const struct c2_tl *list,
			 const void *obj);
size_t c2_tlist_length(const struct c2_tl_descr *d, const struct c2_tl *list);

/**
   Adds an element to the beginning of a list.

   @pre !c2_tlink_is_in(d, obj)
   @post c2_tlink_is_in(d, obj)
 */
void   c2_tlist_add(const struct c2_tl_descr *d, struct c2_tl *list, void *obj);

/**
   Adds an element to the end of a list.

   @pre !c2_tlink_is_in(d, obj)
   @post c2_tlink_is_in(d, obj)
 */
void   c2_tlist_add_tail(const struct c2_tl_descr *d,
			 struct c2_tl *list, void *obj);

/**
   Adds an element after another element of the list.

   @pre !c2_tlink_is_in(d, new)
   @post c2_tlink_is_in(d, new)
 */
void   c2_tlist_add_after(const struct c2_tl_descr *d, void *obj, void *new);

/**
   Adds an element before another element of the list.

   @pre !c2_tlink_is_in(d, new)
   @post c2_tlink_is_in(d, new)
 */
void   c2_tlist_add_before(const struct c2_tl_descr *d, void *obj, void *new);

/**
   Deletes an element from the list.

   @pre   c2_tlink_is_in(d, obj)
   @post !c2_tlink_is_in(d, obj)
 */
void   c2_tlist_del(const struct c2_tl_descr *d, void *obj);

/**
   Moves an element from a list to the head of (possibly the same) list.

   @pre  c2_tlink_is_in(d, obj)
   @post c2_tlink_is_in(d, obj)
 */
void   c2_tlist_move(const struct c2_tl_descr *d,
		     struct c2_tl *list, void *obj);

/**
   Moves an element from a list to the tail of (possibly the same) list.

   @pre  c2_tlink_is_in(d, obj)
   @post c2_tlink_is_in(d, obj)
 */
void   c2_tlist_move_tail(const struct c2_tl_descr *d,
			  struct c2_tl *list, void *obj);
/**
   Returns the first element of a list or NULL if the list is empty.
 */
void  *c2_tlist_head(const struct c2_tl_descr *d, struct c2_tl *list);

/**
   Returns the last element of a list or NULL if the list is empty.
 */
void  *c2_tlist_tail(const struct c2_tl_descr *d, struct c2_tl *list);

/**
   Returns the next element of a list or NULL if @obj is the last element.

   @pre c2_tlist_contains(d, list, obj)
 */
void  *c2_tlist_next(const struct c2_tl_descr *d, struct c2_tl *list,
		     void *obj);
/**
   Returns the previous element of a list or NULL if @obj is the first element.

   @pre c2_tlist_contains(d, list, obj)
 */
void  *c2_tlist_prev(const struct c2_tl_descr *d, struct c2_tl *list,
		     void *obj);
/**
   A variant of c2_tlist_prev() that returns NULL, when obj parameter is
   NULL. This is used by c2_tlist_for(). Compare with (CDR NIL) being NIL.
 */
void *c2_tlist_next_safe(const struct c2_tl_descr *d, struct c2_tl *list,
			 void *obj);
/**
   Iterates over elements of list @head of type @d, assigning them in order
   (from head to tail) to @obj.

   It is safe to delete the "current" object in the body of the loop or modify
   the portion of the list preceding the current element.
 */
#define c2_tlist_for(d, head, obj)					\
	for (void *__tl_tmp, obj = c2_tlist_head(d, head),		\
	     __tl_tmp = c2_tlist_next_safe(d, head, obj);		\
	     obj != NULL;						\
	     obj = __tl_tmp, __tl_tmp = c2_tlist_next_safe(d, head, __tl_tmp))

/** @} end of tlist group */

/* __COLIBRI_LIB_TLIST_H__ */
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
