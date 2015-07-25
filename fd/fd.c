/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nachiket Sahasrabudhe <nachiket_sahasrabuddhe@xyratex.com>
 * Original creation date: 12-Jan-15
 */

#include "conf/obj_ops.h"      /* M0_CONF_DIRNEXT */
#include "conf/obj.h"
#include "conf/diter.h"        /* m0_conf_diter */
#include "conf/confc.h"        /* m0_confc_from_obj */
#include "pool/pool_machine.h" /* m0_poolmach */
#include "pool/pool.h"

#include "fd/fd.h"
#include "fd/fd_internal.h"

#include "lib/errno.h"         /* EINVAL */
#include "lib/memory.h"        /* M0_ALLOC_ARR M0_ALLOC_PTR m0_free */
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_FD
#include "lib/trace.h"         /* M0_ERR */
#include "lib/arith.h"         /* m0_gcd64 m0_enc m0_dec */
#include "lib/hash.h"          /* m0_hash */
#include "lib/assert.h"        /* _0C */
#include "lib/misc.h"          /* m0_permute */


static uint64_t parity_group_size(const struct m0_pdclust_attr *la_attr);

/**
 * Maps an index from the base permutation to appropriate target index
 * in a fault tolerant permutation.
 */
static uint64_t fault_tolerant_idx_get(uint64_t idx, uint64_t *children_nr,
				       uint64_t depth);

/**
 * Fetches the attributes associated with the symmetric tree from a
 * pool version.
 */
static int symm_tree_attr_get(const struct m0_conf_pver *pv, uint64_t *depth,
			      uint64_t *P, uint64_t *children_nr);

static inline bool fd_tile_invariant(const struct m0_fd_tile *tile);

/**
 * Checks the feasibility of expected tolerance using the attributes of
 * the symmetric tree formed from the pool version.
 */
static int tolerance_check(const struct m0_conf_pver *pv,
			   uint64_t *children_nr,
			   uint64_t first_level, uint64_t *failure_level);

/** Counts the nodes present at the given level. */
static int tree_level_nodes_cnt(const struct m0_conf_pver *pv, uint64_t level,
				uint64_t *children_nr);

/**
 * Returns the degree of a least degree node from the given level in
 * the pool version tree.
 */
static int min_children_cnt(const struct m0_conf_pver *pver, uint64_t pv_level,
			    uint64_t *children_nr);

/** Calculates the pool-width associated with the symmetric tree. */
static uint64_t pool_width_calc(uint64_t *children_nr, uint64_t depth);

static int cache_init(struct m0_fd_perm_cache *cache, uint64_t child_nr);

static void cache_fini(struct m0_fd_perm_cache *cache);

/** Returns an index of permuted target. **/
static void permuted_tgt_get(struct m0_pool_version *pver, uint64_t omega,
			     uint64_t *rel_vidx, uint64_t *tgt_idx);

/** Returns relative indices from a symmetric tree. **/
static void inverse_permuted_idx_get(struct m0_pool_version *pver,
				     uint64_t omega, uint64_t perm_idx,
				     uint64_t *rel_idx);

/** Permutes the permutation cache. **/
static void fd_permute(struct m0_fd_perm_cache *cache,
                       struct m0_pdclust_attr *attr, uint64_t omega);

static inline uint64_t tree2pv_level_conv(uint64_t level,
					  uint64_t tree_depth);

static inline uint64_t pv2tree_level_conv(uint64_t level,
					  uint64_t tree_depth);

static inline uint64_t ceil_of(uint64_t a, uint64_t b);

static bool obj_check(const struct m0_conf_obj *obj,
		      const struct m0_conf_obj_type *type)
{
	return m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE &&
		m0_conf_obj_type(M0_CONF_CAST(obj, m0_conf_objv)->cv_real) ==
									type;
}

static bool is_obj_rack(const struct m0_conf_obj *obj)
{
	return obj_check(obj, &M0_CONF_RACK_TYPE);
}

static bool is_obj_encl(const struct m0_conf_obj *obj)
{
	return obj_check(obj, &M0_CONF_ENCLOSURE_TYPE);
}

static bool is_obj_contr(const struct m0_conf_obj *obj)
{
	return obj_check(obj, &M0_CONF_CONTROLLER_TYPE);
}

static bool is_obj_disk(const struct m0_conf_obj *obj)
{
	return obj_check(obj, &M0_CONF_DISK_TYPE);
}

bool (*obj_filter[M0_FTA_DEPTH_MAX]) (const struct m0_conf_obj *obj) = {
	NULL,
	is_obj_rack,
	is_obj_encl,
	is_obj_contr,
	is_obj_disk
};

M0_TL_DESCR_DEFINE(perm_cache, "failure domains cache", M0_INTERNAL,
		   struct m0_fd_perm_cache, fpc_link, fpc_magic,
		   M0_FD_PRMCACHE_MAGIC, M0_FD_PRMCACHE_HEAD_MAGIC);

M0_TL_DEFINE(perm_cache, M0_INTERNAL, struct m0_fd_perm_cache);

#define pv_for(pv, level, obj, rc)                                         \
({                                                                         \
	struct m0_confc      *__confc;                                     \
	struct m0_conf_pver  *__pv    = (struct m0_conf_pver *)(pv);       \
	uint64_t              __level = (level);                           \
	struct m0_conf_diter  __it;                                        \
	struct  m0_fid conf_path[M0_FTA_DEPTH_MAX][M0_FTA_DEPTH_MAX] = {   \
	{M0_FID0, M0_FID0},                                                \
	{M0_CONF_PVER_RACKVS_FID},                                         \
        {M0_CONF_PVER_RACKVS_FID, M0_CONF_RACKV_ENCLVS_FID},               \
        {M0_CONF_PVER_RACKVS_FID, M0_CONF_RACKV_ENCLVS_FID,                \
	 M0_CONF_ENCLV_CTRLVS_FID},                                        \
	{M0_CONF_PVER_RACKVS_FID, M0_CONF_RACKV_ENCLVS_FID,                \
	 M0_CONF_ENCLV_CTRLVS_FID, M0_CONF_CTRLV_DISKVS_FID},              \
         };                                                                \
	__confc = (struct m0_confc *)m0_confc_from_obj(&__pv->pv_obj);     \
	M0_ASSERT(__confc != NULL);                                        \
	rc = m0_conf__diter_init(&__it, __confc, &__pv->pv_obj,            \
			          __level, conf_path[__level]);            \
	while (rc >= 0  &&                                                 \
	       (rc = m0_conf_diter_next_sync(&__it,                        \
					     obj_filter[__level])) !=      \
		M0_CONF_DIRNEXT) {;}                                       \
	for (obj = m0_conf_diter_result(&__it); rc > 0;                    \
	     rc = m0_conf_diter_next_sync(&__it, obj_filter[level])) {     \

#define pv_endfor } if (rc >= 0) m0_conf_diter_fini(&__it); })

static int tree_level_nodes_cnt(const struct m0_conf_pver *pv, uint64_t level,
				uint64_t *children_nr)
{
	struct m0_conf_obj  *obj;
	struct m0_conf_objv *objv;
	int                  rc = -EINVAL;

	M0_PRE(pv != NULL);
	M0_PRE(level > 0 && level < M0_FTA_DEPTH_MAX);

	if (level == 1) {
		*children_nr =
			m0_conf_dir_tlist_length(&pv->pv_rackvs->cd_items);
		rc = *children_nr > 0 ? 0 : M0_ERR(-EINVAL);
		goto out;
	}
	*children_nr = 0;
	pv_for(pv, level - 1, obj, rc) {
		M0_ASSERT(m0_conf_obj_type(obj) ==  &M0_CONF_OBJV_TYPE);
	        objv = M0_CONF_CAST(obj, m0_conf_objv);
		*children_nr +=
			m0_conf_dir_tlist_length(&objv->cv_children->cd_items);
	} pv_endfor;
out:
	return M0_RC(rc);
}

static int min_children_cnt(const struct m0_conf_pver *pv, uint64_t pv_level,
			    uint64_t *children_nr)
{
	struct m0_conf_obj  *obj;
	struct m0_conf_objv *objv;
	/* initialization to placate the compiler. */
	uint64_t             min = 0;
	uint64_t             obj_id;
	int                  rc = -EINVAL;

	M0_PRE(pv != NULL);
	M0_PRE(pv_level > 0 && pv_level < M0_FTA_DEPTH_MAX);

	obj_id = 0;
	*children_nr = 0;
	pv_for(pv, pv_level - 1, obj, rc) {
		M0_ASSERT(m0_conf_obj_type(obj) ==  &M0_CONF_OBJV_TYPE);
		objv = M0_CONF_CAST(obj, m0_conf_objv);
		*children_nr =
			m0_conf_dir_tlist_length(&objv->cv_children->cd_items);
		if (obj_id == 0)
			min = *children_nr;
		else {
			min = min_type(uint64_t, min, *children_nr);
		}
		*children_nr = min;
		++obj_id;
	} pv_endfor;
	return rc == 0 ? M0_RC(0) : M0_ERR(rc);
}

M0_INTERNAL int m0_fd__tile_init(struct m0_fd_tile *tile,
				 const struct m0_pdclust_attr *la_attr,
				 uint64_t *children_nr, uint64_t depth)
{
	M0_PRE(tile != NULL && la_attr != NULL && children_nr != NULL);
	M0_PRE(depth > 0);

	tile->ft_G     = parity_group_size(la_attr);
	tile->ft_cols  = pool_width_calc(children_nr, depth);
	tile->ft_rows  = tile->ft_G / m0_gcd64(tile->ft_G, tile->ft_cols);
	tile->ft_depth = depth;
	M0_ALLOC_ARR(tile->ft_cell, tile->ft_rows * tile->ft_cols);
	if (tile->ft_cell == NULL)
		return M0_ERR(-ENOMEM);
	memcpy(tile->ft_children_nr, children_nr, M0_FTA_DEPTH_MAX);

	M0_POST(parity_group_size(la_attr) <= tile->ft_cols);
	return M0_RC(0);;
}

M0_INTERNAL int m0_fd_tolerance_check(struct m0_conf_pver *pv,
				      uint64_t *failure_level)
{
	uint64_t children_nr[M0_FTA_DEPTH_MAX];
	uint64_t P;
	uint64_t depth;
	int      rc;

	M0_PRE(pv != NULL && failure_level != NULL);

	rc = symm_tree_attr_get(pv, &depth, &P, children_nr);
	if (rc != 0)
		*failure_level = depth;
	return M0_RC(rc);
}

M0_INTERNAL int m0_fd_tile_build(const struct m0_conf_pver *pv,
				 struct m0_pool_version *pool_ver,
				 uint64_t *failure_level)
{
	uint64_t children_nr[M0_FTA_DEPTH_MAX];
	uint64_t P;
	int      rc;

	M0_PRE(pv != NULL && pool_ver != NULL && failure_level != NULL);
	M0_PRE(m0_exists(i, pv->pv_nr_failures_nr,
			 pv->pv_nr_failures[i] != 0));
	rc = symm_tree_attr_get(pv, failure_level, &P, children_nr);
	if (rc != 0) {
		return M0_RC(rc);
	}
	rc = m0_fd__tile_init(&pool_ver->pv_fd_tile, &pv->pv_attr,
			       children_nr, *failure_level);
	if (rc != 0)
		return M0_RC(rc);
	m0_fd__tile_populate(&pool_ver->pv_fd_tile);
	return M0_RC(rc);
}

static inline uint64_t pv2tree_level_conv(uint64_t level,
					  uint64_t tree_depth)
{
	M0_PRE(tree_depth < M0_FTA_DEPTH_MAX &&
	       level > ((M0_FTA_DEPTH_MAX - 1) - tree_depth));
	return level - ((M0_FTA_DEPTH_MAX - 1) - tree_depth);
}

static inline uint64_t tree2pv_level_conv(uint64_t level,
					  uint64_t tree_depth)
{
	M0_PRE(tree_depth < M0_FTA_DEPTH_MAX);
	return level + ((M0_FTA_DEPTH_MAX - 1)  - tree_depth);
}

static int symm_tree_attr_get(const struct m0_conf_pver *pv, uint64_t *depth,
			      uint64_t *P, uint64_t *children_nr)
{
	int      rc;
	uint64_t max_units;
	uint64_t pv_level;
	uint64_t min_children;
	uint64_t G;
	uint64_t tol;
	uint64_t level;

	G = parity_group_size(&pv->pv_attr);
	for (level = 1, tol = 0; tol == 0 && level < M0_FTA_DEPTH_MAX;
	     ++level) {
		rc = tree_level_nodes_cnt(pv, level, children_nr);
		if (rc != 0) {
			*depth = level;
			return M0_RC(rc);
		}
		max_units = ceil_of(G, children_nr[0]);
		tol = pv->pv_attr.pa_K / max_units;
		if (tol < pv->pv_nr_failures[level]) {
			*depth = level;
			 return M0_ERR(-EINVAL);
		}
		if (tol > 0 && pv->pv_nr_failures[level] > 0)
			break;
	}
	*depth = M0_FTA_DEPTH_MAX - level;
	for (pv_level = level; pv_level < M0_FTA_DEPTH_MAX - 1; ++pv_level) {
		rc = min_children_cnt(pv, pv_level, &min_children);
		if (rc != 0) {
			*depth = pv_level;
			return M0_RC(rc);
		}
		children_nr[pv_level - (M0_FTA_DEPTH_MAX - *depth)] =
		  min_children;
	}
	children_nr[*depth] = 1;
	rc = tolerance_check(pv, children_nr, level, depth);
	if (rc != 0)
		return M0_RC(rc);
	*P = pool_width_calc(children_nr, *depth);
	return M0_RC(rc);
}

static inline uint64_t ceil_of(uint64_t a, uint64_t b)
{
	return (a + b - 1) / b;
}

static int tolerance_check(const struct m0_conf_pver *pv,
			   uint64_t *children_nr, uint64_t first_level,
			   uint64_t *failure_level)
{
	int       rc = 0;
	int       i;
	uint64_t  j;
	uint64_t  k;
	uint64_t  G;
	uint64_t  K;
	uint64_t  level;
	uint64_t  nodes_child;
	uint64_t  nodes;
	uint64_t  cnt;
	uint64_t  tol;
	uint64_t  sum;
	uint64_t *units[M0_FTA_DEPTH_MAX];

	G = parity_group_size(&pv->pv_attr);
	K = pv->pv_attr.pa_K;
	/* total nodes at given level. */
	nodes = 1;
	/* total nodes at the level of children. */
	nodes_child = children_nr[0];
	M0_ALLOC_PTR(units[0]);
	if (units[0] == NULL) {
		*failure_level = 0;
		return M0_ERR(-ENOMEM);
	}
	units[0][0] = G;
	for (i = 1, level = first_level; level < M0_FTA_DEPTH_MAX - 1;
	     ++i, ++level) {
		M0_ALLOC_ARR(units[i], nodes_child);
		if (units[i] == NULL) {
			rc = -ENOMEM;
			*failure_level = i;
			goto out;
		}
		tol = pv->pv_nr_failures[level];
		/* Distribute units from parents to children. */
		for (j = 0; j < nodes; ++j) {
			for (k = 0; k < children_nr[i - 1]; ++k) {
				units[i][j * children_nr[i - 1] + k] =
					units[i - 1][j] / children_nr[i - 1];
			}
			for (k = 0; k < units[i - 1][j] % children_nr[i - 1];
			     ++k) {
				units[i][j * children_nr[i - 1] + k] += 1;
			}
		}
		/* Check the tolerance. */
		for (k = 0, sum = 0, cnt = 0;
		     cnt < tol && k < children_nr[i - 1]; ++k) {
			for (j = 0; cnt < tol && j < nodes; ++j) {
				sum += units[i][j *children_nr[i - 1] + k];
				++cnt;
			}
		}
		if (sum > K) {
			*failure_level = i;
			rc = -EINVAL;
			goto out;
		}
		nodes = nodes_child;
		nodes_child *= children_nr[i];
	}
out:
	for (--i; i > -1; --i) {
		m0_free(units[i]);
	}
	return rc == 0 ? M0_RC(rc) : M0_ERR(rc);
}

static uint64_t parity_group_size(const struct m0_pdclust_attr *la_attr)
{
	return la_attr->pa_N + 2 * la_attr->pa_K;
}

static uint64_t pool_width_calc(uint64_t *children_nr, uint64_t depth)
{
	M0_PRE(children_nr != NULL);
	M0_PRE(m0_forall(i, depth, children_nr[i] != 0));

	return m0_reduce(i, depth, 1, * children_nr[i]);
}

M0_INTERNAL void m0_fd__tile_populate(struct m0_fd_tile *tile)
{
	uint64_t  row;
	uint64_t  col;
	uint64_t  idx;
	uint64_t  fidx;
	uint64_t  tidx;
	uint64_t *children_nr;

	M0_PRE(fd_tile_invariant(tile));

	children_nr = tile->ft_children_nr;
	for (row = 0; row < tile->ft_rows; ++row) {
		for (col = 0; col < tile->ft_cols; ++col) {
			idx = m0_enc(tile->ft_cols, row, col);
			tidx = fault_tolerant_idx_get(idx, children_nr,
						      tile->ft_depth);
			tile->ft_cell[idx].ftc_tgt.ta_frame = row;
			tile->ft_cell[idx].ftc_tgt.ta_obj   = tidx;
			fidx = m0_enc(tile->ft_cols, row, tidx);
			m0_dec(tile->ft_G, idx,
			       &tile->ft_cell[fidx].ftc_src.sa_group,
			       &tile->ft_cell[fidx].ftc_src.sa_unit);
		}
	}
}

static inline bool fd_tile_invariant(const struct m0_fd_tile *tile)
{
	return _0C(tile != NULL) && _0C(tile->ft_rows > 0) &&
	       _0C(tile->ft_cols > 0)                      &&
	       _0C(tile->ft_G > 0)                         &&
	       _0C(tile->ft_cell != NULL);
}

static uint64_t fault_tolerant_idx_get(uint64_t idx, uint64_t *children_nr,
				       uint64_t depth)
{
	uint64_t i;
	uint64_t prev;
	uint64_t r;
	uint64_t fd_idx[M0_FTA_DEPTH_MAX];
	uint64_t tidx;

	M0_SET0(&fd_idx);
	for (prev = 1, i = 1; i <= depth; ++i) {
		r  = idx % (prev * children_nr[i - 1]);
		idx -= r;
		fd_idx[i] = r / prev;
		prev *= children_nr[i - 1];
	}
	tidx = fd_idx[depth];
	prev = 1;
	for (i = depth; i > 0; --i) {
		tidx += fd_idx[i - 1] * children_nr[i - 1] * prev;
		prev *= children_nr[i - 1];
	}
	return tidx;
}

M0_INTERNAL void m0_fd_src_to_tgt(const struct m0_fd_tile *tile,
				  const struct m0_pdclust_src_addr *src,
				  struct m0_pdclust_tgt_addr *tgt)
{
	/* A parity group normalized to the first tile. */
	struct m0_pdclust_src_addr src_norm;
	uint64_t                   idx;
	uint64_t                   C;
	uint64_t                   omega;

	M0_PRE(tile != NULL && src != NULL && tgt != NULL);
	M0_PRE(fd_tile_invariant(tile));

	C = tile->ft_rows * tile->ft_cols / tile->ft_G;

	/* Get normalized location. */
	m0_dec(C, src->sa_group, &omega, &src_norm.sa_group);
	src_norm.sa_unit = src->sa_unit;
	M0_ASSERT(src_norm.sa_group < C);
	idx = m0_enc(tile->ft_G, src_norm.sa_group, src_norm.sa_unit);
	*tgt = tile->ft_cell[idx].ftc_tgt;
	/* Denormalize the frame location. */
	tgt->ta_frame += omega * tile->ft_rows;
}

M0_INTERNAL void m0_fd_tgt_to_src(const struct m0_fd_tile *tile,
				  const struct m0_pdclust_tgt_addr *tgt,
				  struct m0_pdclust_src_addr *src)
{
	struct m0_pdclust_tgt_addr tgt_norm;
	uint64_t                   idx;
	uint64_t                   C;
	uint64_t                   omega;

	M0_PRE(tile != NULL && src != NULL && tgt != NULL);
	M0_PRE(fd_tile_invariant(tile));

	C = (tile->ft_rows * tile->ft_cols) / tile->ft_G;
	m0_dec(tile->ft_rows, tgt->ta_frame, &omega, &tgt_norm.ta_frame);
	idx = m0_enc(tile->ft_cols, tgt_norm.ta_frame, tgt->ta_obj);
	*src = tile->ft_cell[idx].ftc_src;
	src->sa_group += omega * C;
}

M0_INTERNAL void m0_fd_tile_destroy(struct m0_fd_tile *tile)
{
	M0_PRE(tile != NULL);

	m0_free0(&tile->ft_cell);
	M0_SET0(&tile);
}

M0_INTERNAL int m0_fd_tree_build(const struct m0_conf_pver *pv,
			         struct m0_fd_tree *tree)
{
	int      rc;
	uint64_t children_nr[M0_FTA_DEPTH_MAX];
	uint64_t depth;
	uint64_t P;
	uint32_t level;

	M0_PRE(pv != NULL && tree != NULL);
	M0_PRE(pv->pv_nr_failures_nr == M0_FTA_DEPTH_MAX);

	rc = symm_tree_attr_get(pv, &depth, &P, children_nr);
	if (rc != 0)
		return M0_RC(rc);
	tree->ft_depth = depth;
	tree->ft_cnt   = 0;
	tree->ft_root  = m0_alloc(sizeof tree->ft_root[0]);
	if (tree->ft_root == NULL)
		return M0_ERR(-ENOMEM);
	perm_cache_tlist_init(&tree->ft_perm_cache);
	rc = m0_fd__tree_root_create(tree, children_nr[0]);
	if (rc != 0)
		return M0_RC(rc);
	for (level = 1; level < tree->ft_depth + 1; ++level) {
		rc = m0_fd__tree_level_populate(pv, tree, level);
		if (rc != 0)
			return M0_RC(rc);
	}
	rc = m0_fd__perm_cache_build(tree);
	return M0_RC(rc);
}

M0_INTERNAL int m0_fd__tree_level_populate(const struct m0_conf_pver *pv,
				           struct m0_fd_tree *tree,
				           uint32_t level)
{
	struct m0_conf_objv        *objv;
	struct m0_conf_obj         *obj;
	struct m0_fd__tree_cursor   cursor;
	struct m0_fd_tree_node    **node;
	uint64_t                    children_nr;
	uint64_t                    pv_level;
	int                         rc;

	M0_PRE(pv != NULL && tree != NULL);
	M0_PRE(tree->ft_root != NULL);
	M0_PRE(level > 0 && level <= tree->ft_depth);

	/* Initialize the cursor for failure-domain tree. */
	rc = m0_fd__tree_cursor_init(&cursor, tree, level);
	if (rc != 0)
		return M0_RC(rc);
	pv_level = tree2pv_level_conv(level, tree->ft_depth);
	pv_for(pv, pv_level, obj, rc) {
		M0_ASSERT(m0_conf_obj_type(obj) ==  &M0_CONF_OBJV_TYPE);
		objv = M0_CONF_CAST(obj, m0_conf_objv);
		if (level < tree->ft_depth)
			children_nr =
			 m0_conf_dir_tlist_length(&objv->cv_children->cd_items);
		else
			children_nr = 0;
		node = m0_fd__tree_cursor_get(&cursor);
		*node = m0_alloc(sizeof node[0][0]);
		if (*node == NULL)
			goto rewind;
		rc = m0_fd__tree_node_init(tree, *node, children_nr,
				           &cursor);
		if (rc != 0)
			goto rewind;
		m0_fd__tree_cursor_next(&cursor);
	} pv_endfor;
	M0_ASSERT(ergo(rc == 0, !m0_fd__tree_cursor_next(&cursor)));

	return M0_RC(rc);
rewind:
	if (*node != NULL) {
		m0_fd__tree_node_fini(tree, *node);
		m0_free(*node);
		*node = NULL;
	}
	--cursor.ftc_child_abs_idx;
	tree->ft_depth = cursor.ftc_child_abs_idx > -1 ? cursor.ftc_depth :
		cursor.ftc_depth - 1;
	return	M0_ERR(rc);
}

M0_INTERNAL int m0_fd__perm_cache_build(struct m0_fd_tree *tree)
{
	struct m0_fd__tree_cursor cursor;
	struct m0_fd_perm_cache  *cache;
	struct m0_fd_tree_node   *node;
	uint64_t                  children_nr;
	uint64_t                  level;
	int                       rc;

	for (level = 0; level < tree->ft_depth; ++level) {
		rc = m0_fd__tree_cursor_init(&cursor, tree, level);
		if (rc != 0) {
			m0_fd__perm_cache_destroy(tree);
			return M0_ERR(-ENOMEM);
		}
		do {
			node = *(m0_fd__tree_cursor_get(&cursor));
			children_nr = node->ftn_child_nr;
			cache = m0_tl_find(perm_cache, cache,
					   &tree->ft_perm_cache,
					   cache->fpc_len == children_nr);
			if (cache == NULL) {
				cache = m0_alloc(sizeof cache[0]);
				if (cache == NULL) {
					m0_fd__perm_cache_destroy(tree);
					return M0_ERR(-ENOMEM);
				}
				cache_init(cache, children_nr);
				perm_cache_tlist_add(&tree->ft_perm_cache,
						     cache);
			}
			node->ftn_cache = cache;
		} while (m0_fd__tree_cursor_next(&cursor));
	}
	return M0_RC(0);
}

static int cache_init(struct m0_fd_perm_cache *cache, uint64_t child_nr)
{
	uint64_t *ptr;

	M0_PRE(cache != NULL);

	M0_SET0(cache);
	cache->fpc_len   = child_nr;
	cache->fpc_omega = ~(uint64_t)0;
	M0_ALLOC_ARR(ptr, child_nr);
	if (ptr == NULL)
		goto err;

	cache->fpc_permute = ptr;
	M0_ALLOC_ARR(ptr, child_nr);
	if (ptr == NULL)
		goto err;

	cache->fpc_inverse = ptr;
	M0_ALLOC_ARR(ptr, child_nr);
	if (ptr == NULL)
		goto err;

	cache->fpc_lcode = ptr;
	perm_cache_tlink_init(cache);
	return M0_RC(0);
err:
	m0_free0(&cache->fpc_permute);
	m0_free0(&cache->fpc_inverse);
	m0_free0(&cache->fpc_lcode);
	M0_SET0(cache);
	return M0_ERR(-ENOMEM);
}

M0_INTERNAL void m0_fd_tree_destroy(struct m0_fd_tree *tree)
{
	struct m0_fd__tree_cursor  cursor;
	struct m0_fd_tree_node   **node;
	uint16_t                   depth;
	int32_t                    i;
	int                        rc;

	depth = tree->ft_depth;
	for (i = depth; i > 0; --i) {
		rc = m0_fd__tree_cursor_init(&cursor, tree, i);
		M0_ASSERT(rc == 0);
		do {
			node = m0_fd__tree_cursor_get(&cursor);
			/* This condition will hit when
			 * m0_fd__tree_level_populate() has got terminated
			 * intermittently.
			 */
			if (*node == NULL)
				break;
			m0_fd__tree_node_fini(tree, *node);
			m0_free0(node);
		} while (m0_fd__tree_cursor_next(&cursor));
	}
	if (tree->ft_root != NULL)
		m0_fd__tree_node_fini(tree, tree->ft_root);
	m0_fd__perm_cache_destroy(tree);
	m0_free0(&tree->ft_root);
	perm_cache_tlist_fini(&tree->ft_perm_cache);
	M0_POST(tree->ft_cnt == 0);
	M0_SET0(tree);
}

M0_INTERNAL void m0_fd__perm_cache_destroy(struct m0_fd_tree *tree)
{
	struct m0_fd_perm_cache *cache;

	M0_PRE(tree != NULL);

	m0_tlist_for(&perm_cache_tl, &tree->ft_perm_cache, cache) {
		cache_fini(cache);
		perm_cache_tlink_del_fini(cache);
		m0_free0(&cache);
	}m0_tlist_endfor;
}

static void cache_fini(struct m0_fd_perm_cache *cache)
{
	m0_free0(&cache->fpc_lcode);
	m0_free0(&cache->fpc_permute);
	m0_free0(&cache->fpc_inverse);
}

M0_INTERNAL void m0_fd_fwd_map(struct m0_pool_version *pver,
			       const struct m0_pdclust_src_addr *src,
			       struct m0_pdclust_tgt_addr *tgt)
{
	struct m0_fd_tile          *tile;
	struct m0_pdclust_src_addr  src_base;
	uint64_t                    rel_vidx[M0_FTA_DEPTH_MAX];
	uint64_t                    omega;
	uint64_t                    children;
	uint64_t                    C;
	uint64_t                    tree_depth;
	uint64_t                    i;
	uint64_t                    vidx;

	M0_PRE(pver != NULL);
	M0_PRE(src != NULL && tgt != NULL);

	tile = &pver->pv_fd_tile;

	/* Get location in fault-tolerant permutation. */
	m0_fd_src_to_tgt(tile, src, tgt);
	tree_depth = pver->pv_fd_tile.ft_depth;
	for (i = 1, children = 1; i < tree_depth; ++i) {
		children *= tile->ft_children_nr[i];
	}
	for (i = 1, vidx = tgt->ta_obj; i <= tree_depth; ++i) {
		rel_vidx[i]  = vidx / children;
		vidx        %= children;
		children    /= tile->ft_children_nr[i];
	}
	C = tile->ft_rows * tile->ft_cols / tile->ft_G;
	m0_dec(C, src->sa_group, &omega, &src_base.sa_group);
	permuted_tgt_get(pver, omega, rel_vidx, &tgt->ta_obj);
}

static void permuted_tgt_get(struct m0_pool_version *pver, uint64_t omega,
			     uint64_t *rel_vidx, uint64_t *tgt_idx)
{
	struct m0_fd_tree         *tree;
	struct m0_fd_tree_node    *node;
	struct m0_fd__tree_cursor  cursor;
	uint64_t                   depth;
	uint64_t                   perm_idx;
	uint64_t                   rel_idx;
	int                        rc;

	tree = &pver->pv_fd_tree;
	node = tree->ft_root;

	for (depth = 1; depth <= tree->ft_depth; ++depth) {
		rel_idx = rel_vidx[depth];
		fd_permute(node->ftn_cache, &pver->pv_attr, omega);
		M0_ASSERT(rel_idx < node->ftn_cache->fpc_len);
		perm_idx = node->ftn_cache->fpc_permute[rel_idx];
		rc = m0_fd__tree_cursor_init_at(&cursor, tree, node, perm_idx);
		M0_ASSERT(rc == 0);
		node = *(m0_fd__tree_cursor_get(&cursor));
		M0_ASSERT(node != NULL);
	}
	*tgt_idx = node->ftn_abs_idx;
}

static void fd_permute(struct m0_fd_perm_cache *cache,
                       struct m0_pdclust_attr *attr, uint64_t omega)
{
        uint32_t i;
        uint64_t rstate;

	if (cache->fpc_omega != omega) {
		/* Initialise columns array that will be permuted. */
		for (i = 0; i < cache->fpc_len; ++i)
			cache->fpc_permute[i] = i;

		/* Initialise PRNG. */
		rstate = m0_hash(attr->pa_seed.u_hi) ^
			m0_hash(attr->pa_seed.u_lo + omega);

		/* Generate permutation number in lexicographic ordering. */
		for (i = 0; i < cache->fpc_len - 1; ++i)
			cache->fpc_lcode[i] = m0_rnd(cache->fpc_len - i,
					&rstate);

		/* Apply the permutation. */
		m0_permute(cache->fpc_len, cache->fpc_lcode,
			   cache->fpc_permute, cache->fpc_inverse);
	}
}

M0_INTERNAL void m0_fd_bwd_map(struct m0_pool_version *pver,
			       const struct m0_pdclust_tgt_addr *tgt,
			       struct m0_pdclust_src_addr *src)
{
	struct m0_pdclust_tgt_addr tgt_ft;
	struct m0_fd_tile         *tile;
	uint64_t                   omega;
	uint64_t                   children;
	uint64_t                   i;
	uint64_t                   vidx;
	uint64_t                   tree_depth;
	uint64_t                   rel_idx[M0_FTA_DEPTH_MAX];

	tile = &pver->pv_fd_tile;
	m0_dec(pver->pv_fd_tile.ft_rows, tgt->ta_frame, &omega,
	       &tgt_ft.ta_frame);
	inverse_permuted_idx_get(pver, omega, tgt->ta_obj, rel_idx);
	tree_depth = pver->pv_fd_tree.ft_depth;
	for (i = 1, children = 1; i < tree_depth; ++i) {
		children *= tile->ft_children_nr[i];
	}
	for (i = 1, vidx = 0; i <= tree_depth; ++i) {
		vidx     += rel_idx[i] * children;
		if (rel_idx[i] >= tile->ft_children_nr[i - 1])
			break;
		children /= tile->ft_children_nr[i];
	}
	if (i > tree_depth) {
		tgt_ft.ta_frame = tgt->ta_frame;
		tgt_ft.ta_obj   = vidx;
		m0_fd_tgt_to_src(&pver->pv_fd_tile, &tgt_ft, src);
	} else {
		/* Input target and frame are unmapped. */
		src->sa_group  = ~(uint64_t)0;
		src->sa_unit   = ~(uint64_t)0;
	}
}

static void inverse_permuted_idx_get(struct m0_pool_version *pver,
				     uint64_t omega, uint64_t perm_idx,
				     uint64_t *rel_idx)
{
	struct m0_fd_tree         *tree;
	struct m0_fd__tree_cursor  cursor;
	struct m0_fd_tree_node    *node;
	int                        rc;
	int                        depth;

	tree = &pver->pv_fd_tree;

	rc = m0_fd__tree_cursor_init(&cursor, tree, tree->ft_depth);
	M0_ASSERT(rc == 0);
	while (cursor.ftc_child_abs_idx < perm_idx &&
	       m0_fd__tree_cursor_next(&cursor));
	M0_ASSERT(cursor.ftc_child_abs_idx == perm_idx);
	perm_idx = cursor.ftc_child_idx;
	node = cursor.ftc_node;
	M0_ASSERT(node != NULL);
	for (depth = tree->ft_depth; depth > 0; --depth) {
		fd_permute(node->ftn_cache, &pver->pv_attr, omega);
		rel_idx[depth] = node->ftn_cache->fpc_inverse[perm_idx];
		perm_idx = node->ftn_rel_idx;
		node = node->ftn_parent;
	}
}
