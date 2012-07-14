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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 2011-06-07
 */

#ifndef __COLIBRI_FOP_FOP_BASE_H__
#define __COLIBRI_FOP_FOP_BASE_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/tlist.h"
#include "lib/vec.h"
#include "addb/addb.h"
#include "fol/fol.h"
#include "fop/fom.h"
#include "lib/vec.h"
#include "rpc/item.h"
#include "xcode/xcode.h"

/**
   @addtogroup fop

   This file contains "basic" fop definitions which are independent of
   networking and store interfaces. The reason for this split is to break
   circular dependency that otherwise forms due to usage of fop formats by the
   networking and store code.

   Technically, this file contains the part of the fop interface necessary to
   build libc2rt library used by the fop2c-produced code. Some parts of the
   interface are too difficult to disentangle properly. For them, fake
   definitions are provided in fop/rt/stub.c. These definitions are used to
   build libc2rt.

   @{
 */

/* import */
struct c2_fol;
struct c2_fop;
struct c2_net_buf_desc;

/* export */
struct c2_fop_type;
struct c2_fop_type_ops;
struct c2_fop_field;
struct c2_fop_field_type;
struct c2_fop_memlayout;
struct c2_fop_type_format;

typedef uint32_t c2_fop_type_code_t;

/**
   Type of a file system operation.

   There is an instance of c2_fop_type for "make directory" command, an instance
   for "write", "truncate", etc.
 */
struct c2_fop_type {
	/** Operation name. */
	const char                       *ft_name;
	/** Linkage into a list of all known operations. */
	struct c2_tlink                   ft_linkage;
	/** Type of a top level field in fops of this type. */
	struct c2_fop_field_type         *ft_top;
	const struct c2_fop_type_ops     *ft_ops;
	/** Format of this fop's top field. */
	struct c2_fop_type_format        *ft_fmt;
	struct c2_xcode_type            **ft_xc_type;
	struct c2_xcode_ctx               ft_xc_ctx;
	struct c2_fol_rec_type            ft_rec_type;
	/** State machine for this fop type */
	struct c2_fom_type                ft_fom_type;
	/** The rpc_item_type associated with rpc_item
	    embedded with this fop. */
	struct c2_rpc_item_type		  ft_rpc_item_type;
	/**
	   ADDB context for events related to this fop type.
	 */
	struct c2_addb_ctx                ft_addb;
	uint64_t                          ft_magix;
};

/**
    Iterates through the registered fop types.

    To iterate across all registered fop types, first call this function with
    NULL parameter. NULL is returned to indicate end of the iteration.

    If a fop type is registered or unregistered while an iteration is in
    progress, behaviour is undefined.

    @code
    ftype = NULL;
    while ((ftype = c2_fop_type_next(ftype)) != NULL) {
            do something with ftype
    }
    @endcode
 */
struct c2_fop_type *c2_fop_type_next(struct c2_fop_type *ftype);
int  c2_fop_type_build(struct c2_fop_type *fopt);
void c2_fop_type_fini(struct c2_fop_type *fopt);

int  c2_fop_type_build_nr(struct c2_fop_type **fopt, int nr);
void c2_fop_type_fini_nr(struct c2_fop_type **fopt, int nr);

struct c2_list;
struct c2_fop_ctx;

/** fop type operations. */
struct c2_fop_type_ops {
	/** XXX temporary entry point for threaded fop execution. */
	int (*fto_execute) (struct c2_fop *fop, struct c2_fop_ctx *ctx);
	/** fol record type operations for this fop type, or NULL is standard
	    operations are to be used. */
	const struct c2_fol_rec_type_ops  *fto_rec_ops;
	/** Action to be taken on receiving reply of a fop. */
	void (*fto_fop_replied)(struct c2_fop *fop, struct c2_fop *bfop);
	/** Return the size of fop object. */
	size_t (*fto_size_get)(struct c2_fop *fop);
	/** Try to coalesce multiple fops into one. */
	int (*fto_io_coalesce)(struct c2_fop *fop, uint64_t rpc_size);
	/** Returns the net buf desc in io fop. */
	void (*fto_io_desc_get)(struct c2_fop *fop,
			        struct c2_net_buf_desc **desc);
};

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
   fop field.
 */
struct c2_fop_field {
	/** Field name. */
	const char               *ff_name;
	struct c2_fop_field_type *ff_type;
	uint32_t                  ff_tag;
	void                    **ff_decor;
};


enum c2_fop_field_aggr {
	FFA_RECORD,
	FFA_UNION,
	FFA_SEQUENCE,
	FFA_TYPEDEF,
	FFA_ATOM,
	FFA_NR
};

enum c2_fop_field_primitive_type {
	FPF_VOID,
	FPF_BYTE,
	FPF_U32,
	FPF_U64,

	FPF_NR
};

/**
   fop field type in a programming language "type" sense.
 */
struct c2_fop_field_type {
	enum c2_fop_field_aggr   fft_aggr;
	const char              *fft_name;
	union {
		struct c2_fop_field_record {
		} u_record;
		struct c2_fop_field_union {
		} u_union;
		struct c2_fop_field_sequence {
			uint64_t s_max;
		} u_sequence;
		struct c2_fop_field_typedef {
		} u_typedef;
		struct c2_fop_field_atom {
			enum c2_fop_field_primitive_type a_type;
		} u_atom;
	} fft_u;
	/* a fop must be decorated, see any dictionary. */
	void                   **fft_decor;
	size_t                   fft_nr;
	struct c2_fop_field    **fft_child;
	struct c2_fop_memlayout *fft_layout;
};

void c2_fop_field_type_fini(struct c2_fop_field_type *t);

extern struct c2_fop_field_type C2_FOP_TYPE_VOID;
extern struct c2_fop_field_type C2_FOP_TYPE_BYTE;
extern struct c2_fop_field_type C2_FOP_TYPE_U32;
extern struct c2_fop_field_type C2_FOP_TYPE_U64;

/**
   Contents of a given field in a given fop instance.

   @note a fop field can potentially have multiple values in the same fop. For
   example, an element field in the array field.
 */
struct c2_fop_field_instance {
	struct c2_fop       *ffi_fop;
	struct c2_fop_field *ffi_field;
	void                *ffi_val;
};

int  c2_fops_init(void);
void c2_fops_fini(void);

#include "fop/fop_format.h"

/** @} end of fop group */

/* __COLIBRI_FOP_FOP_BASE_H__ */
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
