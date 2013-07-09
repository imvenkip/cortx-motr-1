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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 5-Jun-2013
 */


#pragma once

#ifndef __MERO_BE_ALLOC_INTERNAL_H__
#define __MERO_BE_ALLOC_INTERNAL_H__

#include "lib/types.h"	/* m0_bcount_t */
#include "be/list.h"	/* m0_be_list */
#include "be/alloc.h"	/* m0_be_allocator_stats */

/**
 * @defgroup be
 *
 * @{
 */

/**
 * @brief Allocator chunk.
 *
 * - resides in the allocator space;
 * - located just before allocated memory block;
 * - there is at least one chunk in the allocator.
 */
struct be_alloc_chunk {
	/**
	 * M0_BE_ALLOC_MAGIC0
	 * Used to find invalid memory access after allocated chunk.
	 */
	uint64_t	bac_magic0;
	/** for m0_be_allocator_header.ba_chunks list */
	struct m0_tlink bac_linkage;
	/** magic for bac_linkage */
	uint64_t	bac_magic;
	/** for m0_be_allocator_header.ba_free list */
	struct m0_tlink bac_linkage_free;
	/** magic for bac_linkage_free */
	uint64_t	bac_magic_free;
	/** size of chunk */
	m0_bcount_t	bac_size;
	/** is chunk free? */
	bool		bac_free;
	/**
	 * M0_BE_ALLOC_MAGIC1
	 * Used to find invalid memory access before allocated chunk.
	 */
	/** M0_BE_ALLOC_MAGIC1 */
	uint64_t	bac_magic1;
	/** m0_be_alloc() will return address of bac_mem for allocated chunk */
	char		bac_mem[0];
};

/**
 * @brief Allocator header.
 *
 * - allocator space begins at m0_be_allocator_header.bah_addr and have size
 *   m0_be_allocator_header.bah_size bytes;
 * - resides in a segment address space;
 * - is a part of a segment header. It may be changed in the future;
 * - contains list headers for chunks lists.
 */
struct m0_be_allocator_header {
	struct m0_be_list	      bah_chunks;	/**< all chunks */
	struct m0_be_list	      bah_free;		/**< free chunks */
	struct m0_be_allocator_stats  bah_stats;	/**< XXX not used now */
	m0_bcount_t		      bah_size;		/**< memory size */
	void			     *bah_addr;		/**< memory address */
};

/** @} end of be group */

#endif /* __MERO_BE_ALLOC_INTERNAL_H__ */


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
