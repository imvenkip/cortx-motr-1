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
 * Original author: Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation: 12/17/2012
 */

/**
   @page ADDB-DLD-RETRV ADDB Repository Retrieval Detailed Design

   This is a component DLD and hence not all sections are present.
   Refer to the @ref ADDB-DLD "ADDB Detailed Design"
   for the design requirements.

   - @ref ADDB-DLD-RETRV-fspec
   - @ref ADDB-DLD-RETRV-lspec
      - @ref ADDB-DLD-RETRV-lspec-segment
      - @ref ADDB-DLD-RETRV-lspec-cursor
      - @ref ADDB-DLD-RETRV-lspec-thread
   - @ref ADDB-DLD-RETRV-ut

   <hr>
   @section ADDB-DLD-RETRV-fspec Functional Specification

   @see @ref addb_retrieval "ADDB Record Retrieval API"

   <hr>
   @section ADDB-DLD-RETRV-lspec Logical Specification
   The ADDB Repository retrieval component is composed of a record retrieval
   layer built over an abstract segment retrieval layer.  Multiple
   implementations of the segment retrieval layer are possible, such as reading
   directly from a stob or reading from an archival file that was previously
   created from an ADDB repository stob.

   @subsection ADDB-DLD-RETRV-lspec-segment Segment Retrieval Layer
   The segment layer presents an abstract interface for iterating over the
   segments of an ADDB repository.  The abstraction, represented by the
   struct m0_addb_segment_iter provides one function,
   m0_addb_segment_iter::asi_next().  This function is described in more
   detail below.

   Presently, a single implementation of the segment iterator is provided,
   an iterator that operates directly over a (possibly live) stob containing
   ADDB records.

   The segment iterator is allocated and initialized by calling
   m0_addb_stob_iter_alloc().  This function allocates a new object,
   stob_retrieval_iter, containing a struct m0_addb_segment_iter and returns a
   pointer to the object on success.  It verifies that the stob is a compatible
   stob repository by reading its first segment header.  The function increases
   the reference count on the provided m0_stob object.

   After allocation, the upper layer can iterate through the segments by
   repeatedly calling the m0_addb_segment_iter::asi_next() function.  Each time
   the function is called, it reads an additional segment from the repository
   and returns the count of records in the buffer and a cursor into the raw
   record data.  The order in which the segments are retrieved is not defined;
   each implementation may return the segments in a different order.

   In the case of the stob implementation, the function will read the trailer of
   the next segment and then rewind and read the rest of the segment (this
   avoids reading partial data, as specified in
   @ref ADDB-DLD-DSINK-lspec-reader) and verifies that the segment is complete.
   If the segment is not complete, the segment, is dropped and the next is read
   until a complete segment is read, or the end of the stob is reached.

   The stob segment iterator is freed by calling m0_addb_stob_iter_free().  This
   function frees all resources related to the iterator.  The reference count
   on the stob is decreased by this call.

   In the future, additional implementations will be provided, especially one
   that can read records archived from an ADDB repository stob in a flat file.

   @subsection ADDB-DLD-RETRV-lspec-cursor Record Retrieval Layer
   The record retrieval layer is implemented above the segment retrieval layer.
   Record retrieval is represented by a struct m0_addb_cursor.

   This object is initialized by calling m0_addb_cursor_init(), specifying
   the underlying segment iterator and flags to control which types of records
   to be retrieved: event records, context records or both.

   Subsequently, individual records are retrieved by repeatedly calling
   m0_addb_cursor_next().  The order in which records are returned is undefined;
   only that all valid records will be returned.  Internally, this function
   uses the segment iterator to obtain segments from the repository.  It
   then decodes the XCODE-encoded one record on each call, returning a pointer
   to the decoded data.

   The m0_addb_cursor_fini() function should be called when the iterator is no
   longer needed.  It will release any resources previously allocated to the
   iterator.  This should be called before calling m0_addb_stob_iter_free().

   Two helper functions for handling returned records are provided.  Their
   behavior is fully described in the functional specification.
   - m0_addb_rec_is_event()
   - m0_addb_rec_is_ctx()

   @subsection ADDB-DLD-RETRV-lspec-thread Threading and Concurrency Model
   The retrieval APIs provide no synchronization to support simultaneous use of
   the same m0_addb_segment_iter or m0_addb_cursor.  There is no interaction
   between different m0_addb_segment_iter or m0_addb_cursor objects.  Once
   A m0_addb_segment_iter has been associated with a m0_addb_cursor, it should
   not be used on its own until that cursor has been finalized.

   @section ADDB-DLD-RETRV-ut Unit Tests

   @test stob_retrieval_segsize_get() correctly retrieves segment size.

   @test stob_retrieval_iter_next() retrieves all segments, exactly once.

   @test m0_addb_cursor_init() with flags == M0_ADDB_CURSOR_REC_EVENT only
   retrieves event records.

   @test m0_addb_cursor_init() with flags == M0_ADDB_CURSOR_REC_EVENT only
   retrieves context type records.

   @test m0_addb_cursor_init() with flags == M0_ADDB_CURSOR_REC_ANY retrieves
   all records.

 */

/**
   @defgroup addb_retrieval_pvt ADDB Retrieval Internal Interfaces
   @ingroup addb_pvt
   @see @ref addb "Analysis and Data-Base API"
   @{
 */

/**
 * An ADDB segment iterator for use over a stob.
 */
struct stob_retrieval_iter {
	uint64_t                    sri_magic;
	struct m0_addb_segment_iter sri_base;
	struct m0_stob             *sri_stob;
	/** Segment size of the stob */
	m0_bcount_t                 sri_segsize;
	/** Trailer buffer size (may be larger than trailer itself) */
	m0_bcount_t                 sri_trlsize;
	/** Cached stob bshift value */
	uint32_t                    sri_bshift;
	/* following fields are used to hold current segment and perform I/O */
	struct m0_bufvec            sri_buf;
	struct m0_stob_io           sri_io;
	m0_bcount_t                 sri_buf_v_count;
	void                       *sri_buf_ov_buf;
	m0_bcount_t                 sri_io_v_count;
	m0_bindex_t                 sri_io_iv_index;

	/* following fields are used to hold trailer and perform trailer I/O */
	struct m0_bufvec            sri_tbuf;
	struct m0_stob_io           sri_tio;
	m0_bcount_t                 sri_tbuf_v_count;
	void                       *sri_tbuf_ov_buf;
	m0_bcount_t                 sri_tio_v_count;
	m0_bindex_t                 sri_tio_iv_index;
};

/**
 * Helper for determining the segment size of an ADDB stob on first use.
 * @return On success, the positive segment size is returned.
 */
static int stob_retrieval_segsize_get(struct m0_stob *stob)
{
	struct m0_bufvec          sri_buf;
	struct m0_stob_io         sri_io;
	m0_bcount_t               sri_buf_v_count;
	void                     *sri_buf_ov_buf;
	m0_bcount_t               sri_io_v_count;
	m0_bindex_t               sri_io_iv_index;
	struct m0_indexvec       *iv;
	struct m0_bufvec         *obuf;
	struct m0_clink           sri_wait;
	struct m0_stob_domain    *dom;
	struct m0_addb_seg_header header;
	struct m0_bufvec_cursor   cur;
	struct m0_dtx             sri_tx;
	m0_bcount_t               header_size;
	uint32_t                  bshift = stob->so_op->sop_block_shift(stob);
	int                       rc;

	header_size = max64u(sizeof header, 1 << bshift);
	rc = m0_bufvec_alloc_aligned(&sri_buf, 1, header_size, bshift);
	if (rc != 0)
		return rc;

	m0_stob_io_init(&sri_io);
	sri_io.si_opcode = SIO_READ;
	m0_clink_init(&sri_wait, NULL);
	m0_clink_add(&sri_io.si_wait, &sri_wait);

	iv = &sri_io.si_stob;
	iv->iv_vec.v_nr = 1;
	iv->iv_vec.v_count = &sri_io_v_count;
	sri_io_v_count = header_size >> bshift;
	iv->iv_index = &sri_io_iv_index;
	sri_io_iv_index = 0;

	obuf = &sri_io.si_user;
	obuf->ov_vec.v_nr = 1;
	obuf->ov_vec.v_count = &sri_buf_v_count;
	sri_buf_v_count = sri_io_v_count;
	obuf->ov_buf = &sri_buf_ov_buf;
	sri_buf_ov_buf = m0_stob_addr_pack(sri_buf.ov_buf[0], bshift);

	dom = stob->so_domain;
	m0_dtx_init(&sri_tx);
	rc = dom->sd_ops->sdo_tx_make(dom, &sri_tx);
	if (rc != 0)
		goto fail_tx;

	do {
		rc = m0_stob_io_launch(&sri_io, stob, &sri_tx, NULL);
		if (rc < 0)
			break;
		while (sri_io.si_state != SIS_IDLE)
			m0_chan_wait(&sri_wait);
		rc = sri_io.si_rc;
		if (rc < 0)
			break;
		if (sri_io.si_count < sri_io_v_count) {
			rc = -ENODATA;
			break;
		}
		m0_bufvec_cursor_init(&cur, &sri_buf);
		rc = stobsink_header_encdec(&header, &cur, M0_BUFVEC_DECODE);
		M0_ASSERT(rc == 0); /* fail iff short buffer */

		/* AD stob returns zero filled block at EOF */
		if (header.sh_seq_nr == 0 &&
		    header.sh_ver_nr == 0 &&
		    header.sh_segsize == 0) {
			rc = -ENODATA;
			break;
		}
		if (header.sh_seq_nr == 0 ||
		    header.sh_segsize == 0 ||
		    header.sh_segsize > INT32_MAX ||
		    header.sh_ver_nr != STOBSINK_XCODE_VER_NR) {
			rc = -EINVAL;
			break;
		}
		rc = header.sh_segsize;
	} while (0);

	m0_dtx_done(&sri_tx);
fail_tx:
	m0_clink_del(&sri_wait);
	m0_stob_io_fini(&sri_io);
	m0_bufvec_free_aligned(&sri_buf, bshift);
	M0_ASSERT(rc != 0);
	return rc;
}

/**
 * Subroutine that implements m0_addb_segment_iter::asi_next() for the
 * stob retrieval iterator.
 */
static int stob_retrieval_iter_next(struct m0_addb_segment_iter *iter,
				    struct m0_bufvec_cursor     *cur)
{
	struct stob_retrieval_iter *si;
	struct m0_stob             *stob;
	struct m0_stob_domain      *dom;
	struct m0_dtx               sri_tx;
	struct m0_clink             sri_wait;
	struct m0_addb_seg_header   header;
	struct m0_addb_seg_trailer  trailer;
	int                         rc;

	M0_PRE(iter != NULL && cur != NULL);
	si = container_of(iter, struct stob_retrieval_iter, sri_base);
	M0_PRE(si->sri_magic == M0_ADDB_STOBRET_MAGIC);

	stob = si->sri_stob;
	dom = stob->so_domain;
	m0_dtx_init(&sri_tx);
	rc = dom->sd_ops->sdo_tx_make(dom, &sri_tx);
	if (rc != 0)
		return rc;

	m0_clink_init(&sri_wait, NULL);

	while (1) {
		/* read trailer first */
		si->sri_tio.si_obj = NULL;
		si->sri_tio.si_rc = 0;
		si->sri_tio.si_count = 0;
		m0_clink_add(&si->sri_tio.si_wait, &sri_wait);
		rc = m0_stob_io_launch(&si->sri_tio, stob, &sri_tx, NULL);
		if (rc < 0) {
			m0_clink_del(&sri_wait);
			break;
		}
		while (si->sri_tio.si_state != SIS_IDLE)
			m0_chan_wait(&sri_wait);
		m0_clink_del(&sri_wait);
		rc = si->sri_tio.si_rc;
		if (rc < 0)
			break;
		if (si->sri_tio.si_count < si->sri_tio_v_count) {
			rc = 0;
			break;
		}
		m0_bufvec_cursor_init(cur, &si->sri_tbuf);
		m0_bufvec_cursor_move(cur, si->sri_trlsize - sizeof trailer);
		rc = stobsink_trailer_encdec(&trailer, cur, M0_BUFVEC_DECODE);
		M0_ASSERT(rc == 0); /* fail iff short buffer */

		/* now read the whole segment */
		si->sri_io.si_obj = NULL;
		si->sri_io.si_rc = 0;
		si->sri_io.si_count = 0;
		m0_clink_add(&si->sri_io.si_wait, &sri_wait);
		rc = m0_stob_io_launch(&si->sri_io, stob, &sri_tx, NULL);
		if (rc < 0) {
			m0_clink_del(&sri_wait);
			break;
		}
		while (si->sri_io.si_state != SIS_IDLE)
			m0_chan_wait(&sri_wait);
		m0_clink_del(&sri_wait);
		rc = si->sri_io.si_rc;
		if (rc < 0)
			break;
		if (si->sri_io.si_count < si->sri_io_v_count) {
			rc = 0;
			break;
		}

		m0_bufvec_cursor_init(cur, &si->sri_buf);
		rc = stobsink_header_encdec(&header, cur, M0_BUFVEC_DECODE);
		M0_ASSERT(rc == 0); /* fail iff short buffer */

		/* AD stob returns zero filled block at EOF */
		if (header.sh_seq_nr == 0 &&
		    header.sh_ver_nr == 0 &&
		    header.sh_segsize == 0) {
			rc = 0;
			break;
		}
		if (header.sh_seq_nr == 0 ||
		    header.sh_segsize != si->sri_segsize ||
		    header.sh_ver_nr != STOBSINK_XCODE_VER_NR) {
			rc = -EINVAL;
			break;
		}

		si->sri_io_iv_index += si->sri_segsize >> si->sri_bshift;
		si->sri_tio_iv_index += si->sri_segsize >> si->sri_bshift;
		if (header.sh_seq_nr != trailer.st_seq_nr ||
		    trailer.st_rec_nr == 0)
			continue;

		m0_dtx_done(&sri_tx);
		return trailer.st_rec_nr;
	}

	m0_dtx_done(&sri_tx);
	M0_POST(rc <= 0);
	return rc;
}

/**
 * Helper function to free the m0_addb_rec allocated by xcode decode.
 */
static void addb_cursor_rec_free(struct m0_addb_cursor *cur)
{
	M0_PRE(cur != NULL);
	if (cur->ac_rec != NULL) {
		struct m0_xcode_obj obj = {
			.xo_type = m0_addb_rec_xc,
			.xo_ptr  = cur->ac_rec,
		};

		m0_xcode_free(&obj);
		cur->ac_rec = NULL;
	}
}

/**
 * Retrieve the next record in the repository, regardless of record type.
 */
static int addb_cursor_next(struct m0_addb_cursor *cur,
			    struct m0_addb_rec **rec)
{
	int rc = 0;

	M0_PRE(rec != NULL && cur != NULL && cur->ac_iter != NULL);
	addb_cursor_rec_free(cur);
	*rec = NULL;
	if (cur->ac_rec_nr == 0) {
		rc = cur->ac_iter->asi_next(cur->ac_iter, &cur->ac_cur);
		if (rc == 0) {
			rc = -ENODATA;
		} else if (rc > 0) {
			cur->ac_rec_nr = rc;
			rc = 0;
		}
	}
	if (rc == 0) {
		rc = addb_rec_encdec(&cur->ac_rec, &cur->ac_cur,
				     M0_BUFVEC_DECODE);
		if (rc != 0) {
			addb_cursor_rec_free(cur);
			cur->ac_rec_nr = 0;
		} else {
			cur->ac_rec_nr--;
			*rec = cur->ac_rec;
		}
	}

	return rc;
}

/** @} */ /* end of addb_retrieval_pvt group */

/*
 ******************************************************************************
 * Public interfaces
 ******************************************************************************
 */

/**
   @addtogroup addb_retrieval ADDB Record Retrieval
   @{
 */

M0_INTERNAL int m0_addb_stob_iter_alloc(struct m0_addb_segment_iter **iter,
					struct m0_stob *stob)
{
	struct stob_retrieval_iter *si;
	uint32_t                    bshift;
	struct m0_indexvec         *iv;
	struct m0_bufvec           *obuf;
	m0_bcount_t                 trailer_size;
	int rc;

	M0_PRE(iter != NULL && stob != NULL);
	M0_ALLOC_PTR(si);
	if (si == NULL)
		return -ENOMEM;
	si->sri_base.asi_next = stob_retrieval_iter_next;
	rc = stob_retrieval_segsize_get(stob);
	if (rc < 0) {
		m0_free(si);
		return rc;
	}
	si->sri_segsize = rc;
	bshift = stob->so_op->sop_block_shift(stob);
	si->sri_bshift = bshift;
	si->sri_stob = stob;
	rc = m0_bufvec_alloc_aligned(&si->sri_buf, 1, si->sri_segsize, bshift);
	if (rc != 0) {
		m0_free(si);
		return rc;
	}

	trailer_size = max64u(sizeof(struct m0_addb_seg_trailer), 1 << bshift);
	si->sri_trlsize = trailer_size;
	rc = m0_bufvec_alloc_aligned(&si->sri_tbuf, 1, trailer_size, bshift);
	if (rc != 0) {
		m0_bufvec_free_aligned(&si->sri_buf, bshift);
		m0_free(si);
		return rc;
	}

	m0_stob_get(stob);

	m0_stob_io_init(&si->sri_io);
	si->sri_io.si_opcode = SIO_READ;

	iv = &si->sri_io.si_stob;
	iv->iv_vec.v_nr = 1;
	iv->iv_vec.v_count = &si->sri_io_v_count;
	si->sri_io_v_count = si->sri_segsize >> bshift;
	iv->iv_index = &si->sri_io_iv_index;
	si->sri_io_iv_index = 0;

	obuf = &si->sri_io.si_user;
	obuf->ov_vec.v_nr = 1;
	obuf->ov_vec.v_count = &si->sri_buf_v_count;
	si->sri_buf_v_count = si->sri_io_v_count;
	obuf->ov_buf = &si->sri_buf_ov_buf;
	si->sri_buf_ov_buf = m0_stob_addr_pack(si->sri_buf.ov_buf[0], bshift);

	m0_stob_io_init(&si->sri_tio);
	si->sri_tio.si_opcode = SIO_READ;

	iv = &si->sri_tio.si_stob;
	iv->iv_vec.v_nr = 1;
	iv->iv_vec.v_count = &si->sri_tio_v_count;
	si->sri_tio_v_count = si->sri_trlsize >> bshift;
	iv->iv_index = &si->sri_tio_iv_index;
	si->sri_tio_iv_index = (si->sri_segsize - si->sri_trlsize) >> bshift;
	M0_ASSERT((si->sri_tio_iv_index << bshift) ==
		  (si->sri_segsize - si->sri_trlsize));

	obuf = &si->sri_tio.si_user;
	obuf->ov_vec.v_nr = 1;
	obuf->ov_vec.v_count = &si->sri_tbuf_v_count;
	si->sri_tbuf_v_count = si->sri_tio_v_count;
	obuf->ov_buf = &si->sri_tbuf_ov_buf;
	si->sri_tbuf_ov_buf = m0_stob_addr_pack(si->sri_tbuf.ov_buf[0], bshift);

	si->sri_magic = M0_ADDB_STOBRET_MAGIC;
	*iter = &si->sri_base;

	return 0;
}

M0_INTERNAL void m0_addb_stob_iter_free(struct m0_addb_segment_iter *iter)
{
	struct stob_retrieval_iter *si =
	    container_of(iter, struct stob_retrieval_iter, sri_base);

	if (iter != NULL) {
		M0_ASSERT(si->sri_magic == M0_ADDB_STOBRET_MAGIC);
		m0_stob_put(si->sri_stob);
		m0_stob_io_fini(&si->sri_io);
		m0_bufvec_free_aligned(&si->sri_buf, si->sri_bshift);
		m0_stob_io_fini(&si->sri_tio);
		m0_bufvec_free_aligned(&si->sri_tbuf, si->sri_bshift);
		si->sri_magic = 0;
		m0_free(si);
	}
}

M0_INTERNAL int m0_addb_cursor_init(struct m0_addb_cursor *cur,
				    struct m0_addb_segment_iter *iter,
				    uint32_t flags)
{
	M0_PRE(cur != NULL && iter != NULL);

	if (flags == 0)
		flags = M0_ADDB_CURSOR_REC_ANY;
	M0_SET0(cur);
	cur->ac_iter = iter;
	cur->ac_flags = flags;
	return 0;
}

M0_INTERNAL int m0_addb_cursor_next(struct m0_addb_cursor *cur,
				    struct m0_addb_rec **rec)
{
	uint32_t match;
	int      rc;

	do {
		rc = addb_cursor_next(cur, rec);
		if (rc != 0)
			break;
		match = m0_addb_rec_is_ctx(*rec) ?
		    M0_ADDB_CURSOR_REC_CTX : M0_ADDB_CURSOR_REC_EVENT;
	} while ((cur->ac_flags & match) == 0);

	return rc;
}

M0_INTERNAL void m0_addb_cursor_fini(struct m0_addb_cursor *cur)
{
	cur->ac_iter = NULL;
	addb_cursor_rec_free(cur);
}

/** @} end group addb_retrieval */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
