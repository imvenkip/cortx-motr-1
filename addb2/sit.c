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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 03-Feb-2015
 */


/**
 * @addtogroup addb2
 *
 * Addb2 storage iterator
 * ----------------------
 *
 * Storage iterator takes a stob containing trace frames produced by
 * m0_addb2_storage, and iterates over all records in the traces.
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_ADDB

#include "lib/trace.h"
#include "lib/chan.h"
#include "lib/types.h"
#include "lib/assert.h"
#include "lib/arith.h"              /* min_check */
#include "lib/memory.h"
#include "lib/errno.h"              /* EPROTO, EAGAIN */
#include "stob/stob.h"
#include "stob/io.h"
#include "mero/magic.h"
#include "xcode/xcode.h"            /* m0_xcode_encdec */
#include "addb2/addb2.h"
#include "addb2/consumer.h"
#include "addb2/internal.h"
#include "addb2/storage.h"
#include "addb2/storage_xc.h"

/**
 * Storage iterator.
 */
struct m0_addb2_sit {
	struct m0_stob              *s_stob;
	m0_bcount_t                  s_size;
	/**
	 * Earliest header found in the stob.
	 */
	struct m0_addb2_frame_header s_start;
	/**
	 * Latest header found in the stob.
	 */
	struct m0_addb2_frame_header s_end;
	/**
	 * Header of the current frame.
	 */
	struct m0_addb2_frame_header s_current;
	unsigned                     s_bshift;
	m0_bcount_t                  s_bsize;
	struct m0_addb2_cursor       s_cursor;
	struct m0_addb2_source       s_src;
	/**
	 * The buffer into which the current frame is read.
	 */
	char                        *s_buf;
	struct m0_addb2_trace        s_trace;
	/**
	 * The pointer to the current trace body in the ->s_buf[].
	 */
	uint64_t                    *s_trace_ptr;
	/**
	 * The index of the current trace within the current frame.
	 */
	m0_bindex_t                  s_trace_idx;
};

static bool header_is_valid(const struct m0_addb2_sit *it,
			    const struct m0_addb2_frame_header *h);
static int it_init(struct m0_addb2_sit *it,
		   const struct m0_addb2_frame_header *anchor);
static int it_next(struct m0_addb2_sit *it);
static int it_load(struct m0_addb2_sit *it);
static int it_read(const struct m0_addb2_sit *it, void *buf,
		   m0_bindex_t offset, m0_bcount_t count);
static void it_forward(struct m0_addb2_sit *it);
static bool it_rounded(const struct m0_addb2_sit *it, m0_bindex_t index);
static bool it_is_in(const struct m0_addb2_sit *it, m0_bindex_t index);
static bool it_invariant(const struct m0_addb2_sit *it);
static void it_trace_set(struct m0_addb2_sit *it);
static int header_read(struct m0_addb2_sit *it, struct m0_addb2_frame_header *h,
		       m0_bindex_t offset);

int m0_addb2_sit_init(struct m0_addb2_sit **out,
		      struct m0_stob *stob, m0_bcount_t size,
		      const struct m0_addb2_frame_header *anchor)
{
	struct m0_addb2_sit *it;
	int                  result;
	unsigned             shift = m0_stob_block_shift(stob);
	void                *buf;

	M0_ALLOC_PTR(it);
	buf = m0_alloc_aligned(FRAME_SIZE_MAX, shift);
	if (it != NULL && buf != NULL) {
		m0_stob_get(stob);
		it->s_stob   = stob;
		it->s_size   = size;
		it->s_bshift = shift;
		it->s_bsize  = 1ULL << it->s_bshift;
		it->s_buf    = buf;
		m0_addb2_source_init(&it->s_src);
		if (ergo(anchor != NULL, header_is_valid(it, anchor)))
			result = min_check(it_init(it, anchor), 0);
		else
			result = M0_ERR(-EPROTO);
		if (result != 0)
			m0_addb2_sit_fini(it);
	} else {
		m0_free(it);
		m0_free_aligned(buf, FRAME_SIZE_MAX, shift);
		result = M0_ERR(-ENOMEM);
	}
	if (result == 0)
		*out = it;
	M0_POST(ergo(result == 0, it_invariant(*out)));
	return result;
}

void m0_addb2_sit_fini(struct m0_addb2_sit *it)
{
	if (it->s_cursor.cu_trace != NULL)
		m0_addb2_cursor_fini(&it->s_cursor);
	m0_addb2_source_fini(&it->s_src);
	m0_stob_put(it->s_stob);
	m0_free_aligned(it->s_buf, FRAME_SIZE_MAX, it->s_bshift);
	m0_free(it);
}

int m0_addb2_sit_next(struct m0_addb2_sit *it, struct m0_addb2_record **out)
{
	int result;

	M0_PRE(it_invariant(it));

	do {
		result = m0_addb2_cursor_next(&it->s_cursor);
		if (result > 0) {
			*out = &it->s_cursor.cu_rec;
			m0_addb2_consume(&it->s_src, *out);
		} else if (result == 0) {
			m0_addb2_cursor_fini(&it->s_cursor);
			result = it_next(it);
		}
	} while (result == +2);
	M0_POST(ergo(result >= 0, it_invariant(it)));
	return M0_RC(result);
}

struct m0_addb2_source *m0_addb2_sit_source(struct m0_addb2_sit *it)
{
	M0_PRE(it_invariant(it));
	return &it->s_src;
}

M0_INTERNAL int m0_addb2_storage_header(struct m0_stob *stob, m0_bcount_t size,
					struct m0_addb2_frame_header *h)
{
	struct m0_addb2_sit *sit;
	int                  result;

	if (size < sizeof *h)
		return M0_ERR(-EINVAL);

	result = m0_addb2_sit_init(&sit, stob, size, NULL);
	if (result == 0) {
		*h = sit->s_end;
		m0_addb2_sit_fini(sit);
	}
	return result;
}

/**
 * Scans the stob and determines the range of headers.
 *
 * Starting from the given anchor header scan backwards, until either the first
 * even header is found (one with he_prev_offset == 0) or are about to jump over
 * the anchor.
 */
static int it_init(struct m0_addb2_sit *it,
		   const struct m0_addb2_frame_header *anchor)
{
	struct m0_addb2_frame_header *h = &it->s_start;
	int                           result;

	if (anchor != NULL)
		it->s_end = *anchor;
	else
		it_forward(it);
	result = header_read(it, h, 0);
	if (result == 0) {
		if (h->he_prev_offset != 0) {
			while (h->he_prev_offset >
			       anchor->he_offset + anchor->he_size) {
				result = header_read(it, h, h->he_prev_offset);
				if (result != 0)
					break;
			}
		}
		if (result == 0) {
			it->s_current = *h;
			result = it_load(it);
		}
	}
	M0_POST(ergo(result >= 0, it_invariant(it)));
	return M0_RC(result);
}

/**
 * Reads the header at the given offset.
 */
static int header_read(struct m0_addb2_sit *it, struct m0_addb2_frame_header *h,
		       m0_bindex_t offset)
{
	int result;

	M0_ASSERT(sizeof *h <= it->s_bsize);

	result = it_read(it, it->s_buf, offset, it->s_bsize);
	if (result == 0) {
		void                   *addr = it->s_buf;
		struct m0_bufvec        buf  = M0_BUFVEC_INIT_BUF(&addr,
								  &it->s_bsize);
		struct m0_bufvec_cursor cur;

		M0_SET0(h);
		m0_bufvec_cursor_init(&cur, &buf);
		result = m0_xcode_encdec(HEADER_XO(h), &cur, M0_XCODE_DECODE) ?:
			header_is_valid(it, h) && h->he_offset == offset ?
			0 : -EPROTO;
	}
	return M0_RC(result);
}

/**
 * Moves the iterator to the next record once the current trace buffer has been
 * exhausted.
 */
static int it_next(struct m0_addb2_sit *it)
{
	struct m0_addb2_frame_header *h = &it->s_current;
	int                           result;

	M0_PRE(it_invariant(it));

	if (it->s_trace_idx + 1 < h->he_trace_nr) {
		/*
		 * If still within the current frame, go to the next trace.
		 */
		++ it->s_trace_idx;
		it->s_trace_ptr += it->s_trace.tr_nr + 1;
		it_trace_set(it);
		result = +2;
	} else if (h->he_offset >= it->s_end.he_offset &&
		   h->he_offset < it->s_end.he_offset + it->s_end.he_size) {
		/*
		 * Last frame is completed, finish the iterator.
		 */
		result = 0;
	} else {
		h->he_offset += h->he_size;
		if (it->s_size - h->he_offset < FRAME_SIZE_MAX)
			h->he_offset = 0;
		/*
		 * Go to the next frame.
		 */
		result = header_read(it, h, h->he_offset);
		if (result == 0)
			result = it_load(it);
	}
	M0_POST(ergo(result >= 0, it_invariant(it)));
	return M0_RC(result);
}

/**
 * Loads a frame.
 */
static int it_load(struct m0_addb2_sit *it)
{
	struct m0_addb2_frame_header *h = &it->s_current;
	int                           result;

	result = it_read(it, it->s_buf, h->he_offset, h->he_size);
	if (result == 0) {
		struct m0_addb2_frame_header copy;

		/*
		 * Re-read the header to guard against concurrent writes by
		 * m0_addb2_storage.
		 */
		result = header_read(it, &copy, h->he_offset) ?:
			memcmp(h, &copy, sizeof *h) == 0 ? 0 : M0_ERR(-EAGAIN);
		if (result == 0) {
			it->s_trace_ptr = ((void *)it->s_buf) + sizeof *h;
			it_trace_set(it);
			it->s_trace_idx = 0;
			result = +2;
		}
	}
	M0_POST(ergo(result >= 0, it_invariant(it)));
	return M0_RC(result);
}

/**
 * Scans the stob starting from the beginning, looking for valid frames.
 */
static void it_forward(struct m0_addb2_sit *it)
{
	struct m0_addb2_frame_header h;
	uint64_t                     seqno;
	uint64_t                     next;
	int                          result;

	it->s_end = M0_ADDB2_HEADER_INIT;
	result = header_read(it, &h, 0);
	if (result == 0) {
		seqno = h.he_seqno;
		do {
			it->s_end = h;
			next = h.he_offset + h.he_size;
			if (it->s_size - next < FRAME_SIZE_MAX)
				break;
			result = header_read(it, &h, next);
		} while (result == 0 && h.he_seqno == ++seqno);
		result = 0;
	}
}

static bool it_rounded(const struct m0_addb2_sit *it, m0_bindex_t index)
{
	return m0_round_up(index, it->s_bsize) == index;
}

static bool it_is_in(const struct m0_addb2_sit *it, m0_bindex_t index)
{
	return index < it->s_size;
}

static bool header_is_valid(const struct m0_addb2_sit *it,
			    const struct m0_addb2_frame_header *h)
{
	return  it_rounded(it, h->he_offset) &&
		it_rounded(it, h->he_size) &&
		it_rounded(it, h->he_prev_offset) &&
		it_is_in(it, h->he_offset) &&
		it_is_in(it, h->he_prev_offset) &&
		it_is_in(it, h->he_offset + h->he_size - 1) &&
		h->he_magix == M0_ADDB2_FRAME_HEADER_MAGIX &&
		h->he_trace_nr > 0 &&
		h->he_trace_nr <= FRAME_TRACE_MAX &&
		h->he_size > 0 &&
		h->he_trace_nr <= FRAME_SIZE_MAX;
}

static bool it_invariant(const struct m0_addb2_sit *it)
{
	const struct m0_addb2_frame_header *h    = &it->s_current;
	char                               *body = (char *)it->s_trace.tr_body;

	return  _0C(header_is_valid(it, &it->s_current)) &&
		_0C(header_is_valid(it, &it->s_start)) &&
		_0C(header_is_valid(it, &it->s_end)) &&
		_0C(it->s_trace_idx < h->he_trace_nr) &&
		_0C(it->s_start.he_seqno <= h->he_seqno) &&
		_0C(h->he_seqno <= it->s_end.he_seqno) &&
		_0C(ergo(body != NULL,
			 it->s_buf <= body && body < it->s_buf + h->he_size));
}

/**
 * Reads given stob extent in the buffer.
 */
static int it_read(const struct m0_addb2_sit *it, void *buf,
		   m0_bindex_t offset, m0_bcount_t count)
{
	int               result;

	M0_PRE(it_rounded(it, offset));
	M0_PRE(it_rounded(it, count));
	M0_PRE(it_rounded(it, (uint64_t)buf));

	buf = m0_stob_addr_pack(buf, it->s_bshift);
	count  >>= it->s_bshift;
	offset >>= it->s_bshift;

	result = m0_stob_io_bufvec_launch(it->s_stob,
			  &(struct m0_bufvec)M0_BUFVEC_INIT_BUF(&buf, &count),
			  SIO_READ, offset);

	return M0_RC(result);
}

/**
 * Initialises internal trace buffer.
 */
static void it_trace_set(struct m0_addb2_sit *it)
{
	M0_CASSERT(sizeof it->s_trace.tr_nr == sizeof *it->s_trace_ptr);

	it->s_trace.tr_nr   = *it->s_trace_ptr;
	it->s_trace.tr_body =  it->s_trace_ptr + 1;
	M0_SET0(&it->s_cursor);
	m0_addb2_cursor_init(&it->s_cursor, &it->s_trace);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of addb2 group */

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
