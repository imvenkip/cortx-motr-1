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

#ifndef __COLIBRI_FOP_FOP_H__
#define __COLIBRI_FOP_FOP_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/list.h"
#include "addb/addb.h"
#include "fol/fol.h"
#include "fop/fom.h"
#include "fop/fop_base.h"
#include "rpc/rpc2.h"
#include "xcode/xcode.h"

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
   machine" (fom, struct c2_fom).

   This file (fop.h) is a top-level header defining all fop-related data
   structures and entry points. The bulk of the definitions is in fop_base.h,
   which is included from this file.

   @note "Had I been one of the tragic bums who lurked in the mist of that
          station platform where a brittle young FOP was pacing back and forth,
          I would not have withstood the temptation to destroy him."

   @{
*/

/* import */
struct c2_service;
struct c2_fol;
struct c2_db_tx;

/* export */
struct c2_fop_ctx;
struct c2_fop_data;
struct c2_fop;

/**
   A context for fop processing in a service.

   A context is created by a service and passed to
   c2_fop_type_ops::fto_execute() as an argument. It is used to identify a
   particular fop execution in a service.
 */
struct c2_fop_ctx {
	struct c2_service *ft_service;
	/**
	   Service-dependent cookie identifying fop execution. Passed to
	   c2_service_ops::so_reply_post() to post a reply.

	   @see c2_net_reply_post()
	 */
	void              *fc_cookie;
};

/**
    fop storage.

    A fop is stored in a buffer vector. XXX not for now.
 */
struct c2_fop_data {
	void            *fd_data;
};

/** fop. */
struct c2_fop {
	struct c2_fop_type	*f_type;
	/** Pointer to the data where fop is serialised or will be
	    serialised. */
	struct c2_fop_data	 f_data;
	/**
	   ADDB context for events related to this fop.
	 */
	struct c2_addb_ctx	 f_addb;
	/**
	   RPC item for this FOP
	 */
	struct c2_rpc_item	 f_item;
	/** Linkage could be used to have fops in a list. */
	struct c2_list_link	 f_link;
};

/**
   c2_fop_init does not allocate fop data.
 */
void           c2_fop_init (struct c2_fop *fop, struct c2_fop_type *fopt,
			    void *data);
void           c2_fop_fini (struct c2_fop *fop);

/**
   Allocate fop object

   @param fopt fop type to assign to this fop object
   @param data top level data object
   if data == NULL, data is allocated by this function
 */
struct c2_fop *c2_fop_alloc(struct c2_fop_type *fopt, void *data);
void           c2_fop_free (struct c2_fop *fop);
void          *c2_fop_data (struct c2_fop *fop);

/**
  Calculates the onwire size of fop data. This function internally calls
  the fop field type specific functions to calculate the size

  @param fop The data for this fop is to be encoded/decoded.

  @retval Onwire size of the fop in bytes.
*/
size_t c2_fop_xcode_length(struct c2_fop *fop);

/**
   Allocate top level fop data
 */
int            c2_fop_data_alloc (struct c2_fop *fop);

int c2_fop_fol_rec_add(struct c2_fop *fop, struct c2_fol *fol,
		       struct c2_db_tx *tx);

struct c2_rpc_item *c2_fop_to_rpc_item(struct c2_fop *fop);
struct c2_fop *c2_rpc_item_to_fop(const struct c2_rpc_item *item);

/**  Returns a fop type associated with an rpc item type */
struct c2_fop_type *c2_item_type_to_fop_type
		   (const struct c2_rpc_item_type *rit);

/**
   Default implementation of c2_rpc_item_ops::rio_free() interface, for
   fops. If fop is not embeded in any other object, then this routine
   can be set to c2_rpc_item::ri_ops::rio_free().
 */
void c2_fop_item_free(struct c2_rpc_item *item);

extern const struct c2_rpc_item_ops c2_fop_default_item_ops;

/**
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
   .ff extension. See xcode/ff2c/sample.ff for an example. fop format
   description file defines instances of struct c2_xcode_type encoded via
   helper functions generated from ff2c compiler

   During build process, fop format description file is processed by
   xcode/ff2c/ff2c compiler. xcode provides interfaces to iterate over
   hierarchy of such descriptors and to associate user defined state with
   types and fields.

   @see xcode/ff2c/ff2c
*/

extern const struct c2_rpc_item_type_ops c2_rpc_fop_default_item_type_ops;


#define __paste_xc(x) x ## _xc

#define C2_FOP_TYPE_DECLARE_OPS(fopt, name, ops, opcode, itflags, itops) \
	struct c2_fop_type fopt ## _fopt = {				\
		.ft_name    = name,					\
		.ft_xc_type = &__paste_xc(fopt),			\
		.ft_ops     = (ops),					\
		.ft_rpc_item_type = {					\
			.rit_opcode = (opcode),				\
			.rit_flags  = (itflags),			\
			.rit_ops    = (itops)				\
		}							\
	};

#define C2_FOP_TYPE_DECLARE(fopt, name, ops, opcode, itflags)		\
        C2_FOP_TYPE_DECLARE_OPS(fopt, name, ops, opcode, itflags,	\
				&c2_rpc_fop_default_item_type_ops)

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
