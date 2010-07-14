/* -*- C -*- */

#ifndef __COLIBRI_LAYOUT_PDCLUST_H__
#define __COLIBRI_LAYOUT_PDCLUST_H__

#include "layout/layout.h"

/**
   @defgroup pdclust Parity de-clustering.

   Parity de-clustered layouts. See the link below for HLD and references to the
   literature. Parity de-clustering generalises higher RAID patterns (N+K, with
   K > 1) for the case where an object is striped over more target objects
   ("devices" in the traditional RAID terminology) than there are units in a
   parity group. Due to this, parity de-clustered layouts are parametrised by
   three numbers:

   @li N---number of data units in a parity group;

   @li K---number of parity units in a parity group. Data in an object striped
   with a given K can survive a loss of up to K target objects. When a target
   object failure is repaired, distributed spare units are used to store
   re-constructed data. There are K spare units in each parity group, making the
   latter consisting of N+2*K units;

   @li P---number of target objects over which layout stripes data, parity and
   spare units. A target object is divided into frames of unit size.

   Layout maps source units to target frames. This mapping is defined in terms
   of "tiles" which are groups of frames. A tile can be seen either as an L*P
   block of frames, L rows of P columns each, each row containing frames with
   the same offset in every target object, or as a C*(N+2*K) block of C groups,
   N+2*K frames each. Here L and C are two additional layout parameters selected
   so that C*(N+2*K) == L*P.

   Looking at a tile as a C*(N+2*K) block, map C consecutive parity groups (each
   containing N+2*K units) to it, then switch to L*P view and apply a certain
   permutation (depending on tile number) to columns.

   HLD explains why resulting layout mapping function possesses a number of
   desirable properties.

   @see https://docs.google.com/a/horizontalscale.com/Doc?docid=0Aa9lcGbR4emcZGhxY2hqdmdfNjI0Y2pkajVraG4

   @{
 */

/* import */

struct c2_pool;
struct c2_stob_id;

/* export */
struct c2_pdclust_layout;
struct c2_pdclust_src_addr;
struct c2_pdclust_tgt_addr;
struct c2_pdclust_ops;

/**
   Extension of generic c2_layout for a parity de-clustering.
 */
struct c2_pdclust_layout {
	/** super class */
	struct c2_layout             pl_layout;
	/**
	   A datum used to seed PRNG to generate tile column permutations.
	 */
	struct c2_uint128            pl_seed;
	/**
	   Number of data units in a parity group.
	 */
	uint32_t                     pl_N;
	/**
	   Number of parity units in a parity group. This is also the number of
	   spare units in a group.
	 */
	uint32_t                     pl_K;
	/**
	   Number of target objects over which this layout stripes the source.
	 */
	uint32_t                     pl_P;
	/**
	   Number of parity groups in a tile.
	   
	   @see c2_pdclust_layout::pl_L
	 */
	uint32_t                     pl_C;
	/**
	   Number of "frame rows" in a tile. L * P == C * (N + 2 * K).

	   @see c2_pdclust_layout::pl_L
	 */
	uint32_t                     pl_L;

	/**
	   Storage pool this layout is for.
	 */
	struct c2_pool              *pl_pool;
	/**
	   Target object identifiers.
	 */
	struct c2_stob_id           *pl_tgt;

	struct tile_cache {
		uint64_t  tc_tile_no;
		uint32_t *tc_permute;
		uint32_t *tc_inverse;
		/** Lehmer code of permutation.

		    @see http://en.wikipedia.org/wiki/Lehmer_code
		 */
		uint32_t *tc_lcode;
	} pl_tile_cache;
};

enum c2_pdclust_unit_type {
	PUT_DATA,
	PUT_PARITY,
	PUT_NR
};

/**
   Source unit address.

   Source unit address uniquely identifies data, parity or spare unit in a
   layout.
 */
struct c2_pdclust_src_addr {
	/**
	   Parity group number.
	 */
	uint64_t sa_group;
	/**
	   Unit number within its parity group.
	 */
	uint64_t sa_unit;
};

/**
   Target frame address.

   Target frame address uniquely identifies a frame in one of layout target
   objects.
 */
struct c2_pdclust_tgt_addr {
	/**
	   Number of the frame of in its object.
	 */
	uint64_t ta_frame;
	/**
	   Target object number within a layout.
	 */
	uint64_t ta_obj;
};

/**
   Layout mapping function.

   This function contains main parity de-clustering logic. It maps source units
   to target frames. It is used by client IO code to build IO requests and to
   direct them to the target objects.
 */
void c2_pdclust_layout_map(struct c2_pdclust_layout *play, 
			   const struct c2_pdclust_src_addr *src, 
			   struct c2_pdclust_tgt_addr *tgt);
/**
   Reverse layout mapping function.

   This function is a right inverse of layout mapping function. It is used by
   SNS repair and other server side mechanisms.
 */
void c2_pdclust_layout_inv(struct c2_pdclust_layout *play, 
			   const struct c2_pdclust_tgt_addr *tgt,
			   struct c2_pdclust_src_addr *src);

extern const struct c2_layout_type c2_pdclust_layout_type;
extern const struct c2_layout_formula c2_pdclust_NKP_formula;

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
