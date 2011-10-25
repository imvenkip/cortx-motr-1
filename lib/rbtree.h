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

#ifndef __COLIBRI_LIB_RBTREE_H__
#define __COLIBRI_LIB_RBTREE_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include <stddef.h>

/**
   @defgroup rbtree Red-black tree
   @{
 */

struct c2_rbtree_link;

enum c2_rbtree_color {
	COLOR_RED,
	COLOR_BLACK
};

/**
  Comparator function for keys in tree.
  Parameters - key addresses.
  Return value must be:
    0   if *a == *b
    < 0 if *a < *b
    > 0 if *a > *b
 */
typedef int (*c2_rbtree_cmp_t)(void *a, void *b);

/**
   A red-black tree of elements.
 */
struct c2_rbtree {
	struct c2_rbtree_link *r_root;
	c2_rbtree_cmp_t r_cmp;
	// key offset relative link
	// example initialization: offsetof(struct c2_struct, s_key) -
	// offsetof(struct c2_struct, s_linkage)
	ptrdiff_t r_key_offset;
};

/**
   An element in a red-black tree.
 */
struct c2_rbtree_link {
	enum c2_rbtree_color rl_color;
	struct c2_rbtree_link *rl_parent;
	struct c2_rbtree_link *rl_left;
	struct c2_rbtree_link *rl_right;
};

void c2_rbtree_init(struct c2_rbtree *t, c2_rbtree_cmp_t comparator, ptrdiff_t key_offset);
void c2_rbtree_fini(struct c2_rbtree *t);
bool c2_rbtree_is_empty(const struct c2_rbtree *t);

void c2_rbtree_link_init (struct c2_rbtree_link *rl);
void c2_rbtree_link_fini (struct c2_rbtree_link *rl);

// return false if item with same key already exists
bool c2_rbtree_insert(struct c2_rbtree *t, struct c2_rbtree_link *n);
// return false if not found
bool c2_rbtree_remove(struct c2_rbtree *t, struct c2_rbtree_link *n);
// don't remove, return NULL for empty tree
struct c2_rbtree_link *c2_rbtree_min(const struct c2_rbtree *t);
// return NULL if not found
struct c2_rbtree_link *c2_rbtree_find(const struct c2_rbtree *t, void *key);
// return NULL if no next
struct c2_rbtree_link *c2_rbtree_next(struct c2_rbtree_link *node);

bool c2_rbtree_invariant(const struct c2_rbtree *t);

/** @} end of rbtree group */


/* __COLIBRI_LIB_RBTREE_H__ */
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
