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
#include "rpc/item.h"

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
struct c2_fol;
struct c2_db_tx;

/* export */
struct c2_fop_ctx;
struct c2_fop_data;
struct c2_fop;

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

int            c2_fop_init (struct c2_fop *fop, struct c2_fop_type *fopt,
			    void *data);
void           c2_fop_fini (struct c2_fop *fop);
struct c2_fop *c2_fop_alloc(struct c2_fop_type *fopt, void *data);
void           c2_fop_free (struct c2_fop *fop);
void          *c2_fop_data (struct c2_fop *fop);

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

#include "fop/fop_format.h"

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
