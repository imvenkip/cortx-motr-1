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
   flexibility beyond what tlist provides (e.g., a cyclic list without a head
   object) is necessary.

   Similarly to c2_list, tlist is a purely algorithmic module: it deals with
   neither concurrency nor liveness nor with any similar issues that its callers
   are supposed to handle.

   To describe a typical tlist usage pattern, suppose that one wants a list of
   objects of type foo hanging off every object of type bar.

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
             .td_name        = "foo-s of bar",
	     .td_link_offset = offsetof(struct foo, f_linkage),
	     .td_head_magic  = 0x666f6f6261726865 // "foobarhe"
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
	   .td_link_magic_offset = offsetof(struct foo, f_magic),
	   .td_link_magic        = 0x666f6f6261726c69 // "foobarli"
   };
   @endcode

   Magic field can be shared by multiple tlist links embedded in the same object
   and can be used for other sanity checking. An "outermost" finaliser function
   must clear the magic as its last step to catch use-after-fini errors.

   Now, one can populate and manipulate foo-bar lists:

   @code
   struct bar  B;
   struct foo  F;
   struct foo *scan;

   c2_tlist_init(&B.b_list);
   c2_tlink_init(&F.f_linkage);

   c2_tlist_add(&foobar_list, &B.b_list, &F);
   C2_ASSERT(c2_tl_contains(&foobar_list, &B.b_list, &F));

   c2_tlist_for(&foobar_list, &B.b_list, scan)
           C2_ASSERT(scan == &F);
   c2_tlist_endfor;
   @endcode

   @note Differently from c2_list, tlist heads and links must be initialised
   before use, even when first usage overwrites the entity completely. This
   allows stronger checking in tlist manipulation functions.

   <b>Type-safe macros.</b>

   C2_TL_DESCR_DECLARE(), C2_TL_DECLARE(), C2_TL_DESCR_DEFINE() and
   C2_TL_DEFINE() macros generate versions of tlist interface tailored for a
   particular use case.

   4 separate macros are necessary for flexibility. They should be used in
   exactly one of the following ways for any given typed list:

       - static tlist, used in a single module only: C2_TL_DEFINE() and
         C2_TL_DESCR_DEFINE() with scope "static" in the module .c file;

       - tlist exported from a module: C2_TL_DEFINE() and C2_TL_DESCR_DEFINE()
         with scope "" in .c file and C2_TL_DESCR_DECLARE(), C2_TL_DECLARE()
         with scope "extern" in .h file;

       - tlist exported from a module as a collection of inline functions:
         C2_TL_DESCR_DEFINE() in .c file and C2_TL_DESCR_DECLARE() with scope
         "extern" followed by C2_TL_DEFINE() with scope "static inline" in .h
         file.

   Use c2_tl_for() and c2_tl_endfor() to iterate over lists generated by
   C2_TL_DECLARE() and C2_TL_DEFINE().

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
     td_link_magic_offset |  |           |  |
                          v  |           |  |
                             |-----------|  |
                             |link magic |  | td_link_offset
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
    +----------------------------------------------------------+

   @endverbatim
 */
struct c2_tl_descr {
	/** Human-readable list name, used for error messages. */
	const char *td_name;
	/** Offset of list link (c2_tlink) in the ambient object. */
	int         td_link_offset;
	/**
	    Offset of magic field in the ambient object.
	    This is used only when link magic checking is on.

	    @see c2_tl_descr::td_link_magic
	 */
	int         td_link_magic_offset;
	/**
	    Magic stored in an ambient object.

	    If this field is 0, link magic checking is disabled.
	 */
	uint64_t    td_link_magic;
	/**
	    Magic stored in c2_tl::t_magic and checked on all tlist
	    operations.
	 */
	uint64_t    td_head_magic;
};

#define C2_TL_DESCR(name, ambient_type, link_field, link_magic_field,	\
                    link_magic, head_magic)				\
{									\
	.td_name              = name,					\
	.td_link_offset       = offsetof(ambient_type, link_field),	\
	.td_link_magic_offset = offsetof(ambient_type, link_magic_field), \
	.td_link_magic        = link_magic,				\
	.td_head_magic        = head_magic				\
};									\
									\
C2_BASSERT(C2_HAS_TYPE(C2_FIELD_VALUE(ambient_type, link_field),	\
		       struct c2_tlink));				\
C2_BASSERT(C2_HAS_TYPE(C2_FIELD_VALUE(ambient_type, link_magic_field),	\
		       uint64_t))


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
void c2_tlink_fini(const struct c2_tl_descr *d, void *obj);
void c2_tlink_init_at(const struct c2_tl_descr *d,
		      void *obj, struct c2_tl *list);
void c2_tlink_init_at_tail(const struct c2_tl_descr *d,
			   void *obj, struct c2_tl *list);
void c2_tlink_del_fini(const struct c2_tl_descr *d, void *obj);

bool c2_tlist_invariant(const struct c2_tl_descr *d, const struct c2_tl *list);
bool c2_tlink_invariant(const struct c2_tl_descr *d, const void *obj);
bool c2_tlist_invariant_ext(const struct c2_tl_descr *d,
			    const struct c2_tl *list,
			    bool (*check)(const void *, void *), void *datum);

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
void   c2_tlist_move(const struct c2_tl_descr *d, struct c2_tl *list, void *obj);

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
void  *c2_tlist_head(const struct c2_tl_descr *d, const struct c2_tl *list);

/**
   Returns the last element of a list or NULL if the list is empty.
 */
void  *c2_tlist_tail(const struct c2_tl_descr *d, const struct c2_tl *list);

/**
   Returns the next element of a list or NULL if @obj is the last element.

   @pre c2_tlist_contains(d, list, obj)
 */
void  *c2_tlist_next(const struct c2_tl_descr *d,
		     const struct c2_tl *list, void *obj);

/**
   Returns the previous element of a list or NULL if @obj is the first element.

   @pre c2_tlist_contains(d, list, obj)
 */
void  *c2_tlist_prev(const struct c2_tl_descr *d,
		     const struct c2_tl *list, void *obj);

/**
   Iterates over elements of list @head of type @descr, assigning them in order
   (from head to tail) to @obj.

   It is safe to delete the "current" object in the body of the loop or modify
   the portion of the list preceding the current element. It is *not* safe to
   modify the list after the current point.

   @code
   c2_tlist_for(&foobar_list, &B.b_list, foo)
           sum += foo->f_value;
   c2_tlist_endfor;

   c2_tlist_for(&foobar_list, &B.b_list, foo) {
           if (foo->f_value % sum == 0)
	           c2_tlist_del(&foobar_list, foo);
   } c2_tlist_endfor;
   @endcode

   c2_tlist_for() macro has a few points of technical interest:

       - it introduces a scope to declare a temporary variable to hold the
         pointer to a "next" list element. The undesirable result of this is
         that the loop has to be terminated by the matching c2_tlist_endfor
         macro, closing the hidden scope. An alternative would be to use C99
         syntax for iterative statement, which allows a declaration in the
         for-loop header. Unfortunately, even though C99 mode can be enforced
         for compilation of linux kernel modules (by means of CFLAGS_MODULE),
         the kernel doesn't compile correctly in this mode;

       - "inventive" use of comma expression in the loop condition allows to
         calculate next element only once and only when the current element is
         not NULL.

   @see c2_tlist_endfor
 */
#define c2_tlist_for(descr, head, obj)					\
do {									\
	void *__tl;							\
									\
	for (obj = c2_tlist_head(descr, head);				\
	     obj != NULL &&						\
             ((void)(__tl = c2_tlist_next(descr, head, obj)), true);	\
	     obj = __tl) {

/**
   Terminates c2_tlist_for() loop.
 */
#define c2_tlist_endfor ;} (void)__tl; } while (0)

/**
 * Returns a conjunction (logical AND) of an expression evaluated for each list
 * element.
 *
 * Declares a void pointer variable named "var" in a new scope and evaluates
 * user-supplied expression (the last argument) with "var" iterated over
 * successive list elements, while this expression returns true. Returns true
 * iff the whole list was iterated over.
 *
 * The list can be modified by the user-supplied expression.
 *
 * This function is useful for invariant checking.
 *
 * @code
 * bool foo_invariant(const struct foo *f)
 * {
 *         return f_state_is_valid(f) &&
 *                c2_tlist_forall(bars_descr, bar, &f->f_bars,
 *                                bar_is_valid(bar));
 * }
 * @endcode
 *
 * @see c2_forall(), c2_tl_forall(), c2_list_forall(), c2_list_entry_forall().
 */
#define c2_tlist_forall(descr, var, head, ...)	\
({						\
	void *var;				\
						\
	c2_tlist_for(descr, head, var) {	\
		if (!({ __VA_ARGS__ ; }))	\
			break;			\
	} c2_tlist_endfor;			\
	var == NULL;				\
})

#define C2_TL_DESCR_DECLARE(name, scope)	\
scope const struct c2_tl_descr name ## _tl

/**
   Declares a version of tlist interface with definitions adjusted to take
   parameters of a specified ambient type (rather than void) and to hide
   c2_tl_descr from signatures.

   @code
   C2_TL_DECLARE(foo, static, struct foo);
   @endcode

   declares

   @code
   static void foo_tlist_init(struct c2_tl *head);
   static void foo_tlink_init(struct foo *amb);
   static void foo_tlist_move(struct c2_tl *list, struct foo *amb);
   static struct foo *foo_tlist_head(const struct c2_tl *list);
   @endcode

   &c.

   @see C2_TL_DEFINE()
   @see C2_TL_DESCR_DEFINE()
 */
#define C2_TL_DECLARE(name, scope, amb_type)				\
									\
scope void name ## _tlist_init(struct c2_tl *head);			\
scope void name ## _tlist_fini(struct c2_tl *head);			\
scope void name ## _tlink_init(amb_type *amb);				\
scope void name ## _tlink_init_at(amb_type *amb, struct c2_tl *head);	\
scope void name ## _tlink_init_at_tail(amb_type *amb, struct c2_tl *head);\
scope void name ## _tlink_fini(amb_type *amb);				\
scope void name ## _tlink_del_fini(amb_type *amb);			\
scope bool name ## _tlist_invariant(const struct c2_tl *head);		\
scope bool name ## _tlist_invariant_ext(const struct c2_tl *head,       \
                                        bool (*check)(const amb_type *, \
                                        void *), void *);		\
scope bool   name ## _tlist_is_empty(const struct c2_tl *list);		\
scope bool   name ## _tlink_is_in   (const amb_type *amb);		\
scope bool   name ## _tlist_contains(const struct c2_tl *list,		\
				     const amb_type *amb);		\
scope size_t name ## _tlist_length(const struct c2_tl *list);		\
scope void   name ## _tlist_add(struct c2_tl *list, amb_type *amb);	\
scope void   name ## _tlist_add_tail(struct c2_tl *list, amb_type *amb); \
scope void   name ## _tlist_add_after(amb_type *amb, amb_type *new);	\
scope void   name ## _tlist_add_before(amb_type *amb, amb_type *new);	\
scope void   name ## _tlist_del(amb_type *amb);				\
scope void   name ## _tlist_move(struct c2_tl *list, amb_type *amb);	\
scope void   name ## _tlist_move_tail(struct c2_tl *list, amb_type *amb); \
scope amb_type *name ## _tlist_head(const struct c2_tl *list);		\
scope amb_type *name ## _tlist_tail(const struct c2_tl *list);		\
scope amb_type *name ## _tlist_next(const struct c2_tl *list, amb_type *amb);	\
scope amb_type *name ## _tlist_prev(const struct c2_tl *list, amb_type *amb)

#define __AUN __attribute__((unused))

/**
   Defines a tlist descriptor (c2_tl_descr) for a particular ambient type.
 */
#define C2_TL_DESCR_DEFINE(name, hname, scope, amb_type, amb_link_field, \
		     amb_magic_field, amb_magic, head_magic)		\
scope const struct c2_tl_descr name ## _tl = C2_TL_DESCR(hname,		\
							 amb_type,	\
							 amb_link_field, \
							 amb_magic_field, \
							 amb_magic,	\
							 head_magic)

/**
   Defines functions declared by C2_TL_DECLARE().

   The definitions generated assume that tlist descriptor, defined by
   C2_TL_DESC_DEFINED() is in scope.
 */
#define C2_TL_DEFINE(name, scope, amb_type)				\
									\
scope __AUN void name ## _tlist_init(struct c2_tl *head)		\
{									\
	c2_tlist_init(&name ## _tl, head);				\
}									\
									\
scope __AUN void name ## _tlist_fini(struct c2_tl *head)		\
{									\
	c2_tlist_fini(&name ## _tl, head);				\
}									\
									\
scope __AUN void name ## _tlink_init(amb_type *amb)			\
{									\
	c2_tlink_init(&name ## _tl, amb);				\
}									\
									\
scope __AUN void name ## _tlink_init_at(amb_type *amb, struct c2_tl *head) \
{									\
	c2_tlink_init_at(&name ## _tl, amb, head);			\
}									\
									\
scope __AUN void name ## _tlink_init_at_tail(amb_type *amb, struct c2_tl *head) \
{									\
	c2_tlink_init_at_tail(&name ## _tl, amb, head);			\
}									\
									\
scope __AUN void name ## _tlink_fini(amb_type *amb)			\
{									\
	c2_tlink_fini(&name ## _tl, amb);				\
}									\
									\
scope __AUN void name ## _tlink_del_fini(amb_type *amb)			\
{									\
	c2_tlink_del_fini(&name ## _tl, amb);				\
}									\
									\
scope __AUN bool name ## _tlist_invariant(const struct c2_tl *list)	\
{									\
	return c2_tlist_invariant(&name ## _tl, list);			\
}									\
									\
scope __AUN bool name ## _tlist_invariant_ext(const struct c2_tl *list, \
					      bool (*check)(const amb_type *,\
					      void *), void *datum)		\
{									\
	return c2_tlist_invariant_ext(&name ## _tl, list,               \
			 (bool (*)(const void *, void *))check, datum);	\
}									\
									\
scope __AUN bool   name ## _tlist_is_empty(const struct c2_tl *list)	\
{									\
	return c2_tlist_is_empty(&name ## _tl, list);			\
}									\
									\
scope __AUN bool   name ## _tlink_is_in   (const amb_type *amb)		\
{									\
	return c2_tlink_is_in(&name ## _tl, amb);			\
}									\
									\
scope __AUN bool   name ## _tlist_contains(const struct c2_tl *list,	\
				     const amb_type *amb)		\
{									\
	return c2_tlist_contains(&name ## _tl, list, amb);		\
}									\
									\
scope __AUN size_t name ## _tlist_length(const struct c2_tl *list)	\
{									\
	return c2_tlist_length(&name ## _tl, list);			\
}									\
									\
scope __AUN void   name ## _tlist_add(struct c2_tl *list, amb_type *amb) \
{									\
	c2_tlist_add(&name ## _tl, list, amb);				\
}									\
									\
scope __AUN void   name ## _tlist_add_tail(struct c2_tl *list, amb_type *amb) \
{									\
	c2_tlist_add_tail(&name ## _tl, list, amb);			\
}									\
									\
scope __AUN void   name ## _tlist_add_after(amb_type *amb, amb_type *new) \
{									\
	c2_tlist_add_after(&name ## _tl, amb, new);			\
}									\
									\
scope __AUN void   name ## _tlist_add_before(amb_type *amb, amb_type *new) \
{									\
	c2_tlist_add_before(&name ## _tl, amb, new);			\
}									\
									\
scope __AUN void   name ## _tlist_del(amb_type *amb)			\
{									\
	c2_tlist_del(&name ## _tl, amb);				\
}									\
									\
scope __AUN void   name ## _tlist_move(struct c2_tl *list, amb_type *amb) \
{									\
	c2_tlist_move(&name ## _tl, list, amb);				\
}									\
									\
scope __AUN void   name ## _tlist_move_tail(struct c2_tl *list, amb_type *amb) \
{									\
	c2_tlist_move_tail(&name ## _tl, list, amb);			\
}									\
									\
scope __AUN amb_type *name ## _tlist_head(const struct c2_tl *list)	\
{									\
	return c2_tlist_head(&name ## _tl, list);			\
}									\
									\
scope __AUN amb_type *name ## _tlist_tail(const struct c2_tl *list)	\
{									\
	return c2_tlist_tail(&name ## _tl, list);			\
}									\
									\
scope __AUN amb_type *name ## _tlist_next(const struct c2_tl *list,     \
				     amb_type *amb)			\
{									\
	return c2_tlist_next(&name ## _tl, list, amb);			\
}									\
									\
scope __AUN amb_type *name ## _tlist_prev(const struct c2_tl *list,     \
				     amb_type *amb)                     \
{									\
	return c2_tlist_prev(&name ## _tl, list, amb);			\
}									\
									\
struct __ ## name ## _terminate_me_with_a_semicolon { ; }

/**
 * A version of c2_tlist_for() to use with tailored lists.
 *
 * c2_tl_for() loop is terminated with c2_tl_endfor().
 */
#define c2_tl_for(name, head, obj) c2_tlist_for(& name ## _tl, head, obj)

/**
 * Terminates c2_tl_for() loop.
 */
#define c2_tl_endfor c2_tlist_endfor

/**
 * Returns a conjunction (logical AND) of an expression evaluated for each list
 * element.
 *
 * Declares a variable named "var" of list ambient object type in a new scope
 * and evaluates user-supplied expression (the last argument) with "var"
 * iterated over successive list elements, while this expression returns
 * true. Returns true iff the whole list was iterated over.
 *
 * The list can be modified by the user-supplied expression.
 *
 * This function is useful for invariant checking.
 *
 * @code
 * bool foo_invariant(const struct foo *f)
 * {
 *         return c2_tl_forall(bar, b, &f->f_bars,
 *                             b->b_count > 0 && b->b_parent == f);
 * }
 * @endcode
 *
 * @see c2_forall(), c2_tlist_forall(), c2_list_forall(), c2_list_entry_forall().
 */
#define c2_tl_forall(name, var, head, ...)		\
({							\
	typeof (name ## _tlist_head(NULL)) var;		\
							\
	c2_tlist_for(& name ## _tl, head, var) {	\
		if (!({ __VA_ARGS__ ; }))		\
			break;				\
	} c2_tlist_endfor;				\
	var == NULL;					\
})

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
