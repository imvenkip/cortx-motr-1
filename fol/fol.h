/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 09/09/2010
 */

#pragma once

#ifndef __MERO_FOL_FOL_H__
#define __MERO_FOL_FOL_H__

/**
   @defgroup fol File operations log

   File operations log (fol) is a per-node collection of records, describing
   updates to file system state carried out on the node. See HLD for the
   description of requirements, usage patterns and constraints on fol,
   as well as important terminology (update, operation, etc.):
   https://docs.google.com/a/xyratex.com/document/d/1_5UGU0n7CATMiuG6V9eK3cMshiYFotPVnIy478MMnvM/comment .

   A fol is represented by an instance of struct m0_fol. A fol record
   is represented by the m0_fol_rec data type.

   A fol record contains the list of fol record parts, belonging to fol record
   part types, added during updates. These fol record parts provide flexibility
   for modules to participate in a transaction without global knowledge.

   @see m0_fol_rec_part : FOL record part.
   @see m0_fol_rec_part_type : FOL record part type.

   m0_fol_rec_part_ops structure contains operations for undo and redo of
   FOL record parts.

   @see m0_fol_rec_part_init() : Initializes m0_fol_rec_part with
				 m0_fol_rec_part_type_ops.
   @see m0_fol_rec_part_fini() : Finalizes FOL record part.

   @see m0_fol_rec_part_type_register() : Registers FOL record part type.
   @see m0_fol_rec_part_type_deregister() : Deregisters FOL record part type.

   FOL record parts list is kept in m0_fol_rec::fr_parts which is
   initialized in m0_fol_rec_init().

   m0_fol_rec_add() is used to compose FOL record from FOL record descriptor
   and parts.
   fol_record_encode() encodes the FOL record parts in the list
   m0_fol_rec:fr_parts in a buffer, which then will be added into the BE log.

   @see m0_fol_rec_encode()
   @see m0_fol_rec_decode()

   m0_fol_rec_part_type_init() and m0_fol_rec_part_type_fini() are added
   to initialize and finalize FOL part types.
   FOL record part types are registered in a global array of FOL record
   parts using m0_fol_rec_part_type::rpt_index.

   After successful execution of updates on server side, in FOM generic phase
   using m0_fom_fol_rec_add() FOL record parts in the list are combined in a
   FOL record and is made persistent. Before this phase all FOL record parts
   need to be added in the list after completing their updates.

   After retrieving FOL record from the storage, FOL record parts are decoded
   based on part type using index and are used in undo or redo operations.
   <hr>
   @section IOFOLDLD-conformance Conformance

   @{
 */

/* export */
struct m0_fol;
struct m0_fol_rec;

/* import */
#include "lib/types.h"      /* uint64_t */
#include "lib/arith.h"      /* M0_IS_8ALIGNED */
#include "lib/mutex.h"
#include "lib/tlist.h"
#include "fid/fid.h"
#include "be/tx_credit.h"
#include "dtm/dtm_update.h" /* m0_update_id, m0_epoch_id */
#include "fid/fid_xc.h"
#include "dtm/dtm_update_xc.h"

struct m0_be_tx;
struct m0_epoch_id;

enum {
	/*
	 * The maximum possible length of fol record.
	 *
	 * We need to obtain sufficient BE credit before adding new record
	 * to the fol. Fol records are of variable length and the actual
	 * length is hard, if possible, to calculate at the moment of
	 * m0_fol_credit() call. We use the empirical value of maximum
	 * possible record length instead.
	 *
	 * XXX REVISEME: If the value is not sufficient, increase it.
	 * Alternative (proper?) solution is to calculate the size of fol
	 * record as a function of rpc opcode.
	 */
	FOL_REC_MAXSIZE = 1024 * 1024
};

/**
   In-memory representation of a fol.

   <b>Liveness rules and concurrency control.</b>

   m0_fol liveness is managed by the user (the structure is not reference
   counted) which is responsible for guaranteeing that m0_fol_fini() is the last
   call against a given instance.

   FOL code manages concurrency internally: multiple threads can call m0_fol_*()
   functions against the same fol (except for m0_fol_init() and m0_fol_fini()).
 */
struct m0_fol {
	/** Lock, serializing fol access. */
	struct m0_mutex    f_lock;
};

/**
   Initialise in-memory fol structure.
 */
M0_INTERNAL void m0_fol_init(struct m0_fol *fol);
M0_INTERNAL void m0_fol_fini(struct m0_fol *fol);

/**
   Fixed part of a fol record.

   @see m0_fol_rec
 */
struct m0_fol_rec_header {
	/** Number of record parts added to the record. */
	uint32_t            rh_parts_nr;
	/**
	 * Length of the remaining operation type specific data in bytes.
	 *
	 * @note XXX Currently this is the length of encoded record.
	 */
	uint32_t            rh_data_len;
	/**
	 * Identifier of this update.
	 *
	 * @note The update might be for a different node.
	 */
	struct m0_update_id rh_self;
	uint64_t            rh_magic;
} M0_XCA_RECORD;

M0_BASSERT(M0_IS_8ALIGNED(sizeof(struct m0_fol_rec_header)));

/**
   In-memory representation of a fol record.

   m0_fol_rec is bound to a particular fol and remembers its
   location in the log.
 */
struct m0_fol_rec {
	struct m0_fol            *fr_fol;
	uint64_t                  fr_tid;
	struct m0_fol_rec_header  fr_header;
	/** A DTM epoch this update is a part of. */
	struct m0_epoch_id       *fr_epoch;
	/** Identifiers of sibling updates. */
	struct m0_fol_update_ref *fr_sibling;
	/**
	   A list of all FOL record parts in a record.
	   Record parts are linked through m0_fol_rec_part:rp_link to this list.
	 */
	struct m0_tl              fr_parts;
};

/**
   Initializes fol record parts list.

   The user must call m0_fol_rec_fini() when finished dealing with
   the record.
 */
M0_INTERNAL void m0_fol_rec_init(struct m0_fol_rec *rec, struct m0_fol *fol);

/** Finalizes fol record parts list. */
M0_INTERNAL void m0_fol_rec_fini(struct m0_fol_rec *rec);

/**
   Encodes the fol record @rec at the specified buffer @at.

   @see m0_fol_rec_put()
 */
M0_INTERNAL int m0_fol_rec_encode(struct m0_fol_rec *rec, struct m0_buf *at);

/**
   Decodes a record into @rec from the specified buffer @at.

   @at is m0_be_tx::t_payload.

   @rec must be initialized with m0_fol_rec_init() beforehand.
   The user must call m0_fol_rec_fini() when finished dealing with
   the record.
 */
M0_INTERNAL int m0_fol_rec_decode(struct m0_fol_rec *rec, struct m0_buf *at);

M0_INTERNAL bool m0_fol_rec_invariant(const struct m0_fol_rec *drec);

M0_INTERNAL int m0_fols_init(void);
M0_INTERNAL void m0_fols_fini(void);

/** Represents a part of FOL record. */
struct m0_fol_rec_part {
	const struct m0_fol_rec_part_ops  *rp_ops;
	/**
	    Pointer to the data where FOL record part is serialised or
	    will be de-serialised.
	 */
	void				  *rp_data;
	/** Linkage into a fol record parts. */
	struct m0_tlink			   rp_link;
	/** Magic for fol record part list. */
	uint64_t			   rp_magic;
	/**
	 * As rp_data points to the in-memory record part during encoding,
	 * rp_data is freed only when rp_flag is equals to M0_XCODE_DECODE.
	 */
	enum m0_xcode_what 		   rp_flag;
};

struct m0_fol_rec_part_type {
	uint32_t                               rpt_index;
	const char                            *rpt_name;
	/**
	    Xcode type representing the FOL record part.
	    Used to encode, decode or calculate the length of
	    FOL record parts using xcode operations.
	 */
	const struct m0_xcode_type	      *rpt_xt;
	const struct m0_fol_rec_part_type_ops *rpt_ops;
};

struct m0_fol_rec_part_type_ops {
	/**  Sets the record part operations vector. */
	void (*rpto_rec_part_init)(struct m0_fol_rec_part *part);
};

/**
    FOL record parts are decoded from FOL record and then undo or
    redo operations are performed on these parts.
 */
struct m0_fol_rec_part_ops {
	const struct m0_fol_rec_part_type *rpo_type;
	int (*rpo_undo)(struct m0_fol_rec_part *part, struct m0_be_tx *tx);
	int (*rpo_redo)(struct m0_fol_rec_part *part, struct m0_be_tx *tx);
	void (*rpo_undo_credit)(const struct m0_fol_rec_part *part,
				struct m0_be_tx_credit *accum);
	void (*rpo_redo_credit)(const struct m0_fol_rec_part *part,
				struct m0_be_tx_credit *accum);
};

struct m0_fol_rec_part_header {
	uint32_t rph_index;
	uint64_t rph_magic;
} M0_XCA_RECORD;

/**
   During encoding of FOL record data points to the in-memory FOL record
   part object allocated by the calling function.
   In case if decoding data should be NULL, as it is allocated by xcode.
   @pre part != NULL
   @pre type != NULL
 */
M0_INTERNAL void
m0_fol_rec_part_init(struct m0_fol_rec_part *part, void *data,
		     const struct m0_fol_rec_part_type *type);

M0_INTERNAL void m0_fol_rec_part_fini(struct m0_fol_rec_part *part);

/** Register a new fol record part type. */
M0_INTERNAL int
m0_fol_rec_part_type_register(struct m0_fol_rec_part_type *type);

M0_INTERNAL void
m0_fol_rec_part_type_deregister(struct m0_fol_rec_part_type *type);

/** Descriptor for the tlist of fol record parts. */
M0_TL_DESCR_DECLARE(m0_rec_part, M0_EXTERN);
M0_TL_DECLARE(m0_rec_part, M0_INTERNAL, struct m0_fol_rec_part);

M0_INTERNAL void m0_fol_rec_part_add(struct m0_fol_rec *rec,
				     struct m0_fol_rec_part *part);

#define M0_FOL_REC_PART_TYPE_DECLARE(part, scope, undo, redo,	   \
				     undo_cred, redo_cred)	   \
scope struct m0_fol_rec_part_type part ## _type;		   \
static const struct m0_fol_rec_part_ops part ## _ops = {           \
	.rpo_type = &part ## _type,		                   \
	.rpo_undo = undo,			                   \
	.rpo_redo = redo,			                   \
	.rpo_undo_credit = undo_cred,			           \
	.rpo_redo_credit = redo_cred,			           \
};						                   \
static void part ## _ops_init(struct m0_fol_rec_part *part)        \
{							           \
	part->rp_ops = &part ## _ops;			           \
}							           \
static const struct m0_fol_rec_part_type_ops part ## _type_ops = { \
	.rpto_rec_part_init = part ##_ops_init			   \
};

#define M0_FOL_REC_PART_TYPE_XC_OPS(name, part_xc, part_type_ops) \
(struct m0_fol_rec_part_type) {					  \
	.rpt_name = name,					  \
	.rpt_xt   = (part_xc),					  \
	.rpt_ops  = (part_type_ops)				  \
};

#define M0_FOL_REC_PART_TYPE_INIT(part, name)		        \
part ## _type = M0_FOL_REC_PART_TYPE_XC_OPS(name, part ## _xc,  \
				            &part ## _type_ops)

/** @} end of fol group */
#endif /* __MERO_FOL_FOL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
