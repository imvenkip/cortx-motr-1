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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 10/15/2011
 */

#include <stdio.h>

#include "lib/rbtree.h"
#include "lib/assert.h"

// TODO proper C2_PRE, C2_POST, C2_ASSERT
// TODO check all comparator calls
// FIXME fix return xxx ? 0 : 1;
// FIXME long strings (> 80)
// TODO insert 'const'
// TODO add comments

#define LEAF ((struct c2_rbtree_link *)4)
// parent of root node
#define ROOT_PARENT ((struct c2_rbtree_link *)8)

static void print_dot(int depth)
{
	int i;

	for (i = 0; i < depth; ++i)
		printf(".");
}

static inline void *key_of(const struct c2_rbtree *t, const struct c2_rbtree_link *l)
{
	return (char *) l + t->r_key_offset;
}

static inline bool node_is_valid(const struct c2_rbtree_link *node)
{
	return node != NULL && node != LEAF && node != ROOT_PARENT;
}

// return 0 if child == parent->left and return 1 otherwise
static inline int index_of(const struct c2_rbtree_link *child, const struct c2_rbtree_link *parent)
{
	C2_ASSERT(node_is_valid(parent));
	C2_ASSERT(child == parent->rl_left || child == parent->rl_right);
	return child == parent->rl_left ? 0 : 1;
}

// return n->rl_left if index == 0 and n->rl_right otherwise
static inline struct c2_rbtree_link *child(const struct c2_rbtree_link *n, int index)
{
	return index == 0 ? n->rl_left : n->rl_right;
}

static void rbtree_print_node(const struct c2_rbtree *t, const struct c2_rbtree_link *l, int depth, char *pos)
{
	if (l == LEAF)
		return;
	rbtree_print_node(t, l->rl_left, depth + 1, "left");
	print_dot(depth);
	printf("%s %c %d\n", pos, l->rl_color == COLOR_BLACK ? 'B' : 'R', *(int *) key_of(t, l));
	rbtree_print_node(t, l->rl_right, depth + 1, "right");
}

void rbtree_print(const struct c2_rbtree *t)
{
	printf("tree BEGIN\n");
	rbtree_print_node(t, t->r_root, 0, "root");
	printf("tree END\n");
}

static inline enum c2_rbtree_color node_color(const struct c2_rbtree_link *node)
{
	C2_ASSERT(node != NULL && node != ROOT_PARENT);

	return node == LEAF ? COLOR_BLACK : node->rl_color;
}

static inline bool node_is_red(const struct c2_rbtree_link *node)
{
	return node_color(node) == COLOR_RED;
}

static inline bool node_is_black(const struct c2_rbtree_link *node)
{
	return node_color(node) == COLOR_BLACK;
}

static void node_repaint(struct c2_rbtree_link *node, enum c2_rbtree_color color)
{
	if (node_is_valid(node))
		node->rl_color = color;
}

// minimum value in binary search tree
static struct c2_rbtree_link *bst_min(struct c2_rbtree_link *node)
{
	C2_ASSERT(node != NULL && node != ROOT_PARENT);

	if (node == LEAF)
		return NULL;
	while (node->rl_left != LEAF)
		node = node->rl_left;
	return node;
}

struct c2_rbtree_link *bst_next(struct c2_rbtree_link *node)
{
	C2_ASSERT(node_is_valid(node));

	if (node->rl_right != LEAF)
		return bst_min(node->rl_right);
	while (node->rl_parent != ROOT_PARENT && node->rl_parent->rl_left != node)
		node = node->rl_parent;
	node = node->rl_parent;
	return node == ROOT_PARENT ? NULL : node;
}

static int bst_cmp(const struct c2_rbtree *t, struct c2_rbtree_link *a, struct c2_rbtree_link *b)
{
	C2_ASSERT(t != NULL);
	C2_ASSERT(node_is_valid(a) && node_is_valid(b));

	return t->r_cmp(key_of(t, a), key_of(t, b));
}


static struct c2_rbtree_link *bst_find(const struct c2_rbtree *t, void *key)
{
	struct  c2_rbtree_link *node;
	int rc;

	C2_PRE(t != NULL);
	C2_PRE(key != NULL);

	for (node = t->r_root; node != LEAF; ) {
		rc = t->r_cmp(key_of(t, node), key);
		if (rc == 0)
			return node;
		node = child(node, rc < 0);
	}
	return NULL;
}

// set child as left parent if index == 0 and as right parent otherwise
void node_relation_set(struct c2_rbtree_link *parent, struct c2_rbtree_link *child, int index)
{
	if (child != LEAF)
		child->rl_parent = parent;
	if (parent != ROOT_PARENT) {
		if (index == 0)
			parent->rl_left = child;
		else
			parent->rl_right = child;
	}
}

// return NULL if node with given key already exists in the tree
// return node otherwise
static struct c2_rbtree_link *bst_insert(struct c2_rbtree *t, struct c2_rbtree_link *node)
{
	struct c2_rbtree_link *new_parent;
	struct c2_rbtree_link *new_pos;
	int rc;

	C2_ASSERT(t != NULL);
	C2_ASSERT(node != NULL);

	// if tree is empty - insert as new root
	if (t->r_root == LEAF) {
		t->r_root = node;
		node->rl_parent = ROOT_PARENT;
		return node;
	}

	for (new_pos = t->r_root; new_pos!= LEAF; ) {
		rc = bst_cmp(t, node, new_pos);
		if (rc == 0)
			return NULL;
		new_parent = new_pos;
		new_pos = child(new_pos, rc > 0);
	}
	
	// set up parent-child relationship
	node->rl_parent = new_parent;
	node_relation_set(new_parent, node, rc > 0);

	return node;
}

static int node_black_depth(const struct c2_rbtree_link *node)
{
	int depth = 0;

	C2_ASSERT(node != LEAF && node != NULL);

	for ( ; node != ROOT_PARENT; node = node->rl_parent)
		if (node_is_black(node))
			depth++;
	return depth;
}

static struct c2_rbtree_link *node_sibling(const struct c2_rbtree_link *node)
{
	struct c2_rbtree_link *parent;

	C2_ASSERT(node_is_valid(node));
	parent = node->rl_parent;
	C2_ASSERT(parent != ROOT_PARENT);
	return child(parent, 1 - index_of(node, parent));
}

// rotate left if direcion == 0, rotate right otherwise
static void bst_rotate_d(struct c2_rbtree *t, struct c2_rbtree_link *p, int direction)
{
	struct c2_rbtree_link *g;
	struct c2_rbtree_link *n;
	struct c2_rbtree_link *b;

	C2_ASSERT(node_is_valid(p));
	g = p->rl_parent;
	n = child(p, 1 - direction);
	C2_ASSERT(node_is_valid(n));
	b = child(n, direction);
	node_relation_set(p, b, 1 - direction);
	node_relation_set(n, p, direction);
	if (g == ROOT_PARENT) {
		t->r_root = n;
		n->rl_parent = ROOT_PARENT;
	} else
		node_relation_set(g, n, index_of(p, g));
}

static void bst_rotate(struct c2_rbtree *t, struct c2_rbtree_link *child, struct c2_rbtree_link *parent)
{
	C2_ASSERT(child == parent->rl_left || child == parent->rl_right);
	bst_rotate_d(t, parent, 1 - index_of(child, parent));
}

static void node_remove(struct c2_rbtree *t, struct c2_rbtree_link *n)
{
	struct c2_rbtree_link *p;
	struct c2_rbtree_link *c;

	C2_ASSERT(node_is_valid(n));
	p = n->rl_parent;
	// if node is root and no other non-leaf nodes in the tree
	if (p == ROOT_PARENT && n->rl_left == LEAF && n->rl_right == LEAF) {
		// then tree will be empty
		t->r_root = LEAF;
		return;
	}
	c = child(n, 1 - index_of(LEAF, n));
	// removing root
	if (p == ROOT_PARENT) {
		c->rl_parent = ROOT_PARENT;
		t->r_root = c;
	} else
		node_relation_set(p, c, index_of(n, p));
}

static void node_color_swap(struct c2_rbtree_link *a, struct c2_rbtree_link *b)
{
	C2_ASSERT(node_is_valid(a) && node_is_valid(b));
	enum c2_rbtree_color tmp = a->rl_color;
	a->rl_color = b->rl_color;
	b->rl_color = tmp;
}

// swap nodes but preserve colors
static void node_swap(struct c2_rbtree *t, struct c2_rbtree_link *a, struct c2_rbtree_link *b)
{
	struct c2_rbtree_link as;
	struct c2_rbtree_link bs;
	struct c2_rbtree_link *p;

	C2_ASSERT(node_is_valid(a) && node_is_valid(b));
	C2_ASSERT(a != b);
	as = *a;
	bs = *b;
	if (bs.rl_parent == ROOT_PARENT) {
		a->rl_parent = ROOT_PARENT;
		t->r_root = a;
	} else {
		p = bs.rl_parent;
		node_relation_set(p == a ? b : p, a, index_of(b, p));
	}
	if (as.rl_parent == ROOT_PARENT) {
		b->rl_parent = ROOT_PARENT;
		t->r_root = b;
	} else {
		p = as.rl_parent;
		node_relation_set(p == b ? a : p, b, index_of(a, p));
	}
	node_relation_set(a, bs.rl_left  == a ? b : bs.rl_left,  0);
	node_relation_set(b, as.rl_left  == b ? a : as.rl_left,  0);
	node_relation_set(a, bs.rl_right == a ? b : bs.rl_right, 1);
	node_relation_set(b, as.rl_right == b ? a : as.rl_right, 1);
	node_color_swap(a, b);
}

static struct c2_rbtree_link *bst_remove_find(struct c2_rbtree *t, struct c2_rbtree_link *node)
{
	struct c2_rbtree_link *new_pos;

	node = bst_find(t, key_of(t, node));
	// return NULL if node not found
	if (node == NULL)
		return NULL;
	// return node if it has leaf child
	if (node->rl_left == LEAF || node->rl_right == LEAF)
		return node;
	new_pos = bst_min(node->rl_right);
	C2_ASSERT(new_pos != NULL);
	// swap node and new_pos position
	node_swap(t, node, new_pos);
	return node;
}

bool rbtree_is_empty(const struct c2_rbtree *t)
{
	C2_PRE(t != NULL);
	return t->r_root == LEAF;
}

void c2_rbtree_init(struct c2_rbtree *t, c2_rbtree_cmp_t comparator, ptrdiff_t key_offset)
{
	C2_PRE(t != NULL);
	C2_PRE(comparator != NULL);

	// empty tree - root is leaf
	t->r_root = LEAF;
	t->r_cmp = comparator;
	t->r_key_offset = key_offset;
	C2_POST(c2_rbtree_invariant(t));
}

void c2_rbtree_fini(struct c2_rbtree *t)
{
	C2_ASSERT(c2_rbtree_invariant(t));
	C2_ASSERT(rbtree_is_empty(t));
}

bool c2_rbtree_is_empty(const struct c2_rbtree *t)
{
	C2_ASSERT(c2_rbtree_invariant(t));
	
	return rbtree_is_empty(t);
}

void c2_rbtree_link_init(struct c2_rbtree_link *rl)
{
	C2_PRE(rl != NULL);
	rl->rl_color = COLOR_BLACK;
	rl->rl_parent = NULL;
	rl->rl_right = LEAF;
	rl->rl_left = LEAF;
}

void c2_rbtree_link_fini(struct c2_rbtree_link *rl)
{
	C2_PRE(rl != NULL);
}

bool c2_rbtree_insert(struct c2_rbtree *t, struct c2_rbtree_link *n)
{
	struct c2_rbtree_link *p;	// parent
	struct c2_rbtree_link *g;	// grandparent
	struct c2_rbtree_link *u;	// uncle - other child of grandparent

	n = bst_insert(t, n);
	if (n == NULL)
		return false;

start:
	p = n->rl_parent;
	// if it is root node, repaint it black
	if (p == ROOT_PARENT) {
		node_repaint(n, COLOR_BLACK);
		return true;
	}

	// repaint node red
	node_repaint(n, COLOR_RED);

	// if parent is black
	// then no red-black tree properties is broken
	if (node_is_black(p))
		return true;

	// now parent is red and we have grandparent and uncle
	g = p->rl_parent;
	u = node_sibling(p);
	
	// if parent and uncle is red
	// then flip colors in parent, uncle and grandparent
	// and go up
	if (node_is_red(p) && node_is_red(u)) {
		node_repaint(p, COLOR_BLACK);
		node_repaint(u, COLOR_BLACK);
		node_repaint(g, COLOR_RED);
		n = g;
		goto start;
	}

	// now parent is red and uncle is black
	// then grandparent is black
	// rotate tree for have n-p leaning same as p-g
	if (index_of(p, g) != index_of(n, p)) {
		bst_rotate(t, n, p);
		n = p;
		p = n->rl_parent;
	}

	// parent is red, uncle is black, grandparent is black, node is red
	// n-p leaning same as p-g
	// rotate and repaint for have all properties satisfied
	bst_rotate(t, p, g);
	node_repaint(p, COLOR_BLACK);
	node_repaint(g, COLOR_RED);
	return true;
}

bool c2_rbtree_remove(struct c2_rbtree *t, struct c2_rbtree_link *n)
{
	// TODO comment
	struct c2_rbtree_link *p;
	struct c2_rbtree_link *c;
	struct c2_rbtree_link *s;
	struct c2_rbtree_link *sl;
	struct c2_rbtree_link *sr;
	struct c2_rbtree_link *s_child;

	C2_PRE(c2_rbtree_invariant(t));

	// if node hanen't leaf child, find minimal value in right
	// subtree and swap node with it, preserving colors
	n = bst_remove_find(t, n);
	if (n == NULL)
		return false;
	// now node have one leaf child
	C2_ASSERT(child(n, 0) == LEAF || child(n, 1) == LEAF);

	// if node is red, simply remove it
	if (node_is_red(n)) {
		node_remove(t, n);
		return true;
	}

	// c is non-leaf child of n (if n has it)
	c = child(n, 1 - index_of(LEAF, n));
	if (node_is_red(c)) {
		node_repaint(c, COLOR_BLACK);
		node_remove(t, n);
		return true;
	}

	p = n->rl_parent;
	if (p != ROOT_PARENT)
		s = node_sibling(n);
	node_remove(t, n);
	n = c;
start:
	// now n is black
	if (p == ROOT_PARENT)
		return true;

	sl = s->rl_left;
	sr = s->rl_right;
	if (node_is_red(s)) {
		bst_rotate(t, s, p);
		node_color_swap(p, s);
		s = child(p, 1 - index_of(n, p));
		sl = s->rl_left;
		sr = s->rl_right;
	}
	if (node_is_black(s) && node_is_black(sl) && node_is_black(sr)) {
		if (node_is_black(p)) {
			// parent, sibling and all children of sibling is black
			node_repaint(s, COLOR_RED);
			n = p;
			p = n->rl_parent;
			if (p != ROOT_PARENT)
				s = node_sibling(n);
			goto start;
		} else {
			node_color_swap(p, s);
			return true;
		}
	}
	
	C2_ASSERT(node_is_black(s));
	C2_ASSERT(node_is_red(sl) || node_is_red(sr));
	s_child = child(s, index_of(n, p));

	if (node_is_red(s_child)) {
		node_color_swap(s, s_child);
		bst_rotate(t, s_child, s);
		s = s_child;
	}

	s_child = child(s, 1 - index_of(n, p));
	C2_ASSERT(node_is_red(s_child));
	node_repaint(s_child, COLOR_BLACK);
	node_color_swap(p, s);
	bst_rotate(t, s, p);

	return true;
}

struct c2_rbtree_link *c2_rbtree_min(const struct c2_rbtree *t)
{
	C2_PRE(c2_rbtree_invariant(t));
	return bst_min(t->r_root);
}

struct c2_rbtree_link *c2_rbtree_find(const struct c2_rbtree *t, void *key)
{
	C2_PRE(c2_rbtree_invariant(t));
	return bst_find(t, key);
}

struct c2_rbtree_link *c2_rbtree_next(struct c2_rbtree_link *node)
{
	return bst_next(node);
}

// using:
// bst_next
// bst_min
// bst_cmp
// node_color
bool c2_rbtree_invariant(const struct c2_rbtree *t)
{
	struct c2_rbtree_link *node;
	struct c2_rbtree_link *prev;
	int tree_black_depth;
	int current_node_depth;

	C2_ASSERT(t != NULL);
	C2_ASSERT(t->r_cmp != NULL);

	// check for empty tree
	if (t->r_root == LEAF)
		return true;

	// check root node
	node = t->r_root;
	if (node->rl_color != COLOR_BLACK)
		return false;
	if (node->rl_parent != ROOT_PARENT)
		return false;
	
	tree_black_depth = -1;
	prev = NULL;
	for (node = bst_min(t->r_root); node != NULL;
		node = bst_next(node)) {
		// compare node with previous
		if (prev == NULL)
			prev = node;
		else if (bst_cmp(t, node, prev) < 0)
			return false;
		// every red node must have black childs
		if (node_is_red(node)) {
			if (!node_is_black(node->rl_left))
				return false;
			if (!node_is_black(node->rl_right))
				return false;
		}
		// every simple path from node with all leaf child
		// to root must have same number of black nodes
		if (node->rl_left == LEAF && node->rl_right == LEAF) {
			current_node_depth = node_black_depth(node);
			if (tree_black_depth == -1)
				tree_black_depth = current_node_depth;
			else if (tree_black_depth != current_node_depth)
				return false;
		}
	}
	return true;
}

/** @} end of rbtree group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
