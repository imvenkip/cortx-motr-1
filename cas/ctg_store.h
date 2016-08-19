/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Sergey Shilov <sergey.shilov@seagate.com>
 * Original creation date: 15-Aug-2016
 */

#pragma once

#ifndef __MERO_CAS_CTG_STORE_H__
#define __MERO_CAS_CTG_STORE_H__

#include "fop/fom_generic.h"
#include "fop/fom_long_lock.h"
#include "be/op.h"
#include "be/btree.h"
#include "be/tx_credit.h"
#include "format/format.h"
#include "cas/cas.h"

/**
 * @defgroup cas-ctg-store
 *
 * @{
 *
 * CAS catalogue store provides an interface to operate with catalogues.
 * It's a thin layer that hides B-tree implementation details. Most of
 * operations are asynchronous. For usability CAS catalogue store provides
 * separate interfaces for operations over meta, catalogue-index and
 * catalogues created by user.
 * @note All operations over catalogue-index catalogue are synchronous.
 *
 * For now CAS catalogue store has two users: CAS service and DIX
 * repair/rebalance service.
 * @note CAS catalogue store is a singleton in scope of single process.
 * Every user of CAS catalogue store must call m0_ctg_store_init() to initialize
 * static inner structures if the user needs this store to operate. This call is
 * thread-safe. If catalogue store is already initialized then
 * m0_ctg_store_init() does nothing but incrementing inner reference counter.
 *
 * Every user of CAS catalogue store must call m0_ctg_store_fini() if it does
 * not need this store anymore. This call is thread-safe. When
 * m0_ctg_store_fini() is called the inner reference counter is atomically
 * decremented. If the last user of CAS catalogue store calls
 * m0_ctg_store_fini() then release of CAS catalogue store inner structures is
 * initiated.
 *
 * @verbatim
 *
 *     +-----------------+            +------------------+
 *     |                 |            |       DIX        |
 *     |   CAS Service   |            | repair/rebalance |
 *     |                 |            |     service      |
 *     +-----------------+            +------------------+
 *              |                              |
 *              +-----------+      +-----------+
 *                          |      |
 *                          |      |
 *                          V      V
 *                    +------------------+
 *                    |       CAS        |
 *                    |    catalogue     |
 *                    |      store       |
 *                    +------------------+
 *
 * @endverbatim
 *
 * The interface of CAS catalogue store is FOM-oriented. For operations that may
 * be done asynchronous the next phase of FOM should be provided. Corresponding
 * functions return the result that shows whether the FOM should wait or can
 * continue immediately.
 * Every user should take care about locking of CAS catalogues.
 */


/** CAS catalogue. */
struct m0_cas_ctg {
	struct m0_format_header cc_head;
	struct m0_format_footer cc_foot;
	/*
	 * m0_be_btree has it's own volatile-only fields, so it can't be placed
	 * before the m0_format_footer, where only persistent fields allowed
	 */
	struct m0_be_btree      cc_tree;
	struct m0_long_lock     cc_lock;
};

enum m0_cas_ctg_format_version {
	M0_CAS_CTG_FORMAT_VERSION_1 = 1,

	/* future versions, uncomment and update M0_CAS_CTG_FORMAT_VERSION */
	/*M0_CAS_CTG_FORMAT_VERSION_2,*/
	/*M0_CAS_CTG_FORMAT_VERSION_3,*/

	/** Current version, should point to the latest version present */
	M0_CAS_CTG_FORMAT_VERSION = M0_CAS_CTG_FORMAT_VERSION_1
};

/** Structure that describes catalogue operation. */
struct m0_ctg_op {
	/** Caller FOM. */
	struct m0_fom            *co_fom;
	/** Catalogue for which the operation will be performed. */
	struct m0_cas_ctg        *co_ctg;
	/**
	 * BE operation structure for b-tree operations, except for
	 * CO_CUR. Cursor operations use m0_ctg_op::co_cur.
	 *
	 * @note m0_ctg_op::co_cur has its own m0_be_op. It could be used for
	 * all operations, but it is marked deprecated in btree.h.
	 */
	struct m0_be_op           co_beop;
	/** BTree anchor used for inplace operations. */
	struct m0_be_btree_anchor co_anchor;
	/** BTree cursor used for cursor operations. */
	struct m0_be_btree_cursor co_cur;
	/** Shows whether catalogue cursor is initialised. */
	bool                      co_cur_initialised;
	/** Current cursor phase. */
	int                       co_cur_phase;
	/** Manages calling of callback on completion of BE operation. */
	struct m0_clink           co_clink;
	/** Channel to communicate with caller FOM. */
	struct m0_chan            co_channel;
	/** Channel guard. */
	struct m0_mutex           co_channel_lock;
	/** Key buffer. */
	struct m0_buf             co_key;
	/** Value buffer. */
	struct m0_buf             co_val;
	/** Key out buffer. */
	struct m0_buf             co_out_key;
	/** Value out buffer. */
	struct m0_buf             co_out_val;
	/** Operation code to be executed. */
	int                       co_opcode;
	/** Operation type (meta or ordinary catalogue). */
	int                       co_ct;
	/** Operation flags. */
	uint32_t                  co_flags;
	/** Operation result code. */
	int                       co_rc;
};

#define CTG_OP_COMBINE(opc, ct) (((uint64_t)(opc)) | ((ct) << 16))

/**
 * Initialises catalogue store.
 * @note Every user of catalogue store must call this function as catalogue
 * store is a singleton and initialisation function may be called many times
 * from different threads: the first call does actual initialisation, following
 * calls does nothing but returning 0.
 *
 * Firstly the function looks up into BE segment dictionary for meta catalogue.
 * There are three cases:
 * 1. Meta catalogue is presented.
 * 2. meta catalogue is not presented.
 * 3. An error occurred.
 *
 * In the first case the function looks up into meta catalogue for
 * catalogue-index catalogue. If it is not presented or some error occurred then
 * an error returned.
 * @note Both meta catalogue and catalogue-index catalogue must be presented.
 * In the second case the function creates meta catalogue, creates
 * catalogue-index catalogue and inserts catalogue-index context into meta
 * catalogue.
 * In the third case an error returned.
 *
 * @ret 0 on success or negative error code.
 */
M0_INTERNAL int  m0_ctg_store_init();

/**
 * Releases one reference to catalogue store context. If the last reference is
 * released then actual finalisation will be done.
 * @note Every user of catalogue store must call this function.
 *
 * @see m0_ctg_store_init()
 */
M0_INTERNAL void m0_ctg_store_fini();

/** Returns a pointer to meta catalogue context. */
M0_INTERNAL struct m0_cas_ctg *m0_ctg_meta();
/** Returns a pointer to catalogu-index catalogue context. */
M0_INTERNAL struct m0_cas_ctg *m0_ctg_ctidx();

/**
 * Initialises catalogue operation.
 * @note Catalogue operation must be initialised before executing of any
 * operation on catalogues but getting lookup/cursor results and moving of
 * cursor.
 *
 * @param ctg_op Catalogue operation context.
 * @param fom    FOM that needs to execute catalogue operation.
 * @param flags  Catalogue operation flags.
 */
M0_INTERNAL void m0_ctg_op_init    (struct m0_ctg_op *ctg_op,
				    struct m0_fom    *fom,
				    uint32_t          flags);

/**
 * Gets result code of executed catalogue operation.
 *
 * @param ctg_op Catalogue operation context.
 *
 * @ret Result code of executed catalogue operation.
 */
M0_INTERNAL int  m0_ctg_op_rc      (struct m0_ctg_op *ctg_op);

/**
 * Finalises catalogue operation.
 *
 * @param ctg_op Catalogue operation context.
 */
M0_INTERNAL void m0_ctg_op_fini    (struct m0_ctg_op *ctg_op);

/**
 * Creates a new catalogue context and inserts it into meta catalogue.
 *
 * @param ctg_op     Catalogue operation context.
 * @param fid        FID of catalogue to be created and inserted into meta
 *                   catalogue.
 * @param next_phase Next phase of caller FOM.
 *
 * @ret M0_FSO_AGAIN or M0_FSO_WAIT.
 */
M0_INTERNAL int  m0_ctg_meta_insert(struct m0_ctg_op    *ctg_op,
				    const struct m0_fid *fid,
				    int                  next_phase);

/**
 * Looks up a catalogue in meta catalogue.
 *
 * @param ctg_op     Catalogue operation context.
 * @param fid        FID of catalogue to be looked up.
 * @param next_phase Next phase of caller FOM.
 *
 * @ret M0_FSO_AGAIN or M0_FSO_WAIT.
 */
M0_INTERNAL int  m0_ctg_meta_lookup(struct m0_ctg_op    *ctg_op,
				    const struct m0_fid *fid,
				    int                  next_phase);

/** Gets a pointer to catalogue context that was looked up. */
M0_INTERNAL
struct m0_cas_ctg *m0_ctg_meta_lookup_result(struct m0_ctg_op *ctg_op);

/**
 * Deletes a catalogue from meta catalogue.
 *
 * @param ctg_op     Catalogue operation context.
 * @param fid        FID of catalogue to be deleted.
 * @param next_phase Next phase of caller FOM.
 *
 * @ret M0_FSO_AGAIN or M0_FSO_WAIT.
 */
M0_INTERNAL int m0_ctg_meta_delete(struct m0_ctg_op    *ctg_op,
				   const struct m0_fid *fid,
				   int                  next_phase);

/**
 * Inserts a key/value record into catalogue.
 * @note Value itself is not copied inside of this function, user should keep it
 *       until operation is complete. Key is copied before operation execution,
 *       user does not need to keep it since this function is called.
 *
 * @param ctg_op     Catalogue operation context.
 * @param ctg        Context of catalogue for record insertion.
 * @param key        Key to be inserted.
 * @param val        Value to be inserted.
 * @param next_phase Next phase of caller FOM.
 *
 * @ret M0_FSO_AGAIN or M0_FSO_WAIT.
 */
M0_INTERNAL int m0_ctg_insert(struct m0_ctg_op    *ctg_op,
			      struct m0_cas_ctg   *ctg,
			      const struct m0_buf *key,
			      const struct m0_buf *val,
			      int                  next_phase);

/**
 * Deletes a key/value record from catalogue.
 * @note Key is copied before execution of operation, user does not need to keep
 *       it since this function is called.
 *
 * @param ctg_op     Catalogue operation context.
 * @param ctg        Context of catalogue for record deletion.
 * @param key        Key of record to be inserted.
 * @param next_phase Next phase of caller FOM.
 *
 * @ret M0_FSO_AGAIN or M0_FSO_WAIT.
 */
M0_INTERNAL int m0_ctg_delete(struct m0_ctg_op    *ctg_op,
			      struct m0_cas_ctg   *ctg,
			      const struct m0_buf *key,
			      int                  next_phase);

/**
 * Looks up a key/value record in catalogue.
 * @note Key is copied before execution of operation, user does not need to keep
 *       it since this function is called.
 *
 * @param ctg_op     Catalogue operation context.
 * @param ctg        Context of catalogue for record looking up.
 * @param key        Key of record to be looked up.
 * @param next_phase Next phase of caller FOM.
 *
 * @ret M0_FSO_AGAIN or M0_FSO_WAIT.
 */
M0_INTERNAL int m0_ctg_lookup(struct m0_ctg_op    *ctg_op,
			      struct m0_cas_ctg   *ctg,
			      const struct m0_buf *key,
			      int                  next_phase);

/**
 * Gets lookup result.
 * @note Returned pointer is a pointer to original value placed in tree.
 *
 * @param[in]  ctg_op Catalogue operation context.
 * @param[out] buf    Buffer for value.
 */
M0_INTERNAL void m0_ctg_lookup_result(struct m0_ctg_op *ctg_op,
				      struct m0_buf    *buf);

/**
 * Initialises cursor for meta catalogue.
 * @note Passed catalogue operation must be initialised using m0_ctg_op_init()
 *       before calling of this function.
 *
 * @param ctg_op Catalogue operation context.
 */
M0_INTERNAL void m0_ctg_meta_cursor_init(struct m0_ctg_op *ctg_op);

/**
 * Positions cursor on catalogue with FID @fid. Should be called after
 * m0_ctg_meta_cursor_init().
 *
 * @param ctg_op     Catalogue operation context.
 * @param fid        FID of catalogue for cursor positioning.
 * @param next_phase Next phase of caller FOM.
 *
 * @see m0_ctg_meta_cursor_init()
 */
M0_INTERNAL int  m0_ctg_meta_cursor_get(struct m0_ctg_op    *ctg_op,
					const struct m0_fid *fid,
					int                  next_phase);

/**
 * Initialises cursor for catalogue @ctg.
 * @note Passed catalogue operation must be initialised using m0_ctg_op_init()
 *       before calling of this function.
 *
 * @param ctg_op Catalogue operation context.
 * @param ctg    Catalogue context.
 */
M0_INTERNAL void m0_ctg_cursor_init(struct m0_ctg_op  *ctg_op,
				    struct m0_cas_ctg *ctg);

/**
 * Checks whether catalogue cursor is initialised.
 *
 * @param ctg_op Catalogue operation context.
 *
 * @see m0_ctg_meta_cursor_init()
 * @see m0_ctg_cursor_init()
 */
M0_INTERNAL bool m0_ctg_cursor_is_initialised(struct m0_ctg_op *ctg_op);

/**
 * Positions cursor on record with key @key. Should be called after
 * m0_ctg_cursor_init().
 *
 * @param ctg_op     Catalogue operation context.
 * @param key        Key for cursor positioning.
 * @param next_phase Next phase of caller FOM.
 *
 * @see m0_ctg_cursor_init()
 */
M0_INTERNAL int  m0_ctg_cursor_get(struct m0_ctg_op    *ctg_op,
				   const struct m0_buf *key,
				   int                  next_phase);

/**
 * Moves catalogue cursor to the next record.
 *
 * @param ctg_op     Catalogue operation context.
 * @param next_phase Next phase of caller FOM.
 */
M0_INTERNAL int  m0_ctg_cursor_next(struct m0_ctg_op *ctg_op,
				    int               next_phase);

/**
 * Gets current key/value under cursor.
 * @note Key/value data pointers are set to actual record placed in tree.
 *
 * @param[in]  ctg_op Catalogue operation context.
 * @param[out] key    Key buffer.
 * @param[out] val    Value buffer.
 */
M0_INTERNAL void m0_ctg_cursor_kv_get(struct m0_ctg_op *ctg_op,
				      struct m0_buf    *key,
				      struct m0_buf    *val);

/**
 * Releases catalogue cursor, should be called before m0_ctg_cursor_fini().
 *
 * @param ctg_op Catalogue operation context.
 *
 * @see m0_ctg_cursor_fini()
 */
M0_INTERNAL void m0_ctg_cursor_put(struct m0_ctg_op *ctg_op);

/**
 * Finalises catalogue cursor.
 *
 * @param ctg_op Catalogue operation context.
 */
M0_INTERNAL void m0_ctg_cursor_fini(struct m0_ctg_op *ctg_op);

/**
 * Calculates credits for insertion into meta catalogue.
 *
 * @param accum Accumulated credits.
 */
M0_INTERNAL void m0_ctg_meta_insert_credit(struct m0_be_tx_credit *accum);

/**
 * Calculates credits for deletion from meta catalogue.
 *
 * @param accum Accumulated credits.
 */
M0_INTERNAL void m0_ctg_meta_delete_credit(struct m0_be_tx_credit *accum);

/**
 * Calculates credits for insertion of record into catalogue.
 *
 * @param ctg   Catalogue context.
 * @param knob  Key length.
 * @param knob  Value length.
 * @param accum Accumulated credits.
 */
M0_INTERNAL void m0_ctg_insert_credit(struct m0_cas_ctg      *ctg,
				      m0_bcount_t             knob,
				      m0_bcount_t             vnob,
				      struct m0_be_tx_credit *accum);

/**
 * Calculates credits for deletion of record from catalogue.
 *
 * @param ctg   Catalogue context.
 * @param knob  Key length.
 * @param knob  Value length.
 * @param accum Accumulated credits.
 */
M0_INTERNAL void m0_ctg_delete_credit(struct m0_cas_ctg      *ctg,
				      m0_bcount_t             knob,
				      m0_bcount_t             vnob,
				      struct m0_be_tx_credit *accum);

/**
 * Calculates credits for insertion into catalogue-index catalogue.
 *
 * @param accum Accumulated credits.
 */
M0_INTERNAL void m0_ctg_ctidx_insert_credits(struct m0_cas_id       *cid,
					     struct m0_be_tx_credit *accum);

/**
 * Calculates credits for deletion from catalogue-index catalogue.
 *
 * @param accum Accumulated credits.
 */
M0_INTERNAL void m0_ctg_ctidx_delete_credits(struct m0_cas_id       *cid,
					     struct m0_be_tx_credit *accum);

/**
 * Synchronous record insertion into catalogue-index catalogue.
 *
 * @param cid CAS ID containing FID/layout to be inserted.
 * @param tx  BE transaction.
 *
 * @ret 0 on success or negative error code.
 */
M0_INTERNAL int m0_ctg_ctidx_insert(const struct m0_cas_id *cid,
				    struct m0_be_tx        *tx);

/**
 * Synchronous record deletion from catalogue-index catalogue.
 *
 * @param cid CAS ID containing FID/layout to be deleted.
 * @param tx  BE transaction.
 *
 * @ret 0 on success or negative error code.
 */
M0_INTERNAL int m0_ctg_ctidx_delete(const struct m0_cas_id *cid,
				    struct m0_be_tx        *tx);

/**
 * Synchronous record lookup in catalogue-index catalogue.
 *
 * @param[in]  fid    FID of component catalogue.
 * @param[out] layout Layout of index which component catalogue with FID @fid
 *                    belongs to.
 *
 * @ret 0 on success or negative error code.
 */
M0_INTERNAL int m0_ctg_ctidx_lookup(const struct m0_fid   *fid,
				    struct m0_dix_layout **layout);

/** @} end of cas-ctg-store group */
#endif /* __MERO_CAS_CTG_STORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
