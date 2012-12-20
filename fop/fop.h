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

#ifndef __MERO_FOP_FOP_H__
#define __MERO_FOP_FOP_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/list.h"
#include "lib/refs.h"
#include "addb/addb.h"
#include "fol/fol.h"
#include "fop/fom.h"
#include "rpc/item.h"
#include "net/net_otw_types.h"

/**
   @defgroup fop File operation packet

   File Operation Packet (fop, struct m0_fop) is a description of particular
   file system or storage operation. fop code knows how to serialise a fop to
   the network wire or storage buffer, and how to de-serialise it back, which
   makes it functionally similar to ONCRPC XDR.

   A fop consists of fields. The structure and arrangement of a fop fields is
   described by a fop type (m0_fop_type), that also has a pointer to a set of
   operations that are used to manipulate the fop.

   The execution of an operation described by fop is carried out by a "fop
   machine" (fom, struct m0_fom).

   This file (fop.h) is a top-level header defining all fop-related data
   structures and entry points. The bulk of the definitions is in fop_base.h,
   which is included from this file.

   @note "Had I been one of the tragic bums who lurked in the mist of that
          station platform where a brittle young FOP was pacing back and forth,
          I would not have withstood the temptation to destroy him."

   @{
*/

/* import */
struct m0_fol;
struct m0_db_tx;
struct m0_xcode_type;

/* export */
struct m0_fop_data;
struct m0_fop;

/**
    fop storage.

    A fop is stored in a buffer vector. XXX not for now.
 */
struct m0_fop_data {
	void            *fd_data;
};

/** fop. */
struct m0_fop {
	struct m0_ref            f_ref;

	struct m0_fop_type	*f_type;
	/** Pointer to the data where fop is serialised or will be
	    serialised. */
	struct m0_fop_data	 f_data;
	/**
	   ADDB context for events related to this fop.
	 */
	struct m0_addb_ctx	 f_addb;
	/**
	   RPC item for this FOP
	 */
	struct m0_rpc_item	 f_item;
};

/**
   Initialises fop.

   'data' argument if non-null specifies address of top level data object.
   'fop_release' is a pointer to function that will be called when last
   reference on the fop is dropped. fop_release is expected to finalise and
   free the fop. @see m0_fop_release.

   m0_fop_init sets fop reference count to 1. The caller is expected to
   drop the reference using m0_fop_put().

   @pre fop != NULL && fopt != NULL && fop_release != NULL
   @post m0_ref_read(&fop->f_ref) == 1

   @see m0_fop_data_alloc()
 */
M0_INTERNAL void m0_fop_init(struct m0_fop *fop, struct m0_fop_type *fopt,
			     void *data, void (*fop_release)(struct m0_ref *));

/**
 * Finalises fop.
 *
 * Very likely you want to use m0_fop_put().
 *
 * Use this only when you're sure that there are no other references on the fop
 * other than the one you're holding. e.g. the fop is embedded in some other
 * object and because of some error there is a need to finalise fop even before
 * it is posted to RPC.
 *
 * @pre M0_IN(m0_ref_read(&fop->f_ref), (0, 1))
 * @see m0_fop_put
 */
M0_INTERNAL void m0_fop_fini(struct m0_fop *fop);

/**
 * Takes a reference on the fop.
 * @pre m0_ref_read(&fop->f_ref) > 0
 */
struct m0_fop *m0_fop_get(struct m0_fop *fop);

/**
 * Drops reference on the fop.
 * If it was last reference the fop will be finalised and freed.
 * Do not touch fop after call the this function.
 * @pre m0_ref_read(&fop->f_ref) > 0
 */
void m0_fop_put(struct m0_fop *fop);

/**
   Allocates and initialises fop object

   @param fopt fop type to assign to this fop object
   @param data top level data object
   if data == NULL, data is allocated by this function

   @post ergo(result != NULL, m0_ref_read(&result->f_ref) == 1)
 */
struct m0_fop *m0_fop_alloc(struct m0_fop_type *fopt, void *data);

/**
 * Default implementation of fop_release that can be passed to
 * m0_fop_init() when fop is not embedded in any other object.
 */
M0_INTERNAL void m0_fop_release(struct m0_ref *ref);
void *m0_fop_data(struct m0_fop *fop);

/**
   Allocate top level fop data
 */
M0_INTERNAL int m0_fop_data_alloc(struct m0_fop *fop);

M0_INTERNAL int m0_fop_fol_rec_add(struct m0_fop *fop, struct m0_fol *fol,
				   struct m0_db_tx *tx);

struct m0_rpc_item *m0_fop_to_rpc_item(struct m0_fop *fop);
struct m0_fop *m0_rpc_item_to_fop(const struct m0_rpc_item *item);
uint32_t m0_fop_opcode(const struct m0_fop *fop);

/**  Returns a fop type associated with an rpc item type */
M0_INTERNAL struct m0_fop_type *m0_item_type_to_fop_type
    (const struct m0_rpc_item_type *rit);

#define M0_FOP_DEFAULT_ITEM_TYPE_OPS \
	.rito_encode       = m0_fop_item_type_default_encode,      \
	.rito_decode       = m0_fop_item_type_default_decode,      \
	.rito_payload_size = m0_fop_item_type_default_payload_size,\
	.rito_item_get     = m0_fop_item_get,                      \
	.rito_item_put     = m0_fop_item_put

extern const struct m0_rpc_item_type_ops m0_fop_default_item_type_ops;

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
   description file defines instances of struct m0_xcode_type encoded via
   helper functions generated from ff2c compiler

   During build process, fop format description file is processed by
   xcode/ff2c/ff2c compiler. xcode provides interfaces to iterate over
   hierarchy of such descriptors and to associate user defined state with
   types and fields.

   @see xcode/ff2c/ff2c
*/

/**
   Type of a file system operation.

   There is an instance of m0_fop_type for "make directory" command, an instance
   for "write", "truncate", etc.
 */
struct m0_fop_type {
	/** Operation name. */
	const char                       *ft_name;
	/** Linkage into a list of all known operations. */
	struct m0_tlink                   ft_linkage;
	const struct m0_fop_type_ops     *ft_ops;
	/** Xcode type representing this fop type. */
	const struct m0_xcode_type       *ft_xt;
	struct m0_fol_rec_type            ft_rec_type;
	/** State machine for this fop type */
	struct m0_fom_type                ft_fom_type;
	/** The rpc_item_type associated with rpc_item
	    embedded with this fop. */
	struct m0_rpc_item_type		  ft_rpc_item_type;
	/**
	   ADDB context for events related to this fop type.
	 */
	struct m0_addb_ctx                ft_addb;
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
    while ((ftype = m0_fop_type_next(ftype)) != NULL) {
            do something with ftype
    }
    @endcode
 */
M0_INTERNAL struct m0_fop_type *m0_fop_type_next(struct m0_fop_type *ftype);

/** fop type operations. */
struct m0_fop_type_ops {
	/** fol record type operations for this fop type, or NULL if standard
	    operations are to be used. */
	const struct m0_fol_rec_type_ops  *fto_rec_ops;
	/** Action to be taken on receiving reply of a fop. */
	void (*fto_fop_replied)(struct m0_fop *fop, struct m0_fop *bfop);
	/** Try to coalesce multiple fops into one. */
	int (*fto_io_coalesce)(struct m0_fop *fop, uint64_t rpc_size);
	/** Returns the net buf desc in io fop. */
	void (*fto_io_desc_get)(struct m0_fop *fop,
			        struct m0_net_buf_desc **desc);
};

typedef uint32_t m0_fop_type_code_t;

/**
 * Parameters needed for fop type initialisation.
 *
 * This definition deliberately does not follow the "field name prefix" rule.
 *
 * @see M0_FOP_TYPE_INIT() m0_fop_type_init() m0_fop_type_init_nr()
 */
struct __m0_fop_type_init_args {
	const char                        *name;
	uint32_t                           opcode;
	uint64_t                           rpc_flags;
	const struct m0_xcode_type        *xt;
	const struct m0_fop_type_ops      *fop_ops;
	const struct m0_fol_rec_type_ops  *fol_ops;
	const struct m0_fom_type_ops      *fom_ops;
	const struct m0_rpc_item_type_ops *rpc_ops;
	const struct m0_sm_conf		  *sm;
	const struct m0_reqh_service_type *svc_type;
};

int m0_fop_type_init(struct m0_fop_type *ft,
		     const struct __m0_fop_type_init_args *args);

/**
 * Helper macro which can be used to submit fop type initialisation parameters
 * partially and out of order.
 *
 * @see http://www.cofault.com/2005/08/named-formals.html
 */
#define M0_FOP_TYPE_INIT(ft, ...)                                        \
        m0_fop_type_init((ft), &(const struct __m0_fop_type_init_args) { \
                                 __VA_ARGS__ })

void m0_fop_type_fini(struct m0_fop_type *fopt);

struct m0_fop_type_batch {
	struct m0_fop_type             *tb_type;
	struct __m0_fop_type_init_args  tb_args;
};

M0_INTERNAL int m0_fop_type_init_nr(const struct m0_fop_type_batch *batch);
M0_INTERNAL void m0_fop_type_fini_nr(const struct m0_fop_type_batch *batch);

M0_INTERNAL int m0_fops_init(void);
M0_INTERNAL void m0_fops_fini(void);

#define M0_FOP_XCODE_OBJ(f) (struct m0_xcode_obj) {	\
		.xo_type = f->f_type->ft_xt,		\
		.xo_ptr  = m0_fop_data(f),		\
}

/** @} end of fop group */

/* __MERO_FOP_FOP_H__ */
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
