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

/**
 * @addtogroup be
 *
 * @{
 */
#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"
#include "lib/cdefs.h" /* M0_UNUSED */
#include "lib/errno.h"
#include "lib/misc.h"  /* bcopy */
#include "be/btree.h"
#include "be/alloc.h"
#include "be/seg.h"
#include <math.h> /* pow */


/* btree constants */
enum {
	BTREE_FAN_OUT     = 5,
	BTREE_ALLOC_SHIFT = 0,

	KV_NR             = 2 * BTREE_FAN_OUT - 1,
	KV_SIZE           = KV_NR * sizeof(struct bt_key_val *),

	CHILDREN_NR       = 2 * BTREE_FAN_OUT,
	CHILDREN_SIZE     = CHILDREN_NR * sizeof(struct m0_be_bnode *)
};

struct bt_key_val {
        void *key;
        void *val;
};

struct m0_be_bnode {
	struct m0_be_bnode  *b_prev; /**< Pointer used for linked list */
	struct m0_be_bnode  *b_next; /**< Pointer used for linked list */
	bool                 b_leaf; /**< Used to indicate whether leaf or not */
        unsigned int         b_nr_active; /**< Number of active keys */
	unsigned int         b_level;    /**< Level in the B-Tree */
        struct bt_key_val  **b_key_vals; /**< Array of keys and values */
        struct m0_be_bnode **b_children; /**< Array of pointers to child nodes */
};

struct node_pos {
	struct m0_be_bnode *p_node;
	unsigned int	    p_index;
};

static inline void mem_free(const struct m0_be_btree *btree,
			    struct m0_be_tx *tx, void *ptr, m0_bcount_t size)
{
	struct m0_be_allocator *alloc = &btree->bb_seg->bs_allocator;
	struct m0_be_op		op; /* XXX */

	m0_be_op_init(&op);
	m0_be_free(alloc, tx, &op, ptr);
	m0_be_op_fini(&op);
}

/* XXX: check if region structure itself needed outside m0_be_tx_capture() */
static inline void mem_update(const struct m0_be_btree *btree,
			      struct m0_be_tx *tx, void *ptr, m0_bcount_t size)
{
	m0_be_tx_capture(tx, &M0_BE_REG(btree->bb_seg, size, ptr));
}

static inline void *mem_alloc(const struct m0_be_btree *btree,
			      struct m0_be_tx *tx, m0_bcount_t size)
{
	struct m0_be_allocator *alloc = &btree->bb_seg->bs_allocator;
	struct m0_be_op		op; /* XXX */
	void *p;

	m0_be_op_init(&op);
	p = m0_be_alloc(alloc, tx, &op, size, BTREE_ALLOC_SHIFT);
	memset(p, 0, size);
	mem_update(btree, tx, p, size);
	m0_be_op_fini(&op);
	return p;
}

static inline int key_lt(const struct m0_be_btree *btree,
			 const void *key0, const void *key1)
{
	return btree->bb_ops->ko_compare(key0, key1)  <  0;
}

static inline int key_gt(const struct m0_be_btree *btree,
			 const void *key0, const void *key1)
{
	return btree->bb_ops->ko_compare(key0, key1)  >  0;
}

static inline int key_eq(const struct m0_be_btree *btree,
			 const void *key0, const void *key1)
{
	return btree->bb_ops->ko_compare(key0, key1) ==  0;
}


/* ------------------------------------------------------------------
 * Btree internals implementation
 * ------------------------------------------------------------------ */

enum position_t {
	P_LEFT = -1,
	P_RIGHT = 1
};

static struct m0_be_bnode *allocate_btree_node(const struct m0_be_btree *btree,
					       struct m0_be_tx *tx);

static void free_btree_node(struct m0_be_bnode *node,
			    const struct m0_be_btree *btree,
			    struct m0_be_tx *tx);

static void btree_pair_release(struct m0_be_btree *btree,
			       struct m0_be_tx *tx,
			       struct bt_key_val *kv);

static struct node_pos get_btree_node(struct m0_be_btree *btree, void *key,
				      bool slant);

static int delete_key_from_node(struct m0_be_btree *btree,
				struct m0_be_tx *tx,
				struct node_pos *node_pos);

static struct m0_be_bnode *merge_nodes(struct m0_be_btree *btree,
				       struct m0_be_tx *tx,
				       struct m0_be_bnode *n1,
				       struct bt_key_val *kv,
				       struct m0_be_bnode *n2);

static void move_key(struct m0_be_btree *btree,
		     struct m0_be_tx *tx,
		     struct m0_be_bnode *node,
		     unsigned int index,
		     enum position_t pos);

static void get_max_key_pos(struct m0_be_btree *btree,
			    struct m0_be_bnode *subtree,
			    struct node_pos    *pos);

static void get_min_key_pos(struct m0_be_btree *btree,
			    struct m0_be_bnode *subtree,
			    struct node_pos    *pos);

static struct m0_be_bnode *merge_siblings(struct m0_be_btree *btree,
				      struct m0_be_tx *tx,
				      struct m0_be_bnode *parent,
				      unsigned int index, enum position_t pos);

static void copy_key_val(struct m0_be_btree	 *btree,
			 struct m0_be_tx	 *tx,
			 struct bt_key_val	 *src,
			 struct bt_key_val	 *dst);


/* ------------------------------------------------------------------
 * Btree invariant implementation:
 * - assuming that the tree is completely in memory;
 * - checks that keys are in order;
 * - child nodes have keys matching parent;
 * - nodes have expected occupancy: [1..2*order-1] for root and
 *				    [order-1..2*order-1] for leafs.
 *
 * Note: as far as height of practical tree will be 10-15, invariant can be
 * written in recusieve form.
 * ------------------------------------------------------------------ */
enum { BTREE_INVARIANT_HEIGHT_MAX = 15 };

static bool btree_node_invariant(const struct m0_be_btree *btree,
				 const struct m0_be_bnode *node,
				 bool root)
{
	return
		node->b_level < BTREE_INVARIANT_HEIGHT_MAX &&
		/* expected occupancy */
		ergo(root, 0 <= node->b_nr_active &&
		     node->b_nr_active <= KV_NR) &&
		ergo(!root, BTREE_FAN_OUT-1 <= node->b_nr_active &&
		     node->b_nr_active <= KV_NR) &&
		/* keys are in order */
		ergo(node->b_nr_active > 1,
		     m0_forall(i, node->b_nr_active - 1,
			       i < 1 ? true : node->b_key_vals[i]->key >
			       node->b_key_vals[i - 1]->key)) &&
		/* matchig parent */
		ergo(node->b_nr_active > 1,
		     m0_forall(i, node->b_nr_active - 1,
			       node->b_leaf ? true :
			       btree_node_invariant(btree, node->b_children[i],
						    false) &&
			       m0_forall(j, node->b_children[i]->b_nr_active,
					 key_gt(btree,
						/* key 0: */
						node->b_key_vals[i]->key,
						/* key 1: */
						node->b_children[i] ->
						b_key_vals[j]->key))));
}


/* ------------------------------------------------------------------
 * b-tree internals.
 * ------------------------------------------------------------------ */

static inline bool btree_invariant(const struct m0_be_btree *btree)
{
	/* Invariant may not work due to the implementation details of the tree
	 * which is obvoiusly bad. If so, please report this to ABilenko and
	 * feel free to disable invariant for a while.
	 */

	return btree_node_invariant(btree, btree->bb_root, true);
}

static void node_update(struct m0_be_bnode       *node,
			const struct m0_be_btree *btree,
			struct m0_be_tx          *tx)
{
	/* Update node itself */
	mem_update(btree, tx, node, sizeof(struct m0_be_bnode));
	/* Update keys and values pointers */
	mem_update(btree, tx, node->b_key_vals, KV_SIZE);
	/* Update links. We must not update children nodes, just pointers */
	mem_update(btree, tx, node->b_children, CHILDREN_SIZE);
}

/**
 * Used to create a btree with just the root node
 */
static void btree_create(struct m0_be_btree *btree, struct m0_be_tx *tx)
{
	btree->bb_root = allocate_btree_node(btree, tx);

	/* memory for the node has to be reserved by m0_be_tx_open() */
	M0_ASSERT(btree->bb_root != NULL);
}

/**
 * Function used to allocate memory for the btree node
 * @return The allocated B-tree node
 */
static struct m0_be_bnode *
allocate_btree_node(const struct m0_be_btree *btree, struct m0_be_tx *tx)
{
	struct m0_be_bnode *node;

	/*  Allocate memory for the node */
	node = (struct m0_be_bnode *)mem_alloc(btree, tx, sizeof(struct m0_be_bnode));
	M0_ASSERT(node != NULL);	/* @todo: analyse return code */

	/*  Initialize the number of active nodes */
	node->b_nr_active = 0;

	/*  Initialize the keys */
	node->b_key_vals = (struct bt_key_val **)mem_alloc(btree, tx, KV_SIZE);
	M0_ASSERT(node->b_key_vals != NULL); /* @todo: analyse return code */

	/*  Initialize the child pointers */
	node->b_children = (struct m0_be_bnode **)mem_alloc(btree, tx, CHILDREN_SIZE);
	M0_ASSERT(node->b_children != NULL); /* @todo: analyse return code */

	/*  Use to determine whether it is a leaf */
	node->b_leaf = true;

	/*  Use to determine the level in the tree */
	node->b_level = 0;

	/* Initialize the linked list pointer to NULL */
	node->b_next = NULL;

	return node;
}

/**
 * Function used to free the memory allocated to the b-tree
 */
static void free_btree_node(struct m0_be_bnode       *node,
			    const struct m0_be_btree *btree,
			    struct m0_be_tx          *tx)
{
	mem_free(btree, tx, node->b_children, CHILDREN_SIZE);
	mem_free(btree, tx, node->b_key_vals, KV_SIZE);
	mem_free(btree, tx, node, sizeof(struct m0_be_bnode));
}

/**
 * Used to split the child node and adjust the parent so that it has a pointer
 * to the new child
 */
static void btree_split_child(struct m0_be_btree *btree,
			      struct m0_be_tx	 *tx,
			      struct m0_be_bnode *parent,
			      unsigned int	  index,
			      struct m0_be_bnode *child)
{
	int i;
	unsigned int order = BTREE_FAN_OUT;

	struct m0_be_bnode *new_child = allocate_btree_node(btree, tx);
	M0_ASSERT(new_child != NULL);

	new_child->b_leaf = child->b_leaf;
	new_child->b_level = child->b_level;
	new_child->b_nr_active = BTREE_FAN_OUT - 1;

	/*  Copy the higher order keys to the new child */
	for (i = 0; i < order - 1; i++) {
		new_child->b_key_vals[i] = child->b_key_vals[i + order];
		if (!child->b_leaf) {
			new_child->b_children[i] = child->b_children[i + order];
		}
	}

	/*  Copy the last child pointer */
	if (!child->b_leaf) {
		new_child->b_children[i] = child->b_children[i + order];
	}

	child->b_nr_active = order - 1;

	for (i = parent->b_nr_active + 1; i > index + 1; i--) {
		parent->b_children[i] = parent->b_children[i - 1];
	}
	parent->b_children[index + 1] = new_child;

	for (i = parent->b_nr_active; i > index; i--) {
		parent->b_key_vals[i] = parent->b_key_vals[i - 1];
	}

	parent->b_key_vals[index] = child->b_key_vals[order - 1];
	parent->b_nr_active++;

	/* Update affected memory regions in tx: */
	node_update(parent, btree, tx);
	node_update(child, btree, tx);
	node_update(new_child, btree, tx);
}

/**
 * Used to insert a key in the non-full node
 */
static void btree_insert_nonfull(struct m0_be_btree *btree,
				 struct m0_be_tx    *tx,
				 struct m0_be_bnode *parent_node,
				 struct bt_key_val  *key_val)
{
	void *key = key_val->key;
	int i;
	struct m0_be_bnode *child;
	struct m0_be_bnode *node = parent_node;

 insert:i = node->b_nr_active - 1;
	if (node->b_leaf) {
		while (i >= 0 && key_lt(btree, key, node->b_key_vals[i]->key)) {
			node->b_key_vals[i + 1] = node->b_key_vals[i];
			i--;
		}
		node->b_key_vals[i + 1] = key_val;
		node->b_nr_active++;

		/* Update affected memory regions */
		node_update(node, btree, tx);
	} else {
		while (i >= 0 && key_lt(btree, key, node->b_key_vals[i]->key)) {
			i--;
		}
		i++;
		child = node->b_children[i];

		if (child->b_nr_active == KV_NR) {
			btree_split_child(btree, tx, node, i, child);
			if (key_gt(btree, key_val->key, node->b_key_vals[i]->key)) {
				i++;
			}
		}
		node = node->b_children[i];
		goto insert;
	}
}

/**
 * Function used to insert node into a B-Tree
 */
static void btree_insert_key(struct m0_be_btree *btree,
			     struct m0_be_tx	*tx,
			     struct bt_key_val	*key_val)
{
	struct m0_be_bnode *rnode;

	M0_PRE(btree_invariant(btree));

	rnode = btree->bb_root;
	if (rnode->b_nr_active == KV_NR) {
		struct m0_be_bnode *new_root;
		new_root = allocate_btree_node(btree, tx);
		M0_ASSERT(new_root != NULL);

		new_root->b_level = btree->bb_root->b_level + 1;
		btree->bb_root = new_root;
		new_root->b_leaf = false;
		new_root->b_nr_active = 0;
		new_root->b_children[0] = rnode;
		btree_split_child(btree, tx, new_root, 0, rnode);
		btree_insert_nonfull(btree, tx, new_root, key_val);

		/* Update tree structure itself */
		/* XXX not needed right now */
		mem_update(btree, tx, btree, sizeof(struct m0_be_btree));
		node_update(btree->bb_root, btree, tx);
	} else
		btree_insert_nonfull(btree, tx, rnode, key_val);

	M0_POST(btree_invariant(btree));
}

/**
 *	Used to get the position of the MAX key within the subtree
 *	@param btree The btree
 *	@param subtree The subtree to be searched
 *	@return The node_pos containing the key and position of the key
 */
static void get_max_key_pos(struct m0_be_btree *btree,
			    struct m0_be_bnode *node,
			    struct node_pos    *pos)
{
	for (; node != NULL && !node->b_leaf;
	     node = node->b_children[node->b_nr_active])
		;

	pos->p_node  = node;
	pos->p_index = node != NULL ? node->b_nr_active - 1 : 0;
}

/**
 *	Used to get the position of the MAX key within the subtree
 *	@param btree The btree
 *	@param subtree The subtree to be searched
 *	@return The node_pos containing the key and position of the key
 */
static void get_min_key_pos(struct m0_be_btree *btree,
			    struct m0_be_bnode *node,
			    struct node_pos    *pos)
{
	for (; node != NULL && !node->b_leaf; node = node->b_children[0])
		;

	pos->p_node  = node;
	pos->p_index = 0;
}

/**
 *	Merge nodes n1 and n2 (case 3b from Cormen)
 *	@param btree The btree
 *	@param node The parent node
 *	@param index of the child
 *	@param pos left or right
 */
static struct m0_be_bnode *merge_siblings(struct m0_be_btree *btree,
					  struct m0_be_tx    *tx,
					  struct m0_be_bnode *parent,
					  unsigned int	      index,
					  enum position_t     pos)
{
	unsigned int i;
	unsigned int j;
	struct m0_be_bnode *new_node;
	struct m0_be_bnode *n1;
	struct m0_be_bnode *n2;

	if (index == parent->b_nr_active)
		index--;

	n1 = parent->b_children[index];
	n2 = parent->b_children[index + 1];

	/* Merge the current node with the left node */
	new_node = allocate_btree_node(btree, tx);
	new_node->b_level = n1->b_level;
	new_node->b_leaf = n1->b_leaf;

	for (j = 0; j < BTREE_FAN_OUT - 1; j++) {
		new_node->b_key_vals[j] = n1->b_key_vals[j];
		new_node->b_children[j] = n1->b_children[j];
	}

	new_node->b_key_vals[BTREE_FAN_OUT - 1] = parent->b_key_vals[index];
	new_node->b_children[BTREE_FAN_OUT - 1] = n1->b_children[BTREE_FAN_OUT - 1];

	for (j = 0; j < BTREE_FAN_OUT - 1; j++) {
		new_node->b_key_vals[j + BTREE_FAN_OUT] = n2->b_key_vals[j];
		new_node->b_children[j + BTREE_FAN_OUT] = n2->b_children[j];
	}
	new_node->b_children[KV_NR] = n2->b_children[BTREE_FAN_OUT - 1];

	parent->b_children[index] = new_node;

	for (j = index; j < parent->b_nr_active; j++) {
		parent->b_key_vals[j] = parent->b_key_vals[j + 1];
		parent->b_children[j + 1] = parent->b_children[j + 2];
	}

	new_node->b_nr_active = n1->b_nr_active + n2->b_nr_active + 1;
	parent->b_nr_active--;

	for (i = parent->b_nr_active; i < KV_NR; i++) {
		parent->b_key_vals[i] = NULL;
	}

	free_btree_node(n1, btree, tx);
	free_btree_node(n2, btree, tx);

	if (parent->b_nr_active == 0 && btree->bb_root == parent) {
		free_btree_node(parent, btree, tx);
		btree->bb_root = new_node;
		new_node->b_leaf = (new_node->b_level == 0);
	} else {
		/* Update affected memory regions */
		node_update(parent, btree, tx);
	}

	return new_node;
}

/**
 * Move the key from node to another
 * @param btree The B-Tree
 * @param node The parent node
 * @param index of the key to be moved done
 * @param pos the position of the child to receive the key
 */
static void move_key(struct m0_be_btree	  *btree,
		     struct m0_be_tx	  *tx,
		     struct m0_be_bnode	  *node,
		     unsigned int	   index,
		     enum position_t	   pos)
{
	struct m0_be_bnode *lchild;
	struct m0_be_bnode *rchild;
	unsigned int i;

	if (pos == P_RIGHT) {
		index--;
	}
	lchild = node->b_children[index];
	rchild = node->b_children[index + 1];

	/*  Move the key from the parent to the left child */
	if (pos == P_LEFT) {
		lchild->b_key_vals[lchild->b_nr_active] = node->b_key_vals[index];
		lchild->b_children[lchild->b_nr_active + 1] = rchild->b_children[0];
		rchild->b_children[0] = NULL;
		lchild->b_nr_active++;

		node->b_key_vals[index] = rchild->b_key_vals[0];
		rchild->b_key_vals[0] = NULL;

		for (i = 0; i < rchild->b_nr_active - 1; i++) {
			rchild->b_key_vals[i] = rchild->b_key_vals[i + 1];
			rchild->b_children[i] = rchild->b_children[i + 1];
		}
		rchild->b_children[rchild->b_nr_active - 1] =
		    rchild->b_children[rchild->b_nr_active];
		rchild->b_nr_active--;
	} else {
		/*  Move the key from the parent to the right child */
		for (i = rchild->b_nr_active; i > 0; i--) {
			rchild->b_key_vals[i] = rchild->b_key_vals[i - 1];
			rchild->b_children[i + 1] = rchild->b_children[i];
		}
		rchild->b_children[1] = rchild->b_children[0];
		rchild->b_children[0] = NULL;

		rchild->b_key_vals[0] = node->b_key_vals[index];

		rchild->b_children[0] = lchild->b_children[lchild->b_nr_active];
		lchild->b_children[lchild->b_nr_active] = NULL;

		node->b_key_vals[index] = lchild->b_key_vals[lchild->b_nr_active - 1];
		lchild->b_key_vals[lchild->b_nr_active - 1] = NULL;

		lchild->b_nr_active--;
		rchild->b_nr_active++;
	}

	/* Update affected memory regions in tx: */
	node_update(node, btree, tx);
	node_update(lchild, btree, tx);
	node_update(rchild, btree, tx);
}

/**
 * Merge nodes n1 and n2
 * @return combined node
 */
static struct m0_be_bnode *merge_nodes(struct m0_be_btree *btree,
				   struct m0_be_tx *tx,
				   struct m0_be_bnode *n1,
				   struct bt_key_val *kv,
				   struct m0_be_bnode *n2)
{
	struct m0_be_bnode *new_node;
	unsigned int i;

	new_node = allocate_btree_node(btree, tx);
	new_node->b_leaf = true;

	for (i = 0; i < n1->b_nr_active; i++) {
		new_node->b_key_vals[i] = n1->b_key_vals[i];
		new_node->b_children[i] = n1->b_children[i];
	}
	new_node->b_children[n1->b_nr_active] = n1->b_children[n1->b_nr_active];
	new_node->b_key_vals[n1->b_nr_active] = kv;

	for (i = 0; i < n2->b_nr_active; i++) {
		new_node->b_key_vals[i + n1->b_nr_active + 1] = n2->b_key_vals[i];
		new_node->b_children[i + n1->b_nr_active + 1] = n2->b_children[i];
	}
	new_node->b_children[KV_NR] = n2->b_children[n2->b_nr_active];

	new_node->b_nr_active = n1->b_nr_active + n2->b_nr_active + 1;
	new_node->b_leaf = n1->b_leaf;
	new_node->b_level = n1->b_level;

	free_btree_node(n1, btree, tx);
	free_btree_node(n2, btree, tx);

	return new_node;
}

/**
 * Used to delete a key from the B-tree node
 * @return 0 on success -1 on error
 */
int delete_key_from_node(struct m0_be_btree	 *btree,
			 struct m0_be_tx	 *tx,
			 struct node_pos	 *node_pos)
{
	unsigned int i;
	struct bt_key_val *key_val;
	struct m0_be_bnode *node = node_pos->p_node;

	if (node->b_leaf == false)
		return -1;

	key_val = node->b_key_vals[node_pos->p_index];
	node->b_key_vals[node_pos->p_index] = NULL;

	for (i = node_pos->p_index; i < node->b_nr_active - 1; i++)
		node->b_key_vals[i] = node->b_key_vals[i + 1];

	btree_pair_release(btree, tx, key_val);

	node->b_nr_active--;

	if (node->b_nr_active == 0 && node != btree->bb_root)
		free_btree_node(node, btree, tx);
	else
		/* Update affected memory regions in tx: */
		node_update(node, btree, tx);

	return 0;
}

/**
 * Function used to delete a node from a  B-Tree
 */
static int btree_delete_key(struct m0_be_btree   *btree,
			    struct m0_be_tx      *tx,
			    struct m0_be_bnode   *subtree,
			    void                 *key)
{
	unsigned int i, index;
	struct m0_be_bnode *node = NULL, *rsibling, *lsibling;
	struct m0_be_bnode *comb_node, *parent;
	struct node_pos sub_node_pos;
	struct node_pos node_pos;
	struct bt_key_val *key_val, *new_key_val;
	void *kv = key;

	M0_PRE(btree_invariant(btree));

	node = subtree;
	parent = NULL;

del_loop:
	for (i = 0;; i = 0) {

		/* If there are no keys simply return */
		if (!node->b_nr_active)
			return -1;

		/*  Fix the index of the key greater than or equal */
		/*  to the key that we would like to search */

		while (i < node->b_nr_active &&
		       key_gt(btree, kv, node->b_key_vals[i]->key)) {
			i++;
		}
		index = i;

		/*  If we find such key break */
		if (i < node->b_nr_active && key_eq(btree, kv, node->b_key_vals[i]->key)) {
			break;
		}
		if (node->b_leaf)
			return -1;

		/* Store the parent node */
		parent = node;

		/*  To get a child node */
		node = node->b_children[i];

		/* If NULL not found */
		if (node == NULL)
			return -1;

		if (index == (parent->b_nr_active)) {
			lsibling = parent->b_children[parent->b_nr_active - 1];
			rsibling = NULL;
		} else if (index == 0) {
			lsibling = NULL;
			rsibling = parent->b_children[1];
		} else {
			lsibling = parent->b_children[i - 1];
			rsibling = parent->b_children[i + 1];
		}

		if (node->b_nr_active == BTREE_FAN_OUT - 1 && parent) {
			/*  The current node has (t - 1) keys but the */
			/*  right sibling has > (t - 1) keys */
			if (rsibling
			    && (rsibling->b_nr_active > BTREE_FAN_OUT - 1)) {
				move_key(btree, tx, parent, i, P_LEFT);
			} else
			/*  The current node has (t - 1) keys but the */
			/*  left sibling has (t - 1) keys */
			if (lsibling
				    && (lsibling->b_nr_active >
						BTREE_FAN_OUT - 1)) {
				move_key(btree, tx, parent, i, P_RIGHT);
			} else
			/*  Left sibling has (t - 1) keys */
			if (lsibling  &&
			    (lsibling->b_nr_active == BTREE_FAN_OUT - 1)) {
				node = merge_siblings(btree, tx, parent, i, P_LEFT);
			} else
			/*  Right sibling has (t - 1) keys */
			if (rsibling &&
			    (rsibling->b_nr_active ==BTREE_FAN_OUT - 1)) {
				node = merge_siblings(btree, tx, parent, i, P_RIGHT);
			}
		}
	}

	/* Case 1:
	 * The node containing the key is found and is the leaf node. */
	/* Also the leaf node has keys greater than the minimum required. */
	/* Simply remove the key */
	if (node->b_leaf && (node->b_nr_active > BTREE_FAN_OUT - 1)) {
		node_pos.p_node = node;
		node_pos.p_index = index;
		delete_key_from_node(btree, tx, &node_pos);
		mem_update(btree, tx, btree, sizeof(struct m0_be_btree));
		node_update(btree->bb_root, btree, tx);
		M0_POST(btree_invariant(btree));
		return 0;
	}
	/* If the leaf node is the root permit deletion even if the */
	/* number of keys is less than (t - 1) */
	if (node->b_leaf && (node == btree->bb_root)) {
		node_pos.p_node = node;
		node_pos.p_index = index;
		delete_key_from_node(btree, tx, &node_pos);
		mem_update(btree, tx, btree, sizeof(struct m0_be_btree));
		node_update(btree->bb_root, btree, tx);
		M0_POST(btree_invariant(btree));
		return 0;
	}

	/* Case 2:
	 * The node containing the key is found and is an internal node */
	if (node->b_leaf == false) {
		if (node->b_children[index]->b_nr_active > BTREE_FAN_OUT - 1) {
			get_max_key_pos(btree, node->b_children[index],
					&sub_node_pos);
			key_val =
			    sub_node_pos.p_node->b_key_vals[sub_node_pos.p_index];

			new_key_val =
			    (struct bt_key_val *)
				mem_alloc(btree, tx, sizeof(struct bt_key_val));
			/* @todo: analyse return code */
			M0_ASSERT(new_key_val != NULL);

			copy_key_val(btree, tx, key_val, new_key_val);
			node->b_key_vals[index] = new_key_val;

			/* Update key vals pointer, WARNING: can be XXX. */
			/*rc =*/ mem_update(btree, tx, node->b_key_vals, KV_SIZE);
			/* M0_ASSERT(rc == 0); */

			btree_delete_key(btree, tx, node->b_children[index],
					 key_val->key);
			if (sub_node_pos.p_node->b_leaf == false) {
				M0_LOG(M0_ERROR, "Not leaf");
			}
		} else if ((node->b_children[index + 1]->b_nr_active >
			    BTREE_FAN_OUT - 1)) {
			get_min_key_pos(btree, node->b_children[index + 1],
					&sub_node_pos);
			key_val =
			    sub_node_pos.p_node->b_key_vals[sub_node_pos.p_index];

			new_key_val =
			    (struct bt_key_val *)
				mem_alloc(btree, tx, sizeof(struct bt_key_val));
			M0_ASSERT(new_key_val != NULL);	/* @todo: analyse return code */

			copy_key_val(btree, tx, key_val, new_key_val);
			node->b_key_vals[index] = new_key_val;

			/* Update key vals pointer, WARNING: can be XXX. */
			mem_update(btree, tx, node->b_key_vals, KV_SIZE);

			btree_delete_key(btree, tx, node->b_children[index + 1],
					 key_val->key);
			if (sub_node_pos.p_node->b_leaf == false) {
				M0_LOG(M0_ERROR, "Not leaf");
			}

		} else if (node->b_children[index]->b_nr_active == BTREE_FAN_OUT - 1
			   && node->b_children[index + 1]->b_nr_active ==
			   BTREE_FAN_OUT - 1) {

			comb_node = merge_nodes(btree, tx, node->b_children[index],
						node->b_key_vals[index],
						node->b_children[index + 1]);
			node->b_children[index] = comb_node;

			for (i = index + 1; i < node->b_nr_active; i++) {
				node->b_children[i] = node->b_children[i + 1];
				node->b_key_vals[i - 1] = node->b_key_vals[i];
			}
			node->b_nr_active--;
			if (node->b_nr_active == 0 && btree->bb_root == node) {
				free_btree_node(node, btree, tx);
				btree->bb_root = comb_node;
			} else {
				/* XXX: crazy looking update procedure... */
				node_update(node, btree, tx);
			}

			node = comb_node;
			goto del_loop;
		}
	}
	/*  Case 3: */
	/*  In this case start from the top of the tree and continue */
	/*  moving to the leaf node making sure that each node that */
	/*  we encounter on the way has at least 't' (order of the tree) */
	/*  keys */
	if (node->b_leaf && (node->b_nr_active > BTREE_FAN_OUT - 1)) {
		node_pos.p_node = node;
		node_pos.p_index = index;
		delete_key_from_node(btree, tx, &node_pos);
	}

	/* Update affected memory regions in tx: */
	mem_update(btree, tx, btree, sizeof(struct m0_be_btree));
	node_update(btree->bb_root, btree, tx);

	M0_POST(btree_invariant(btree));
	return 0;
}

/**
 * Function used to get the node containing the given key
 * @param btree The btree to be searched
 * @param key The the key to be searched
 * @return The node and position of the key within the node
 */
struct node_pos get_btree_node(struct m0_be_btree *btree, void *key, bool slant)
{
	struct node_pos kp = { .p_node = NULL };
	struct m0_be_bnode *node;
	unsigned int i = 0;

	node = btree->bb_root;

	for (;; i = 0) {

		/*  Find the index of the key greater than or equal */
		/*  to the key that we would like to search */
		while (i < node->b_nr_active &&
		       key_gt(btree, key, node->b_key_vals[i]->key)) {
			i++;
		}

		/*  If we find such key return the key-value pair */
		if (i < node->b_nr_active &&
		    key_eq(btree, key, node->b_key_vals[i]->key)) {
			kp.p_node = node;
			kp.p_index = i;
			return kp;
		}
		/*  If the node is leaf and if we did not find the key */
		/*  return NULL */
		if (node->b_leaf) {
			if (slant && i < node->b_nr_active) {
				kp.p_node = node;
				kp.p_index = i;
			}
			return kp;
		}
		/*  To got a child node */
		node = node->b_children[i];
	}
	return kp;
}

/**
 * Used to destory btree
 * @param btree The B-tree
 */
static void btree_destroy(struct m0_be_btree *btree, struct m0_be_tx *tx)
{
	int i = 0;
	struct m0_be_bnode *head, *tail, *node;
	struct m0_be_bnode *child, *del_node;

	node = btree->bb_root;
	head = node;
	tail = node;

	head->b_next = NULL;
	while (head != NULL) {
		if (!head->b_leaf) {
			for (i = 0; i < head->b_nr_active + 1; i++) {
				child = head->b_children[i];
				tail->b_next = child;
				tail = child;
				child->b_next = NULL;
			}
		}
		del_node = head;
		head = head->b_next;
		for (i = 0; i < del_node->b_nr_active; i++)
			btree_pair_release(btree, tx, del_node->b_key_vals[i]);
		free_btree_node(del_node, btree, tx);
	}
	btree->bb_root = NULL;
}

/**
 * Function used to search a node in a B-Tree
 * @param btree The B-tree to be searched
 * @param key Key of the node to be search
 * @return The key-value pair
 */
static struct bt_key_val *btree_search(struct m0_be_btree *btree, void *key)
{

	struct bt_key_val *key_val = NULL;
	struct node_pos kp = get_btree_node(btree, key, false);

	if (kp.p_node) {
		key_val = kp.p_node->b_key_vals[kp.p_index];
	}
	return key_val;
}

/**
 * Used to copy key value from source to destination
 * @param src The source key value
 * @param dst The dest key value
 */
static void copy_key_val(struct m0_be_btree *btree,
			 struct m0_be_tx    *tx,
			 struct bt_key_val  *src,
			 struct bt_key_val  *dst)
{
	unsigned int keysize;
	unsigned int datasize;

	M0_ENTRY();

	keysize = btree->bb_ops->ko_ksize(src->key);
	dst->key = (void *)mem_alloc(btree, tx, keysize);
	M0_ASSERT(dst->key != NULL);	/* @todo: analyse return code */

	bcopy(src->key, dst->key, keysize);

	if (src->val) {
		datasize = btree->bb_ops->ko_vsize(src->val);
		dst->val = (void *)mem_alloc(btree, tx, datasize);
		M0_ASSERT(dst->val != NULL);	/* @todo: analyse return code */
		bcopy(src->val, dst->val, datasize);
	}

	M0_LEAVE();
}

/**
 * Get the max key in the btree
 * @param btree The btree
 * @return The max key
 */
static void *btree_get_max_key(struct m0_be_btree *btree)
{
	struct node_pos node_pos;

	get_max_key_pos(btree, btree->bb_root, &node_pos);
	return node_pos.p_node->b_key_vals[node_pos.p_index]->key;
}

/**
 * Get the min key in the btree
 * @param btree The btree
 * @return The max key
 */
static void *btree_get_min_key(struct m0_be_btree *btree)
{
	struct node_pos node_pos;

	get_min_key_pos(btree, btree->bb_root, &node_pos);
	return node_pos.p_node->b_key_vals[node_pos.p_index]->key;
}

static void
btree_pair_release(struct m0_be_btree *btree, struct m0_be_tx *tx,
				struct bt_key_val *kv)
{
	if (kv->key) {
		mem_free(btree, tx, kv->key, btree->bb_ops->ko_ksize(kv->key));
		kv->key = NULL;
	}
	if (kv->val) {
		mem_free(btree, tx, kv->val, btree->bb_ops->ko_vsize(kv->val));
		kv->val = NULL;
	}
	mem_free(btree, tx, kv, sizeof(struct bt_key_val));
}

M0_UNUSED static struct bt_key_val *btree_pair_setup(struct m0_be_btree *btree,
						     struct m0_be_tx    *tx,
						     void *key, size_t key_size,
						     void *val, size_t val_size)
{
	struct bt_key_val *kv;

	M0_PRE(val_size == btree->bb_ops->ko_vsize(val));
	M0_PRE(key_size == btree->bb_ops->ko_ksize(key));

	/* XXX: ENOMEM has to be checked */

	kv = (struct bt_key_val *)mem_alloc(btree, tx, sizeof(struct bt_key_val));
	M0_ASSERT(kv != NULL);

	kv->key = mem_alloc(btree, tx, key_size);
	M0_ASSERT(kv != NULL);

	kv->val = mem_alloc(btree, tx, val_size);
	M0_ASSERT(kv != NULL);

	bcopy(key, kv->key, key_size);
	bcopy(val, kv->val, val_size);

	return kv;
}


/* ------------------------------------------------------------------
 * Btree external interfaces implementation
 * ------------------------------------------------------------------ */

#define BTREE_OP_FILL(op, tree, tx, optype, anchor) ({	\
	(op)->bo_u.u_btree.t_anchor = (anchor);		\
	(op)->bo_u.u_btree.t_tree   = (tree);		\
	(op)->bo_u.u_btree.t_in     = NULL;		\
	(op)->bo_u.u_btree.t_op     = optype;		\
	(op)->bo_u.u_btree.t_tx     = (tx);		\
	(op)->bo_utype              = M0_BOP_TREE;	\
	})


M0_INTERNAL void m0_be_btree_init(struct m0_be_btree *tree,
				  struct m0_be_seg   *seg,
				  const struct m0_be_btree_kv_ops *ops)
{
	M0_PRE(ops != NULL);

	m0_rwlock_init(&tree->bb_lock);
	tree->bb_ops  = ops;
	tree->bb_seg  = seg;

	if (!m0_be_seg_contains(seg, tree->bb_root))
		tree->bb_root = NULL;
}

M0_INTERNAL void m0_be_btree_fini(struct m0_be_btree *tree)
{
	m0_rwlock_fini(&tree->bb_lock);
}

M0_INTERNAL void m0_be_btree_create(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op)
{
	M0_PRE(tree->bb_root == NULL && tree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);
	/* M0_PRE(m0_rwlock_is_locked(tx->t_be.b_tx.te_lock)); */

	BTREE_OP_FILL(op, tree, tx, M0_BBO_CREATE, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_write_lock(&tree->bb_lock);

	btree_create(tree, tx);

	m0_rwlock_write_unlock(&tree->bb_lock);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_btree_destroy(struct m0_be_btree *tree,
				     struct m0_be_tx *tx,
				     struct m0_be_op *op)
{
	M0_PRE(tree->bb_root != NULL && tree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	BTREE_OP_FILL(op, tree, tx, M0_BBO_DESTROY, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_write_lock(&tree->bb_lock);

	btree_destroy(tree, tx);

	m0_rwlock_write_unlock(&tree->bb_lock);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

static void btree_node_alloc_credit(const struct m0_be_btree     *tree,
					  struct m0_be_tx_credit *accum)
{
	struct m0_be_allocator *a = &tree->bb_seg->bs_allocator;

	m0_be_allocator_credit(a, M0_BAO_ALLOC, sizeof(struct m0_be_bnode),
			       BTREE_ALLOC_SHIFT, accum);
	m0_be_allocator_credit(a, M0_BAO_ALLOC, CHILDREN_SIZE,
			       BTREE_ALLOC_SHIFT, accum);
	m0_be_allocator_credit(a, M0_BAO_ALLOC, KV_SIZE,
			       BTREE_ALLOC_SHIFT, accum);

}

static void btree_node_free_credit(const struct m0_be_btree     *tree,
					 struct m0_be_tx_credit *accum)
{
	struct m0_be_allocator *a = &tree->bb_seg->bs_allocator;

	m0_be_allocator_credit(a, M0_BAO_FREE, sizeof(struct m0_be_bnode),
			       BTREE_ALLOC_SHIFT, accum);
	m0_be_allocator_credit(a, M0_BAO_FREE, CHILDREN_SIZE,
			       BTREE_ALLOC_SHIFT, accum);
	m0_be_allocator_credit(a, M0_BAO_FREE, KV_SIZE,
			       BTREE_ALLOC_SHIFT, accum);

}

static void btree_node_update_credit(struct m0_be_tx_credit *accum,
					m0_bcount_t nr)
{
	struct m0_be_tx_credit  kv_update_cred = M0_BE_TX_CREDIT(1, KV_SIZE);
	struct m0_be_tx_credit  children_update_cred =
				M0_BE_TX_CREDIT(1, CHILDREN_SIZE);
	struct m0_be_tx_credit  struct_node_update_cred =
				M0_BE_TX_CREDIT_TYPE(struct m0_be_bnode);
	struct m0_be_tx_credit  sum_cred = M0_BE_TX_CREDIT_ZERO;

	m0_be_tx_credit_add(&sum_cred, &kv_update_cred);
	m0_be_tx_credit_add(&sum_cred, &children_update_cred);
	m0_be_tx_credit_add(&sum_cred, &struct_node_update_cred);

	m0_be_tx_credit_mac(accum, &sum_cred, nr);
}

/* XXX */
static void btree_credit(const struct m0_be_btree     *tree,
			       struct m0_be_tx_credit *accum)
{
	uint32_t height;

	height = tree->bb_root == NULL ? 2 : tree->bb_root->b_level;
	m0_be_tx_credit_mul(accum, 2*height + 1);
}

static void btree_rebalance_credit(const struct m0_be_btree     *tree,
					 struct m0_be_tx_credit *accum)
{
	struct m0_be_tx_credit  node_cred = M0_BE_TX_CREDIT_ZERO;

	btree_node_alloc_credit(tree, &node_cred);
	btree_node_update_credit(&node_cred, 1);
	btree_credit(tree, &node_cred);
	m0_be_tx_credit_add(accum, &node_cred);
}

static void kv_insert_credit(const struct m0_be_btree     *tree,
				   m0_bcount_t             ksize,
				   m0_bcount_t             vsize,
				   struct m0_be_tx_credit *accum)
{
	struct m0_be_allocator *a = &tree->bb_seg->bs_allocator;
	struct m0_be_tx_credit  kv_update_cred =
		M0_BE_TX_CREDIT(1, ksize + vsize + sizeof(struct bt_key_val));

	m0_be_allocator_credit(a, M0_BAO_ALLOC, sizeof(struct bt_key_val),
				BTREE_ALLOC_SHIFT, accum);
	m0_be_allocator_credit(a, M0_BAO_ALLOC, ksize,
				BTREE_ALLOC_SHIFT, accum);
	m0_be_allocator_credit(a, M0_BAO_ALLOC, vsize,
				BTREE_ALLOC_SHIFT, accum);
	m0_be_tx_credit_add(accum, &kv_update_cred);
}

static void kv_delete_credit(const struct m0_be_btree     *tree,
				   m0_bcount_t             ksize,
				   m0_bcount_t             vsize,
				   struct m0_be_tx_credit *accum)
{
	struct m0_be_allocator *a = &tree->bb_seg->bs_allocator;

	m0_be_allocator_credit(a, M0_BAO_FREE, ksize, BTREE_ALLOC_SHIFT, accum);
	m0_be_allocator_credit(a, M0_BAO_FREE, vsize, BTREE_ALLOC_SHIFT, accum);
	m0_be_allocator_credit(a, M0_BAO_FREE, sizeof(struct bt_key_val),
						BTREE_ALLOC_SHIFT, accum);
}

M0_INTERNAL void m0_be_btree_insert_credit(const struct m0_be_btree     *tree,
						 m0_bcount_t             nr,
						 m0_bcount_t             ksize,
						 m0_bcount_t             vsize,
						 struct m0_be_tx_credit *accum)
{
	uint32_t		height;
	struct m0_be_tx_credit	cred = M0_BE_TX_CREDIT_ZERO;

	height = tree->bb_root == NULL ? 2 : tree->bb_root->b_level;

	btree_node_alloc_credit(tree, &cred);
	btree_node_update_credit(&cred, 3); /* see btree_split_child() */
	m0_be_tx_credit_mul(&cred, height + 1);

	kv_insert_credit(tree, ksize, vsize, &cred);
	m0_be_tx_credit_mac(accum, &cred, nr);
}

M0_INTERNAL void m0_be_btree_delete_credit(const struct m0_be_btree     *tree,
						 m0_bcount_t             nr,
						 m0_bcount_t             ksize,
						 m0_bcount_t             vsize,
						 struct m0_be_tx_credit *accum)
{
	struct m0_be_tx_credit cred = M0_BE_TX_CREDIT_ZERO;

	kv_delete_credit(tree, ksize, vsize, &cred);
	btree_node_update_credit(&cred, 1);
	btree_node_free_credit(tree, &cred);
	btree_rebalance_credit(tree, &cred);
	m0_be_tx_credit_mac(accum, &cred, nr);
}

M0_INTERNAL void m0_be_btree_update_credit(const struct m0_be_btree     *tree,
						 m0_bcount_t             nr,
						 m0_bcount_t             vsize,
						 struct m0_be_tx_credit *accum)
{
	struct m0_be_tx_credit  cred = M0_BE_TX_CREDIT_ZERO;
	struct m0_be_allocator *a = &tree->bb_seg->bs_allocator;
	struct m0_be_tx_credit  val_update_cred =
		M0_BE_TX_CREDIT(1, vsize + sizeof(struct bt_key_val));

	m0_be_allocator_credit(a, M0_BAO_FREE, vsize, BTREE_ALLOC_SHIFT, &cred);
	m0_be_allocator_credit(a, M0_BAO_ALLOC,vsize, BTREE_ALLOC_SHIFT, &cred);
	m0_be_tx_credit_add(&cred, &val_update_cred);
	m0_be_tx_credit_mac(accum, &cred, nr);
}

M0_INTERNAL void m0_be_btree_create_credit(const struct m0_be_btree     *tree,
						 m0_bcount_t             nr,
						 struct m0_be_tx_credit *accum)
{
	struct m0_be_tx_credit  cred = M0_BE_TX_CREDIT_ZERO;

	btree_node_alloc_credit(tree, &cred);
	btree_node_update_credit(&cred, 1);
	m0_be_tx_credit_mac(accum, &cred, nr);
}

static int btree_count_items(struct m0_be_btree *tree, m0_bcount_t *ksize,
						       m0_bcount_t *vsize)
{
	struct m0_be_btree_cursor cursor;
	struct m0_buf		  start;
	int                       count = 0;
	struct m0_buf		  key;
	struct m0_buf		  val;
	int                       rc;

	*ksize = 0;
	*vsize = 0;
	if (tree->bb_root != NULL) {
		m0_be_btree_cursor_init(&cursor, tree);

		m0_be_op_init(&cursor.bc_op);
		m0_be_btree_minkey(tree, &cursor.bc_op, &start);
		m0_be_op_fini(&cursor.bc_op);

		m0_be_op_init(&cursor.bc_op);
		m0_be_btree_cursor_get(&cursor, &start, true);
		M0_ASSERT(m0_be_op_state(&cursor.bc_op) == M0_BOS_SUCCESS);
		rc = cursor.bc_op.bo_u.u_btree.t_rc;
		m0_be_op_fini(&cursor.bc_op);

		while (rc != -ENOENT) {
			m0_be_btree_cursor_kv_get(&cursor, &key, &val);
			if (key.b_nob > *ksize) *ksize = key.b_nob;
			if (val.b_nob > *vsize) *vsize = val.b_nob;
			m0_be_op_init(&cursor.bc_op);
			m0_be_btree_cursor_next(&cursor);
			M0_ASSERT(m0_be_op_state(&cursor.bc_op) ==
							M0_BOS_SUCCESS);
			rc = cursor.bc_op.bo_u.u_btree.t_rc;
			m0_be_op_fini(&cursor.bc_op);
			++count;
		}

		m0_be_btree_cursor_fini(&cursor);
	}

	return count;
}

M0_INTERNAL void m0_be_btree_destroy_credit(struct m0_be_btree     *tree,
					    m0_bcount_t             nr,
					    struct m0_be_tx_credit *accum)
{
	struct m0_be_tx_credit    cred = M0_BE_TX_CREDIT_ZERO;
	int                       count = 1;
	m0_bcount_t               ksize;
	m0_bcount_t               vsize;

	count += btree_count_items(tree, &ksize, &vsize);
	kv_delete_credit(tree, ksize, vsize, &cred);
	btree_node_free_credit(tree, &cred);
	m0_be_tx_credit_mac(accum, &cred, count * nr);
}

M0_INTERNAL void m0_be_btree_insert(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    const struct m0_buf *key,
				    const struct m0_buf *val)
{
	void			*key_data;
	void			*val_data;
	struct bt_key_val	*kv; /* XXX: update credit accounting */

	M0_PRE(tree->bb_root != NULL && tree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);
	M0_PRE(key->b_nob == tree->bb_ops->ko_ksize(key->b_addr));
	M0_PRE(val->b_nob == tree->bb_ops->ko_vsize(val->b_addr));

	BTREE_OP_FILL(op, tree, tx, M0_BBO_INSERT, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_write_lock(&tree->bb_lock);

	key_data = mem_alloc(tree, tx, key->b_nob);
	val_data = mem_alloc(tree, tx, val->b_nob);
	kv = mem_alloc(tree, tx, sizeof(struct bt_key_val));
	memcpy(key_data, key->b_addr, key->b_nob);
	memcpy(val_data, val->b_addr, val->b_nob);
	kv->key = key_data;
	kv->val = val_data;
	mem_update(tree, tx, kv, sizeof(struct bt_key_val));
	mem_update(tree, tx, key_data, key->b_nob);
	mem_update(tree, tx, val_data, val->b_nob);

	btree_insert_key(tree, tx, kv);

	m0_rwlock_write_unlock(&tree->bb_lock);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_btree_update(struct m0_be_btree *btree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    const struct m0_buf *key,
				    const struct m0_buf *val)
{
	struct bt_key_val *kv;

	M0_PRE(btree->bb_root != NULL && btree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);
	M0_PRE(key->b_nob == btree->bb_ops->ko_ksize(key->b_addr));
	M0_PRE(val->b_nob == btree->bb_ops->ko_vsize(val->b_addr));

	BTREE_OP_FILL(op, btree, tx, M0_BBO_UPDATE, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_write_lock(&btree->bb_lock);

	kv = btree_search(btree, key->b_addr);
	if (kv != NULL) {
		if (val->b_nob > btree->bb_ops->ko_vsize(kv->val)) {
			mem_free(btree, tx, kv->val,
					btree->bb_ops->ko_vsize(kv->val));
			kv->val = mem_alloc(btree, tx, val->b_nob);
			mem_update(btree, tx, kv, sizeof(struct bt_key_val));
		}
		memcpy(kv->val, val->b_addr, val->b_nob);
		mem_update(btree, tx, kv->val, val->b_nob);
	} else
		op->bo_u.u_btree.t_rc = -ENOENT;

	m0_rwlock_write_unlock(&btree->bb_lock);
	m0_be_op_state_set(op, kv != NULL ? M0_BOS_SUCCESS : M0_BOS_FAILURE);
}

M0_INTERNAL void m0_be_btree_delete(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    const struct m0_buf *key)
{
	int rc;

	M0_PRE(tree->bb_root != NULL && tree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	BTREE_OP_FILL(op, tree, tx, M0_BBO_DELETE, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_write_lock(&tree->bb_lock);

	rc = btree_delete_key(tree, tx, tree->bb_root, key->b_addr);
	if (rc != 0)
		op->bo_u.u_btree.t_rc = -ENOENT;

	m0_rwlock_write_unlock(&tree->bb_lock);
	m0_be_op_state_set(op, rc == 0 ? M0_BOS_SUCCESS : M0_BOS_FAILURE);
}

M0_INTERNAL void m0_be_btree_lookup(struct m0_be_btree *btree,
				    struct m0_be_op *op,
				    const struct m0_buf *key,
				    struct m0_buf *dest_value)
{
	struct bt_key_val	*kv;
	m0_bcount_t		 vsize;

	M0_PRE(btree->bb_root != NULL && btree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	BTREE_OP_FILL(op, btree, NULL, M0_BBO_LOOKUP, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_read_lock(&btree->bb_lock);

	kv = btree_search(btree, key->b_addr);
	if (kv != NULL) {
		vsize = btree->bb_ops->ko_vsize(kv->val);
		if (vsize < dest_value->b_nob)
			dest_value->b_nob = vsize;
		memcpy(dest_value->b_addr, kv->val, dest_value->b_nob);
	} else
		op->bo_u.u_btree.t_rc = -ENOENT;

	m0_rwlock_read_unlock(&btree->bb_lock);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_btree_maxkey(struct m0_be_btree *btree,
				    struct m0_be_op *op,
				    struct m0_buf *out)
{
	void *key;

	M0_PRE(btree->bb_root != NULL && btree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	BTREE_OP_FILL(op, btree, NULL, M0_BBO_MAXKEY, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_read_lock(&btree->bb_lock);

	key = btree_get_max_key(btree);
	out->b_addr = key;
	out->b_nob = btree->bb_ops->ko_vsize(key);

	m0_rwlock_read_unlock(&btree->bb_lock);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_btree_minkey(struct m0_be_btree *btree,
				    struct m0_be_op *op,
				    struct m0_buf *out)
{
	void *key;

	M0_PRE(btree->bb_root != NULL && btree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	BTREE_OP_FILL(op, btree, NULL, M0_BBO_MINKEY, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_read_lock(&btree->bb_lock);

	key = btree_get_min_key(btree);
	out->b_addr = key;
	out->b_nob = btree->bb_ops->ko_vsize(key);

	m0_rwlock_read_unlock(&btree->bb_lock);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}


/* ------------------------------------------------------------------
 * Btree external inplace interfaces implementation
 * ------------------------------------------------------------------ */

M0_INTERNAL void m0_be_btree_update_inplace(struct m0_be_btree        *btree,
					    struct m0_be_tx           *tx,
					    struct m0_be_op           *op,
					    const struct m0_buf       *key,
					    struct m0_be_btree_anchor *anchor)
{
	struct bt_key_val *kv;

	M0_PRE(btree->bb_root != NULL && btree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);
	M0_PRE(key->b_nob == btree->bb_ops->ko_ksize(key->b_addr));

	BTREE_OP_FILL(op, btree, tx, M0_BBO_UPDATE, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_write_lock(&btree->bb_lock);

	anchor->ba_write = true;
	kv = btree_search(btree, key->b_addr);
	if (kv != NULL) {
		if (anchor->ba_value.b_nob > btree->bb_ops->ko_vsize(kv->val)) {
			mem_free(btree, tx, kv->val,
					btree->bb_ops->ko_vsize(kv->val));
			kv->val = mem_alloc(btree, tx, anchor->ba_value.b_nob);
			mem_update(btree, tx, kv, sizeof(struct bt_key_val));
		}
		anchor->ba_value.b_addr = kv->val;
	} else
		op->bo_u.u_btree.t_rc = -ENOENT;

	m0_be_op_state_set(op, kv != NULL ? M0_BOS_SUCCESS : M0_BOS_FAILURE);
}

M0_INTERNAL void m0_be_btree_insert_inplace(struct m0_be_btree        *tree,
					    struct m0_be_tx           *tx,
					    struct m0_be_op           *op,
					    const struct m0_buf       *key,
					    struct m0_be_btree_anchor *anchor)
{
	void			*key_data;
	void			*val_data;
	struct bt_key_val	*kv; /* XXX: update credit accounting */

	M0_PRE(tree->bb_root != NULL && tree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);
	M0_PRE(key->b_nob == tree->bb_ops->ko_ksize(key->b_addr));

	BTREE_OP_FILL(op, tree, tx, M0_BBO_INSERT, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_write_lock(&tree->bb_lock);

	key_data = mem_alloc(tree, tx, key->b_nob);
	val_data = mem_alloc(tree, tx, anchor->ba_value.b_nob);
	kv = mem_alloc(tree, tx, sizeof(struct bt_key_val));
	kv->key = key_data;
	kv->val = val_data;
	memcpy(key_data, key->b_addr, key->b_nob);
	mem_update(tree, tx, kv, sizeof(struct bt_key_val));
	mem_update(tree, tx, key_data, key->b_nob);
	anchor->ba_value.b_addr = val_data;

	btree_insert_key(tree, tx, kv);

	anchor->ba_write = true;

	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_btree_lookup_inplace(struct m0_be_btree        *btree,
					    struct m0_be_op           *op,
					    const struct m0_buf       *key,
					    struct m0_be_btree_anchor *anchor)
{
	struct bt_key_val *kv;

	M0_PRE(btree->bb_root != NULL && btree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	BTREE_OP_FILL(op, btree, NULL, M0_BBO_INSERT, anchor);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_read_lock(&btree->bb_lock);

	anchor->ba_write = false;
	kv = btree_search(btree, key->b_addr);
	if (kv != NULL) {
		anchor->ba_value.b_addr = kv->val;
		anchor->ba_value.b_nob = btree->bb_ops->ko_vsize(kv->val);
	} else
		op->bo_u.u_btree.t_rc = -ENOENT;

	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_btree_release(struct m0_be_btree              *btree,
				     struct m0_be_tx                 *tx,
				     struct m0_be_op                 *op,
				     const struct m0_be_btree_anchor *anchor)
{
	M0_PRE(btree->bb_root != NULL && btree->bb_ops != NULL);
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);
	M0_PRE(ergo(anchor->ba_write, tx != NULL));
	m0_be_op_state_set(op, M0_BOS_ACTIVE);

	if (anchor->ba_write) {
		mem_update(btree, tx, anchor->ba_value.b_addr,
				      anchor->ba_value.b_nob);
		m0_rwlock_write_unlock(&btree->bb_lock);
	} else
		m0_rwlock_read_unlock(&btree->bb_lock);

	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}


/* ------------------------------------------------------------------
 * Btree cursor interfaces implementation
 * ------------------------------------------------------------------ */

static void print_single_node(struct m0_be_bnode *node)
{
	int i;

	M0_LOG(M0_DEBUG, "{");
	for (i = 0; i < node->b_nr_active; ++i) {
		void *key = node->b_key_vals[i]->key;
		void *val = node->b_key_vals[i]->val;

		M0_LOG(M0_DEBUG, "key: %s, val: %s, level: %d",
		       (char *)key, (char *)val, node->b_level);
	}
	M0_LOG(M0_DEBUG, "} (%p,%d) ", node, !!node->b_leaf);
}

static void iter_prepare(struct m0_be_bnode *node, bool print)
{

	int i = 0;
	unsigned int current_level;

	struct m0_be_bnode *head, *tail;
	struct m0_be_bnode *child;

	if (print)
		M0_LOG(M0_DEBUG, "---8<---8<---8<---8<---8<---8<---");

	if (node == NULL)
		goto out;

	current_level = node->b_level;
	head = node;
	tail = node;

	head->b_next = NULL;
	while (head != NULL) {
		if (head->b_level < current_level) {
			current_level = head->b_level;
			if (print)
				M0_LOG(M0_DEBUG, "***");
		}
		if (print)
			print_single_node(head);

		if (!head->b_leaf) {
			for (i = 0; i < head->b_nr_active + 1; i++) {
				child = head->b_children[i];
				tail->b_next = child;
				tail = child;
				child->b_next = NULL;
			}
		}
		head = head->b_next;
	}
out:
	if (print)
		M0_LOG(M0_DEBUG, "---8<---8<---8<---8<---8<---8<---");
}

/* XXX: needs revision */
M0_INTERNAL void m0_be_btree_cursor_init(struct m0_be_btree_cursor *cur,
					 struct m0_be_btree *btree)
{
	iter_prepare(btree->bb_root, false);
	cur->bc_tree = btree;

	cur->bc_node = btree->bb_root;
	while (cur->bc_node->b_next != NULL) {
		cur->bc_node->b_next->b_prev = cur->bc_node;
		cur->bc_node = cur->bc_node->b_next;
	}

	cur->bc_node->b_next = btree->bb_root;
	btree->bb_root->b_prev = cur->bc_node;
}

M0_INTERNAL void m0_be_btree_cursor_fini(struct m0_be_btree_cursor *cursor)
{
}

M0_INTERNAL void m0_be_btree_cursor_get(struct m0_be_btree_cursor *cur,
					const struct m0_buf *key, bool slant)
{
	struct node_pos    last;
	struct bt_key_val  *kv;
	struct m0_be_op    *op = &cur->bc_op;
	struct m0_be_btree *tree = cur->bc_tree;

	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	BTREE_OP_FILL(op, tree, NULL, M0_BBO_CURSOR_GET, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_read_lock(&tree->bb_lock);

	/* cursor move */
	last = get_btree_node(cur->bc_tree, key->b_addr, slant);
	cur->bc_last_node = last.p_node;
	cur->bc_last_pos = last.p_index;

	if (cur->bc_last_node == NULL) {
		M0_SET0(&op->bo_u.u_btree.t_out);
		M0_SET0(&op->bo_u.u_btree.t_out2);
		op->bo_u.u_btree.t_rc = -ENOENT;
		goto out;
	}

	cur->bc_pos = cur->bc_last_pos;
	cur->bc_node = cur->bc_last_node;

	kv = cur->bc_node->b_key_vals[cur->bc_pos];
	/* cursor end move */

	op->bo_u.u_btree.t_out.b_addr = kv->val;
	op->bo_u.u_btree.t_out.b_nob = cur->bc_tree->bb_ops->ko_vsize(kv->val);
	op->bo_u.u_btree.t_out2.b_addr = kv->key;
	op->bo_u.u_btree.t_out2.b_nob = cur->bc_tree->bb_ops->ko_ksize(kv->key);

out:
	m0_rwlock_read_unlock(&tree->bb_lock);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_btree_cursor_next(struct m0_be_btree_cursor *cur)
{
	struct bt_key_val  *kv;
	struct m0_be_op    *op   = &cur->bc_op;
	struct m0_be_btree *tree = cur->bc_tree;

	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	BTREE_OP_FILL(op, tree, NULL, M0_BBO_CURSOR_NEXT, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_read_lock(&tree->bb_lock);


	/* cursor move */
	++cur->bc_pos;

	if (cur->bc_pos >= cur->bc_node->b_nr_active) {
		cur->bc_pos = 0;
		cur->bc_node = cur->bc_node->b_next;
	}

	if (cur->bc_pos  == cur->bc_last_pos &&
	    cur->bc_node == cur->bc_last_node) {
		M0_SET0(&op->bo_u.u_btree.t_out);
		M0_SET0(&op->bo_u.u_btree.t_out2);
		op->bo_u.u_btree.t_rc = -ENOENT;
		goto out;
	}
	/* cursor end move */

	kv = cur->bc_node->b_key_vals[cur->bc_pos];
	op->bo_u.u_btree.t_out.b_addr = kv->val;
	op->bo_u.u_btree.t_out.b_nob = cur->bc_tree->bb_ops->ko_vsize(kv->val);
	op->bo_u.u_btree.t_out2.b_addr = kv->key;
	op->bo_u.u_btree.t_out2.b_nob = cur->bc_tree->bb_ops->ko_ksize(kv->key);

out:
	m0_rwlock_read_unlock(&tree->bb_lock);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_btree_cursor_prev(struct m0_be_btree_cursor *cur)
{
	struct bt_key_val  *kv;
	struct m0_be_op    *op   = &cur->bc_op;
	struct m0_be_btree *tree = cur->bc_tree;

	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	BTREE_OP_FILL(op, tree, NULL, M0_BBO_CURSOR_PREV, NULL);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_rwlock_read_lock(&tree->bb_lock);

	if (cur->bc_pos == 0) {
		cur->bc_node = cur->bc_node->b_prev;
		cur->bc_pos = cur->bc_node->b_nr_active - 1;
	} else
		--cur->bc_pos;

	if (cur->bc_pos == cur->bc_last_pos &&
	    cur->bc_node == cur->bc_last_node) {
		op->bo_u.u_btree.t_rc = -ENOENT;
		goto out;
	}

	kv = cur->bc_node->b_key_vals[cur->bc_pos];
	op->bo_u.u_btree.t_out.b_addr = kv->val;
	op->bo_u.u_btree.t_out.b_nob = cur->bc_tree->bb_ops->ko_vsize(kv->val);
	op->bo_u.u_btree.t_out2.b_addr = kv->key;
	op->bo_u.u_btree.t_out2.b_nob = cur->bc_tree->bb_ops->ko_ksize(kv->key);
out:
	m0_rwlock_read_unlock(&tree->bb_lock);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}


M0_INTERNAL void m0_be_btree_cursor_put(struct m0_be_btree_cursor *cursor)
{
}

M0_INTERNAL void m0_be_btree_cursor_kv_get(struct m0_be_btree_cursor *cur,
					   struct m0_buf *key,
					   struct m0_buf *val)
{
	M0_PRE(m0_be_op_state(&cur->bc_op) == M0_BOS_SUCCESS);

	*val = cur->bc_op.bo_u.u_btree.t_out;
	*key = cur->bc_op.bo_u.u_btree.t_out2;
}

M0_INTERNAL void btree_dbg_print(struct m0_be_btree *tree)
{
	iter_prepare(tree->bb_root, true);
}



#undef M0_TRACE_SUBSYSTEM
/** @} end of be group */

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
