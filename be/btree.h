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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 29-May-2013
 */

#pragma once
#ifndef __MERO_BE_BTREE_H__
#define __MERO_BE_BTREE_H__

/**
 * @defgroup be
 *
 * @{
 */

#include "lib/rwlock.h"
#include "lib/buf.h"
#include "be/seg.h"
#include "be/be.h"

struct m0_be_tx;
struct m0_be_bnode;
struct m0_be_tx_credit;
struct m0_be_btree_kv_ops;

/**
 * Represents persistent storage based on inmemory btree which can be stored on
 * disk.
 */
struct m0_be_btree {
	/** Lock, taken when some operations are performed over the tree */
	struct m0_rwlock                 bb_lock;
	/** Segment in which @bb_root node is being stored */
	struct m0_be_seg                *bb_seg;
	/** Inmemory root node of the tree */
	struct m0_be_bnode              *bb_root;
	/** operation vector, treating keys and values, given by the user */
	const struct m0_be_btree_kv_ops *bb_ops;
};

/**
 * Possible persistent operations over the tree.
 * Enumeration items are being reused to define transaction credit types also.
 */
enum m0_be_btree_op {
	M0_BBO_CREATE,	/**< used for m0_be_btree_create() */
	M0_BBO_DESTROY, /**< used for m0_be_btree_destroy() */
	M0_BBO_INSERT,  /**< used for m0_be_btree_{inplace_|}insert() */
	M0_BBO_DELETE,  /**< used for m0_be_btree_{inplace_|}delete() */
	M0_BBO_UPDATE,  /**< used for m0_be_btree_{inplace_|}update() */
	M0_BBO_LOOKUP,  /**< used for m0_be_btree_lookup() */
	M0_BBO_MAXKEY,  /**< used for m0_be_btree_maxkey() */
	M0_BBO_MINKEY,  /**< used for m0_be_btree_minkey() */
	M0_BBO_CURSOR_GET, /**<  used for m0_be_btree_cursor_get() */
	M0_BBO_CURSOR_NEXT, /**< used for m0_be_btree_cursor_next() */
	M0_BBO_CURSOR_PREV, /**< used for m0_be_btree_cursor_prev() */
};

/**
 * Operations user has to define for the keys and values sored in the tree.
 */
struct m0_be_btree_kv_ops {
	/** Returns the key size */
        m0_bcount_t   (*ko_ksize)   (const void *key);
	/** Returns the value size */
        m0_bcount_t   (*ko_vsize)   (const void *data);
	/** @return 1 if key0 > key1, -1 if key0 < key2, 0 if key0 == key2 */
        int           (*ko_compare) (const void *key0, const void *key1);
};


/* ------------------------------------------------------------------
 * Btree construction interface
 * ------------------------------------------------------------------ */

/**
 * Initalises internal structures of @tree like mutexes, @ops, lying in virtual
 * memory of the program and not in mmaped() segment memory.
 */
M0_INTERNAL void m0_be_btree_init(struct m0_be_btree *tree,
				  struct m0_be_seg   *seg,
				  const struct m0_be_btree_kv_ops *ops);

/**
 * Finalizes in-memory structures of btree. Doesn't touch segment on the disk.
 * @see m0_be_btree_destroy() call which removes tree structures stored on the
 * disk.
 */
M0_INTERNAL void m0_be_btree_fini(struct m0_be_btree *tree);

/**
 * Creates btree in its segment. Operation is asynchronous. User has to rely on
 * op::bo_sm state. When it transits into M0_BOS_SUCCESS btree is created in the
 * segment. In case of failure op::bo_sm is transitted into M0_BOS_FAILURE.
 *
 * User has to use either m0_be_op_wait() or m0_be_op_tick_ret() when it's
 * needed to know that operation is finished.
 *
 * Use case:
 *     m0_be_btree_init(&tree, seg, kv_ops, NULL);
 *     m0_be_btree_create(&tree, tx, op);
 *     M0_ASSERT(m0_be_op_wait(op) == 0); // tree is created in its segment...
 */
M0_INTERNAL void m0_be_btree_create(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op);

/**
 * Destroys btree in its segment. @see m0_be_btree_create for more details.
 */
M0_INTERNAL void m0_be_btree_destroy(struct m0_be_btree *tree,
				     struct m0_be_tx *tx,
				     struct m0_be_op *op);


/* ------------------------------------------------------------------
 * Btree manipulation interface
 * ------------------------------------------------------------------ */

/**
 * Calculates how many internal resources of tx_engine, described by
 * m0_be_tx_credit, is needed to perform the create operation over the @tree.
 * Function updates @accum structure which is an input for m0_be_tx_prep().
 *
 * @param nr     number of @optype operations.
 */
M0_INTERNAL void m0_be_btree_create_credit(const struct m0_be_btree     *tree,
						 m0_bcount_t             nr,
						 struct m0_be_tx_credit *accum);

/**
 * Calculates how many internal resources of tx_engine, described by
 * m0_be_tx_credit, is needed to perform the create operation over the @tree.
 * Function updates @accum structure which is an input for m0_be_tx_prep().
 *
 * @param nr     number of @optype operations.
 */
M0_INTERNAL void m0_be_btree_destroy_credit(const struct m0_be_btree *tree,
					    m0_bcount_t               nr,
					    struct m0_be_tx_credit   *accum);

/**
 * Calculates how many internal resources of tx_engine, described by
 * m0_be_tx_credit, is needed to perform the insert operation over the @tree.
 * Function updates @accum structure which is an input for m0_be_tx_prep().
 *
 * @param nr     number of @optype operations.
 * @param ksize  key data size.
 * @param vsize  value data size.
 */
M0_INTERNAL void m0_be_btree_insert_credit(const struct m0_be_btree     *tree,
						 m0_bcount_t             nr,
						 m0_bcount_t             ksize,
						 m0_bcount_t             vsize,
						 struct m0_be_tx_credit *accum);

/**
 * Calculates how many internal resources of tx_engine, described by
 * m0_be_tx_credit, is needed to perform the delete operation over the @tree.
 * Function updates @accum structure which is an input for m0_be_tx_prep().
 *
 * @param nr     number of @optype operations.
 * @param ksize  key data size.
 * @param vsize  value data size.
 */
M0_INTERNAL void m0_be_btree_delete_credit(const struct m0_be_btree     *tree,
						 m0_bcount_t             nr,
						 m0_bcount_t             ksize,
						 m0_bcount_t             vsize,
						 struct m0_be_tx_credit *accum);

/**
 * Calculates how many internal resources of tx_engine, described by
 * m0_be_tx_credit, is needed to perform the update operation over the @tree.
 * Function updates @accum structure which is an input for m0_be_tx_prep().
 *
 * @param nr     number of @optype operations.
 */
M0_INTERNAL void m0_be_btree_update_credit(const struct m0_be_btree     *tree,
						 m0_bcount_t             nr,
						 m0_bcount_t             vsize,
						 struct m0_be_tx_credit *accum);

/**
 * Inserts @key and @value into btree. Operation is asynchronous.
 *
 * Note0: interface is asynchronous and relies on op::bo_sm.
 * Operation is considered to be finished after op::bo_sm transits to
 * M0_BOS_SUCCESS | M0_BOS_FAILURE: after that point other operations will see
 * the effect of this one.
 *
 * Note1: When op::bo_sm state transits from M0_BOS_INIT into M0_BOS_SUCCESS
 * then page where requested memory regions of the tree is loaded from the disk
 * into the mmaped segment.
 *
 * Note2: When tx::bt_sm state transits from M0_BTS_PREPARE to M0_BTS_PLACED
 * or M0_BTS_DONE then data passed to the function in @key and @value become
 * persistent.
 *
 */
M0_INTERNAL void m0_be_btree_insert(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    const struct m0_buf *key,
				    const struct m0_buf *value);

/**
 * Updates @key and @value into btree. Operation is asynchronous.
 *
 * @see m0_be_btree_insert()
 */
M0_INTERNAL void m0_be_btree_update(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    const struct m0_buf *key,
				    const struct m0_buf *value);

/**
 * Deletes @key and @value from btree. Operation is asynchronous.
 *
 * @see m0_be_btree_insert()
 */
M0_INTERNAL void m0_be_btree_delete(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    const struct m0_buf *key);

/**
 * Looks up for a @dest_value for the given @key.
 *
 * @see m0_be_btree_create() regarding @op structure "mission".
 */
M0_INTERNAL void m0_be_btree_lookup(struct m0_be_btree *tree,
				    struct m0_be_op *op,
				    const struct m0_buf *key,
				    struct m0_buf *dest_value);

/**
 * Looks up for a maximum key value in the given @tree.
 *
 * @see m0_be_btree_create() regarding @op structure "mission".
 */
M0_INTERNAL void m0_be_btree_maxkey(struct m0_be_btree *tree,
				    struct m0_be_op *op,
				    struct m0_buf *out);

/**
 * Looks up for a minimum key value in the given @tree.
 *
 * @see m0_be_btree_create() regarding @op structure "mission".
 */
M0_INTERNAL void m0_be_btree_minkey(struct m0_be_btree *tree,
				    struct m0_be_op *op,
				    struct m0_buf *out);


/* ------------------------------------------------------------------
 * Btree inplace manipulation interface
 * ------------------------------------------------------------------ */

/**
 * Btree anchor, used to perform btree inplace operations in which neither keys
 * nor values are not being copied.
 *
 * In cases, when data in m0_be_btree_anchor::ba_value is updated,
 * m0_be_btree_release() has to capture the region data lies in.
 */
struct m0_be_btree_anchor {
	 /**
	  * A value, accessed through m0_be_btree_lookup_inplace(),
	  * m0_be_btree_insert_inplace(), m0_be_btree_update_inplace()
	  */
	struct m0_buf ba_value;

	/** Is write lock being held? */
	bool          ba_write;
};

/**
 * Updates @value looked up by given @key in btree. Operation is asynchronous.
 * User can either use existing @value buffer and copy inserted data there or
 * allocate his own. The last assumes that both @value buffer and node buffer
 * in which key is inserted has to be captured prior to this call.
 *
 * @see m0_be_btree_insert, note0 - note2.
 *
 * Note3: m0_be_btree::bb_lock is being held inside this function. To do this,
 * user has to set @anchor::ba_write and lock will be held for write if it's
 * true and for read otherwize.
 *
 * Note3: Neither given @key nor @value is copied or allocated in the tree after
 * this call.
 *
 * Usage:
 *
 * m0_be_btree_update_inplace(tree, tx, op, key, anchor);
 * M0_ASSERT(m0_be_op_wait(op) == 0); // wait for the completion...
 * update(anchor->ba_value.b_addr, anchor->ba_value.b_nob);
 * m0_be_btree_release(tree, anchor);
 * ...
 * m0_be_tx_close(tx);
 */
M0_INTERNAL void m0_be_btree_update_inplace(struct m0_be_btree *tree,
					    struct m0_be_tx *tx,
					    struct m0_be_op *op,
					    const struct m0_buf *key,
					    struct m0_be_btree_anchor *anchor);

/**
 * Inserts given @key and @value in btree. User has to allocate his own @value
 * buffer and capture node buffer in which @key is inserted.
 *
 * @see m0_be_btree_update_inplace()
 */
M0_INTERNAL void m0_be_btree_insert_inplace(struct m0_be_btree *tree,
					    struct m0_be_tx *tx,
					    struct m0_be_op *op,
					    const struct m0_buf *key,
					    struct m0_be_btree_anchor *anchor);

/**
 * Looks up a value stored in the @tree by the given @key.
 *
 * @see m0_be_btree_update_inplace()
 */
M0_INTERNAL void m0_be_btree_lookup_inplace(struct m0_be_btree *tree,
					    struct m0_be_op *op,
					    const struct m0_buf *key,
					    struct m0_be_btree_anchor *anchor);

/**
 * Completes m0_be_btree_*_inplace() operation by capturing all affected
 * regions with m0_be_tx_capture() and unlocking m0_be_btree::bb_lock.
 */
M0_INTERNAL void m0_be_btree_release(struct m0_be_btree *tree,
				     struct m0_be_op *op,
				     const struct m0_be_btree_anchor *anchor);


/* ------------------------------------------------------------------
 * Btree cursor interface
 * ------------------------------------------------------------------ */

/**
 * Btree cursor.
 *
 * Read-only cursor can be positioned with m0_be_btree_cursor_get() and moved
 * with m0_be_btree_cursor_next(), m0_be_btree_cursor_prev().
 */
struct m0_be_btree_cursor {
	struct m0_be_bnode *bc_node;
	unsigned int        bc_pos;
	struct m0_be_bnode *bc_last_node;
	unsigned int        bc_last_pos;

	struct m0_be_btree *bc_tree;
	struct m0_be_op     bc_op;
};

/**
 * Initialises cursor and its internal structures.
 *
 * Note: interface is synchronous.
 */
M0_INTERNAL void m0_be_btree_cursor_init(struct m0_be_btree_cursor *cursor,
					 struct m0_be_btree *tree);

/**
 * Finalizes cursor.
 *
 * Note: interface is synchronous.
 */
M0_INTERNAL void m0_be_btree_cursor_fini(struct m0_be_btree_cursor *cursor);

/**
 * Fills cursor internal buffers with current key and value obtained from the
 * tree. Operation may cause IO dependigly on cursor::bc_op state
 *
 * Note: interface is asynchronous and relies on cursor::bc_op::bo_sm. When it
 * transits into M0_BOS_SUCCESS | M0_BOS_FAILURE operation is considered to be
 * finished.
 *
 * Note: allowed sequence of cursor calls is:
 * - m0_be_btree_cursor_init();
 * - m0_be_btree_cursor_get();
 * - m0_be_btree_cursor_next()* | m0_be_btree_cursor_prev()*;
 * - m0_be_btree_cursor_kv_get()*;
 * - m0_be_btree_cursor_put();
 * - m0_be_btree_cursor_fini();
 *
 * @param slant[in] if slant == true then cursor will return a minimum key not
 *  less than given, otherwize it'll be set on exact key if it's possible.
 */
M0_INTERNAL void m0_be_btree_cursor_get(struct m0_be_btree_cursor *cursor,
					const struct m0_buf *key, bool slant);

/**
 * Fills cursor internal buffers with next key and value obtained from the
 * tree. Operation may cause IO dependigly on cursor::bc_op state
 *
 * Note: @see m0_be_btree_cursor_get note.
 */
M0_INTERNAL void m0_be_btree_cursor_next(struct m0_be_btree_cursor *cursor);

/**
 * Fills cursor internal buffers with prev key and value obtained from the
 * tree. Operation may cause IO dependigly on cursor::bc_op state
 *
 * Note: @see m0_be_btree_cursor_get note.
 */
M0_INTERNAL void m0_be_btree_cursor_prev(struct m0_be_btree_cursor *cursor);

/**
 * Unpins pages associated with cursor, releases cursor values.
 *
 * Note: interface is synchronous.
 */
M0_INTERNAL void m0_be_btree_cursor_put(struct m0_be_btree_cursor *cursor);

/**
 * Sets key and value buffers to point on internal structures of cursor
 * representing current key and value, cursor is placed on.
 *
 * Note: interface is synchronous.
 */
M0_INTERNAL void m0_be_btree_cursor_kv_get(struct m0_be_btree_cursor *cursor,
					   struct m0_buf *key,
					   struct m0_buf *value);




M0_INTERNAL void btree_dbg_print(struct m0_be_btree *tree);

/** @} end of be group */
#endif /* __MERO_BE_BTREE_H__ */

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
