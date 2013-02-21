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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/13/2010
 */

#include <stdio.h>     /* asprintf */
#include <stdlib.h>
#include <string.h>

#include "lib/vec.h"
#include "lib/errno.h"
#include "lib/arith.h"   /* M0_3WAY */
#include "db/extmap.h"
#include "db/extmap_xc.h"

/**
   @addtogroup extmap

   <b>Extent map implementation details.</b>

   @see extmap_internal.h

   Few notes:

   @li m0_emap_cursor::ec_seg is "external" representation of iteration
   state. m0_emap_cursor::ec_key and m0_emap_cursor::ec_rec are "internal";

   @li after internal state has changed it is "opened" by it_open() which
   updates external stats to match changes;

   @li similarly when external state has changed, it is "packed" into internal
   one by it_pack().

   @li m0_emap_cursor::ec_pair is descriptor of buffers for data-base
   operations. Buffers are aliased to m0_emap_cursor::ec_key and
   m0_emap_cursor::ec_rec by it_init().

   @li emap_invariant() checks implementation invariant: an extent map is an
   collection of segments with non-empty extents forming the partition of the
   name-space and ordered by their starting offsets. This creates a separate
   cursor within the same transaction as the cursor it is called against.

   @li A segment ([A, B), V) is stored as a record (A, V) with a key (prefix,
   B). Note, that the _high_ extent end is used as a key. This way, standard
   m0_db_cursor_get() can be used to position a cursor on a segment containing a
   given offset. Also note, that there is some redundancy in the persistent
   state: two consecutive segments ([A, B), V) and ([B, C), U) are stored as
   records (A, V) and (B, U) with keys (prefix, B) and (prefix, C)
   respectively. B is stored twice. Generally, starting offset of a segment
   extent can always be deduced from the key of previous segment (and for the
   first segment it's 0), so some slight economy of storage could be achieved at
   the expense of increased complexity and occasional extra storage traffic.

   @note emap_invariant() is potentially expensive. Consider turning it off
   conditionally.

   @{
 */

/*
static void key_print(const struct m0_emap_key *k)
{
	printf("%08lx.%08lx:%08lx", k->ek_prefix.u_hi, k->ek_prefix.u_lo,
	       k->ek_offset);
}
*/

static int emap_cmp(struct m0_table *table,
		    const void *key0, const void *key1)
{
	const struct m0_emap_key *a0 = key0;
	const struct m0_emap_key *a1 = key1;

/*	static const char compare[] = "<=>";

	key_print(a0);
	printf(" %c ", compare[(m0_uint128_cmp(&a0->ek_prefix,
						 &a1->ek_prefix) ?:
				  M0_3WAY(a0->ek_offset,
					  a1->ek_offset)) + 1]);
	key_print(a1);
	printf("\n"); */
	return m0_uint128_cmp(&a0->ek_prefix, &a1->ek_prefix) ?:
		M0_3WAY(a0->ek_offset, a1->ek_offset);
}

static const struct m0_table_ops emap_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct m0_emap_key)
		},
		[TO_REC] = {
			.max_size = sizeof(struct m0_emap_rec)
		},
	},
	.key_cmp = emap_cmp
};

M0_INTERNAL int m0_emap_init(struct m0_emap *emap, struct m0_dbenv *db,
			     const char *mapname)
{
	return m0_table_init(&emap->em_mapping, db, mapname, 0, &emap_ops);
}

M0_INTERNAL void m0_emap_fini(struct m0_emap *emap)
{
	m0_table_fini(&emap->em_mapping);
}

static void emap_pack(const struct m0_emap_seg *ext,
		      struct m0_emap_key *key, struct m0_emap_rec *rec)
{
	key->ek_prefix = ext->ee_pre;
	key->ek_offset = ext->ee_ext.e_end;
	rec->er_start  = ext->ee_ext.e_start;
	rec->er_value  = ext->ee_val;
}

static void emap_open(const struct m0_emap_key *key,
		      const struct m0_emap_rec *rec, struct m0_emap_seg *ext)
{
	ext->ee_pre         = key->ek_prefix;
	ext->ee_ext.e_start = rec->er_start;
	ext->ee_ext.e_end   = key->ek_offset;
	ext->ee_val         = rec->er_value;
}

M0_INTERNAL struct m0_emap_seg *m0_emap_seg_get(struct m0_emap_cursor *iterator)
{
	return &iterator->ec_seg;
}

M0_INTERNAL bool m0_emap_ext_is_last(const struct m0_ext *ext)
{
	return ext->e_end == M0_BINDEX_MAX + 1;
}

M0_INTERNAL bool m0_emap_ext_is_first(const struct m0_ext *ext)
{
	return ext->e_start == 0;
}

static void it_pack(struct m0_emap_cursor *it)
{
	emap_pack(&it->ec_seg, &it->ec_key, &it->ec_rec);
}

static void it_open(struct m0_emap_cursor *it)
{
	emap_open(&it->ec_key, &it->ec_rec, &it->ec_seg);
}

static bool it_prefix_ok(const struct m0_emap_cursor *it)
{
	return m0_uint128_eq(&it->ec_seg.ee_pre, &it->ec_prefix);
}

#define IT_DO_OPEN(it, func)					\
({								\
	int __result;						\
	struct m0_emap_cursor *__it = (it);			\
								\
	__result = (*(func))(&__it->ec_cursor, &__it->ec_pair);	\
	if (__result == 0) {					\
		it_open(__it);					\
		if (!it_prefix_ok(__it))			\
			__result = -ESRCH;			\
	}							\
	__result;						\
})

#define IT_DO_PACK(it, func)				\
({							\
	struct m0_emap_cursor *__it = (it);		\
							\
	it_pack(__it);					\
	(*(func))(&__it->ec_cursor, &__it->ec_pair);	\
})

static int it_init(struct m0_emap *emap, struct m0_db_tx *tx,
		   const struct m0_uint128 *prefix, m0_bindex_t offset,
		   struct m0_emap_cursor *it, uint32_t flags)
{
	m0_db_pair_setup(&it->ec_pair, &emap->em_mapping,
			 &it->ec_key, sizeof it->ec_key,
			 &it->ec_rec, sizeof it->ec_rec);
	it->ec_key.ek_prefix = it->ec_prefix = *prefix;
	it->ec_key.ek_offset = offset + 1;
	it->ec_map           = emap;
	return m0_db_cursor_init(&it->ec_cursor, &emap->em_mapping, tx, flags);
}

static void emap_close(struct m0_emap_cursor *it)
{
	m0_db_cursor_fini(&it->ec_cursor);
	m0_db_pair_fini(&it->ec_pair);
}

static int emap_lookup(struct m0_emap *emap, struct m0_db_tx *tx,
		       const struct m0_uint128 *prefix, m0_bindex_t offset,
		       struct m0_emap_cursor *it)
{
	int result;

	result = it_init(emap, tx, prefix, offset, it, 0);
	if (result == 0) {
		result = IT_DO_OPEN(it, &m0_db_cursor_get);
		if (result != 0)
			emap_close(it);
	}
	M0_POST(ergo(result == 0, m0_ext_is_in(&it->ec_seg.ee_ext, offset)));
	return result;
}

static int emap_next(struct m0_emap_cursor *it)
{
	return IT_DO_OPEN(it, &m0_db_cursor_next);
}

#ifdef ENABLE_DEBUG
static bool emap_invariant_check(struct m0_emap_cursor *it)
{
	int                   result;
	m0_bindex_t           reached;
	m0_bcount_t           total;

	reached = 0;
	total   = 0;
	if (!m0_emap_ext_is_first(&it->ec_seg.ee_ext))
		return false;
	while (1) {
		if (it->ec_seg.ee_ext.e_start != reached)
			return false;
		if (it->ec_seg.ee_ext.e_end <= reached)
			return false;
		reached = it->ec_seg.ee_ext.e_end;
		total += m0_ext_length(&it->ec_seg.ee_ext);
		if (m0_emap_ext_is_last(&it->ec_seg.ee_ext))
			break;
		result = emap_next(it);
		if (result != 0)
			return true;
	}
	if (total != M0_BCOUNT_MAX)
		return false;
	if (reached != M0_BINDEX_MAX + 1)
		return false;
	return true;
}

static bool emap_invariant(struct m0_emap_cursor *it)
{
	struct m0_emap_cursor scan;
	int                   result;
	bool                  check;

	result = emap_lookup(it->ec_map, it->ec_cursor.c_tx,
			     &it->ec_key.ek_prefix, 0, &scan);
	if (result == 0) {
		check = emap_invariant_check(&scan);
		emap_close(&scan);
	} else
		check = true;
	return check;
}

#else /* !ENABLE_DEBUG */

static bool emap_invariant(struct m0_emap_cursor *it)
{
	return true;
}
#endif

int m0_emap_lookup(struct m0_emap *emap, struct m0_db_tx *tx,
		   const struct m0_uint128 *prefix, m0_bindex_t offset,
		   struct m0_emap_cursor *it)
{
	int result;

	M0_PRE(offset <= M0_BINDEX_MAX);

	result = emap_lookup(emap, tx, prefix, offset, it);
	M0_ASSERT_EX(ergo(result == 0, emap_invariant(it)));
	return result;
}

M0_INTERNAL int m0_emap_next(struct m0_emap_cursor *it)
{
	M0_PRE(!m0_emap_ext_is_last(&it->ec_seg.ee_ext));
	M0_INVARIANT_EX(emap_invariant(it));

	return emap_next(it);
}

M0_INTERNAL int m0_emap_prev(struct m0_emap_cursor *it)
{
	M0_PRE(!m0_emap_ext_is_first(&it->ec_seg.ee_ext));
	M0_INVARIANT_EX(emap_invariant(it));

	return IT_DO_OPEN(it, &m0_db_cursor_prev);
}

M0_INTERNAL void m0_emap_close(struct m0_emap_cursor *it)
{
	M0_INVARIANT_EX(emap_invariant(it));
	emap_close(it);
}

M0_INTERNAL int emap_split_internal(struct m0_emap_cursor *it,
				    struct m0_indexvec *vec, m0_bindex_t scan)
{
	uint32_t    i;
	int         result;
	m0_bcount_t count;

	result = m0_db_cursor_del(&it->ec_cursor);
	if (result == 0) {
		for (result = 0, i = 0; i < vec->iv_vec.v_nr; ++i) {
			count = vec->iv_vec.v_count[i];
			if (count != 0) {
				it->ec_seg.ee_ext.e_start = scan;
				it->ec_seg.ee_ext.e_end   = scan = scan + count;
				it->ec_seg.ee_val         = vec->iv_index[i];
				result = IT_DO_PACK(it, m0_db_cursor_add);
				if (result != 0)
					break;
			}
		}
	}
	return result;
}

M0_INTERNAL int m0_emap_split(struct m0_emap_cursor *it,
			      struct m0_indexvec *vec)
{
	int result;

	M0_PRE(m0_vec_count(&vec->iv_vec) == m0_ext_length(&it->ec_seg.ee_ext));
	M0_INVARIANT_EX(emap_invariant(it));

	result = emap_split_internal(it, vec, it->ec_seg.ee_ext.e_start);
	M0_ASSERT_EX(ergo(result == 0, emap_invariant(it)));
	return result;
}

int m0_emap_paste(struct m0_emap_cursor *it, struct m0_ext *ext, uint64_t val,
		  void (*del)(struct m0_emap_seg *),
		  void (*cut_left)(struct m0_emap_seg *, struct m0_ext *,
				   uint64_t),
		  void (*cut_right)(struct m0_emap_seg *, struct m0_ext *,
				    uint64_t))
{
	int                    result   = 0;
	uint64_t               val_orig;
	struct m0_emap_seg    *seg      = &it->ec_seg;
	struct m0_ext         *chunk    = &seg->ee_ext;
	const struct m0_ext    ext0     = *ext;

	M0_PRE(m0_ext_is_in(chunk, ext->e_start));
	M0_INVARIANT_EX(emap_invariant(it));

	/*
	 * Iterate over existing segments overlapping with the new one,
	 * calculating for each, what parts have to be deleted and what remains.
	 *
	 * In the worst case, an existing segment can split into three
	 * parts. Generally, some of these parts can be empty.
	 *
	 * Cutting and deleting segments is handled uniformly by
	 * emap_split_internal(), thanks to the latter skipping empty segments.
	 *
	 * Note that the _whole_ new segment is inserted on the last iteration
	 * of the loop below (see length[1] assignment), thus violating the map
	 * invariant until the loop exits (the map is "porous" during that
	 * time).
	 */

	while (!m0_ext_is_empty(ext)) {
		m0_bcount_t        length[3];
		m0_bindex_t        bstart[3] = { 0 };
		m0_bcount_t        consumed;
		struct m0_ext      clip;
		struct m0_indexvec vec = {
			.iv_vec = {
				.v_nr    = 3,
				.v_count = length
			},
			.iv_index = bstart
		};

		m0_ext_intersection(ext, chunk, &clip);
		M0_ASSERT(clip.e_start == ext->e_start);
		consumed = m0_ext_length(&clip);
		M0_ASSERT(consumed > 0);

		length[0] = clip.e_start - chunk->e_start;
		length[1] = clip.e_end == ext->e_end ? m0_ext_length(&ext0) : 0;
		length[2] = chunk->e_end - clip.e_end;

		bstart[1] = val;
		val_orig  = seg->ee_val;
		if (length[0] > 0) {
			cut_left(seg, &clip, val_orig);
			bstart[0] = seg->ee_val;
		}
		if (length[2] > 0) {
			cut_right(seg, &clip, val_orig);
			bstart[2] = seg->ee_val;
		}
		if (length[0] == 0 && length[2] == 0)
			del(seg);

		result = emap_split_internal(it, &vec, length[0] > 0 ?
					     chunk->e_start : ext0.e_start);
		if (result != 0)
			break;

		ext->e_start += consumed;
		M0_ASSERT(ext->e_start <= ext->e_end);

		if (!m0_ext_is_empty(ext)) {
			M0_ASSERT(!m0_emap_ext_is_last(&seg->ee_ext));
			result = emap_next(it);
			if (result != 0)
				break;
		}
	}
	M0_ASSERT_EX(ergo(result == 0, emap_invariant(it)));
	return result;

	/*
	 * A tale of two keys.
	 *
	 * Primordial version of this function inserted the whole new extent (as
	 * specified by @ext) at the first iteration of the loop. From time to
	 * time the (clip.e_start == ext->e_start) assertion got violated for no
	 * apparent reason. Eventually, after a lot of tracing (by Anatoliy),
	 * the following sequence was tracked down:
	 *
	 * - on entry to m0_emap_paste():
	 *
	 *   emap: *[0, 512) [512, 1024) [1024, 2048) [2048, ...)
	 *   ext:   [0, 1024)
	 *
	 *   (where current cursor position is starred).
	 *
	 * - at the end of the first iteration, instead of expected
	 *
	 *   emap: [0, 1024) *[512, 1024) [1024, 2048) [2048, ...)
	 *
	 *   the map was
	 *
	 *   emap: [0, 1024) *[1024, 2048) [2048, ...)
	 *
	 * - that is, the call to emap_split_internal():
	 *
	 *   - deleted [0, 512) (as expected),
	 *   - inserted [0, 1024) (as expected),
	 *   - deleted [512, 1024) ?!
	 *
	 * The later is seemingly impossible, because the call deletes exactly
	 * one segment. The surprising explanation is that segment ([L, H), V)
	 * is stored as a record (L, V) with H as a key (this is documented at
	 * the top of this file) and the [0, 1024) segment has the same key as
	 * already existing [512, 1024) one, with the former forever masking the
	 * latter.
	 *
	 * The solution is to insert the new extent as the last step, but the
	 * more important moral of this melancholy story is
	 *
	 *         Thou shalt wit thine abstraction levels.
	 *
	 * In the present case, emap_split_internal() operates on the level of
	 * records and keys which turns out to be subtly different from the
	 * level of segments and maps.
	 */
}

M0_INTERNAL int m0_emap_merge(struct m0_emap_cursor *it, m0_bindex_t delta)
{
	int result;

	M0_PRE(!m0_emap_ext_is_last(&it->ec_seg.ee_ext));
	M0_PRE(delta <= m0_ext_length(&it->ec_seg.ee_ext));
	M0_INVARIANT_EX(emap_invariant(it));

	if (it->ec_seg.ee_ext.e_end == delta) {
		result = m0_db_cursor_del(&it->ec_cursor);
	} else {
		it->ec_seg.ee_ext.e_end -= delta;
		result = IT_DO_PACK(it, &m0_db_cursor_set);
	}
	if (result == 0) {
		result = emap_next(it);
		if (result == 0) {
			it->ec_seg.ee_ext.e_start -= delta;
			result = IT_DO_PACK(it, &m0_db_cursor_set);
		}
	}
	M0_ASSERT_EX(ergo(result == 0, emap_invariant(it)));
	return result;
}

M0_INTERNAL int m0_emap_obj_insert(struct m0_emap *emap, struct m0_db_tx *tx,
				   const struct m0_uint128 *prefix,
				   uint64_t val)
{
	struct m0_emap_cursor it;
	int                   result;

	result = it_init(emap, tx, prefix, 0, &it, M0_DB_CURSOR_RMW);
	if (result == 0) {
		it.ec_seg.ee_pre         = *prefix;
		it.ec_seg.ee_ext.e_start = 0;
		it.ec_seg.ee_ext.e_end   = M0_BINDEX_MAX + 1;
		it.ec_seg.ee_val         = val;
		it_pack(&it);
		result = m0_table_insert(tx, &it.ec_pair);
		M0_ASSERT_EX(ergo(result == 0, emap_invariant(&it)));
		m0_emap_close(&it);
	}
	return result;
}

M0_INTERNAL int m0_emap_obj_delete(struct m0_emap *emap, struct m0_db_tx *tx,
				   const struct m0_uint128 *prefix)
{
	struct m0_emap_cursor it;
	int                   result;

	result = m0_emap_lookup(emap, tx, prefix, 0, &it);
	if (result == 0) {
		M0_ASSERT(m0_emap_ext_is_first(&it.ec_seg.ee_ext) &&
			  m0_emap_ext_is_last(&it.ec_seg.ee_ext));
		M0_INVARIANT_EX(emap_invariant(&it));
		result = m0_db_cursor_del(&it.ec_cursor);
		m0_emap_close(&it);
	}
	return result;
}

static bool m0_emap_caret_invariant(const struct m0_emap_caret *car)
{
	return
		m0_ext_is_in(&car->ct_it->ec_seg.ee_ext, car->ct_index) ||
		(m0_emap_ext_is_last(&car->ct_it->ec_seg.ee_ext) &&
		 car->ct_index == M0_BINDEX_MAX + 1);
}

M0_INTERNAL void m0_emap_caret_init(struct m0_emap_caret *car,
				    struct m0_emap_cursor *it,
				    m0_bindex_t index)
{
	M0_PRE(index <= M0_BINDEX_MAX);
	M0_PRE(m0_ext_is_in(&it->ec_seg.ee_ext, index));
	car->ct_it    = it;
	car->ct_index = index;
	M0_ASSERT(m0_emap_caret_invariant(car));
}

M0_INTERNAL void m0_emap_caret_fini(struct m0_emap_caret *car)
{
	M0_ASSERT(m0_emap_caret_invariant(car));
}

M0_INTERNAL int m0_emap_caret_move(struct m0_emap_caret *car, m0_bcount_t count)
{
	int result;

	M0_ASSERT(m0_emap_caret_invariant(car));
	while (count > 0 && car->ct_index < M0_BINDEX_MAX + 1) {
		m0_bcount_t step;

		step = m0_emap_caret_step(car);
		if (count >= step) {
			result = m0_emap_next(car->ct_it);
			if (result < 0)
				return result;
		} else
			step = count;
		car->ct_index += step;
		count -= step;
	}
	M0_ASSERT(m0_emap_caret_invariant(car));
	return car->ct_index == M0_BINDEX_MAX + 1;
}

M0_INTERNAL m0_bcount_t m0_emap_caret_step(const struct m0_emap_caret *car)
{
	M0_ASSERT(m0_emap_caret_invariant(car));
	return car->ct_it->ec_seg.ee_ext.e_end - car->ct_index;
}

/** @} end group extmap */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
