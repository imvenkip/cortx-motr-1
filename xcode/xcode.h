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
 * Original creation date: 25-Dec-2011
 */

#pragma once

#ifndef __COLIBRI_XCODE_XCODE_H__
#define __COLIBRI_XCODE_XCODE_H__

#include "lib/vec.h"                /* c2_bufvec_cursor */
#include "lib/types.h"              /* c2_bcount_t */
#include "xcode/xcode_attr.h"       /* C2_XC_ATTR */

/**
   @defgroup xcode

   xcode module implements a modest set of introspection facilities. A user
   defines a structure (c2_xcode_type) which describes the in memory layout of a
   C data-type. xcode provides interfaces to iterate over hierarchy of such
   descriptors and to associate user defined state with types and fields.

   A motivating example of xcode usage is universal encoding and decoding
   interface (c2_xcode_decode(), c2_xcode_encode(), c2_xcode_length()) which
   converts between an in-memory object and its serialized representation.

   Other usages of xcoding interfaces are:

       - pretty-printing,

       - pointer swizzling and adjustment when loading RVM segments into memory,

       - consistency checking: traversing data-structures in memory validating
         check-sums and invariants.

   Not every C data-structure can be represented by xcode. The set of
   representable data-structures is defined inductively according to the
   "aggregation type" of data-structure:

       - ATOM aggregation type: scalar data-types void, uint8_t, uint32_t and
         uint64_t are representable,

       - RECORD aggregation type: a struct type, whose members are all
         representable is representable,

       - UNION aggregation type: a "discriminated union" structure of the form

         @code
         struct {
                 scalar_t discriminator;
	         union {
	                 ...
	         } u;
         };
         @endcode

         where scalar_t is one of the scalar data-types mentioned above and all
         union fields are representable is representable,

       - SEQUENCE aggregation type: a "counted array" structure of the form

         @code
         struct {
                 scalar_t  nr;
		 el_t     *el;
         };
         @endcode

         where scalar_t is one of the scalar data-types mentioned above and el_t
         is representable is representable,

       - OPAQUE aggregation type: pointer type is representable when it is used
         as the type of a field in a representable type and a special function
         (c2_xcode_field::xf_opaque()) is assigned to the field, which returns
         the representation of the type of an object the pointer points to.

         The usage of function allows representation of pointer structures where
         the actual type of the object pointed to depends on the contents of its
         parent structure.

   A representable type is described by an instance of struct c2_xcode_type,
   which describes type attributes and also contains an array
   (c2_xcode_type::xct_child[]) of "fields". A field is represented by struct
   c2_xcode_field and describes a sub-object. The field points to a
   c2_xcode_type instance, describing the type of sub-object. This way a
   hierarchy (forest of trees) of types is organized. Its leaves are atomic
   types.

   Sub-objects are located contiguously in memory, except for SEQUENCE elements
   and OPAQUE fields.

   xcode description of a data-type can be provided either by

       - manually creating an instance of c2_xcode_type structure, describing
         properties of the data-type or

       - by creating a description of the desired serialized format of a
         data-type and using ff2c translator (xcode/ff2c/ff2c.c) to produce C
         files (.c and .h) containing the matching data-type definitions and
         xcode descriptors.

   The first method is suitable for memory-only structures. The second method is
   for structures designed to be transmitted over network of stored on
   persistent storage (fops, db records, &c.).
 */
/** @{ */

/* import */
struct c2_bufvec_cursor;

/* export */
struct c2_xcode;
struct c2_xcode_type;
struct c2_xcode_type_ops;
struct c2_xcode_ctx;
struct c2_xcode_obj;
struct c2_xcode_field;
struct c2_xcode_cursor;

/**
   Type of aggregation for a data-type.

   A value of this enum, stored in c2_code_type::xct_aggr determines how fields
   of the type are interpreted.
 */
enum c2_xcode_aggr {
	/**
	   RECORD corresponds to C struct. Fields of RECORD type are located one
	   after another in memory.
	 */
	C2_XA_RECORD,
	/**
	   UNION corresponds to discriminated union. Its first field is referred
	   to as a "discriminator" and has an atomic type. Other fields of union
	   are tagged (c2_xcode_field::xf_tag) and the value of the
	   discriminator field determines which of the following fields is
	   actually used.

	   @note that, similarly to C2_XA_SEQUENCE, the discriminator field can
	   have C2_XAT_VOID type. In this case its tag is used instead of its
	   value (use cases are not clear).
	 */
	C2_XA_UNION,
	/**
	   SEQUENCE corresponds to counted array. Sequence always has two
	   fields: a scalar "counter" field and a field denoting the element of
	   the array.

	   @note if counter has type C2_XAT_VOID, its tag used as a
	   counter. This is used to represent fixed size arrays without an
	   explicit counter field.
	 */
	C2_XA_SEQUENCE,
	/**
	   TYPEDEF is an alias for another type. It always has a single field.
	 */
	C2_XA_TYPEDEF,
	/**
	   OPAQUE represents a pointer.

	   A field of OPAQUE type must have c2_xcode_field::xf_opaque() function
	   pointer set to a function which determines the actual type of the
	   object pointed to.
	 */
	C2_XA_OPAQUE,
	/**
	   ATOM represents "atomic" data-types having no internal
	   structure. c2_xcode_type-s with c2_xcode_type::xct_aggr set to
	   C2_XA_ATOM have c2_xcode_type::xct_nr == 0 and no fields.

	   Atomic types are enumerated in c2_xode_atom_type.
	 */
	C2_XA_ATOM,
	C2_XA_NR
};

/**
   Human-readable names of c2_xcode_aggr values.
 */
extern const char *c2_xcode_aggr_name[C2_XA_NR];

/**
    Atomic types.

    To each value of this enumeration, except for C2_XAT_NR, a separate
    c2_xcode_type (C2_XT_VOID, C2_XT_U8, &c.).
 */
enum c2_xode_atom_type {
	C2_XAT_VOID,
	C2_XAT_U8,
	C2_XAT_U32,
	C2_XAT_U64,

	C2_XAT_NR
};

/** Human-readable names of elements of c2_xcode_atom_type */
extern const char *c2_xcode_atom_type_name[C2_XAT_NR];

enum { C2_XCODE_DECOR_MAX = 10 };

/** Field of data-type. */
struct c2_xcode_field {
	/** Field name. */
	const char                 *xf_name;
	/** Field type. */
	const struct c2_xcode_type *xf_type;
	/** Tag, associated with this field.

	    Tag is used in the following ways:

	        - if first field of a SEQUENCE type has type VOID, its tag is
                  used as a count of element in the sequence;

		- tag of non-first field of a UNION type is used to determine
                  when the field is actually present in the object: the field is
                  present iff its tag equals the discriminator of the union.

		  The discriminator is the value of the first field of the
		  union.
	 */
	uint64_t                    xf_tag;
	/**
	   Fields with c2_xcode_type::xf_type == &C2_XT_OPAQUE are "opaque"
	   fields. An opaque field corresponds to a
	   pointer. c2_xcode_type::xf_opaque() is called by the xcode to follow
	   the pointer. This function returns (in its "out" parameter) a type of
	   the object pointed to. "par" parameter refers to the parent object to
	   which the field belongs.
	 */
	int                       (*xf_opaque)(const struct c2_xcode_obj   *par,
					       const struct c2_xcode_type **out);
	/**
	   Byte offset of this field from the beginning of the object.
	 */
	uint32_t                    xf_offset;
	/**
	   "Decorations" are used by xcode users to associate additional
	   information with introspection elements.

	   @see c2_xcode_decor_register()
	   @see c2_xcode_type::xct_decor[]
	 */
	void                       *xf_decor[C2_XCODE_DECOR_MAX];
};

/**
   This struct represents a data-type.
 */
struct c2_xcode_type {
	/** What sub-objects instances of this type have and how they are
	    organized? */
	enum c2_xcode_aggr              xct_aggr;
	/** Type name. */
	const char                     *xct_name;
	/** Custom operations. */
	const struct c2_xcode_type_ops *xct_ops;
	/**
	    Which atomic type this is?

	    This field is valid only when xt->xct_aggr == C2_XA_ATOM.
	 */
	enum c2_xode_atom_type          xct_atype;
	/**
	   "Decorations" are used by xcode users to associate additional
	   information with introspection elements.

	   @see c2_xcode_decor_register()
	   @see c2_xcode_field::xf_decor[]
	 */
	void                           *xct_decor[C2_XCODE_DECOR_MAX];
	/** Size in bytes of in-memory instances of this type. */
	size_t                          xct_sizeof;
	/** Number of fields. */
	size_t                          xct_nr;
	/** Array of fields. */
	struct c2_xcode_field           xct_child[0];
};

/** "Typed" xcode object. */
struct c2_xcode_obj {
	/** Object's type. */
	const struct c2_xcode_type *xo_type;
	/** Pointer to object in memory. */
	void                       *xo_ptr;
};

/**
   Custom xcoding functions.

   User provides these functions (which are all optional) to use non-standard
   xcoding.

   @see c2_xcode_decode()
 */
struct c2_xcode_type_ops {
	int (*xto_length)(struct c2_xcode_ctx *ctx, const void *obj);
	int (*xto_encode)(struct c2_xcode_ctx *ctx, const void *obj);
	int (*xto_decode)(struct c2_xcode_ctx *ctx, void *obj);
};

enum { C2_XCODE_DEPTH_MAX = 10 };

/**
   @name iteration

   xcode provides an iteration interface to walk through the hierarchy of types
   and fields.

   This interface consists of two functions: c2_xcode_next(), c2_xcode_skip()
   and a c2_xcode_cursor data-type.

   c2_xcode_next() takes a starting type (c2_xcode_type) and walks the tree of
   its fields, their types, their fields &c., all the way down to the atomic
   types.

   c2_xcode_next() can be used to walk the tree in any "standard" order:
   preorder, inorder and postorder traversals are supported. To this end,
   c2_xcode_next() visits each tree node multiple times, setting the flag
   c2_xcode_cursor::xcu_stack[]::s_flag according to the order.
 */
/** @{ */

/**
    Traversal order.
 */
enum c2_xcode_cursor_flag {
	/** This value is never returned by c2_xcode_next(). It is set by the
	    user to indicate the beginning of iteration. */
	C2_XCODE_CURSOR_NONE,
	/** Tree element is visited for the first time. */
	C2_XCODE_CURSOR_PRE,
	/** The sub-tree, rooted at an element's field has been processed
	    fully. */
	C2_XCODE_CURSOR_IN,
	/** All fields have been processed fully, this is the last time the
	    element is visited. */
	C2_XCODE_CURSOR_POST,
	C2_XCODE_CURSOR_NR
};

/** Human-readable names of values in c2_xcode_cursor_flag */
extern const char *c2_xcode_cursor_flag_name[C2_XCODE_CURSOR_NR];

/**
    Cursor that captures the state of iteration.

    The cursor contains a stack of "frames". A frame describes the iteration at
    a particular level.
 */
struct c2_xcode_cursor {
	/** Depth of the iteration. */
	int xcu_depth;
	struct c2_xcode_cursor_frame {
		/** An object that the iteration is currently in. */
		struct c2_xcode_obj       s_obj;
		/** A field within the object that the iteration is currently
		    at. */
		int                       s_fieldno;
		/** A sequence element within the field that the iteration is
		    currently at.

		    This is valid iff ->s_obj->xo_type->xcf_aggr ==
		    C2_XA_SEQUENCE.
		 */
		uint64_t                  s_elno;
		/** Flag, indicating visiting order. */
		enum c2_xcode_cursor_flag s_flag;
		/** Datum reserved for cursor users. */
		uint64_t                  s_datum;
	} xcu_stack[C2_XCODE_DEPTH_MAX];
};

void c2_xcode_cursor_init(struct c2_xcode_cursor *it,
			  const struct c2_xcode_obj *obj);

/**
   Iterates over tree of xcode types.

   To start the iteration, call this with the cursor where
   c2_xcode_cursor_frame::s_obj field of the 0th stack frame is set to the
   desired object and the rest of the cursor is zeroed (see
   c2_xcode_ctx_init()).

   c2_xcode_next() returns a positive value when iteration can be continued, 0
   when the iteration is complete and negative error code on error. The intended
   usage pattern is

   @code
   while ((result = c2_xcode_next(it)) > 0) {
           ... process next tree node ...
   }
   @endcode

   On each return, c2_xcode_next() sets the cursor to point to the next element
   reached in iteration. The information about the element is stored in the
   topmost element of the cursor's stack and can be extracted with
   c2_xcode_cursor_top().

   An element with N children is reached 1 + N + 1 times: once in preorder, once
   in inorder after each child is processed and once in postorder. Here N equals

       - number of fields in a RECORD object;

       - 1 or 2 in a UNION object: one for discriminator and one for an actually
         present field, if any;

       - 1 + (number of elements in array) in a SEQUENCE object. Additional 1 is
         for count field;

       - 0 for an ATOMIC object.

   For example, to traverse the tree in preorder, one does something like

   @code
   while ((result = c2_xcode_next(it)) > 0) {
           if (c2_xcode_cursor_top(it)->s_flag == C2_XCODE_CURSOR_PRE) {
	           ... process the element ...
           }
   }
   @endcode
 */
int  c2_xcode_next(struct c2_xcode_cursor *it);

/**
   Abandons the iteration at the current level and returns one level up.
 */
void c2_xcode_skip(struct c2_xcode_cursor *it);

/** Returns the topmost frame in the cursor's stack. */
struct c2_xcode_cursor_frame *c2_xcode_cursor_top(struct c2_xcode_cursor *it);

/** @} iteration. */

/**
   @name xcoding.

   Encoding-decoding (collectively xcoding) support is implemented on top of
   introspection facilities provided by the xcode module. xcoding provides 3
   operations:

       - sizing (c2_xcode_length()): returns the size of a buffer sufficient to
         hold serialized object representation;

       - encoding (c2_xcode_encode()): constructs a serialized object
         representation in a given buffer;

       - decoding (c2_xcode_decode()): constructs an in-memory object, given its
         serialized representation.

   xcoding traverses the tree of sub-objects, starting from the topmost object
   to be xcoded. For each visited object, if a method, corresponding to the
   xcoding operation (encode, decode, length) is not NULL in object's type
   c2_xcode_type_ops vector, this method is called and no further processing of
   this object is done. Otherwise, "standard xcoding" takes place.

   Standard xcoding is non-trivial only for leaves in the sub-object tree (i.e.,
   for objects of ATOM aggregation type):

       - for encoding, place object's value into the buffer, convert it to
         desired endianness and advance buffer position;

       - for decoding, extract value from the buffer, convert it, store in the
         in-memory object and advance buffer position;

       - for sizing, increment required buffer size by the size of atomic type.

   In addition, decoding allocates memory as necessary.
 */
/** @{ xcoding */

/** Endianness (http://en.wikipedia.org/wiki/Endianness) */
enum c2_xcode_endianness {
	/** Little-endian. */
	C2_XEND_LE,
	/** Big-endian. */
	C2_XEND_BE,
	C2_XEND_NR
};

/** Human-readable names of values in c2_xcode_endianness */
extern const char *c2_xcode_endianness_name[C2_XEND_NR];

/** xcoding context.

    The context contains information about attributes of xcoding operation and
    its progress.
 */
struct c2_xcode_ctx {
	/** Endianness of serialized representation. */
	enum c2_xcode_endianness xcx_end;
	/**
	    Current point in the buffer vector.

	    The cursor points to the where encoding will write the next byte and
	    from where decoding will read the next byte.
	 */
	struct c2_bufvec_cursor  xcx_buf;
	/**
	   State of the iteration through object tree.
	 */
	struct c2_xcode_cursor   xcx_it;
	/**
	   Allocation function used by decoding to allocate the topmost object
	   and all its non-inline sub-objects (arrays and opaque sub-objects).
	 */
	void                  *(*xcx_alloc)(struct c2_xcode_cursor *ctx, size_t);
};

/**
   Sets up the context to start xcoding of a given object.
 */
void c2_xcode_ctx_init(struct c2_xcode_ctx *ctx, const struct c2_xcode_obj *obj);

int c2_xcode_decode(struct c2_xcode_ctx *ctx);
int c2_xcode_encode(struct c2_xcode_ctx *ctx);

/** Calculates the length of serialized representation. */
int c2_xcode_length(struct c2_xcode_ctx *ctx);
void *c2_xcode_alloc(struct c2_xcode_cursor *it, size_t nob);
/** @} xcoding. */

/**
 * Reads an object from a human-readable string representation.
 *
 * String has the following EBNF grammar:
 *
 *     S           ::= RECORD | UNION | SEQUENCE | ATOM
 *     RECORD      ::= '(' [S-LIST] ')'
 *     S-LIST      ::= S | S-LIST ',' S
 *     UNION       ::= '{ TAG '|' [S] '}'
 *     SEQUENCE    ::= STRING | ARRAY
 *     STRING      ::= '"' CHAR* '"'
 *     ARRAY       ::= '[' COUNT ':' [S-LIST] ']'
 *     ATOM        ::= EMPTY | NUMBER
 *     TAG         ::= ATOM
 *     COUNT       ::= ATOM
 *
 * Where CHAR is any non-NUL character, NUMBER is anything recognizable by
 * sscanf(3) as a number and EMPTY is the empty string. White-spaces between
 * tokens are ignored.
 *
 * Examples:
 * @verbatim
 * (0, 1)
 * (0, (1, 2))
 * ()
 * {1| (1, 2)}
 * {2| 6}
 * {3|}               -- a union with invalid discriminant or with a void value
 * [0:]               -- 0 sized array
 * [3: 6, 5, 4]
 * [: 1, 2, 3]        -- fixed size sequence
 * "incomprehensible" -- a byte (U8) sequence with 16 elements
 * 10                 -- number 10
 * 0xa                -- number 10
 * 012                -- number 10
 * (0, "katavothron", {42| [3: ("x"), ("y"), ("z")]}, "paradiorthosis")
 * @endverbatim
 *
 * Typedefs and opaque types require no special syntax.
 *
 * @retval 0 success
 * @retval -EPROTO syntax error
 * @retval -EINVAL garbage in string after end of representation
 * @retval -ve other error (-ENOMEM, &c.)
 *
 * Error or not, the caller should free the (partially) constructed object with
 * c2_xcode_free().
 */
int  c2_xcode_read(struct c2_xcode_obj *obj, const char *str);
void c2_xcode_free(struct c2_xcode_obj *obj);
int  c2_xcode_cmp (const struct c2_xcode_obj *o0, const struct c2_xcode_obj *o1);

/**
   Returns the address of a sub-object within an object.

   @param obj     - typed object
   @param fieldno - ordinal number of field
   @param elno    - for a SEQUENCE field, index of the element to
                    return the address of.

   The behaviour of this function for SEQUENCE objects depends on "elno"
   value. SEQUENCE objects have the following structure:

   @code
   struct x_seq {
           scalar_t  xs_nr;
           struct y *xs_body;
   };
   @endcode

   where xs_nr stores a number of elements in the sequence and xs_body points to
   an array of the elements.

   With fieldno == 1, c2_xcode_addr() returns

       - &xseq->xs_body when (elno == ~0ULL) and

       - &xseq->xs_body[elno] otherwise.
 */
void *c2_xcode_addr(const struct c2_xcode_obj *obj, int fieldno, uint64_t elno);

/**
   Helper macro to return field value cast to a given type.
 */
#define C2_XCODE_VAL(obj, fieldno, elno, __type) \
        ((__type *)c2_xcode_addr(obj, fieldno, elno))

/**
   Constructs a c2_xcode_obj instance representing a sub-object of a given
   object.

   Address of sub-object (subobj->xo_ptr) is obtained by calling
   c2_xcode_addr().

   Type of sub-object (subobj->xo_type) is usually the type stored in the parent
   object's field (c2_xcode_field::xf_type), but for opaque fields it is
   obtained by calling c2_xcode_field::xf_opaque().
 */
int c2_xcode_subobj(struct c2_xcode_obj *subobj, const struct c2_xcode_obj *obj,
		    int fieldno, uint64_t elno);

/**
   Returns the value of first field in a given object, assuming this field is
   atomic.

   This function is suitable to return discriminator of a UNION object or
   element count of a SEQUENCE object.

   @note when the first field has C2_XT_VOID type, the tag
   (c2_xcode_field::xf_tag) of this field is returned.
 */
uint64_t c2_xcode_tag(const struct c2_xcode_obj *obj);

bool c2_xcode_type_invariant(const struct c2_xcode_type *xt);

extern const struct c2_xcode_type C2_XT_VOID;
extern const struct c2_xcode_type C2_XT_U8;
extern const struct c2_xcode_type C2_XT_U32;
extern const struct c2_xcode_type C2_XT_U64;

extern const struct c2_xcode_type C2_XT_OPAQUE;

/**
   Void type used by ff2c in places where C syntax requires a type name.
 */
typedef char c2_void_t[0];

/**
   Returns a previously unused "decoration number", which can be used as an
   index in c2_xcode_field::xf_decor[] and c2_xcode_type::xct_decor[] arrays.

   This number can be used to associate additional state with xcode
   introspection elements:

   @code
   // in module foo
   foo_decor_num = c2_xcode_decor_register();

   ...

   struct c2_xcode_type  *xt;
   struct c2_xcode_field *f;

   xt->xct_decor[foo_decor_num] = c2_alloc(sizeof(struct foo_type_decor));
   f->xf_decor[foo_decor_num] = c2_alloc(sizeof(struct foo_field_decor));
   @endcode
 */
int c2_xcode_decor_register(void);

struct c2_bob_type;

/**
 * Partially initializes a branded object type from a xcode type descriptor.
 *
 * @see bob.h
 */
void c2_xcode_bob_type_init(struct c2_bob_type *bt,
			    const struct c2_xcode_type *xt,
			    size_t magix_field, uint64_t magix);

/** @} end of xcode group */

/* __COLIBRI_XCODE_XCODE_H__ */
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
