/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 *                  Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 07/15/2010
 */

#pragma once

#ifndef __COLIBRI_LAYOUT_PDCLUST_H__
#define __COLIBRI_LAYOUT_PDCLUST_H__

/**
 * @defgroup pdclust Parity de-clustering.
 *
 * Parity de-clustered layouts. See the link below for HLD and references to the
 * literature. Parity de-clustering generalises higher RAID patterns (N+K, with
 * K > 1) for the case where an object is striped over more target objects
 * ("devices" in the traditional RAID terminology) than there are units in a
 * parity group. Due to this, parity de-clustered layouts are parametrised by
 * three numbers:
 *
 * - N---number of data units in a parity group;
 *
 * - K---number of parity units in a parity group. Data in an object striped
 *   with a given K can survive a loss of up to K target objects. When a target
 *   object failure is repaired, distributed spare units are used to store
 *   re-constructed data. There are K spare units in each parity group, making
 *   the latter consisting of N+2*K units;
 *
 * - P---number of target objects over which layout stripes data, parity and
 *   spare units. A target object is divided into frames of unit size.
 *
 * Layout maps source units to target frames. This mapping is defined in terms
 * of "tiles" which are groups of frames. A tile can be seen either as an L*P
 * block of frames, L rows of P columns each, each row containing frames with
 * the same offset in every target object, or as a C*(N+2*K) block of C groups,
 * N+2*K frames each. Here L and C are two additional layout parameters selected
 * so that C*(N+2*K) == L*P.
 *
 * Looking at a tile as a C*(N+2*K) block, map C consecutive parity groups (each
 * containing N+2*K units) to it, then switch to L*P view and apply a certain
 * permutation (depending on tile number) to columns.
 *
 * HLD explains why resulting layout mapping function possesses a number of
 * desirable properties.
 *
 * @see https://docs.google.com/a/horizontalscale.com/Doc?docid=0Aa9lcGbR4emcZGhxY2hqdmdfNjI0Y2pkajVraG4
 *
 * @{
 */

/* import */
#include "lib/arith.h" /* C2_IS_8ALIGNED */
#include "sns/parity_math.h"
#include "layout/layout.h"

struct c2_pool;
struct c2_stob_id;

/* export */
struct c2_pdclust_attr;
struct c2_layout_pdclust_rec;
struct c2_pdclust_layout;
struct c2_pdclust_instance;
enum c2_pdclust_unit_type;
struct c2_pdclust_src_addr;
struct c2_pdclust_tgt_addr;

/**
 * Attributes specific to PDCLUST layout type.
 * These attributes are part of c2_pdclust_layout which is in-memory layout
 * object and are stored in the Layout DB as well, through
 * c2_layout_pdclust_rec.
 */
struct c2_pdclust_attr {
	/** Number of data units in a parity group. */
	uint32_t           pa_N;

	/**
	 * Number of parity units in a parity group. This is also the number of
	 * spare units in a group.
	 */
	uint32_t           pa_K;

	/**
	 * Number of target objects over which this layout stripes the source.
	 */
	uint32_t           pa_P;

	/** Stripe unit size. Specified in number of bytes. */
	uint64_t           pa_unit_size;

	/** A datum used to seed PRNG to generate tile column permutations. */
	struct c2_uint128  pa_seed;
};

/**
 * Pdclust layout type specific part of the record for the layouts table.
 *
 * @note This structure needs to be maintained as 8 bytes aligned.
 */
struct c2_layout_pdclust_rec {
	/** Layout enumeration type id. */
	uint32_t               pr_let_id;

	struct c2_pdclust_attr pr_attr;
};
C2_BASSERT(C2_IS_8ALIGNED(sizeof(struct c2_layout_pdclust_rec)));

/**
 * Extension of the generic c2_striped_layout for the parity de-clustered
 * layout type.
 *
 * @invariant pdl->pl_C * (pdl->pl_attr.pa_N + 2 * pdl->pl_attr.pa_K) ==
 *            pdl->pl_L * pdl->pl_attr.pa_P
 * @invariant pdl->pl_base.sl_enum->le_ops->leo_nr(pdl->pl_base.sl_enum) ==
 *            pdl->pl_attr.pa_P
 */
struct c2_pdclust_layout {
	/** Super class */
	struct c2_striped_layout  pl_base;
	/** Parity de-clustering layout attributes. */
	struct c2_pdclust_attr    pl_attr;
	/**
	 * Number of parity groups in a tile.
	 * @see c2_pdclust_layout::pl_L
	 */
	uint32_t                  pl_C;
	/**
	 * Number of "frame rows" in a tile. L * P == C * (N + 2 * K).
	 * @see c2_pdclust_layout::pl_C
	 */
	uint32_t                  pl_L;

	uint64_t                  pl_magic;
};

/**
 * Parity de-clustered layout instance for a particular file.
 *
 * This structure contains information necessary to execute IO against a
 * particular parity de-clustered file.
 */
struct c2_pdclust_instance {
	/* Super class, storing pointer to the layout being used. */
	struct c2_layout_instance    pi_base;
	/**
	 * Caches information about the most recently used tile.
	 *
	 * Some auxiliary data, such as permutations, used by layout mapping
	 * function is relatively expensive to re-compute. To reduce the
	 * overhead, such information is cached.
	 *
	 * Currently only information for a single tile is cached. More
	 * sophisticated schemes are possible.
	 */
	struct tile_cache {
		/** Tile to which caches information pertains. */
		uint64_t  tc_tile_no;

		/**
		 * Column permutation for this tile.
		 * This is an array of c2_pdclust_layout::pl_P elements, each
		 * element less than c2_pdclust_layout::pl_P without
		 * duplicates.
		 */
		uint32_t *tc_permute;

		/**
		 * Inverse column permutation.
		 *
		 * @invariant tc_permute[tc_inverse[x]] == x
		 * @invariant tc_inverse[tc_permute[x]] == x
		 */
		uint32_t *tc_inverse;

		/**
		 * Lehmer code of permutation.
		 * This array of c2_pdclust_layout::pl_P elements is used to
		 * generate tc_permute[] and tc_inverse[] arrays. Strictly
		 * speaking, it is not needed after above arrays are built, but
		 * it is kept for completeness.
		 *
		 * Technically speaking, this array is a lexicographic number
		 * of permutation written in factorial number system (see HLD
		 * for references).
		 *
		 * @see http://en.wikipedia.org/wiki/Lehmer_code
		 */
		uint32_t *tc_lcode;
	} pi_tile_cache;

	/** Parity math information, initialised according to the layout. */
	struct c2_parity_math        pi_math;

	uint64_t                     pi_magic;
};

/** Classification of units in a parity group. */
enum c2_pdclust_unit_type {
	C2_PUT_DATA,
	C2_PUT_PARITY,
	C2_PUT_SPARE,
	C2_PUT_NR
};

/**
 * Source unit address.
 *
 * Source unit address uniquely identifies data, parity or spare unit in a
 * layout.
 */
struct c2_pdclust_src_addr {
	/** Parity group number. */
	uint64_t sa_group;
	/** Unit number within its parity group. */
	uint64_t sa_unit;
};

/**
 * Target frame address.
 *
 * Target frame address uniquely identifies a frame in one of the layout target
 * objects.
 */
struct c2_pdclust_tgt_addr {
	/** Number of the frame in its target object. */
	uint64_t ta_frame;
	/** Target object number within a layout. */
	uint64_t ta_obj;
};

/**
 * Allocates and builds a layout object with the pdclust layout type,
 * by setting its intial ref count to 1.
 * @post ergo(rc == 0, pdclust_invariant(*out))
 * @post ergo(rc == 0, l->l_ref == 1)
 *
 * @note The layout object built by this API is to be finalised by releasing
 * 'the reference on it that has been held during its creation'.
 * @see c2_layout_put()
 *
 * In short:
 * Dual to c2_layout_put() when it is the last reference being released.
 */
int c2_pdclust_build(struct c2_layout_domain *dom,
		     uint64_t lid,
		     const struct c2_pdclust_attr *attr,
		     struct c2_layout_enum *le,
		     struct c2_pdclust_layout **out);

uint32_t c2_pdclust_N(const struct c2_pdclust_layout *pl);
uint32_t c2_pdclust_K(const struct c2_pdclust_layout *pl);
uint32_t c2_pdclust_P(const struct c2_pdclust_layout *pl);
uint64_t c2_pdclust_unit_size(const struct c2_pdclust_layout *pl);

/** Returns c2_pdclust_layout object given a c2_layout object. */
struct c2_pdclust_layout *c2_layout_to_pdl(const struct c2_layout *l);

/** Returns c2_layout object given a c2_pdclust_layout object. */
struct c2_layout *c2_pdl_to_layout(struct c2_pdclust_layout *pl);

/** Returns type of the given unit according to layout information. */
enum c2_pdclust_unit_type
c2_pdclust_unit_classify(const struct c2_pdclust_layout *play,
			 int unit);

/** Returns c2_pdclust_instance object given a c2_layout_instance object. */
struct c2_pdclust_instance *c2_layout_instance_to_pdi(
					const struct c2_layout_instance *li);

/**
 * Layout mapping function.
 *
 * This function contains main parity de-clustering logic. It maps source units
 * to target frames. It is used by client IO code to build IO requests and to
 * direct them to the target objects.
 */
void c2_pdclust_instance_map(struct c2_pdclust_instance *pi,
			     const struct c2_pdclust_src_addr *src,
			     struct c2_pdclust_tgt_addr *tgt);
/**
 * Reverse layout mapping function.
 *
 * This function is a right inverse of layout mapping function. It is used by
 * SNS repair and other server side mechanisms.
 */
void c2_pdclust_instance_inv(struct c2_pdclust_instance *pi,
			     const struct c2_pdclust_tgt_addr *tgt,
			     struct c2_pdclust_src_addr *src);

extern struct c2_layout_type c2_pdclust_layout_type;

/** @} end group pdclust */

/* __COLIBRI_LAYOUT_PDCLUST_H__ */
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
