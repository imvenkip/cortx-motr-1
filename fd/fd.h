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
 * Original creation date: 06-Jan-15
 */

#pragma once

#ifndef __MERO_LAYOUT_FD_H__
#define __MERO_LAYOUT_FD_H__

#include "layout/pdclust.h"
#include "ha/note.h"


/**
 * @defgroup failure_domains Failure Domains
 * @{
 * @section DLD-failure-domains-intro Overview
 * A parity declustered layout divides a file into a collection of
 * parity groups. Each parity group has N number of data units, K number of
 * parity units, and S spare units, where S >= K. A parity declustered layout
 * de-clusters these parity groups across the available pool of hardware
 * resources, in such a way that:
 * - load per hardware resource is minimal in case of failure of any of the h/w
 *   resource.
 * - load across all h/w resources is as uniform as possible.
 *
 * A pool of h/w resources includes (but is not restricted to) racks,
 * enclosures, controllers, and disks. These resources form a
 * hierarchical structure that forms a tree. We call each of these resources a
 * failure-domain.
 * @section DLD-failure-domains-def Definitions
 * - Failure domain: Any h/w resource failure of which can cause a loss of a
 *   file data is called as a failure domain.
 * - Failure domain tree: A hierarchical topology in which failure domains are
 *   arranged is called a failure domain tree.
 * - Tolerance constraint: A vector of the size of height of failure domain
 *   tree, i^th member of which represents expected tolerable failures at
 *   that level of failure domain tree.
 * - Base tile: When parity groups from a tile are laid down sequentially over
 *   available pool of targets then we call such arrangement a base tile.
 * - Fault tolerant tile: A fault-tolerant permutation of base tile, that is
 *   applicable to all tiles across all files.
 * @section DLD-failure-domains-highlights Design Highlights
 * We aim to address two key issues:
 * - ensure that parity groups are declustered across the pool of resources
 *   such that user provided tolerance constraint is supported.
 * - data is distributed in such a way that load balancing is achieved during
 *   IO and repair of data.
 *
 * Failure domains algorithm achieves the first goal by creating a
 * fault tolerant tile, mapping of which will be common across all tiles
 * across all files. This tile is created only once, when pool version is
 * built. The second objective is attained by applying a sequence
 * of cascaded permutations to units from the fault tolerant tile, one at
 * each level of failure domains tree. These permutations are applied when IO or
 * SNS-repair require to map a parity group and unit, to target and frame
 * (and vice versa).
 * Please refer <a href ="https://docs.google.com/a/seagate.com/document/d/1GCDZEbtG1K22ilnEPB5HGXUzHpvgJ4wgMw7d1m3Ux6s/edit#">HLD</a>
 * for details of the algorithm.
 * @section DLD-failure-domains-req Requirements
 * @b r.conf.pool.pool_version.fault_tolerant_permutation
 * Implementation must generate a fault tolerant permutation of base tile that
 * guarantees the required tolerance constraint for failure domains.
 * @section DLD-failure-domains-dep Dependencies
 * Pools in confc: Pools in confc are required in order to create a
 * fault-tolerant tile.
 */

/* import */
struct m0_pdclust_src_addr;
struct m0_conf_pver;
struct m0_confc;
struct m0_poolmach;
struct m0_pool_version;

/**
 * Maximum allowable depth of pool version tree.
 */
enum m0_fd_tree_attr {
	M0_FTA_DEPTH_MAX = 5,
};

/**
 * Represents a cell of a fault tolerant tile stored in m0_pool_version.
 * Base tile provides an implicit mapping between (parity group, unit) and
 * (target, frame). The fault tolerant tile stores two values within every
 * cell:
 * - (target, frame) to which a pair (parity group, unit), that would have
 *   otherwise belonged to current cell in the base tile, is moved to.
 * - (parity group, unit) that is stored in the current cell.
 */
struct m0_fd_tile_cell {
	/**
	 * Target and frame to which this base tile element maps. This will be
	 * used by m0_pdclust_instance_map().
	 */
	struct m0_pdclust_tgt_addr ftc_tgt;
	/**
	 * Parity group and unit mapped to this cell. This will be
	 * used by m0_pdclust_instance_inv().
	 */
	struct m0_pdclust_src_addr ftc_src;
	/**
	 * HA state for the cell. The cell is in active state iff all of its
	 * parents have the state M0_NC_ACTIVE. This field is helpful in
	 * unit-testing, and it does not play any role in the production code.
	 */
	enum m0_ha_obj_state       ftc_ha_state;
};

struct m0_fd_tile {
	/** Number of rows in a tile. */
	uint64_t                ft_rows;
	/** Number of columnss in a tile. Also represents the pool-width
	 * associated with the symmetric tree.
	 */
	uint64_t                ft_cols;
	/** Parity group size. */
	uint64_t                ft_G;
	/** Depth of the symmetric tree associated with the tile. */
	uint64_t                ft_depth;
	/** Children per level for the symmetric tree. */
	uint64_t                ft_children_nr[M0_FTA_DEPTH_MAX];
	/** An array of tile cells. */
	struct m0_fd_tile_cell *ft_cell;
};

/**
 * Checks the feasibility of required tolerance for various failure domains.
 * @param[in]  pv            The pool version in configuration, for which the
 *			     tolerance of failure domains is to be checked.
 *			     m0_conf_pver::pv_nr_failures holds the required
 *			     tolerances.
 * @param[out] failure_level Indicates the level for which the input tolerance
 *                           can not be supported, when returned value by the
 *                           function is -EINVAL. In all other cases its value
 *                           is undefined.
 * @retval     0            On success.
 * @retval     -EINVAL       When tolerance can not be met.
 * @retval     -ENOMEM       When system is out of memory.
 */
M0_INTERNAL int m0_fd_tolerance_check(struct m0_conf_pver *pv,
				      uint64_t *failure_level);
/**
 * Allocates and prepares a fault tolerant tile for input pool version.
 * Returns infeasibility in case tolerance constraint for the pool version
 * can not be met.
 * @param[in]  pv            The pool version present in configuration.
 * @param[in[  pool_ver      In-memory representation of pv.
 * @param[out] failure_level Failure_level holds the level for which required
 *			     tolerance can not be met. In case of a success
 *			     this value holds the depth of the symmetric tree
 *			     formed from the input tree.
 * @retval     0	     On success.
 * @retval     -EINVAL       When required tolerance can not be met.
 * @retval     -ENOMEM       When a tile can not be allocated.
 *
 */
M0_INTERNAL int m0_fd_tile_build(const struct m0_conf_pver *pv,
				 struct m0_pool_version *pool_ver,
				 uint64_t *failure_level);

/** Frees the memory allocated for the fault tolerant tile. */
M0_INTERNAL void m0_fd_tile_destroy(struct m0_fd_tile *tile);

/**
 * Returns the target and frame at which the input unit from a given
 * parity group is located.
 * @param[in]  tile The failure permutation.
 * @param[in]  src  Parity group and a unit within it to be located in the
 *                  tile.
 * @param[out] tgt  Target and frame associated with the input src.
 */
M0_INTERNAL void m0_fd_src_to_tgt(const struct m0_fd_tile *tile,
				  const struct m0_pdclust_src_addr *src,
				  struct m0_pdclust_tgt_addr *tgt);
/**
 * Returns the parity group and unit located at given target and frame.
 * @param[in]  tile The failure permutation.
 * @param[in]  tgt  The target and frame co-ordinates for the required parity
 *                  group and unit.
 * @param[out] src  Parity group and unit located at given target and frame.
 */
M0_INTERNAL void m0_fd_tgt_to_src(const struct m0_fd_tile *tile,
				  const struct m0_pdclust_tgt_addr *tgt,
				  struct m0_pdclust_src_addr *src);

/** @} end group Failure_Domains */
/* __MERO_LAYOUT_FD_H__ */
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
