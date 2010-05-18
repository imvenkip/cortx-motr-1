/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_H__
#define __COLIBRI_FOP_FOP_H__

#include "lib/cdefs.h"
#include "lib/vec.h"

/**
   @defgroup fop File operation packet

   File Operation Packet (fop, struct c2_fop) is a description of particular
   file system or storage operation. fop code knows how to serialise a fop to
   the network wire or storage buffer, and how to de-serialise it back, which
   makes it functionally similar to ONCRPC XDR.

   A fop consists of fields. The structure and arrangement of a fop fields is
   described by a fop type (c2_fop_type), that also has a pointer to a set of
   operations that are used to manipulate the fop.

   The execution of an operation described by fop is carried out by a "fop
   machine" (fop, struct c2_fom).

   @note "Had I been one of the tragic bums who lurked in the mist of that
          station platform where a brittle young FOP was pacing back and forth,
          I would not have withstood the temptation to destroy him."

   @{
*/

/* import */
struct c2_fom;

/* export */
struct c2_fop_type;
struct c2_fop_type_ops;
struct c2_fop_data;
struct c2_fop;
struct c2_fop_field;
struct c2_fop_field_ops;
struct c2_fop_field_base;

typedef uint32_t c2_fop_type_code_t;

/**
   Type of a file system operation.

   There is an instance of c2_fop_type for "make directory" command, an instance
   for "write", "truncate", etc.
 */
struct c2_fop_type {
	/** Unique operation code. */
	c2_fop_type_code_t            ft_code;
	/** Operation name. */
	const char                   *ft_name;
	/** Linkage into a list of all known operations. */
	struct c2_list_link           ft_linkage;
	/** Top level field in the fops of this type. */
	struct c2_fop_field          *ft_top;
	const struct c2_fop_type_ops *ft_ops;
};

/** fop type operations. */
struct c2_fop_type_ops {
	/** Create a fom that will carry out operation described by the fop. */
	int (*fto_fom_init)(struct c2_fop *fop, struct c2_fom **fom);
};

/** fop. */
struct c2_fop {
	struct c2_fop_type *f_type;
	/** Pointer to the data where fop is serialised or will be
	    serialised. */
	struct c2_fop_data  f_data;
};

/** 
    fop storage.

    A fop is stored in a buffer vector.
 */
struct c2_fop_data {
	struct c2_bufvec fd_vec;
};

int  c2_fop_type_register  (struct c2_fop_type *ftype, 
			    struct c2_rpcmachine *rpcm);
void c2_fop_type_unregister(struct c2_fop_type *ftype);

/** True iff fop describes an operation that would mutate file system state when
    executed. */
bool c2_fop_is_update(const struct c2_fop_type *type);
/** True iff fop is a batch, i.e., contains other fops. */
bool c2_fop_is_batch (const struct c2_fop_type *type);

/**
   General class to which a fop field belongs.

   fop field kinds classify fop fields very broadly.
 */
enum c2_fop_field_kind {
	/**
	 * A fop field is "builtin" fop sub-system relies on the existence and
	 * functions of this field. A builtin field might be stored differently
	 * from other fields. For example, fields used for operation sequencing
	 * (slot and sequence identifiers) are interpreted by the rpc
	 * sub-system. To represent such fields uniformly, a special builtin fop
	 * field is introduced. Another example of built-in field is a field
	 * containing fop operation code.
	 */
	FFK_BUILTIN,
	/**
	   A fop field is standard if it belong to a small set of fields that
	   generic fop code can interpret. Examples of such fields are object,
	   lock, and other resource identifiers; security signatures, user
	   identifiers, etc.
	 */
	FFK_STANDARD,
	/**
	   Fields not belonging to the above kinds fall to this kind.
	 */
	FFK_OTHER,

	FFK_NR
};

/**
   fop field type describes the set of values that can be stored in this field.

   This enum describes "type" (in a programming language sense) of a fop field.
 */
enum c2_fop_field_type {
	/** Invalid type. */
	FFT_ZERO,
	/** Void type. */
	FFT_VOID,
	/** Boolean. */
	FFT_BOOL,
	/** This field is a record, containing other fields. */
	FFT_RECORD,
	/** This field is a discriminated union, containing some field from a
	    set of possible cases. */
	FFT_UNION,
	/** This field is an array, containing a number of instances of the same
	    element field. */
	FFT_ARRAY,
	/** This field is a bit-mask. */
	FFT_BITMASK,
	/** This field contains a file or object identifier. */
	FFT_FID,
	/** This field contains file name (a path component). */
	FFT_NAME,
	/** This field contains a path-name. */
	FFT_PATH,
	/** This field contains a principal (user or group) identifier. */
	FFT_PRINCIPAL,
	/** This field contains a cryptographic capability. */
	FFT_CAPABILITY,
	/** This field contains a time-stamp. */
	FFT_TIMESTAMP,
	/** This field contains an epoch number. */
	FFT_EPOCH,
	/** This field contains a file system object version. */
	FFT_VERSION,
	/** This field contains an linear offset within a file system object. */
	FFT_OFFSET,
	/** This field contains a count of something. */
	FFT_COUNT,
	/** This field contains a data buffer. */
	FFT_BUFFER,
	/** This field contains a resource identifier. */
	FFT_RESOURCE,
	/** This field contains a lock identifier. */
	FFT_LOCK,
	/** This field contains a cluster node identifier. */
	FFT_NODE,
	/** This field contains a fop. */
	FFT_FOP,
	/** This field contains something else. */
	FFT_OTHER,

	FFT_NR
};

/**
   Attributes that multiple "similar" field in different fop types can share.

   For example, many fop types would have a field storing identifier of the
   object on which an operation is performed. Some field attributes, like field
   name can be different in different fop types. Shared attributes are factored
   out to c2_fop_field_base.
 */
struct c2_fop_field_base {
	const enum c2_fop_field_kind   ffb_kind;
	const enum c2_fop_field_type   ffb_type;
	/** Is this field compound (array, record, fop, union, etc.) or
	    non-compound (atom)? */
	const bool                     ffb_compound;
	const struct c2_fop_field_ops *ffb_ops;
};

/**
   fop field.

   fop structure is described in terms of fields. Fields of a given fop type are
   arranged in a tree. Non-compound fields are the leaves of this tree and the
   root and all intermediate nodes are compound fields.

   @note a tree of fields describes structure of field in a fop type, not an
   actual arrangement of fields in a particular fop instance. For example, a
   field of array type has a single child node that corresponds to the field of
   array elements. In the actual fop there can be multiple (or 0) instances of
   this element field.
 */
struct c2_fop_field {
	/** Field name. */
	const char                     *ff_name;
	/** Base attributes. */
	const struct c2_fop_field_base *ff_base;
	/** Sibling node in the field tree. */
	struct c2_list_link             ff_sibling;
	/** List of children fields (if any), linked through
	    c2_fop_field::ff_sibling. Only compound fields have this list
	    non-empty. */
	struct c2_list                  ff_child;
	/** Pointer to the parent field in the tree, or NULL for the top field
	    in fop type (c2_fop_type::ft_top). */
	struct c2_fop_field            *ff_parent;
};

/** Allocate fop field and initialise its attributes. */
struct c2_fop_field *c2_fop_field_alloc(void);
/** Finalise the field destroying all its state including sub-tree of fields
    rooted at the field. */
void                 c2_fop_field_fini(struct c2_fop_field *field);

/** Call-back function supplied to fop field tree iterating functions. */
typedef bool (*c2_fop_field_cb_t)(struct c2_fop_field *, unsigned, void *);

/** 
    Values of this type describe position within a compound field.

    Zero means the beginning of a compound field. For record field the value of
    iterator means the number of sub-field. For an array field it means an index
    of an element, etc.
*/
typedef uint64_t c2_fop_field_iterator_t;

struct c2_fop_field_ops {
	
};

enum {
	/** Maximal depth of a fop field tree. */
	C2_FOP_MAX_FIELD_DEPTH = 8
};

/**
   Contents of a given field in a given fop instance.
 */
struct c2_fop_field_val {
	struct c2_fop       *ffv_fop;
	struct c2_fop_field *ffv_field;
	void                *ffv_val;
};

/**
   Iterator through fop fields.

   A fop iterator goes through the values that fields have for a given fop.
 */
struct c2_fop_iterator {
	struct c2_fop                  *ffi_fop;
	/**
	   Stack describing the iterator position within nested compound fields.
	 */
	struct {
		/** Compound field that the iterator is currently in. */
		struct c2_fop_field    *s_field;
		/** Position within fop data storage. */
		struct c2_vec_cursor    s_pos;
		/** Position within this field. */
		c2_fop_field_iterator_t s_it;
	}                               ffi_stack[C2_FOP_MAX_FIELD_DEPTH];
	/** Current stack depth. */
	int                             ffi_depth;
};

void c2_fop_iterator_init(struct c2_fop_iterator *it, struct c2_fop *fop);
void c2_fop_iterator_fini(struct c2_fop_iterator *it);
int  c2_fop_iterator_get (struct c2_fop_iterator *it, 
			  struct c2_fop_field_val *val);

int  c2_fops_init(void);
void c2_fops_fini(void);

/** @} end of fop group */

/* __COLIBRI_FOP_FOP_H__ */
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
