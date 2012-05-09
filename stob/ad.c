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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <string.h>                 /* memset */

#include "db/extmap.h"
#include "dtm/dtm.h"                /* c2_dtx */
#include "lib/thread.h"             /* LAMBDA */
#include "lib/memory.h"
#include "lib/arith.h"              /* min_type, min3 */
#include "lib/tlist.h"
#include "lib/misc.h"		    /* C2_SET0 */

#include "stob/stob.h"
#include "stob/ad.h"

/**
   @addtogroup stobad

   <b>Implementation of c2_stob with Allocation Data (AD).</b>

   An object created by ad_domain_stob_find() is kept in a per-domain in-memory
   list, until last reference to it is released and ad_stob_fini() is called.

   @todo this code is identical to one in c2_linux_stob_type and must be factored
   out.

   <b>AD extent map.</b>

   AD uses single c2_emap instance to store logical-physical translations for
   all stobs in the domain.

   For each ad storage object, its identifier (c2_stob_id) is used as a prefix
   to identify an extent map in c2_emap.

   The meaning of a segment ([A, B), V) in an extent map maintained for a
   storage object X depends on the value of V:

   @li if V is less than AET_MIN, the segment represents a mapping from extent
   [A, B) in X's name-space to extent [V, V + B - A) in the underlying object's
   name-space. This is the most usual "allocated extent", specifying where X
   data live;

   @li if V is AET_HOLE, the segment represents a hole [A, B) in X;

   @li if V is AET_NONE, the segment represents an extent that is not part of
   X's address-space. For example, when a new empty file is created, its extent
   map initially consists of a single segment ([0, C2_BINDEX_MAX + 1),
   AET_NONE);

   @li other values of V greater than or equal to AET_MIN could be used for
   special purpose segments in the future.

   @{
 */

static const struct c2_stob_type_op ad_stob_type_op;
static const struct c2_stob_op ad_stob_op;
static const struct c2_stob_domain_op ad_stob_domain_op;
static const struct c2_stob_io_op ad_stob_io_op;

static const struct c2_addb_loc ad_stob_addb_loc = {
	.al_name = "ad-stob"
};

static struct c2_addb_ctx ad_stob_ctx;

#define ADDB_GLOBAL_ADD(name, rc)					\
C2_ADDB_ADD(&ad_stob_ctx, &ad_stob_addb_loc, c2_addb_func_fail, (name), (rc))

#define ADDB_ADD(obj, ev, ...)	\
C2_ADDB_ADD(&(obj)->so_addb, &ad_stob_addb_loc, ev , ## __VA_ARGS__)

#define ADDB_CALL(obj, name, rc)	\
C2_ADDB_ADD(&(obj)->so_addb, &ad_stob_addb_loc, c2_addb_func_fail, (name), (rc))

struct ad_domain {
	struct c2_stob_domain      ad_base;

	char			   ad_path[MAXPATHLEN];

	struct c2_dbenv           *ad_dbenv;
	/**
	   Extent map storing mapping from logical to physical offsets.
	 */
	struct c2_emap             ad_adata;

	/**
	   Set to true in c2_ad_stob_setup(). Used in pre-conditions to
	   guarantee that the domain is fully initialized.
	 */
	bool                       ad_setup;
	/**
	    Backing store storage object, where storage objects of this domain
	    are stored in.
	 */
	struct c2_stob            *ad_bstore;
	/** List of all existing c2_stob's. */
	struct c2_tl               ad_object;
	struct c2_ad_balloc       *ad_ballroom;

};

/**
   AD storage object.

   There is very little of the state besides c2_stob.
 */
struct ad_stob {
	struct c2_stob      as_stob;
	struct c2_tlink     as_linkage;
	uint64_t            as_magix;
};

C2_TL_DESCR_DEFINE(ad, "ad stobs", static, struct ad_stob, as_linkage, as_magix,
		   0xc01101da1fe11c1a /* colloidal felicia */,
		   0x1dea112ed5ea51de /* idealized seaside */);

C2_TL_DEFINE(ad, static, struct ad_stob);

static inline struct ad_stob *stob2ad(struct c2_stob *stob)
{
	return container_of(stob, struct ad_stob, as_stob);
}

static inline struct ad_domain *domain2ad(struct c2_stob_domain *dom)
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
	AET_MIN = C2_BINDEX_MAX - (1ULL << 32),
	/**
	   This value is used to tag an extent that does not belong to the
	   stob's name-space. For example, an extent [X, C2_BINDEX_MAX + 1)
	   would usually be AET_NONE for a file of size X.
	 */
	AET_NONE,
	/**
	   This value is used to tag a hole in the storage object.
	 */
	AET_HOLE
};

/**
   Implementation of c2_stob_type_op::sto_init().
 */
static int ad_stob_type_init(struct c2_stob_type *stype)
{
	c2_stob_type_init(stype);
	return 0;
}

/**
   Implementation of c2_stob_type_op::sto_fini().
 */
static void ad_stob_type_fini(struct c2_stob_type *stype)
{
	c2_stob_type_fini(stype);
}

/**
   Implementation of c2_stob_domain_op::sdo_fini().

   Finalizes all still existing in-memory objects.
 */
static void ad_domain_fini(struct c2_stob_domain *self)
{
	struct ad_domain *adom;

	adom = domain2ad(self);
	if (adom->ad_setup) {
		adom->ad_ballroom->ab_ops->bo_fini(adom->ad_ballroom);
		c2_emap_fini(&adom->ad_adata);
		c2_stob_put(adom->ad_bstore);
	}
	ad_tlist_fini(&adom->ad_object);
	c2_stob_domain_fini(self);
	c2_free(adom);
}

static const char prefix[] = "ad.";

/**
   Implementation of c2_stob_type_op::sto_domain_locate().

   @note the domain returned is not immediately ready for
   use. c2_ad_stob_setup() has to be called against it first.
 */
static int ad_stob_type_domain_locate(struct c2_stob_type *type,
				      const char *domain_name,
				      struct c2_stob_domain **out)
{
	struct ad_domain      *adom;
	struct c2_stob_domain *dom;
	int                    result;

	C2_ASSERT(domain_name != NULL);
	C2_ASSERT(strlen(domain_name) <
		  ARRAY_SIZE(adom->ad_path) - ARRAY_SIZE(prefix));

	C2_ALLOC_PTR(adom);
	if (adom != NULL) {
		adom->ad_setup = false;
		ad_tlist_init(&adom->ad_object);
		dom = &adom->ad_base;
		dom->sd_ops = &ad_stob_domain_op;
		c2_stob_domain_init(dom, type);
		sprintf(adom->ad_path, "%s%s", prefix, domain_name);
		dom->sd_name = adom->ad_path + ARRAY_SIZE(prefix) - 1;
		*out = dom;
		result = 0;
	} else {
		C2_ADDB_ADD(&type->st_addb, &ad_stob_addb_loc, c2_addb_oom);
		result = -ENOMEM;
	}
	return result;
}

int c2_ad_stob_setup(struct c2_stob_domain *dom, struct c2_dbenv *dbenv,
		     struct c2_stob *bstore, struct c2_ad_balloc *ballroom,
		     c2_bcount_t container_size, c2_bcount_t bshift,
		     c2_bcount_t blocks_per_group, c2_bcount_t res_groups)
{
	int			 result;
	c2_bcount_t		 groupsize;
	c2_bcount_t		 blocksize;
	struct ad_domain	*adom;

	adom = domain2ad(dom);

	C2_PRE(dom->sd_ops == &ad_stob_domain_op);
	C2_PRE(!adom->ad_setup);
	C2_PRE(bstore->so_state == CSS_EXISTS);

	blocksize = 1 << bshift;
	groupsize = blocks_per_group * blocksize;

	C2_PRE(groupsize > blocksize);
	C2_PRE(container_size > groupsize);
	C2_PRE(container_size / groupsize > res_groups);

	result = ballroom->ab_ops->bo_init(ballroom, dbenv, bshift,
					   container_size, blocks_per_group,
					   res_groups);
	if (result == 0) {
		adom->ad_dbenv    = dbenv;
		adom->ad_bstore   = bstore;
		adom->ad_ballroom = ballroom;
		adom->ad_setup    = true;
		c2_stob_get(adom->ad_bstore);
		result = c2_emap_init(&adom->ad_adata, dbenv, adom->ad_path);
	}
	return result;
}

/**
   Searches for the object with a given identifier in the domain object list.

   This function is used by ad_domain_stob_find() to check whether in-memory
   representation of an object already exists.
 */
static struct ad_stob *ad_domain_lookup(struct ad_domain *adom,
					const struct c2_stob_id *id)
{
	struct ad_stob *obj;
	bool            found;

	C2_PRE(adom->ad_setup);

	found = false;
	c2_tl_for(ad, &adom->ad_object, obj) {
		if (c2_stob_id_eq(id, &obj->as_stob.so_id)) {
			c2_stob_get(&obj->as_stob);
			found = true;
			break;
		}
	} c2_tl_endfor;
	return found ? obj : NULL;
}

/**
   Implementation of c2_stob_domain_op::sdo_stob_find().

   Returns an in-memory representation of the object with a given identifier.
 */
static int ad_domain_stob_find(struct c2_stob_domain *dom,
				  const struct c2_stob_id *id,
				  struct c2_stob **out)
{
	struct ad_domain *adom;
	struct ad_stob   *astob;
	struct ad_stob   *ghost;
	struct c2_stob   *stob;
	int               result;

	adom = domain2ad(dom);

	C2_PRE(adom->ad_setup);

	result = 0;
	c2_rwlock_read_lock(&dom->sd_guard);
	astob = ad_domain_lookup(adom, id);
	c2_rwlock_read_unlock(&dom->sd_guard);

	if (astob == NULL) {
		C2_ALLOC_PTR(astob);
		if (astob != NULL) {
			c2_rwlock_write_lock(&dom->sd_guard);
			ghost = ad_domain_lookup(adom, id);
			if (ghost == NULL) {
				stob = &astob->as_stob;
				stob->so_op = &ad_stob_op;
				c2_stob_init(stob, id, dom);
				ad_tlink_init_at(astob, &adom->ad_object);
			} else {
				c2_free(astob);
				astob = ghost;
				c2_stob_get(&astob->as_stob);
			}
			c2_rwlock_write_unlock(&dom->sd_guard);
		} else {
			C2_ADDB_ADD(&dom->sd_addb,
				    &ad_stob_addb_loc, c2_addb_oom);
			result = -ENOMEM;
		}
	}
	if (result == 0)
		*out = &astob->as_stob;
	return result;
}

/**
   Implementation of c2_stob_domain_op::sdo_tx_make().
 */
static int ad_domain_tx_make(struct c2_stob_domain *dom, struct c2_dtx *tx)
{
	struct ad_domain *adom;

	adom = domain2ad(dom);
	C2_PRE(adom->ad_setup);
	return c2_dtx_open(tx, adom->ad_dbenv);
}

/**
   Implementation of c2_stob_op::sop_fini().

   Closes the object's file descriptor.
 */
static void ad_stob_fini(struct c2_stob *stob)
{
	struct ad_stob *astob;

	astob = stob2ad(stob);
	ad_tlink_del_fini(astob);
	c2_stob_fini(&astob->as_stob);
	c2_free(astob);
}

/**
   Implementation of c2_stob_op::sop_create().
 */
static int ad_stob_create(struct c2_stob *obj, struct c2_dtx *tx)
{
	struct ad_domain *adom;

	adom = domain2ad(obj->so_domain);
	C2_PRE(adom->ad_setup);
	return c2_emap_obj_insert(&adom->ad_adata, &tx->tx_dbtx,
				  &obj->so_id.si_bits, AET_NONE);
}

static int ad_cursor(struct ad_domain *adom, struct c2_stob *obj,
		     uint64_t offset, struct c2_dtx *tx,
		     struct c2_emap_cursor *it)
{
	int result;

	result = c2_emap_lookup(&adom->ad_adata, &tx->tx_dbtx,
				&obj->so_id.si_bits, offset, it);
	if (result != 0 && result != -ENOENT && result != -ESRCH)
		ADDB_CALL(obj, "c2_emap_lookup", result);
	return result;
}

/**
   Implementation of c2_stob_op::sop_locate().
 */
static int ad_stob_locate(struct c2_stob *obj, struct c2_dtx *tx)
{
	struct c2_emap_cursor it;
	int                   result;
	struct ad_domain     *adom;

	adom = domain2ad(obj->so_domain);
	C2_PRE(adom->ad_setup);
	result = ad_cursor(adom, obj, 0, tx, &it);
	if (result == 0)
		c2_emap_close(&it);
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

   Storage object IO (c2_stob_io) is a request to transfer data from the user
   address-space to the storage object name-space. The source and target
   locations are specified by sequences of intervals: c2_stob_io::si_user for
   locations in source address space, which are user buffers and
   c2_stob_io::si_stob for locations in the target name-space, which are extents
   in the storage object.

   AD IO implementation takes a c2_stob_io against an AD storage object and
   constructs a "back" c2_stob_io against the underlying storage object.

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
   allocator (c2_ad_balloc), specifies where newly written data should go.

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

   @li c2_vec_cursor for c2_vec-derived sequences (list of user buffers and list
   of target extents in a storage object);

   @li c2_emap_caret for an extent map;

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
	struct c2_stob_io *ai_fore;
	/** Back IO request */
	struct c2_stob_io  ai_back;
	/** Clink registered with back-io completion channel to intercept back
	    IO completion notification. */
	struct c2_clink    ai_clink;
};

static bool ad_endio(struct c2_clink *link);

/**
   Helper function to allocate a given number of blocks in the underlying
   storage object.
 */
static int c2_ad_balloc(struct ad_domain *adom, struct c2_dtx *tx,
			c2_bcount_t count, struct c2_ext *out)
{
	C2_PRE(adom->ad_setup);
	return adom->ad_ballroom->ab_ops->bo_alloc(adom->ad_ballroom,
						   tx, count, out);
}

/**
   Helper function to free a given block extent in the underlying storage
   object.
 */
static int ad_bfree(struct ad_domain *adom, struct c2_dtx *tx,
		    struct c2_ext *ext)
{
	C2_PRE(adom->ad_setup);
	return adom->ad_ballroom->ab_ops->bo_free(adom->ad_ballroom, tx, ext);
}

/**
   Implementation of c2_stob_op::sop_io_init().

   Allocates private IO state structure.
 */
int ad_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io)
{
	struct ad_stob_io *aio;
	int                result;

	C2_PRE(io->si_state == SIS_IDLE);

	C2_ALLOC_PTR(aio);
	if (aio != NULL) {
		io->si_stob_private = aio;
		io->si_op = &ad_stob_io_op;
		aio->ai_fore = io;
		c2_stob_io_init(&aio->ai_back);
		c2_clink_init(&aio->ai_clink, &ad_endio);
		c2_clink_add(&aio->ai_back.si_wait, &aio->ai_clink);
		result = 0;
	} else {
		ADDB_ADD(stob, c2_addb_oom);
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
	struct c2_stob_io *back = &aio->ai_back;

	C2_ASSERT(back->si_stob.iv_vec.v_count ==
		  back->si_user.ov_vec.v_count);

	c2_free(back->si_user.ov_vec.v_count);
	back->si_user.ov_vec.v_count = NULL;
	back->si_stob.iv_vec.v_count = NULL;

	c2_free(back->si_user.ov_buf);
	back->si_user.ov_buf = NULL;

	c2_free(back->si_stob.iv_index);
	back->si_stob.iv_index = NULL;

	back->si_obj = NULL;
}

/**
   Implementation of c2_stob_io_op::sio_fini().
 */
static void ad_stob_io_fini(struct c2_stob_io *io)
{
	struct ad_stob_io *aio = io->si_stob_private;

	ad_stob_io_release(aio);
	c2_clink_del(&aio->ai_clink);
	c2_clink_fini(&aio->ai_clink);
	c2_stob_io_fini(&aio->ai_back);
	c2_free(aio);
}

/**
   Initializes cursors at the beginning of a pass.
 */
static int ad_cursors_init(struct c2_stob_io *io, struct ad_domain *adom,
			   struct c2_emap_cursor *it,
			   struct c2_vec_cursor *src, struct c2_vec_cursor *dst,
			   struct c2_emap_caret *map)
{
	int result;

	result = ad_cursor(adom, io->si_obj, io->si_stob.iv_index[0],
			   io->si_tx, it);
	if (result == 0) {
		c2_vec_cursor_init(src, &io->si_user.ov_vec);
		c2_vec_cursor_init(dst, &io->si_stob.iv_vec);
		c2_emap_caret_init(map, it, io->si_stob.iv_index[0]);
	}
	return result;
}

/**
   Finalizes the cursors that need finalisation.
 */
static void ad_cursors_fini(struct c2_emap_cursor *it,
			    struct c2_vec_cursor *src,
			    struct c2_vec_cursor *dst,
			    struct c2_emap_caret *map)
{
	c2_emap_caret_fini(map);
	c2_emap_close(it);
}

/**
   Allocates back IO buffers after number of fragments has been calculated.

   @see ad_stob_io_release()
 */
static int ad_vec_alloc(struct c2_stob *obj,
			struct c2_stob_io *back, uint32_t frags)
{
	c2_bcount_t *counts;
	int          result;

	C2_ASSERT(back->si_user.ov_vec.v_count == NULL);

	result = 0;
	if (frags > 0) {
		C2_ALLOC_ARR(counts, frags);
		back->si_user.ov_vec.v_count = counts;
		back->si_stob.iv_vec.v_count = counts;
		C2_ALLOC_ARR(back->si_user.ov_buf, frags);
		C2_ALLOC_ARR(back->si_stob.iv_index, frags);

		back->si_user.ov_vec.v_nr = frags;
		back->si_stob.iv_vec.v_nr = frags;

		if (counts == NULL || back->si_user.ov_buf == NULL ||
		    back->si_stob.iv_index == NULL) {
			ADDB_ADD(obj, c2_addb_oom);
			result = -ENOMEM;
		}
	}
	return result;
}

/**
   Block size shift for objects of this domain.
 */
static uint32_t ad_bshift(const struct ad_domain *adom)
{
	C2_PRE(adom->ad_setup);
	return adom->ad_bstore->so_op->sop_block_shift(adom->ad_bstore);
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
static int ad_read_launch(struct c2_stob_io *io, struct ad_domain *adom,
			  struct c2_vec_cursor *src, struct c2_vec_cursor *dst,
			  struct c2_emap_caret *map)
{
	struct c2_emap_cursor *it;
	struct c2_emap_seg    *seg;
	struct c2_stob_io     *back;
	struct ad_stob_io     *aio       = io->si_stob_private;
	uint32_t               frags;
	uint32_t               frags_not_empty;
	uint32_t               bshift    = ad_bshift(adom);
	c2_bcount_t            frag_size; /* measured in blocks */
	c2_bindex_t            off;       /* measured in blocks */
	int                    result;
	int                    i;
	int                    idx;
	bool                   eosrc;
	bool                   eodst;
	int                    eomap;

	C2_PRE(io->si_opcode == SIO_READ);

	it   = map->ct_it;
	seg  = c2_emap_seg_get(it);
	back = &aio->ai_back;

	frags = frags_not_empty = 0;
	do {
		off = io->si_stob.iv_index[dst->vc_seg] + dst->vc_offset;

		/*
		 * The next fragment starts at the offset off and the extents
		 * map has to be positioned at this offset. There are two ways
		 * to do this:
		 *
		 * * lookup an extent containing off (c2_emap_lookup()), or
		 *
		 * * iterate from the current position (c2_emap_caret_move())
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
		C2_ASSERT(off >= map->ct_index);
		eomap = c2_emap_caret_move(map, off - map->ct_index);
		if (eomap < 0) {
			ADDB_CALL(io->si_obj, "caret_move:shift", eomap);
			return eomap;
		}
		C2_ASSERT(!eomap);
		C2_ASSERT(c2_ext_is_in(&seg->ee_ext, off));

		frag_size = min3(c2_vec_cursor_step(src),
				 c2_vec_cursor_step(dst),
				 c2_emap_caret_step(map));
		C2_ASSERT(frag_size > 0);
		if (frag_size > (size_t)~0ULL) {
			ADDB_CALL(io->si_obj, "frag_overflow", frag_size);
			return -EOVERFLOW;
		}

		frags++;
		if (seg->ee_val < AET_MIN)
			frags_not_empty++;

		eosrc = c2_vec_cursor_move(src, frag_size);
		eodst = c2_vec_cursor_move(dst, frag_size);
		eomap = c2_emap_caret_move(map, frag_size);
		if (eomap < 0) {
			ADDB_CALL(io->si_obj, "caret_move:frag", eomap);
			return eomap;
		}

		C2_ASSERT(eosrc == eodst);
		C2_ASSERT(!eomap);
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
		c2_bindex_t  off;

		buf = io->si_user.ov_buf[src->vc_seg] + src->vc_offset;
		off = io->si_stob.iv_index[dst->vc_seg] + dst->vc_offset;

		C2_ASSERT(off >= map->ct_index);
		eomap = c2_emap_caret_move(map, off - map->ct_index);
		if (eomap < 0) {
			ADDB_CALL(io->si_obj, "caret_move:shift", eomap);
			return eomap;
		}
		C2_ASSERT(!eomap);
		C2_ASSERT(c2_ext_is_in(&seg->ee_ext, off));

		frag_size = min3(c2_vec_cursor_step(src),
				 c2_vec_cursor_step(dst),
				 c2_emap_caret_step(map));

		if (seg->ee_val == AET_NONE || seg->ee_val == AET_HOLE) {
			/*
			 * Read of a hole or unallocated space (beyond
			 * end of the file).
			 */
			memset(c2_stob_addr_open(buf, bshift),
			       0, frag_size << bshift);
			io->si_count += frag_size;
		} else {
			C2_ASSERT(seg->ee_val < AET_MIN);

			back->si_user.ov_vec.v_count[idx] = frag_size;
			back->si_user.ov_buf[idx] = buf;

			back->si_stob.iv_index[idx] = seg->ee_val +
				(off - seg->ee_ext.e_start);
			idx++;
		}
		c2_vec_cursor_move(src, frag_size);
		c2_vec_cursor_move(dst, frag_size);
		result = c2_emap_caret_move(map, frag_size);
		if (result < 0) {
			ADDB_CALL(io->si_obj, "caret_move:io", eomap);
			break;
		}
		C2_ASSERT(result == 0);
	}
	C2_ASSERT(ergo(result == 0, idx == frags_not_empty));
	return result;
}

/**
   A linked list of allocated extents.
 */
struct ad_write_ext {
	struct c2_ext        we_ext;
	struct ad_write_ext *we_next;
};

/**
   A cursor over allocated extents.
 */
struct ad_wext_cursor {
	const struct ad_write_ext *wc_wext;
	c2_bcount_t                wc_done;
};

static void ad_wext_cursor_init(struct ad_wext_cursor *wc,
				struct ad_write_ext *wext)
{
	wc->wc_wext = wext;
	wc->wc_done = 0;
}

static c2_bcount_t ad_wext_cursor_step(struct ad_wext_cursor *wc)
{
	C2_PRE(wc->wc_wext != NULL);
	C2_PRE(wc->wc_done < c2_ext_length(&wc->wc_wext->we_ext));

	return c2_ext_length(&wc->wc_wext->we_ext) - wc->wc_done;
}

static bool ad_wext_cursor_move(struct ad_wext_cursor *wc, c2_bcount_t count)
{
	while (count > 0 && wc->wc_wext != NULL) {
		c2_bcount_t step;

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
static uint32_t ad_write_count(struct c2_stob_io *io, struct c2_vec_cursor *src,
			       struct ad_wext_cursor *wc)
{
	uint32_t               frags;
	c2_bcount_t            frag_size;
	bool                   eosrc;
	bool                   eoext;

	frags = 0;

	do {
		frag_size = min_check(c2_vec_cursor_step(src),
				      ad_wext_cursor_step(wc));
		C2_ASSERT(frag_size > 0);
		C2_ASSERT(frag_size <= (size_t)~0ULL);

		eosrc = c2_vec_cursor_move(src, frag_size);
		eoext = ad_wext_cursor_move(wc, frag_size);

		C2_ASSERT(ergo(eosrc, eoext));
		++frags;
	} while (!eoext);
	return frags;
}

/**
   Fills back IO request with information about fragments.
 */
static void ad_write_back_fill(struct c2_stob_io *io, struct c2_stob_io *back,
			       struct c2_vec_cursor *src,
			       struct ad_wext_cursor *wc)
{
	c2_bcount_t    frag_size;
	uint32_t       idx;
	bool           eosrc;
	bool           eoext;

	idx = 0;
	do {
		void *buf;

		frag_size = min_check(c2_vec_cursor_step(src),
				      ad_wext_cursor_step(wc));

		buf = io->si_user.ov_buf[src->vc_seg] + src->vc_offset;

		back->si_user.ov_vec.v_count[idx] = frag_size;
		back->si_user.ov_buf[idx] = buf;

		back->si_stob.iv_index[idx] =
			wc->wc_wext->we_ext.e_start + wc->wc_done;

		eosrc = c2_vec_cursor_move(src, frag_size);
		eoext = ad_wext_cursor_move(wc, frag_size);
		idx++;
		C2_ASSERT(eosrc == eoext);
	} while (!eoext);
	C2_ASSERT(idx == back->si_stob.iv_vec.v_nr);
}

/**
 * Helper function used by ad_write_map_ext() to free sub-segment "ext" from
 * allocated segment "seg".
 */
static int seg_free(struct c2_stob_io *io, struct ad_domain *adom,
		    const struct c2_emap_seg *seg, const struct c2_ext *ext,
		    uint64_t val)
{
	c2_bcount_t   delta = ext->e_start - seg->ee_ext.e_start;
	struct c2_ext tocut = {
		.e_start = val + delta,
		.e_end   = val + delta + c2_ext_length(ext)
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
static int ad_write_map_ext(struct c2_stob_io *io, struct ad_domain *adom,
			    c2_bindex_t offset, struct c2_emap_cursor *orig,
			    const struct c2_ext *ext)
{
	int                    result;
	int                    rc = 0;
	struct c2_emap_cursor  it;
	/* an extent in the logical name-space to be mapped to ext. */
	struct c2_ext          todo = {
		.e_start = offset,
		.e_end   = offset + c2_ext_length(ext)
	};

	result = c2_emap_lookup(orig->ec_map, orig->ec_cursor.c_tx,
				&orig->ec_seg.ee_pre, offset, &it);
	if (result != 0)
		return result;
	/*
	 * Insert a new segment into extent map, overwriting parts of the map.
	 *
	 * Some existing segments are deleted completely, others are
	 * cut. c2_emap_paste() invokes supplied call-backs to notify the caller
	 * about changes in the map.
	 *
	 * Call-backs are used to free space from overwritten parts of the file.
	 *
	 * Each call-back takes a segment argument, seg. seg->ee_ext is a
	 * logical extent of the segment and seg->ee_val is the starting offset
	 * of the corresponding physical extent.
	 */
	result = c2_emap_paste
		(&it, &todo, ext->e_start,
	 LAMBDA(void, (struct c2_emap_seg *seg) {
			 /* handle extent deletion. */
			 rc = rc ?: seg_free(io, adom, seg,
					     &seg->ee_ext, seg->ee_val);
		 }),
	 LAMBDA(void, (struct c2_emap_seg *seg, struct c2_ext *ext,
		       uint64_t val) {
			/* cut left */
			C2_ASSERT(ext->e_start > seg->ee_ext.e_start);

			seg->ee_val = val;
			rc = rc ?: seg_free(io, adom, seg, ext, val);
		}),
	 LAMBDA(void, (struct c2_emap_seg *seg, struct c2_ext *ext,
		       uint64_t val) {
			/* cut right */
			C2_ASSERT(seg->ee_ext.e_end > ext->e_end);
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
	c2_emap_close(&it);
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
static int ad_write_map(struct c2_stob_io *io, struct ad_domain *adom,
			struct c2_vec_cursor *dst,
			struct c2_emap_caret *map, struct ad_wext_cursor *wc)
{
	int           result;
	c2_bcount_t   frag_size;
	bool          eodst;
	bool          eoext;
	struct c2_ext todo;

	result = 0;
	do {
		c2_bindex_t offset;

		offset    = io->si_stob.iv_index[dst->vc_seg] + dst->vc_offset;
		frag_size = min_check(c2_vec_cursor_step(dst),
				      ad_wext_cursor_step(wc));

		todo.e_start = wc->wc_wext->we_ext.e_start + wc->wc_done;
		todo.e_end   = todo.e_start + frag_size;

		result = ad_write_map_ext(io, adom, offset, map->ct_it, &todo);

		if (result != 0)
			break;

		eodst = c2_vec_cursor_move(dst, frag_size);
		eoext = ad_wext_cursor_move(wc, frag_size);

		C2_ASSERT(eodst == eoext);
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
		c2_free(wext);
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
static int ad_write_launch(struct c2_stob_io *io, struct ad_domain *adom,
			   struct c2_vec_cursor *src, struct c2_vec_cursor *dst,
			   struct c2_emap_caret *map)
{
	c2_bcount_t           todo;
	uint32_t              frags;
	int                   result;
	struct ad_write_ext   head;
	struct ad_write_ext  *wext;
	struct ad_write_ext  *next;
	struct c2_stob_io    *back;
	struct ad_stob_io    *aio       = io->si_stob_private;
	struct ad_wext_cursor wc;

	C2_PRE(io->si_opcode == SIO_WRITE);

	todo = c2_vec_count(&io->si_user.ov_vec);
	back = &aio->ai_back;
        C2_SET0(&head);
	wext = &head;
	wext->we_next = NULL;
	while (1) {
		c2_bcount_t got;

		result = c2_ad_balloc(adom, io->si_tx, todo, &wext->we_ext);
		if (result != 0)
			break;
		got = c2_ext_length(&wext->we_ext);
		C2_ASSERT(todo >= got);
		todo -= got;
		if (todo > 0) {
			C2_ALLOC_PTR(next);
			if (next != NULL) {
				wext->we_next = next;
				wext = next;
			} else {
				ADDB_ADD(io->si_obj, c2_addb_oom);
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
			c2_vec_cursor_init(src, &io->si_user.ov_vec);
			c2_vec_cursor_init(dst, &io->si_stob.iv_vec);
			ad_wext_cursor_init(&wc, &head);

			ad_write_back_fill(io, back, src, &wc);

			c2_vec_cursor_init(src, &io->si_user.ov_vec);
			c2_vec_cursor_init(dst, &io->si_stob.iv_vec);
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
static int ad_stob_io_launch(struct c2_stob_io *io)
{
	struct ad_domain     *adom    = domain2ad(io->si_obj->so_domain);
	struct ad_stob_io    *aio     = io->si_stob_private;
	struct c2_emap_cursor it;
	struct c2_vec_cursor  src;
	struct c2_vec_cursor  dst;
	struct c2_emap_caret  map;
	struct c2_stob_io    *back    = &aio->ai_back;
	int                   result;
	bool                  wentout = false;

	C2_PRE(adom->ad_setup);
	C2_PRE(io->si_obj->so_domain->sd_type == &c2_ad_stob_type);
	C2_PRE(io->si_stob.iv_vec.v_nr > 0);
	C2_PRE(c2_vec_count(&io->si_user.ov_vec) > 0);

	/* prefix fragments execution mode is not yet supported */
	C2_ASSERT((io->si_flags & SIF_PREFIX) == 0);
	/* only read-write at the moment */
	C2_ASSERT(io->si_opcode == SIO_READ || io->si_opcode == SIO_WRITE);

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
		C2_IMPOSSIBLE("Invalid io type.");
	}
	ad_cursors_fini(&it, &src, &dst, &map);
	if (result == 0) {
		if (back->si_stob.iv_vec.v_nr > 0) {
			/**
			 * Sorts index vecs in incremental order.
			 * @todo : Needs to check performance impact
			 *        of sorting each stobio on ad stob.
			 */
			c2_stob_iovec_sort(back);
			result = c2_stob_io_launch(back, adom->ad_bstore,
						   io->si_tx, io->si_scope);
			wentout = result == 0;
		} else {
			/*
			 * Back IO request was constructed OK, but is empty (all
			 * IO was satisfied from holes). Notify caller about
			 * completion.
			 */
			C2_ASSERT(io->si_opcode == SIO_READ);
			ad_endio(&aio->ai_clink);
		}
	}
	if (!wentout)
		ad_stob_io_release(aio);
	return result;
}

/**
   An implementation of c2_stob_op::sop_lock() method.
 */
static void ad_stob_io_lock(struct c2_stob *stob)
{
}

/**
   An implementation of c2_stob_op::sop_unlock() method.
 */
static void ad_stob_io_unlock(struct c2_stob *stob)
{
}

/**
   An implementation of c2_stob_op::sop_is_locked() method.
 */
static bool ad_stob_io_is_locked(const struct c2_stob *stob)
{
	return true;
}

/**
   An implementation of c2_stob_op::sop_block_shift() method.

   AD uses the same block size as its backing store object.
 */
static uint32_t ad_stob_block_shift(const struct c2_stob *stob)
{
	return ad_bshift(domain2ad(stob->so_domain));
}

/**
   An implementation of c2_stob_domain_op::sdo_block_shift() method.
 */
static uint32_t ad_stob_domain_block_shift(struct c2_stob_domain *sd)
{
	return ad_bshift(domain2ad(sd));
}

static bool ad_endio(struct c2_clink *link)
{
	struct ad_stob_io *aio;
	struct c2_stob_io *io;

	aio = container_of(link, struct ad_stob_io, ai_clink);
	io = aio->ai_fore;

	C2_ASSERT(io->si_state == SIS_BUSY);
	C2_ASSERT(aio->ai_back.si_state == SIS_IDLE);

	io->si_rc     = aio->ai_back.si_rc;
	io->si_count += aio->ai_back.si_count;
	io->si_state  = SIS_IDLE;
	ad_stob_io_release(aio);
	c2_chan_broadcast(&io->si_wait);
	return true;
}

static const struct c2_stob_io_op ad_stob_io_op = {
	.sio_launch  = ad_stob_io_launch,
	.sio_fini    = ad_stob_io_fini
};

static const struct c2_stob_type_op ad_stob_type_op = {
	.sto_init          = ad_stob_type_init,
	.sto_fini          = ad_stob_type_fini,
	.sto_domain_locate = ad_stob_type_domain_locate
};

static const struct c2_stob_domain_op ad_stob_domain_op = {
	.sdo_fini        = ad_domain_fini,
	.sdo_stob_find   = ad_domain_stob_find,
	.sdo_tx_make     = ad_domain_tx_make,
	.sdo_block_shift = ad_stob_domain_block_shift
};

static const struct c2_stob_op ad_stob_op = {
	.sop_fini         = ad_stob_fini,
	.sop_create       = ad_stob_create,
	.sop_locate       = ad_stob_locate,
	.sop_io_init      = ad_stob_io_init,
	.sop_io_lock      = ad_stob_io_lock,
	.sop_io_unlock    = ad_stob_io_unlock,
	.sop_io_is_locked = ad_stob_io_is_locked,
	.sop_block_shift  = ad_stob_block_shift
};

struct c2_stob_type c2_ad_stob_type = {
	.st_op    = &ad_stob_type_op,
	.st_name  = "adstob",
	.st_magic = 0x3129A830
};

const struct c2_addb_ctx_type ad_stob_ctx_type = {
	.act_name = "adstob"
};

int c2_ad_stobs_init(void)
{
	c2_addb_ctx_init(&ad_stob_ctx, &ad_stob_ctx_type,
			 &c2_addb_global_ctx);
	return C2_STOB_TYPE_OP(&c2_ad_stob_type, sto_init);
}

void c2_ad_stobs_fini(void)
{
	C2_STOB_TYPE_OP(&c2_ad_stob_type, sto_fini);
	c2_addb_ctx_fini(&ad_stob_ctx);
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
