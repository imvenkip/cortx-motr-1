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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 29-May-2013
 */
#pragma once
#ifndef __MERO_BE_BTREE_H__
#define __MERO_BE_BTREE_H__

#include "be/be.h"
#include "be/seg.h"
#include "lib/rwlock.h"
#include "lib/buf.h"

/**
 * @defgroup be
 *
 * @{
 */

/* export */
struct m0_be_btree;
struct m0_be_bnode;
struct m0_be_btree_kv_ops;

/* import */
struct m0_be_tx;
struct m0_be_tx_credit;

/** In-memory B-tree, that can be stored on disk. */
struct m0_be_btree {
	/** The lock to acquire when performing operations on the tree. */
	struct m0_rwlock                 bb_lock;
	/** Segment in which ->bb_root node is being stored. */
	struct m0_be_seg                *bb_seg;
	/** Root node of the tree. */
	struct m0_be_bnode              *bb_root;
	/** operation vector, treating keys and values, given by the user */
	const struct m0_be_btree_kv_ops *bb_ops;
};

/** Btree operations vector. */
struct m0_be_btree_kv_ops {
	/** Size of key.  XXX RENAMEME? s/ko_ksize/ko_key_size/ */
	m0_bcount_t (*ko_ksize)(const void *key);

	/** Size of value.  XXX RENAMEME? s/ko_vsize/ko_val_size/
	 */
	m0_bcount_t (*ko_vsize)(const void *data);

	/**
	 * Key comparison function.
	 *
	 * Should return -ve, 0 or +ve value depending on how key0 and key1
	 * compare in key ordering.
	 *
	 * XXX RENAMEME? s/ko_compare/ko_key_cmp/
	 */
	int         (*ko_compare)(const void *key0, const void *key1);
};

/**
 * Type of persistent operation over the tree.
 *
 * These values are also re-used to define transaction credit types.
 */
enum m0_be_btree_op {
	M0_BBO_CREATE,	    /**< Used for m0_be_btree_create() */
	M0_BBO_DESTROY,     /**< .. m0_be_btree_destroy() */
	M0_BBO_INSERT,      /**< .. m0_be_btree_{,inplace_}insert() */
	M0_BBO_DELETE,      /**< .. m0_be_btree_{,inplace_}delete() */
	M0_BBO_UPDATE,      /**< .. m0_be_btree_{,inplace_}update() */
	M0_BBO_LOOKUP,      /**< .. m0_be_btree_lookup() */
	M0_BBO_MAXKEY,      /**< .. m0_be_btree_maxkey() */
	M0_BBO_MINKEY,      /**< .. m0_be_btree_minkey() */
	M0_BBO_CURSOR_GET,  /**< .. m0_be_btree_cursor_get() */
	M0_BBO_CURSOR_NEXT, /**< .. m0_be_btree_cursor_next() */
	M0_BBO_CURSOR_PREV, /**< .. m0_be_btree_cursor_prev() */
};

/* ------------------------------------------------------------------
 * Btree construction
 * ------------------------------------------------------------------ */

/**
 * Initalises internal structures of the @tree (e.g., mutexes, @ops),
 * located in virtual memory of the program and not in mmaped() segment
 * memory.
 */
M0_INTERNAL void m0_be_btree_init(struct m0_be_btree *tree,
				  struct m0_be_seg *seg,
				  const struct m0_be_btree_kv_ops *ops);

/**
 * Finalises in-memory structures of btree.
 *
 * Does not touch segment on disk.
 * @see m0_be_btree_destroy(), which does remove tree structure from the
 * segment.
 */
M0_INTERNAL void m0_be_btree_fini(struct m0_be_btree *tree);

/**
 * Creates btree on segment.
 *
 * The operation is asynchronous. Use m0_be_op_wait() or
 * m0_be_op_tick_ret() to wait for its completion.
 *
 * Example:
 * @code
 *         m0_be_btree_init(&tree, seg, kv_ops);
 *         m0_be_btree_create(&tree, tx, op);
 *         rc = m0_be_op_wait(op);
 *         if (rc == 0) {
 *                 ... // work with newly created tree
 *         }
 * @endcode
 */
M0_INTERNAL void m0_be_btree_create(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op);

/** Deletes btree from segment, asynchronously. */
M0_INTERNAL void m0_be_btree_destroy(struct m0_be_btree *tree,
				     struct m0_be_tx *tx,
				     struct m0_be_op *op);

/* ------------------------------------------------------------------
 * Btree credits
 * ------------------------------------------------------------------ */

/**
 * Calculates the credit needed to create @nr nodes and adds this credit to
 * @accum.
 */
M0_INTERNAL void m0_be_btree_create_credit(const struct m0_be_btree *tree,
					   m0_bcount_t nr,
					   struct m0_be_tx_credit *accum);

/**
 * Calculates the credit needed to destroy @nr nodes and adds this credit
 * to @accum.
 */
M0_INTERNAL void m0_be_btree_destroy_credit(struct m0_be_btree *tree,
					    m0_bcount_t nr,
					    struct m0_be_tx_credit *accum);

/**
 * Calculates how many internal resources of tx_engine, described by
 * m0_be_tx_credit, is needed to perform the insert operation over the @tree.
 * Function updates @accum structure which is an input for m0_be_tx_prep().
 *
 * @param nr     Number of @optype operations.
 * @param ksize  Key data size.
 * @param vsize  Value data size.
 */
M0_INTERNAL void m0_be_btree_insert_credit(const struct m0_be_btree *tree,
					   m0_bcount_t nr,
					   m0_bcount_t ksize,
					   m0_bcount_t vsize,
					   struct m0_be_tx_credit *accum);

/**
 * Calculates how many internal resources of tx_engine, described by
 * m0_be_tx_credit, is needed to perform the delete operation over the @tree.
 * Function updates @accum structure which is an input for m0_be_tx_prep().
 *
 * @param nr     Number of @optype operations.
 * @param ksize  Key data size.
 * @param vsize  Value data size.
 */
M0_INTERNAL void m0_be_btree_delete_credit(const struct m0_be_btree *tree,
						 m0_bcount_t nr,
						 m0_bcount_t ksize,
						 m0_bcount_t vsize,
						 struct m0_be_tx_credit *accum);

/**
 * Calculates how many internal resources of tx_engine, described by
 * m0_be_tx_credit, is needed to perform the update operation over the @tree.
 * Function updates @accum structure which is an input for m0_be_tx_prep().
 *
 * @param nr  Number of @optype operations.
 */
M0_INTERNAL void m0_be_btree_update_credit(const struct m0_be_btree *tree,
						 m0_bcount_t nr,
						 m0_bcount_t vsize,
						 struct m0_be_tx_credit *accum);

/* ------------------------------------------------------------------
 * Btree manipulation
 * ------------------------------------------------------------------ */

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
 * Updates the @value at the @key in btree. Operation is asynchronous.
 *
 * -ENOENT is set to @op->bo_u.u_btree.t_rc if not found.
 *
 * @see m0_be_btree_insert()
 */
M0_INTERNAL void m0_be_btree_update(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    const struct m0_buf *key,
				    const struct m0_buf *value);

/**
 * Deletes the entry by the given @key from btree. Operation is asynchronous.
 *
 * -ENOENT is set to @op->bo_u.u_btree.t_rc if not found.
 *
 * @see m0_be_btree_insert()
 */
M0_INTERNAL void m0_be_btree_delete(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    const struct m0_buf *key);

/**
 * Looks up for a @dest_value by the given @key in btree.
 * The result is copied into provided @dest_value buffer.
 *
 * -ENOENT is set to @op->bo_u.u_btree.t_rc if not found.
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
 * Btree in-place manipulation
 * ------------------------------------------------------------------ */

/**
 * Btree anchor, used to perform btree inplace operations in which
 * values are not being copied and the ->bb_lock is not released
 * until m0_be_btree_release() is called.
 *
 * In cases, when data in m0_be_btree_anchor::ba_value is updated,
 * m0_be_btree_release() will capture the region data lies in.
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
 * User provides the size of the value buffer that will be updated
 * via @anchor->ba_value.b_nob and gets the ready memory buffer
 * via @anchor->ba_value.b_addr.
 *
 * -ENOENT is set to @op->bo_u.u_btree.t_rc if not found.
 *
 * @see m0_be_btree_insert, note0 - note2.
 *
 * Usage:
 * @code
 *         anchor->ba_value.b_nob = new_value_size;
 *         m0_be_btree_update_inplace(tree, tx, op, key, anchor);
 *
 *         rc = m0_be_op_wait(op);
 *         M0_ASSERT(rc == 0);
 *
 *         update(anchor->ba_value.b_addr);
 *         m0_be_btree_release(tree, anchor);
 *         ...
 *         m0_be_tx_close(tx);
 * @endcode
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
 * -ENOENT is set to @op->bo_u.u_btree.t_rc if not found.
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
M0_INTERNAL void m0_be_btree_release(struct m0_be_btree              *tree,
				     struct m0_be_tx                 *tx,
				     const struct m0_be_btree_anchor *anchor);

/* ------------------------------------------------------------------
 * Btree cursor
 * ------------------------------------------------------------------ */

/**
 * Btree cursor stack entry.
 *
 * Used for cursor depth-first in-order traversing.
 */
struct m0_be_btree_cursor_stack_entry {
	struct m0_be_bnode *bs_node;
	int                 bs_idx;
};

/** Btree configuration constants. */
enum {
	BTREE_FAN_OUT    =  5,
	BTREE_HEIGHT_MAX = 15
};

/**
 * Btree cursor.
 *
 * Read-only cursor can be positioned with m0_be_btree_cursor_get() and moved
 * with m0_be_btree_cursor_next(), m0_be_btree_cursor_prev().
 */
struct m0_be_btree_cursor {
	struct m0_be_bnode                   *bc_node;
	int                                   bc_pos;
	struct m0_be_btree_cursor_stack_entry bc_stack[BTREE_HEIGHT_MAX];
	int                                   bc_stack_pos;
	struct m0_be_btree                   *bc_tree;
	struct m0_be_op                       bc_op; /* XXX DELETEME */
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
 *  less than given, otherwise it'll be set on exact key if it's possible.
 */
M0_INTERNAL void m0_be_btree_cursor_get(struct m0_be_btree_cursor *cursor,
					const struct m0_buf *key, bool slant);

/** Synchronous version of m0_be_btree_cursor_get(). */
M0_INTERNAL int m0_be_btree_cursor_get_sync(struct m0_be_btree_cursor *cur,
					    const struct m0_buf *key,
					    bool slant);

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
 * Moves cursor to the first key in the btree.
 *
 * @note The call is synchronous.
 */
M0_INTERNAL int m0_be_btree_cursor_first_sync(struct m0_be_btree_cursor *cur);

/**
 * Moves cursor to the last key in the btree.
 *
 * @note The call is synchronous.
 */
M0_INTERNAL int m0_be_btree_cursor_last_sync(struct m0_be_btree_cursor *cur);

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
 * Any of @key and @val pointers can be NULL, but not both.
 *
 * Note: interface is synchronous.
 */
M0_INTERNAL void m0_be_btree_cursor_kv_get(struct m0_be_btree_cursor *cur,
					   struct m0_buf *key,
					   struct m0_buf *val);

/**
 * @pre  tree->bb_root != NULL
 */
M0_INTERNAL bool m0_be_btree_is_empty(struct m0_be_btree *tree);

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
