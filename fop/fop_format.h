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
#include "xcode/xcode.h"

extern const struct c2_rpc_item_type_ops c2_rpc_fop_default_item_type_ops;

#define __paste_xc(x) x ## _xc

#define C2_FOP_TYPE_DECLARE_OPS_XC(fopt, name, ops, opcode, itflags, itops) \
struct c2_fop_type fopt ## _fopt = {				            \
	.ft_name    = name,						    \
        .ft_xc_type = &__paste_xc(fopt),                                    \
	.ft_ops     = (ops),						    \
	.ft_rpc_item_type = {						    \
		.rit_opcode = (opcode),					    \
		.rit_flags  = (itflags),				    \
		.rit_ops    = (itops)					    \
	}								    \
};

#define C2_FOP_TYPE_DECLARE_XC(fopt, name, ops, opcode, itflags)	    \
        C2_FOP_TYPE_DECLARE_OPS_XC(fopt, name, ops, opcode, itflags,	    \
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
