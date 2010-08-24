#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <string.h>                 /* memset */

#include "lib/thread.h"             /* LAMBDA */
#include "lib/memory.h"
#include "lib/arith.h"              /* min_type, min3 */
#include "stob/ad.h"

/**
   @addtogroup stobad

   <b>Implementation of c2_stob with Allocation Data (AD).</b>

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

	struct c2_dbenv           *ad_dbenv;
	struct c2_emap             ad_adata;

	bool                       ad_setup;
	/**
	    Backing store storage object, where storage objects of this domain
	    are stored in.
	 */
	struct c2_stob            *ad_bstore;
	/** List of all existing c2_stob's. */
	struct c2_list             ad_object;
	struct c2_space_allocator *ad_balloc;

};

struct ad_stob {
	struct c2_stob      as_stob;
	struct c2_list_link as_linkage;
};

static inline struct ad_stob *stob2ad(struct c2_stob *stob)
{
	return container_of(stob, struct ad_stob, as_stob);
}

static inline struct ad_domain *domain2ad(struct c2_stob_domain *dom)
{
	return container_of(dom, struct ad_domain, ad_base);
}

enum ad_stob_allocation_extent_type {
	AET_MIN = C2_BINDEX_MAX - (1ULL << 32),
	AET_NONE,
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
	c2_emap_fini(&adom->ad_adata);
	c2_stob_put(adom->ad_bstore);
	c2_stob_domain_fini(self);
	c2_free(adom);
}

/**
   Implementation of c2_stob_type_op::sto_domain_locate().

   Initialises data-base.
 */
static int ad_stob_type_domain_locate(struct c2_stob_type *type, 
				      const char *domain_name,
				      struct c2_stob_domain **out)
{
	struct ad_domain      *adom;
	struct c2_stob_domain *dom;
	int                    result;

	C2_ALLOC_PTR(adom);
	if (adom != NULL) {
		adom->ad_setup = false;
		c2_list_init(&adom->ad_object);
		dom = &adom->ad_base;
		dom->sd_ops = &ad_stob_domain_op;
		c2_stob_domain_init(dom, type);
		*out = dom;
		result = 0;
	} else {
		C2_ADDB_ADD(&type->st_addb, &ad_stob_addb_loc, c2_addb_oom);
		result = -ENOMEM;
	}
	return result;
}

int ad_setup(struct c2_stob_domain *dom, struct c2_dbenv *dbenv,
	     struct c2_stob *bstore, struct c2_space_allocator *balloc)
{
	struct ad_domain *adom;

	adom = domain2ad(dom);

	C2_PRE(dom->sd_ops == &ad_stob_domain_op);
	C2_PRE(!adom->ad_setup);
	C2_PRE(bstore->so_state == CSS_EXISTS);

	adom->ad_dbenv  = dbenv;
	adom->ad_bstore = bstore;
	adom->ad_balloc = balloc;
	adom->ad_setup = true;
	c2_stob_get(adom->ad_bstore);
	return c2_emap_init(&adom->ad_adata, dbenv, "ad");
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
	c2_list_for_each_entry(&adom->ad_object, obj, 
			       struct ad_stob, as_linkage) {
		if (c2_stob_id_eq(id, &obj->as_stob.so_id)) {
			c2_stob_get(&obj->as_stob);
			found = true;
			break;
		}
	}
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
				c2_list_add(&adom->ad_object, 
					    &astob->as_linkage);
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
	return c2_db_tx_init(&tx->tx_dbtx, adom->ad_dbenv, 0);
}

/**
   Implementation of c2_stob_op::sop_fini().

   Closes the object's file descriptor.
 */
static void ad_stob_fini(struct c2_stob *stob)
{
	struct ad_stob *astob;

	astob = stob2ad(stob);
	c2_list_del(&astob->as_linkage);
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
	if (result != 0)
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
	return result;
}

/*****************************************************************************/
/*                                    IO code                                */
/*****************************************************************************/

struct ad_stob_io {
	struct c2_stob_io *ai_fore;
	struct c2_stob_io  ai_back;
	struct c2_clink    ai_clink;
};

static void ad_endio (struct c2_clink *link);
static int  ad_balloc(struct c2_stob_io *io, c2_bcount_t length, 
		      struct c2_ext *out);

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

static void ad_stob_io_release(struct ad_stob_io *aio)
{
	struct c2_stob_io *back = &aio->ai_back;

	c2_free(back->si_user.div_vec.ov_vec.v_count);
	back->si_user.div_vec.ov_vec.v_count = NULL;

	c2_free(back->si_user.div_vec.ov_buf);
	back->si_user.div_vec.ov_buf = NULL;

	c2_free(back->si_stob.ov_index);
	back->si_stob.ov_index = NULL;

	back->si_obj = NULL;
}

static void ad_stob_io_fini(struct c2_stob_io *io)
{
	struct ad_stob_io *aio = io->si_stob_private;

	ad_stob_io_release(aio);
	c2_clink_del(&aio->ai_clink);
	c2_clink_fini(&aio->ai_clink);
	c2_stob_io_fini(&aio->ai_back);
	c2_free(aio);
}

static int ad_cursors_init(struct c2_stob_io *io, struct ad_domain *adom,
			   struct c2_emap_cursor *it,
			   struct c2_vec_cursor *src, struct c2_vec_cursor *dst,
			   struct c2_emap_caret *map)
{
	int result;

	result = ad_cursor(adom, io->si_obj, io->si_stob.ov_index[0], 
			   io->si_tx, it);
	if (result == 0) {
		c2_vec_cursor_init(src, &io->si_user.div_vec.ov_vec);
		c2_vec_cursor_init(dst, &io->si_stob.ov_vec);
		c2_emap_caret_init(map, it, io->si_stob.ov_index[0]);
	}
	return result;
}

static void ad_cursors_fini(struct c2_emap_cursor *it,
			    struct c2_vec_cursor *src, 
			    struct c2_vec_cursor *dst,
			    struct c2_emap_caret *map)
{
	c2_emap_caret_fini(map);
	c2_emap_close(it);
}

static int ad_vec_alloc(struct c2_stob *obj, 
			struct c2_stob_io *back, uint32_t frags)
{
	c2_bcount_t *counts;
	int          result;

	C2_ASSERT(back->si_user.div_vec.ov_vec.v_count == NULL);

	result = 0;
	if (frags > 0) {
		C2_ALLOC_ARR(counts, frags);
		back->si_user.div_vec.ov_vec.v_count = counts;
		back->si_stob.ov_vec.v_count = counts;
		C2_ALLOC_ARR(back->si_user.div_vec.ov_buf, frags);
		C2_ALLOC_ARR(back->si_stob.ov_index, frags);

		back->si_user.div_vec.ov_vec.v_nr = frags;
		back->si_stob.ov_vec.v_nr = frags;

		if (counts == NULL || back->si_user.div_vec.ov_buf == NULL ||
		    back->si_stob.ov_index == NULL) {
			ADDB_ADD(obj, c2_addb_oom);
			result = -ENOMEM;
		}
	}
	return result;
}

/**
   @note assumes that allocation data can not change concurrently.
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
	c2_bcount_t            frag_size;
	int                    result;
	int                    i;
	int                    idx;
	bool                   eosrc;
	bool                   eodst;
	int                    eomap;

	C2_PRE(io->si_opcode == SIO_READ);

	seg = c2_emap_seg_get(map->ct_it);
	frags = frags_not_empty = 0;
	do {
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

	it   = map->ct_it;
	seg  = c2_emap_seg_get(it);
	back = &aio->ai_back;

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
			
		frag_size = min3(c2_vec_cursor_step(src), 
				 c2_vec_cursor_step(dst),
				 c2_emap_caret_step(map));
		buf = io->si_user.div_vec.ov_buf[src->vc_seg] + 
			src->vc_offset;
		off = io->si_stob.ov_index[dst->vc_seg] + dst->vc_offset;
		C2_ASSERT(c2_ext_is_in(&seg->ee_ext, off));

		if (seg->ee_val == AET_NONE || seg->ee_val == AET_HOLE) {
			/*
			 * Read of a hole or unallocated space (beyond
			 * end of the file).
			 */
			memset(buf, 0, frag_size);
			if (seg->ee_val == AET_HOLE)
				io->si_count += frag_size;
		} else {
			C2_ASSERT(seg->ee_val < AET_MIN);

			back->si_user.div_vec.ov_vec.v_count[idx] = frag_size;
			back->si_user.div_vec.ov_buf[idx] = buf;

			back->si_stob.ov_index[idx] = seg->ee_val + 
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

static uint32_t ad_write_count(struct c2_stob_io *io, struct c2_vec_cursor *src,
			       struct c2_vec_cursor *dst, 
			       const struct c2_ext *ext)
{
	struct c2_ext          todo;
	uint32_t               frags;
	c2_bcount_t            frag_size;
	bool                   eosrc;
	bool                   eodst;
	bool                   eoext;

	todo = *ext;
	frags = 0;

	do {
		frag_size = min3(c2_vec_cursor_step(src), 
				 c2_vec_cursor_step(dst), c2_ext_length(&todo));
		C2_ASSERT(frag_size > 0);
		C2_ASSERT(frag_size <= (size_t)~0ULL);

		eosrc = c2_vec_cursor_move(src, frag_size);
		eodst = c2_vec_cursor_move(dst, frag_size);
		todo.e_start += frag_size;
		eoext = c2_ext_is_empty(&todo);

		C2_ASSERT(eosrc == eodst);
		C2_ASSERT(ergo(eosrc, eoext));
		++frags;
	} while (!eoext);
	return frags;
}

static void ad_write_back_fill(struct c2_stob_io *io, struct c2_stob_io *back,
			       struct c2_vec_cursor *src, 
			       struct c2_vec_cursor *dst, 
			       const struct c2_ext *ext, uint32_t *idx)
{
	struct c2_ext          todo;
	c2_bcount_t            frag_size;

	todo = *ext;
	while (!c2_ext_is_empty(&todo)) {
		void        *buf;
			
		frag_size = min3(c2_vec_cursor_step(src), 
				 c2_vec_cursor_step(dst), c2_ext_length(&todo));

		buf = io->si_user.div_vec.ov_buf[src->vc_seg] + src->vc_offset;

		back->si_user.div_vec.ov_vec.v_count[*idx] = frag_size;
		back->si_user.div_vec.ov_buf[*idx] = buf;

		back->si_stob.ov_index[*idx] = todo.e_start;

		c2_vec_cursor_move(src, frag_size);
		c2_vec_cursor_move(dst, frag_size);
		todo.e_start += frag_size;
		(*idx)++;
	}
	C2_ASSERT(*idx <= back->si_stob.ov_vec.v_nr);
}

static int ad_write_map(struct c2_stob_io *io, struct c2_emap_cursor *it, 
			c2_bindex_t offset, const struct c2_ext *ext)
{
	struct c2_ext todo = *ext;

	return c2_emap_paste
		(it, &todo, offset,
		 LAMBDA(void, (struct c2_emap_seg *seg) {
		 /* handle extent deletion. */
			 }),
		 LAMBDA(void, (struct c2_emap_seg *seg, struct c2_ext *ext,
			       uint64_t val) {
		/* cut left: nothing */
		C2_ASSERT(val == seg->ee_val);
			 }),
		 LAMBDA(void, (struct c2_emap_seg *seg, struct c2_ext *ext,
			       uint64_t val) {
		/* cut right: default just works. */
		C2_ASSERT(ergo(val < AET_MIN, 
			       val == seg->ee_val + (ext->e_start - 
						     seg->ee_ext.e_start) +
			       c2_ext_length(ext)));
		C2_ASSERT(ergo(val >= AET_MIN, val == seg->ee_val));
			 }));
}

struct ad_write_ext {
	c2_bindex_t          we_offset;
	struct c2_ext        we_ext;
	struct ad_write_ext *we_next;
};

static int ad_write_launch(struct c2_stob_io *io, struct ad_domain *adom,
			   struct c2_vec_cursor *src, struct c2_vec_cursor *dst,
			   struct c2_emap_caret *map)
{
	c2_bcount_t          todo;
	uint32_t             frags;
	uint32_t             idx;
	int                  result;
	struct ad_write_ext  head;
	struct ad_write_ext *wext;
	struct ad_write_ext *next;
	struct c2_stob_io   *back;
	struct ad_stob_io   *aio       = io->si_stob_private;

	C2_PRE(io->si_opcode == SIO_WRITE);

	todo = c2_vec_count(&io->si_user.div_vec.ov_vec);
	back = &aio->ai_back;
	wext = &head;
	wext->we_offset = io->si_stob.ov_index[0];
	wext->we_next   = NULL;
	while (1) {
		c2_bcount_t got;

		result = ad_balloc(io, todo, &wext->we_ext);
		if (result != 0)
			return result;
		got = c2_ext_length(&wext->we_ext);
		C2_ASSERT(todo >= got);
		todo -= got;
		if (todo > 0) {
			C2_ALLOC_PTR(next);
			if (next != NULL) {
				next->we_offset = wext->we_offset + got;
				wext->we_next = next;
				wext = next;
			} else {
				ADDB_ADD(io->si_obj, c2_addb_oom);
				return -ENOMEM;
			}
		} else
			break;
	}

	for (frags = 0, wext = &head; wext != NULL; wext = wext->we_next)
		frags += ad_write_count(io, src, dst, &wext->we_ext);

	result = ad_vec_alloc(io->si_obj, back, frags);
	if (result != 0)
		return result;

	c2_vec_cursor_init(src, &io->si_user.div_vec.ov_vec);
	c2_vec_cursor_init(dst, &io->si_stob.ov_vec);

	idx = 0;
	for (wext = &head; wext != NULL && result == 0; wext = wext->we_next)
		ad_write_back_fill(io, back, src, dst, &wext->we_ext, &idx);
	C2_ASSERT(idx == frags);

	c2_vec_cursor_init(src, &io->si_user.div_vec.ov_vec);
	c2_vec_cursor_init(dst, &io->si_stob.ov_vec);

	for (wext = &head; wext != NULL && result == 0; wext = wext->we_next)
		result = ad_write_map(io, map->ct_it, 
				      wext->we_offset, &wext->we_ext);
	return result;
}

/**
   Launch asynchronous IO.

   @li calculate how many fragments IO operation has;

   @li allocate vectors

   @li launch back-store io.
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
	C2_PRE(io->si_obj->so_domain->sd_type == &ad_stob_type);
	C2_PRE(io->si_stob.ov_vec.v_nr > 0);
	C2_PRE(c2_vec_count(&io->si_user.div_vec.ov_vec) > 0);

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
		if (back->si_user.div_vec.ov_vec.v_nr > 0) {
			result = c2_stob_io_launch(back, adom->ad_bstore,
						   io->si_tx, io->si_scope);
			wentout = result == 0;
		} else
			ad_endio(&aio->ai_clink);
	}
	if (!wentout)
		ad_stob_io_release(aio);
	return result;
}

/**
   An implementation of c2_stob_op::sop_lock() method.
 */
void ad_stob_io_lock(struct c2_stob *stob)
{
}

/**
   An implementation of c2_stob_op::sop_unlock() method.
 */
void ad_stob_io_unlock(struct c2_stob *stob)
{
}

/**
   An implementation of c2_stob_op::sop_is_locked() method.
 */
bool ad_stob_io_is_locked(const struct c2_stob *stob)
{
	return true;
}

/* Mock allocator */
static int ad_balloc(struct c2_stob_io *io, c2_bcount_t length, 
		     struct c2_ext *out)
{
	static c2_bindex_t reached = 0;

	out->e_start = reached;
	out->e_end   = (reached += length);
	return 0;
}

static void ad_endio(struct c2_clink *link)
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
	.sdo_fini      = ad_domain_fini,
	.sdo_stob_find = ad_domain_stob_find,
	.sdo_tx_make   = ad_domain_tx_make
};

static const struct c2_stob_op ad_stob_op = {
	.sop_fini         = ad_stob_fini,
	.sop_create       = ad_stob_create,
	.sop_locate       = ad_stob_locate,
	.sop_io_init      = ad_stob_io_init,
	.sop_io_lock      = ad_stob_io_lock,
	.sop_io_unlock    = ad_stob_io_unlock,
	.sop_io_is_locked = ad_stob_io_is_locked,
};

struct c2_stob_type ad_stob_type = {
	.st_op    = &ad_stob_type_op,
	.st_name  = "adstob",
	.st_magic = 0x3129A830
};

const struct c2_addb_ctx_type ad_stob_ctx_type = {
	.act_name = "adieu"
};

int ad_stobs_init(void)
{
	c2_addb_ctx_init(&ad_stob_ctx, &ad_stob_ctx_type, 
			 &c2_addb_global_ctx);
	return ad_stob_type.st_op->sto_init(&ad_stob_type);
}

void ad_stobs_fini(void)
{
	ad_stob_type.st_op->sto_fini(&ad_stob_type);
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
