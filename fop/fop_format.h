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
 * Original creation date: 05/19/2010
 */

#pragma once

#ifndef __COLIBRI_FOP_FOP_FORMAT_H__
#define __COLIBRI_FOP_FOP_FORMAT_H__

/**
   @addtogroup fop

   <b>Fop format</b>

   A fop type contains as its part a "fop format". A fop format is a description
   of structure of data in a fop instance. Fop format describes fop instance
   data structure as a tree of fields. Leaves of this tree are fields of
   "atomic" types (VOID, BYTE, U32 and U64) and non-leaf nodes are "aggregation
   fields": record, union, sequence or typedef.

   The key point of fop formats is that data structure description can be
   analysed at run-time, by traversing the tree:

   @li to pack and unpack fop instance between in-memory and on-wire
   representation fop format tree is traversed recursively and fop fields are
   serialized or de-serialized.

   @li the same for converting fop between in-memory and data-base record
   formats;

   @li finally, when a new fop type is added, data-structure definitions for
   instances of this type are also generated automatically by traversing the
   format tree (by code in fop_format_c.c and fop2c). These data-structure
   definitions can be different for different platforms (e.g., kernel and user
   space).

   Fop formats are introduced in "fop format description files", usually having
   .ff extension. See fop/ut/test_format.ff for an example. fop format
   description file defines instances of struct c2_fop_type_format encoded via
   helper macros from fop_format_def.h.

   During build process, fop format description file is processed by fop/fop2c
   "compiler". This compiler runs c2_fop_type_format_parse() function on
   c2_fop_type_format instances from fop format descriptions. This function
   builds c2_fop_field_type instances organized into a tree.

   After this, fop2c runs various functions from fop_format_c.c on the resulting
   tree. These functions traverse the tree and generate C language files
   containing data-type definitions corresponding to the fop format and an
   additional auxiliary data-structure c2_fop_memlayout describing how fop
   fields are laid out in memory.

   @see fop/fop2c

   @{
*/

#ifndef __KERNEL__
# include "fop_user.h"
#else
# include "linux_kernel/fop_kernel.h"
#endif

extern const struct c2_rpc_item_type_ops c2_rpc_fop_default_item_type_ops;

struct c2_fop_memlayout;

struct c2_fop_type_format {
	struct c2_fop_field_type  *ftf_out;
	enum c2_fop_field_aggr     ftf_aggr;
	const char                *ftf_name;
	uint64_t                   ftf_val;
	struct c2_fop_memlayout   *ftf_layout;
	const struct c2_fop_field_format {
		const char                      *c_name;
		const struct c2_fop_type_format *c_type;
		uint32_t                         c_tag;
	} ftf_child[];
};

int  c2_fop_type_format_parse(struct c2_fop_type_format *fmt);
void c2_fop_type_format_fini(struct c2_fop_type_format *fmt);

int  c2_fop_type_format_parse_nr(struct c2_fop_type_format **fmt, int nr);
void c2_fop_type_format_fini_nr(struct c2_fop_type_format **fmt, int nr);

/**
   Returns the address of a sub-field within a field.

   @param ftype  - a type of enclosing field
   @param obj    - address of an enclosing object with type @ftype
   @param fileno - ordinal number of sub-field

   @param elno   - for a FFA_SEQUENCE sub-field, index of the element to
                   return the address of.

   The behaviour of this function for FFA_SEQUENCE fields depends on @elno
   value. FFA_SEQEUNCE fields have the following structure:

   @code
   struct x_seq {
           uint32_t  xs_nr;
           struct y *xs_body;
   } *xseq;
   @endcode

   where xs_nr stores a number of elements in the sequence and xs_body points to
   an array of the elements.

   With fileno == 1, c2_fop_type_field_addr() returns &xseq->xs_body when @elno
   == ~0 and &xseq->xs_body[elno] otherwise.
 */
void *c2_fop_type_field_addr(const struct c2_fop_field_type *ftype, void *obj,
			     int fileno, uint32_t elno);

struct c2_fop_field *
c2_fop_type_field_find(const struct c2_fop_field_type *ftype,
		       const char *fname);

extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_VOID_tfmt;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_BYTE_tfmt;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U32_tfmt;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U64_tfmt;

#define __paste(x) x ## _tfmt
#ifndef __layout
#define __layout(x) &(x ## _memlayout)
#endif

/**
   Used as a placeholder only to parse .ff files which need definitions
   from other .ff files to build properly.
   This macro is used by fop2c.in script to search for dependency .ff files.
   Otherwise, this macro is just a placeholder and hence NULL here.
 */
#define FOPDEP(x)

#define C2_FOP_FIELD_TAG(_tag, _name, _type)	\
{						\
	.c_name = #_name,			\
	.c_type = &__paste(_type),		\
	.c_tag  = (_tag)			\
}

#define C2_FOP_FIELD(_name, _type) C2_FOP_FIELD_TAG(0, _name, _type)

#define C2_FOP_FORMAT(_name, _aggr, ...)	\
struct c2_fop_type_format __paste(_name) = {	\
	.ftf_aggr = (_aggr),			\
	.ftf_name = #_name,			\
	.ftf_val  = 1,				\
	.ftf_layout = __layout(_name),		\
	.ftf_child = {				\
		__VA_ARGS__,			\
		{ .c_name = NULL }		\
	}					\
}

struct c2_fop_decorator {
	const char *dec_name;
	uint32_t    dec_id;
	void      (*dec_type_fini)(void *value);
	void      (*dec_field_fini)(void *value);
};

void c2_fop_decorator_register(struct c2_fop_decorator *dec);

void *c2_fop_type_decoration_get(const struct c2_fop_field_type *ftype,
				 const struct c2_fop_decorator *dec);
void  c2_fop_type_decoration_set(const struct c2_fop_field_type *ftype,
				 const struct c2_fop_decorator *dec, void *val);

void *c2_fop_field_decoration_get(const struct c2_fop_field *field,
				  const struct c2_fop_decorator *dec);
void  c2_fop_field_decoration_set(const struct c2_fop_field *field,
				  const struct c2_fop_decorator *dec,
				  void *val);

int  c2_fop_comp_init(void);
void c2_fop_comp_fini(void);

int  c2_fop_comp_udef(struct c2_fop_field_type *ftype);
int  c2_fop_comp_kdef(struct c2_fop_field_type *ftype);
int  c2_fop_comp_ulay(struct c2_fop_field_type *ftype);
int  c2_fop_comp_klay(struct c2_fop_field_type *ftype);

typedef char c2_fop_void_t[0];

struct c2_fop_memlayout {
	xdrproc_t fm_uxdr;
	size_t    fm_sizeof;
	struct {
		int ch_offset;
	} fm_child[];
};

#define C2_FOP_TYPE_DECLARE_OPS(fopt, name, ops, opcode, itflags, itops) \
struct c2_fop_type fopt ## _fopt = {					\
	.ft_name = name,						\
	.ft_fmt  = &__paste(fopt),					\
	.ft_ops  = (ops),						\
	.ft_rpc_item_type = {						\
		.rit_opcode = (opcode),					\
		.rit_flags  = (itflags),				\
		.rit_ops    = (itops)					\
	}								\
};

#define C2_FOP_TYPE_DECLARE(fopt, name, ops, opcode, itflags)		\
        C2_FOP_TYPE_DECLARE_OPS(fopt, name, ops, opcode, itflags,	\
				&c2_rpc_fop_default_item_type_ops)

/** @} end of fop group */

/* __COLIBRI_FOP_FOP_FORMAT_H__ */
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
