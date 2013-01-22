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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/24/2010
 */

#include <errno.h>
#include <string.h>                 /* memset */

#include "db/extmap.h"
#include "dtm/dtm.h"                /* m0_dtx */
#include "fol/fol.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_STOB
#include "lib/trace.h"
#include "lib/thread.h"             /* LAMBDA */
#include "lib/memory.h"
#include "lib/arith.h"              /* min_type, min3 */
#include "lib/misc.h"		    /* M0_SET0 */

#include "stob/stob.h"
#include "stob/cache.h"
#include "stob/ad.h"
#include "stob/stob_addb.h"
#include "stob/ad_xc.h"

/**
   @addtogroup stobad

   <b>Implementation of m0_stob with Allocation Data (AD).</b>

   An object created by ad_domain_stob_find() is kept in a per-domain in-memory
   list, until last reference to it is released and ad_stob_fini() is called.

   @todo this code is identical to one in m0_linux_stob_type and must be
   factored out.

   <b>AD extent map.</b>

   AD uses single m0_emap instance to store logical-physical translations for
   all stobs in the domain.

   For each ad storage object, its identifier (m0_stob_id) is used as a prefix
   to identify an extent map in m0_emap.

   The meaning of a segment ([A, B), V) in an extent map maintained for a
   storage object X depends on the value of V:

   @li if V is less than AET_MIN, the segment represents a mapping from extent
   [A, B) in X's name-space to extent [V, V + B - A) in the underlying object's
   name-space. This is the most usual "allocated extent", specifying where X
   data live;

   @li if V is AET_HOLE, the segment represents a hole [A, B) in X;

   @li if V is AET_NONE, the segment represents an extent that is not part of
   X's address-space. For example, when a new empty file is created, its extent
   map initially consists of a single segment ([0, M0_BINDEX_MAX + 1),
   AET_NONE);

   @li other values of V greater than or equal to AET_MIN could be used for
   special purpose segments in the future.

   @{
 */

static const struct m0_stob_type_op ad_stob_type_op;
static const struct m0_stob_op ad_stob_op;
static const struct m0_stob_domain_op ad_stob_domain_op;
static const struct m0_stob_io_op ad_stob_io_op;

struct ad_domain {
	struct m0_stob_domain      ad_base;

	char			   ad_path[MAXPATHLEN];

	struct m0_dbenv           *ad_dbenv;
	/**
	   Extent map storing mapping from logical to physical offsets.
	 */
	struct m0_emap             ad_adata;

	/**
	   Set to true in m0_ad_stob_setup(). Used in pre-conditions to
	   guarantee that the domain is fully initialized.
	 */
	bool                       ad_setup;
	/**
	    Backing store storage object, where storage objects of this domain
	    are stored in.
	 */
	struct m0_stob            *ad_bstore;
	struct m0_stob_cache       ad_cache;
	struct m0_ad_balloc       *ad_ballroom;
	int			   ad_babshift;

};

/**
   AD storage object.

   There is very little of the state besides m0_stob_cacheable.
 */
struct ad_stob {
	struct m0_stob_cacheable as_stob;
};

static inline struct ad_stob *stob2ad(struct m0_stob *stob)
{
	return container_of(stob, struct ad_stob, as_stob.ca_stob);
}

static inline struct ad_domain *domain2ad(struct m0_stob_domain *dom)
{
	return container_of(dom, struct ad_domain, ad_base);
}

/**
   Types of allocation extents.

   Values of this enum are stored as "physical extent start" in allocation
   extents.
 */
enum ad_stob_allocation_extent_type {
	/**
	    Minimal "special" extent type. All values less than this are valid
	    start values of normal allocated extents.
	 */
	AET_MIN = M0_BINDEX_MAX - (1ULL << 32),
	/**
	   This value is used to tag an extent that does not belong to the
	   stob's name-space. For example, an extent [X, M0_BINDEX_MAX + 1)
	   would usually be AET_NONE for a file of size X.
	 */
	AET_NONE,
	/**
	   This value is used to tag a hole in the storage object.
	 */
	AET_HOLE
};

static void ad_rec_part_init(struct m0_fol_rec_part *part);

static const struct m0_fol_rec_part_type_ops ad_part_type_ops = {
	.rpto_rec_part_init = ad_rec_part_init,
};

struct m0_fol_rec_part_type m0_ad_part_type;

static const struct m0_fol_rec_part_ops ad_part_ops = {
	.rpo_type = &m0_ad_part_type,
	.rpo_undo = NULL,
	.rpo_redo = NULL,
};

static void ad_rec_part_init(struct m0_fol_rec_part *part)
{
	part->rp_ops = &ad_part_ops;
}

/**
   Implementation of m0_stob_type_op::sto_init().
 */
static int ad_stob_type_init(struct m0_stob_type *stype)
{
	m0_stob_type_init(stype);
	m0_xc_ad_init();

	m0_ad_part_type = (struct m0_fol_rec_part_type) {
		.rpt_name = "AD record part",
		.rpt_xt   = m0_ad_rec_part_xc,
		.rpt_ops  = &ad_part_type_ops
	};

	return m0_fol_rec_part_type_register(&m0_ad_part_type);
}

/**
   Implementation of m0_stob_type_op::sto_fini().
 */
static void ad_stob_type_fini(struct m0_stob_type *stype)
{
	m0_xc_ad_fini();
	m0_stob_type_fini(stype);
	m0_fol_rec_part_type_deregister(&m0_ad_part_type);
}

/**
   Implementation of m0_stob_domain_op::sdo_fini().

   Finalizes all still existing in-memory objects.
 */
static void ad_domain_fini(struct m0_stob_domain *self)
{
	struct ad_domain *adom;

	adom = domain2ad(self);
	if (adom->ad_setup) {
		adom->ad_ballroom->ab_ops->bo_fini(adom->ad_ballroom);
		m0_emap_fini(&adom->ad_adata);
		m0_stob_put(adom->ad_bstore);
	}
	m0_stob_cache_fini(&adom->ad_cache);
	m0_stob_domain_fini(self);
	m0_free(adom);
}

static const char prefix[] = "ad.";

/**
   Implementation of m0_stob_type_op::sto_domain_locate().

   @note the domain returned is not immediately ready for
   use. m0_ad_stob_setup() has to be called against it first.
 */
static int ad_stob_type_domain_locate(struct m0_stob_type *type,
				      const char *domain_name,
				      struct m0_stob_domain **out)
{
	struct ad_domain      *adom;
	struct m0_stob_domain *dom;
	int                    result;

	M0_ASSERT(domain_name != NULL);
	M0_ASSERT(strlen(domain_name) <
		  ARRAY_SIZE(adom->ad_path) - ARRAY_SIZE(prefix));

	M0_ALLOC_PTR(adom);
	if (adom != NULL) {
		adom->ad_setup = false;
		dom = &adom->ad_base;
		dom->sd_ops = &ad_stob_domain_op;
		m0_stob_domain_init(dom, type);
		m0_stob_cache_init(&adom->ad_cache);
		sprintf(adom->ad_path, "%s%s", prefix, domain_name);
		dom->sd_name = adom->ad_path + ARRAY_SIZE(prefix) - 1;
		*out = dom;
		result = 0;
	} else {
		M0_STOB_OOM(AD_DOM_LOCATE);
		result = -ENOMEM;
	}
	return result;
}

/**
   Block size shift for objects of this domain.
 */
static uint32_t ad_bshift(const struct ad_domain *adom)
{
	M0_PRE(adom->ad_setup);
	return adom->ad_bstore->so_op->sop_block_shift(adom->ad_bstore);
}

M0_INTERNAL int m0_ad_stob_setup(struct m0_stob_domain *dom,
				 struct m0_dbenv *dbenv, struct m0_stob *bstore,
				 struct m0_ad_balloc *ballroom,
				 m0_bcount_t container_size, uint32_t bshift,
				 m0_bcount_t blocks_per_group,
				 m0_bcount_t res_groups)
{
	int			 result;
	m0_bcount_t		 groupsize;
	m0_bcount_t		 blocksize;
	struct ad_domain	*adom;

	adom = domain2ad(dom);

	M0_PRE(dom->sd_ops == &ad_stob_domain_op);
	M0_PRE(!adom->ad_setup);
	M0_PRE(bstore->so_state == CSS_EXISTS);

	blocksize = 1 << bshift;
	groupsize = blocks_per_group * blocksize;

	M0_PRE(groupsize > blocksize);
	M0_PRE(container_size > groupsize);
	M0_PRE(container_size / groupsize > res_groups);

	result = ballroom->ab_ops->bo_init(ballroom, dbenv, bshift,
					   container_size, blocks_per_group,
					   res_groups);
	if (result == 0) {
		adom->ad_dbenv    = dbenv;
		adom->ad_bstore   = bstore;
		adom->ad_ballroom = ballroom;
		adom->ad_setup    = true;
		M0_ASSERT(bshift >= ad_bshift(adom));
		adom->ad_babshift = bshift - ad_bshift(adom);
		m0_stob_get(adom->ad_bstore);
		result = m0_emap_init(&adom->ad_adata, dbenv, adom->ad_path);
	}
	return result;
}

static int ad_incache_init(struct m0_stob_domain *dom,
			   const struct m0_stob_id *id,
			   struct m0_stob_cacheable **out)
{
	struct ad_stob           *astob;
	struct m0_stob_cacheable *incache;

	M0_ALLOC_PTR(astob);
	if (astob != NULL) {
		*out = incache = &astob->as_stob;
		incache->ca_stob.so_op = &ad_stob_op;
		m0_stob_cacheable_init(incache, id, dom);
		return 0;
	} else {
		M0_STOB_OOM(AD_INCACHE_INIT);
		return -ENOMEM;
	}
}

/**
   Implementation of m0_stob_domain_op::sdo_stob_find().

   Returns an in-memory representation of the object with a given identifier.
 */
static int ad_domain_stob_find(struct m0_stob_domain *dom,
			       const struct m0_stob_id *id,
			       struct m0_stob **out)
{
	struct m0_stob_cacheable *incache;
	struct ad_domain         *adom;
	int                       result;

	adom = domain2ad(dom);
	result = m0_stob_cache_find(&adom->ad_cache, dom, id,
				    ad_incache_init, &incache);
	*out = &incache->ca_stob;
	return result;
}

/**
   Implementation of m0_stob_domain_op::sdo_tx_make().
 */
static int ad_domain_tx_make(struct m0_stob_domain *dom, struct m0_dtx *tx)
{
	struct ad_domain *adom;

	adom = domain2ad(dom);
	M0_PRE(adom->ad_setup);
	return m0_dtx_open(tx, adom->ad_dbenv);
}

/**
   Implementation of m0_stob_op::sop_fini().
 */
static void ad_stob_fini(struct m0_stob *stob)
{
	struct ad_stob *astob;

	astob = stob2ad(stob);
	m0_stob_cacheable_fini(&astob->as_stob);
	m0_free(astob);
}

/**
   Implementation of m0_stob_op::sop_create().
 */
static int ad_stob_create(struct m0_stob *obj, struct m0_dtx *tx)
{
	struct ad_domain *adom;

	adom = domain2ad(obj->so_domain);
	M0_PRE(adom->ad_setup);
	return m0_emap_obj_insert(&adom->ad_adata, &tx->tx_dbtx,
				  &obj->so_id.si_bits, AET_NONE);
}

static int ad_cursor(struct ad_domain *adom, struct m0_stob *obj,
		     uint64_t offset, struct m0_dtx *tx,
		     struct m0_emap_cursor *it)
{
	int result;

	result = m0_emap_lookup(&adom->ad_adata, &tx->tx_dbtx,
				&obj->so_id.si_bits, offset, it);
	if (result != 0 && result != -ENOENT && result != -ESRCH)
		M0_STOB_FUNC_FAIL(AD_CURSOR, result);
	return result;
}

/**
   Implementation of m0_stob_op::sop_locate().
 */
static int ad_stob_locate(struct m0_stob *obj, struct m0_dtx *tx)
{
	struct m0_emap_cursor it;
	int                   result;
	struct ad_domain     *adom;

	adom = domain2ad(obj->so_domain);
	M0_PRE(adom->ad_setup);
	result = ad_cursor(adom, obj, 0, tx, &it);
	if (result == 0)
		m0_emap_close(&it);
	else if (result == -ESRCH)
		result = -ENOENT;
	return result;
}

/** @} end group stobad */

/*****************************************************************************/
/*                                    IO code                                */
/*****************************************************************************/

/**
   @addtogroup stobad

   <b>AD IO implementation.</b>

   Storage object IO (m0_stob_io) is a request to transfer data from the user
   address-space to the storage object name-space. The source and target
   locations are specified by sequences of intervals: m0_stob_io::si_user for
   locations in source address space, which are user buffers and
   m0_stob_io::si_stob for locations in the target name-space, which are extents
   in the storage object.

   AD IO implementation takes a m0_stob_io against an AD storage object and
   constructs a "back" m0_stob_io against the underlying storage object.

   The user buffers list of the back IO request can differ from the user buffers
   list of the original IO request, because in the case of read, some parts of
   the original IO request might correspond to holes in the AD object and
   produce no back IO.

   For writes, the reason to make back IO user buffers list different from the
   original user buffers list is to make memory management identical in read and
   write case. See ad_stob_io_release() and ad_vec_alloc().

   Two other interval sequences are used by AD to construct back IO request:

   @li extent map for the AD object specifies how logical offsets in the AD
   object map to offsets in the underlying object. The extent map can be seen as
   a sequence of matching logical and physical extents;

   @li for a write, a sequence of allocated extents, returned by the block
   allocator (ad_balloc), specifies where newly written data should go.

   Note that intervals of these sequences belong to different name-spaces (user
   address-space, AD object name-space, underlying object name-space), but they
   have the same total length, except for the extent map.

   Construction of back IO request proceeds by making a number of passes over
   these sequences. An interval boundary in any of the sequences can,
   potentially, introduce a discontinuity in the back IO request (a point where
   back IO has to switch another user buffer or to another offset in the
   underlying object). To handle this, the IO request is split into a number of
   "fragments", each fragment fitting completely inside corresponding intervals
   in all relevant sequences. Fragment number calculation constitutes the first
   pass.

   To make code more uniform, sequences are represented by "cursor-like" data
   structures:

   @li m0_vec_cursor for m0_vec-derived sequences (list of user buffers and list
   of target extents in a storage object);

   @li m0_emap_caret for an extent map;

   @li ad_wext_cursor for sequence of allocated extents.

   With cursors a pass looks like

   @code
   frags = 0;
   do {
           frag_size = min(cursor_step(seq1), cursor_step(seq2), ...);

	   ... handle fragment of size frag_size ...

	   end_of_cursor = cursor_move(seq1, frag_step);
	   cursor_move(seq2, frag_step);
	   ...
   } while (!end_of_cursor)
   @endcode

   Here cursor_step returns a distance to start of the next interval in a
   sequence and cursor_move advances a sequence to a given amount.

   @{
 */

/**
   AD private IO state.
 */
struct ad_stob_io {
	/** IO request */
	struct m0_stob_io *ai_fore;
	/** Back IO request */
	struct m0_stob_io  ai_back;
	/** Clink registered with back-io completion channel to intercept back
	    IO completion notification. */
	struct m0_clink    ai_clink;
};

static bool ad_endio(struct m0_clink *link);

/**
   Helper function to allocate a given number of blocks in the underlying
   storage object.
 */
static int ad_balloc(struct ad_domain *adom, struct m0_dtx *tx,
			m0_bcount_t count, struct m0_ext *out)
{
	int rc;

	M0_PRE(adom->ad_setup);
	count >>= adom->ad_babshift;
	M0_LOG(M0_DEBUG, "count=%lu", (unsigned long)count);
	M0_ASSERT(count > 0);
	rc = adom->ad_ballroom->ab_ops->bo_alloc(adom->ad_ballroom,
						   tx, count, out);
	out->e_start <<= adom->ad_babshift;
	out->e_end <<= adom->ad_babshift;

	return rc;
}

/**
   Helper function to free a given block extent in the underlying storage
   object.
 */
static int ad_bfree(struct ad_domain *adom, struct m0_dtx *tx,
		    struct m0_ext *ext)
{
	struct m0_ext tgt;

	M0_PRE(adom->ad_setup);
	M0_PRE((ext->e_start & ((1ULL << adom->ad_babshift) - 1)) == 0);
	M0_PRE((ext->e_end   & ((1ULL << adom->ad_babshift) - 1)) == 0);

	tgt.e_start = ext->e_start >> adom->ad_babshift;
	tgt.e_end   = ext->e_end   >> adom->ad_babshift;
	return adom->ad_ballroom->ab_ops->bo_free(adom->ad_ballroom, tx, &tgt);
}

/**
   Implementation of m0_stob_op::sop_io_init().

   Allocates private IO state structure.
 */
M0_INTERNAL int ad_stob_io_init(struct m0_stob *stob, struct m0_stob_io *io)
{
	struct ad_stob_io *aio;
	int                result;

	M0_PRE(io->si_state == SIS_IDLE);

	M0_ALLOC_PTR(aio);
	if (aio != NULL) {
		io->si_stob_private = aio;
		io->si_op = &ad_stob_io_op;
		aio->ai_fore = io;
		m0_stob_io_init(&aio->ai_back);
		m0_clink_init(&aio->ai_clink, &ad_endio);
		m0_clink_add_lock(&aio->ai_back.si_wait, &aio->ai_clink);
		result = 0;
	} else {
		M0_STOB_OOM(AD_IO_INIT);
		result = -ENOMEM;
	}
	return result;
}

/**
   Releases vectors allocated for back IO.

   @note that back->si_stob.ov_vec.v_count is _not_ freed separately, as it is
   aliased to back->si_user.z_bvec.ov_vec.v_count.

   @see ad_vec_alloc()
 */
static void ad_stob_io_release(struct ad_stob_io *aio)
{
	struct m0_stob_io *back = &aio->ai_back;

	M0_ASSERT(back->si_stob.iv_vec.v_count ==
		  back->si_user.ov_vec.v_count);

	m0_free(back->si_user.ov_vec.v_count);
	back->si_user.ov_vec.v_count = NULL;
	back->si_stob.iv_vec.v_count = NULL;

	m0_free(back->si_user.ov_buf);
	back->si_user.ov_buf = NULL;

	m0_free(back->si_stob.iv_index);
	back->si_stob.iv_index = NULL;

	back->si_obj = NULL;
}

/**
   Implementation of m0_stob_io_op::sio_fini().
 */
static void ad_stob_io_fini(struct m0_stob_io *io)
{
	struct ad_stob_io *aio = io->si_stob_private;

	ad_stob_io_release(aio);
	m0_clink_del_lock(&aio->ai_clink);
	m0_clink_fini(&aio->ai_clink);
	m0_stob_io_fini(&aio->ai_back);
	m0_free(aio);
}

/**
   Initializes cursors at the beginning of a pass.
 */
static int ad_cursors_init(struct m0_stob_io *io, struct ad_domain *adom,
			   struct m0_emap_cursor *it,
			   struct m0_vec_cursor *src, struct m0_vec_cursor *dst,
			   struct m0_emap_caret *map)
{
	int result;

	result = ad_cursor(adom, io->si_obj, io->si_stob.iv_index[0],
			   io->si_tx, it);
	if (result == 0) {
		m0_vec_cursor_init(src, &io->si_user.ov_vec);
		m0_vec_cursor_init(dst, &io->si_stob.iv_vec);
		m0_emap_caret_init(map, it, io->si_stob.iv_index[0]);
	}
	return result;
}

/**
   Finalizes the cursors that need finalisation.
 */
static void ad_cursors_fini(struct m0_emap_cursor *it,
			    struct m0_vec_cursor *src,
			    struct m0_vec_cursor *dst,
			    struct m0_emap_caret *map)
{
	m0_emap_caret_fini(map);
	m0_emap_close(it);
}

/**
   Allocates back IO buffers after number of fragments has been calculated.

   @see ad_stob_io_release()
 */
static int ad_vec_alloc(struct m0_stob *obj,
			struct m0_stob_io *back, uint32_t frags)
{
	m0_bcount_t *counts;
	int          result;

	M0_ASSERT(back->si_user.ov_vec.v_count == NULL);

	result = 0;
	if (frags > 0) {
		M0_ALLOC_ARR(counts, frags);
		back->si_user.ov_vec.v_count = counts;
		back->si_stob.iv_vec.v_count = counts;
		M0_ALLOC_ARR(back->si_user.ov_buf, frags);
		M0_ALLOC_ARR(back->si_stob.iv_index, frags);

		back->si_user.ov_vec.v_nr = frags;
		back->si_stob.iv_vec.v_nr = frags;

		if (counts == NULL || back->si_user.ov_buf == NULL ||
		    back->si_stob.iv_index == NULL) {
			M0_STOB_OOM(AD_VEC_ALLOC);
			result = -ENOMEM;
		}
	}
	return result;
}

/**
   Constructs back IO for read.

   This is done in two passes:

   @li first, calculate number of fragments, taking holes into account. This
   pass iterates over user buffers list (src), target extents list (dst) and
   extents map (map). Once this pass is completed, back IO vectors can be
   allocated;

   @li then, iterate over the same sequences again. For holes, call memset()
   immediately, for other fragments, fill back IO vectors with the fragment
   description.

   @note assumes that allocation data can not change concurrently.

   @note memset() could become a bottleneck here.

   @note cursors and fragment sizes are measured in blocks.
 */
static int ad_read_launch(struct m0_stob_io *io, struct ad_domain *adom,
			  struct m0_vec_cursor *src, struct m0_vec_cursor *dst,
			  struct m0_emap_caret *map)
{
	struct m0_emap_cursor *it;
	struct m0_emap_seg    *seg;
	struct m0_stob_io     *back;
	struct ad_stob_io     *aio       = io->si_stob_private;
	uint32_t               frags;
	uint32_t               frags_not_empty;
	uint32_t               bshift    = ad_bshift(adom);
	m0_bcount_t            frag_size; /* measured in blocks */
	m0_bindex_t            off;       /* measured in blocks */
	int                    result;
	int                    i;
	int                    idx;
	bool                   eosrc;
	bool                   eodst;
	int                    eomap;

	M0_PRE(io->si_opcode == SIO_READ);

	it   = map->ct_it;
	seg  = m0_emap_seg_get(it);
	back = &aio->ai_back;

	frags = frags_not_empty = 0;
	do {
		off = io->si_stob.iv_index[dst->vc_seg] + dst->vc_offset;

		/*
		 * The next fragment starts at the offset off and the extents
		 * map has to be positioned at this offset. There are two ways
		 * to do this:
		 *
		 * * lookup an extent containing off (m0_emap_lookup()), or
		 *
		 * * iterate from the current position (m0_emap_caret_move())
		 *   until off is reached.
		 *
		 * Lookup incurs an overhead of tree traversal, whereas
		 * iteration could become expensive when extents map is
		 * fragmented and target extents are far from each other.
		 *
		 * Iteration is used for now, because when extents map is
		 * fragmented or IO locality of reference is weak, performance
		 * will be bad anyway.
		 *
		 * Note: the code relies on the target extents being in
		 * increasing offset order in dst.
		 */
		M0_ASSERT(off >= map->ct_index);
		eomap = m0_emap_caret_move(map, off - map->ct_index);
		if (eomap < 0) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_1, eomap);
			return eomap;
		}
		M0_ASSERT(!eomap);
		M0_ASSERT(m0_ext_is_in(&seg->ee_ext, off));

		frag_size = min3(m0_vec_cursor_step(src),
				 m0_vec_cursor_step(dst),
				 m0_emap_caret_step(map));
		M0_ASSERT(frag_size > 0);
		if (frag_size > (size_t)~0ULL) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_2, -EOVERFLOW);
			return -EOVERFLOW;
		}

		frags++;
		if (seg->ee_val < AET_MIN)
			frags_not_empty++;

		eosrc = m0_vec_cursor_move(src, frag_size);
		eodst = m0_vec_cursor_move(dst, frag_size);
		eomap = m0_emap_caret_move(map, frag_size);
		if (eomap < 0) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_3, eomap);
			return eomap;
		}

		M0_ASSERT(eosrc == eodst);
		M0_ASSERT(!eomap);
	} while (!eosrc);

	ad_cursors_fini(it, src, dst, map);

	result = ad_vec_alloc(io->si_obj, back, frags_not_empty);
	if (result != 0)
		return result;

	result = ad_cursors_init(io, adom, it, src, dst, map);
	if (result != 0)
		return result;

	for (idx = i = 0; i < frags; ++i) {
		void        *buf;
		m0_bindex_t  off;

		buf = io->si_user.ov_buf[src->vc_seg] + src->vc_offset;
		off = io->si_stob.iv_index[dst->vc_seg] + dst->vc_offset;

		M0_ASSERT(off >= map->ct_index);
		eomap = m0_emap_caret_move(map, off - map->ct_index);
		if (eomap < 0) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_4, eomap);
			return eomap;
		}
		M0_ASSERT(!eomap);
		M0_ASSERT(m0_ext_is_in(&seg->ee_ext, off));

		frag_size = min3(m0_vec_cursor_step(src),
				 m0_vec_cursor_step(dst),
				 m0_emap_caret_step(map));

		if (seg->ee_val == AET_NONE || seg->ee_val == AET_HOLE) {
			/*
			 * Read of a hole or unallocated space (beyond
			 * end of the file).
			 */
			memset(m0_stob_addr_open(buf, bshift),
			       0, frag_size << bshift);
			io->si_count += frag_size;
		} else {
			M0_ASSERT(seg->ee_val < AET_MIN);

			back->si_user.ov_vec.v_count[idx] = frag_size;
			back->si_user.ov_buf[idx] = buf;

			back->si_stob.iv_index[idx] = seg->ee_val +
				(off - seg->ee_ext.e_start);
			idx++;
		}
		m0_vec_cursor_move(src, frag_size);
		m0_vec_cursor_move(dst, frag_size);
		result = m0_emap_caret_move(map, frag_size);
		if (result < 0) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_5, eomap);
			break;
		}
		M0_ASSERT(result == 0);
	}
	M0_ASSERT(ergo(result == 0, idx == frags_not_empty));
	return result;
}

/**
   A linked list of allocated extents.
 */
struct ad_write_ext {
	struct m0_ext        we_ext;
	struct ad_write_ext *we_next;
};

/**
   A cursor over allocated extents.
 */
struct ad_wext_cursor {
	const struct ad_write_ext *wc_wext;
	m0_bcount_t                wc_done;
};

static void ad_wext_cursor_init(struct ad_wext_cursor *wc,
				struct ad_write_ext *wext)
{
	wc->wc_wext = wext;
	wc->wc_done = 0;
}

static m0_bcount_t ad_wext_cursor_step(struct ad_wext_cursor *wc)
{
	M0_PRE(wc->wc_wext != NULL);
	M0_PRE(wc->wc_done < m0_ext_length(&wc->wc_wext->we_ext));

	return m0_ext_length(&wc->wc_wext->we_ext) - wc->wc_done;
}

static bool ad_wext_cursor_move(struct ad_wext_cursor *wc, m0_bcount_t count)
{
	while (count > 0 && wc->wc_wext != NULL) {
		m0_bcount_t step;

		step = ad_wext_cursor_step(wc);
		if (count >= step) {
			wc->wc_wext = wc->wc_wext->we_next;
			wc->wc_done = 0;
			count -= step;
		} else {
			wc->wc_done += count;
			count = 0;
		}
	}
	return wc->wc_wext == NULL;
}

/**
   Calculates how many fragments this IO request contains.

   @note extent map and dst are not used here, because write allocates new space
   for data, ignoring existing allocations in the overwritten extent of the
   file.
 */
static uint32_t ad_write_count(struct m0_stob_io *io, struct m0_vec_cursor *src,
			       struct ad_wext_cursor *wc)
{
	uint32_t               frags;
	m0_bcount_t            frag_size;
	bool                   eosrc;
	bool                   eoext;

	frags = 0;

	do {
		frag_size = min_check(m0_vec_cursor_step(src),
				      ad_wext_cursor_step(wc));
		M0_ASSERT(frag_size > 0);
		M0_ASSERT(frag_size <= (size_t)~0ULL);

		eosrc = m0_vec_cursor_move(src, frag_size);
		eoext = ad_wext_cursor_move(wc, frag_size);

		M0_ASSERT(ergo(eosrc, eoext));
		++frags;
	} while (!eoext);
	return frags;
}

/**
   Fills back IO request with information about fragments.
 */
static void ad_write_back_fill(struct m0_stob_io *io, struct m0_stob_io *back,
			       struct m0_vec_cursor *src,
			       struct ad_wext_cursor *wc)
{
	m0_bcount_t    frag_size;
	uint32_t       idx;
	bool           eosrc;
	bool           eoext;

	idx = 0;
	do {
		void *buf;

		frag_size = min_check(m0_vec_cursor_step(src),
				      ad_wext_cursor_step(wc));

		buf = io->si_user.ov_buf[src->vc_seg] + src->vc_offset;

		back->si_user.ov_vec.v_count[idx] = frag_size;
		back->si_user.ov_buf[idx] = buf;

		back->si_stob.iv_index[idx] =
			wc->wc_wext->we_ext.e_start + wc->wc_done;

		eosrc = m0_vec_cursor_move(src, frag_size);
		eoext = ad_wext_cursor_move(wc, frag_size);
		idx++;
		M0_ASSERT(eosrc == eoext);
	} while (!eoext);
	M0_ASSERT(idx == back->si_stob.iv_vec.v_nr);
}

/**
 * Helper function used by ad_write_map_ext() to free sub-segment "ext" from
 * allocated segment "seg".
 */
static int seg_free(struct m0_stob_io *io, struct ad_domain *adom,
		    const struct m0_emap_seg *seg, const struct m0_ext *ext,
		    uint64_t val)
{
	m0_bcount_t   delta = ext->e_start - seg->ee_ext.e_start;
	struct m0_ext tocut = {
		.e_start = val + delta,
		.e_end   = val + delta + m0_ext_length(ext)
	};
	return val < AET_MIN ? ad_bfree(adom, io->si_tx, &tocut) : 0;
}

/**
   Inserts allocated extent into AD storage object allocation map, possibly
   overwriting a number of existing extents.

   @param offset - an offset in AD stob name-space;
   @param ext - an extent in the underlying object name-space.

   This function updates extent mapping of AD storage to map an extent in its
   logical name-space, starting with offset to an extent ext in the underlying
   storage object name-space.
 */
static int ad_write_map_ext(struct m0_stob_io *io, struct ad_domain *adom,
			    m0_bindex_t offset, struct m0_emap_cursor *orig,
			    const struct m0_ext *ext)
{
	int                    result;
	int                    rc = 0;
	struct m0_emap_cursor  it;
	/* an extent in the logical name-space to be mapped to ext. */
	struct m0_ext          todo = {
		.e_start = offset,
		.e_end   = offset + m0_ext_length(ext)
	};

	result = m0_emap_lookup(orig->ec_map, orig->ec_cursor.c_tx,
				&orig->ec_seg.ee_pre, offset, &it);
	if (result != 0)
		return result;
	/*
	 * Insert a new segment into extent map, overwriting parts of the map.
	 *
	 * Some existing segments are deleted completely, others are
	 * cut. m0_emap_paste() invokes supplied call-backs to notify the caller
	 * about changes in the map.
	 *
	 * Call-backs are used to free space from overwritten parts of the file.
	 *
	 * Each call-back takes a segment argument, seg. seg->ee_ext is a
	 * logical extent of the segment and seg->ee_val is the starting offset
	 * of the corresponding physical extent.
	 */
	result = m0_emap_paste
		(&it, &todo, ext->e_start,
	 LAMBDA(void, (struct m0_emap_seg *seg) {
			 /* handle extent deletion. */
			 rc = rc ?: seg_free(io, adom, seg,
					     &seg->ee_ext, seg->ee_val);
		 }),
	 LAMBDA(void, (struct m0_emap_seg *seg, struct m0_ext *ext,
		       uint64_t val) {
			/* cut left */
			M0_ASSERT(ext->e_start > seg->ee_ext.e_start);

			seg->ee_val = val;
			rc = rc ?: seg_free(io, adom, seg, ext, val);
		}),
	 LAMBDA(void, (struct m0_emap_seg *seg, struct m0_ext *ext,
		       uint64_t val) {
			/* cut right */
			M0_ASSERT(seg->ee_ext.e_end > ext->e_end);
			if (val < AET_MIN) {
				seg->ee_val = val +
					(ext->e_end - seg->ee_ext.e_start);
				/*
				 * Free physical sub-extent, but only when
				 * sub-extent starts at the left boundary of the
				 * logical extent, because otherwise "cut left"
				 * already freed it.
				 */
				if (ext->e_start == seg->ee_ext.e_start)
					rc = rc ?: seg_free(io, adom,
							    seg, ext, val);
			} else
				seg->ee_val = val;
		}));
	m0_emap_close(&it);
	return result ?: rc;
}

/**
   Updates extent map, inserting newly allocated extents into it.

   @param dst - target extents in AD storage object;
   @param wc - allocated extents.

   Total size of extents in dst and wc is the same, but their boundaries not
   necessary match. Iterate over both sequences at the same time, mapping
   contiguous chunks of AD stob name-space to contiguous chunks of the
   underlying object name-space.

 */
static int ad_write_map(struct m0_stob_io *io, struct ad_domain *adom,
			struct m0_vec_cursor *dst,
			struct m0_emap_caret *map, struct ad_wext_cursor *wc)
{
	int           result;
	m0_bcount_t   frag_size;
	bool          eodst;
	bool          eoext;
	struct m0_ext todo;

	result = 0;
	do {
		m0_bindex_t offset;

		offset    = io->si_stob.iv_index[dst->vc_seg] + dst->vc_offset;
		frag_size = min_check(m0_vec_cursor_step(dst),
				      ad_wext_cursor_step(wc));

		todo.e_start = wc->wc_wext->we_ext.e_start + wc->wc_done;
		todo.e_end   = todo.e_start + frag_size;

		result = ad_write_map_ext(io, adom, offset, map->ct_it, &todo);

		if (result != 0)
			break;

		eodst = m0_vec_cursor_move(dst, frag_size);
		eoext = ad_wext_cursor_move(wc, frag_size);

		M0_ASSERT(eodst == eoext);
	} while (!eodst);
	return result;
}

/**
   Frees wext list.
 */
static void ad_wext_fini(struct ad_write_ext *wext)
{
	struct ad_write_ext *next;

	for (wext = wext->we_next; wext != NULL; wext = next) {
		next = wext->we_next;
		m0_free(wext);
	}
}

/**
   AD write.

   @li allocates space for data to be written (first loop);

   @li calculates number of fragments (ad_write_count());

   @li constructs back IO (ad_write_back_fill());

   @li updates extent map for this AD object with allocated extents
       (ad_write_map()).
 */
static int ad_write_launch(struct m0_stob_io *io, struct ad_domain *adom,
			   struct m0_vec_cursor *src, struct m0_vec_cursor *dst,
			   struct m0_emap_caret *map)
{
	m0_bcount_t           todo;
	uint32_t              frags;
	int                   result;
	struct ad_write_ext   head;
	struct ad_write_ext  *wext;
	struct ad_write_ext  *next;
	struct m0_stob_io    *back;
	struct ad_stob_io    *aio       = io->si_stob_private;
	struct ad_wext_cursor wc;

	M0_PRE(io->si_opcode == SIO_WRITE);

	todo = m0_vec_count(&io->si_user.ov_vec);
	M0_LOG(M0_DEBUG, "op=%d sz=%lu", io->si_opcode, (unsigned long)todo);
	back = &aio->ai_back;
        M0_SET0(&head);
	wext = &head;
	wext->we_next = NULL;
	while (1) {
		m0_bcount_t got;

		result = ad_balloc(adom, io->si_tx, todo, &wext->we_ext);
		if (result != 0)
			break;
		got = m0_ext_length(&wext->we_ext);
		M0_ASSERT(todo >= got);
		todo -= got;
		if (todo > 0) {
			M0_ALLOC_PTR(next);
			if (next != NULL) {
				wext->we_next = next;
				wext = next;
			} else {
				M0_STOB_OOM(AD_WRITE_LAUNCH);
				result = -ENOMEM;
				break;
			}
		} else
			break;
	}

	if (result == 0) {
		ad_wext_cursor_init(&wc, &head);
		frags = ad_write_count(io, src, &wc);

		result = ad_vec_alloc(io->si_obj, back, frags);
		if (result == 0) {
			m0_vec_cursor_init(src, &io->si_user.ov_vec);
			m0_vec_cursor_init(dst, &io->si_stob.iv_vec);
			ad_wext_cursor_init(&wc, &head);

			ad_write_back_fill(io, back, src, &wc);

			m0_vec_cursor_init(src, &io->si_user.ov_vec);
			m0_vec_cursor_init(dst, &io->si_stob.iv_vec);
			ad_wext_cursor_init(&wc, &head);

			result = ad_write_map(io, adom, dst, map, &wc);
		}
	}
	ad_wext_fini(&head);

	return result;
}

/**
   Launch asynchronous IO.

   Call ad_write_launch() or ad_read_launch() to do the bulk of work, then
   launch back IO just constructed.
 */
static int ad_stob_io_launch(struct m0_stob_io *io)
{
	struct ad_domain     *adom    = domain2ad(io->si_obj->so_domain);
	struct ad_stob_io    *aio     = io->si_stob_private;
	struct m0_emap_cursor it;
	struct m0_vec_cursor  src;
	struct m0_vec_cursor  dst;
	struct m0_emap_caret  map;
	struct m0_stob_io    *back    = &aio->ai_back;
	int                   result;
	bool                  wentout = false;

	M0_PRE(adom->ad_setup);
	M0_PRE(io->si_obj->so_domain->sd_type == &m0_ad_stob_type);
	M0_PRE(io->si_stob.iv_vec.v_nr > 0);
	M0_PRE(m0_vec_count(&io->si_user.ov_vec) > 0);

	/* prefix fragments execution mode is not yet supported */
	M0_ASSERT((io->si_flags & SIF_PREFIX) == 0);
	/* only read-write at the moment */
	M0_ASSERT(io->si_opcode == SIO_READ || io->si_opcode == SIO_WRITE);

	result = ad_cursors_init(io, adom, &it, &src, &dst, &map);
	if (result != 0)
		return result;

	back->si_opcode = io->si_opcode;
	back->si_flags  = io->si_flags;

	switch (io->si_opcode) {
	case SIO_READ:
		result = ad_read_launch(io, adom, &src, &dst, &map);
		break;
	case SIO_WRITE:
		result = ad_write_launch(io, adom, &src, &dst, &map);
		break;
	default:
		M0_IMPOSSIBLE("Invalid io type.");
	}
	ad_cursors_fini(&it, &src, &dst, &map);
	if (result == 0) {
		if (back->si_stob.iv_vec.v_nr > 0) {
			/**
			 * Sorts index vecs in incremental order.
			 * @todo : Needs to check performance impact
			 *        of sorting each stobio on ad stob.
			 */
			m0_stob_iovec_sort(back);
			result = m0_stob_io_launch(back, adom->ad_bstore,
						   io->si_tx, io->si_scope);
			wentout = result == 0;
		} else {
			/*
			 * Back IO request was constructed OK, but is empty (all
			 * IO was satisfied from holes). Notify caller about
			 * completion.
			 */
			M0_ASSERT(io->si_opcode == SIO_READ);
			ad_endio(&aio->ai_clink);
		}
	}
	if (!wentout)
		ad_stob_io_release(aio);
	return result;
}

/**
   An implementation of m0_stob_op::sop_block_shift() method.

   AD uses the same block size as its backing store object.
 */
static uint32_t ad_stob_block_shift(const struct m0_stob *stob)
{
	return ad_bshift(domain2ad(stob->so_domain));
}

static bool ad_endio(struct m0_clink *link)
{
	struct ad_stob_io *aio;
	struct m0_stob_io *io;

	aio = container_of(link, struct ad_stob_io, ai_clink);
	io = aio->ai_fore;

	M0_ASSERT(io->si_state == SIS_BUSY);
	M0_ASSERT(aio->ai_back.si_state == SIS_IDLE);

	io->si_rc     = aio->ai_back.si_rc;
	io->si_count += aio->ai_back.si_count;
	io->si_state  = SIS_IDLE;
	ad_stob_io_release(aio);
	m0_chan_broadcast_lock(&io->si_wait);
	return true;
}

static const struct m0_stob_io_op ad_stob_io_op = {
	.sio_launch  = ad_stob_io_launch,
	.sio_fini    = ad_stob_io_fini
};

static const struct m0_stob_type_op ad_stob_type_op = {
	.sto_init          = ad_stob_type_init,
	.sto_fini          = ad_stob_type_fini,
	.sto_domain_locate = ad_stob_type_domain_locate
};

static const struct m0_stob_domain_op ad_stob_domain_op = {
	.sdo_fini        = ad_domain_fini,
	.sdo_stob_find   = ad_domain_stob_find,
	.sdo_tx_make     = ad_domain_tx_make,
};

static const struct m0_stob_op ad_stob_op = {
	.sop_fini         = ad_stob_fini,
	.sop_create       = ad_stob_create,
	.sop_locate       = ad_stob_locate,
	.sop_io_init      = ad_stob_io_init,
	.sop_block_shift  = ad_stob_block_shift
};

struct m0_stob_type m0_ad_stob_type = {
	.st_op    = &ad_stob_type_op,
	.st_name  = "adstob",
	.st_magic = 0x3129A830
};

M0_INTERNAL int m0_ad_stobs_init(void)
{
	return M0_STOB_TYPE_OP(&m0_ad_stob_type, sto_init);
}

M0_INTERNAL void m0_ad_stobs_fini(void)
{
	M0_STOB_TYPE_OP(&m0_ad_stob_type, sto_fini);
}

/** @} end group stobad */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
