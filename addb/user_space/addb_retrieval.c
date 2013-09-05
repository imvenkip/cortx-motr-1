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
   struct m0_addb_segment_iter provides three functions.
   - m0_addb_segment_iter::asi_next()
   - m0_addb_segment_iter::asi_nextbuf()
   - m0_addb_segment_iter::asi_fini()

   These functions are described in more detail below.

   Two implementations of the segment iterator is provided, an iterator that
   operates directly over a (possibly live) stob containing ADDB records, and
   an iterator that operates over a file.  It is assumed that the file was
   previously extracted from the stob, eg. by using the stob-based iterator.

   The segment iterator is allocated and initialized by calling either
   m0_addb_stob_iter_alloc() or m0_addb_file_iter_alloc().  These functions
   allocate a new object, stob_segment_iter or file_segment_iter
   respectively, each containing a struct m0_addb_segment_iter, and return a
   pointer to the object on success.  The functions verify that the stob or
   file is a compatible repository by reading its first segment header.  In the
   case of m0_addb_stob_iter_alloc(), the reference count on the provided
   m0_stob object is incremented.

   After allocation, the upper layer can iterate through the segments by
   repeatedly calling either the m0_addb_segment_iter::asi_next() or the
   m0_addb_segment_iter::asi_nextbuf() function.

   Each time m0_addb_segment_iter::asi_next() is called, it reads an additional
   segment from the repository and returns the count of records in the buffer
   and a cursor into the raw record data.  The order in which the segments are
   retrieved is not defined; each implementation may return the segments in a
   different order.  This function is provided primarily for use by the
   record retrieval layer.

   The m0_addb_segment_iter::asi_nextbuf() function behaves similarly, except
   that it returns 0 on success, negative on failure, and it returns a
   pointer to a m0_bufvec containing the entire segment, including the header
   and trailer.  This function is provided primarily to allow a stob to be
   dumped to a regular file.

   In the case of the stob implementation, the functions will read the trailer
   of the next segment and then rewind and read the rest of the segment (this
   avoids reading partial data, as specified in
   @ref ADDB-DLD-DSINK-lspec-reader) and verifies that the segment is complete.
   If the segment is not complete, the segment, is dropped and the next is read
   until a complete segment is read, or the end of the stob is reached.

   The segment iterator is freed by calling m0_addb_segment_iter_free().
   Internally, this function uses m0_addb_segment_iter::asi_fini() to free all
   resources related to the iterator.  In the case of the stob iterator, the
   reference count on the stob is decreased by this call.

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
   iterator.  This should be called before calling m0_addb_segment_iter_free().

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

   @test stob_segment_iter_next() retrieves all segments, exactly once.

   @test addb_segment_iter_nextbuf() retrieves all segments of stob,
   exactly once.

   @test file_retrieval_segsize_get() correctly retrieves segment size.

   @test file_segment_iter_next() retrieves all segments, exactly once.

   @test addb_segment_iter_nextbuf() retrieves all segments of file,
   exactly once.

   @test m0_addb_cursor_init() with flags == M0_ADDB_CURSOR_REC_EVENT only
   retrieves event records.

   @test m0_addb_cursor_init() with flags == M0_ADDB_CURSOR_REC_EVENT only
   retrieves context type records.

   @test m0_addb_cursor_init() with flags == M0_ADDB_CURSOR_REC_ANY retrieves
   all records.

   @test addb cursor APIs work correctly over either a stob or file iterator.
 */

#include <stdio.h>

/**
   @defgroup addb_retrieval_pvt ADDB Retrieval Internal Interfaces
   @ingroup addb_pvt
   @see @ref addb "Analysis and Data-Base API"
   @{
 */

/**
 * Private fields common to both stob and file segment iterators.
 */
struct addb_segment_iter {
	uint64_t                    asi_magic;
	struct m0_addb_segment_iter asi_base;
	/** repository segment size */
	m0_bcount_t                 asi_segsize;
	/** minimum sequence number the iterator will return */
	uint64_t                    asi_min_seq_nr;
	/** sequence number of current segment */
	uint64_t                    asi_seq_nr;
	/** holds current segment */
	struct m0_bufvec            asi_buf;
};

/**
 * An ADDB segment iterator for use over a stob.
 */
struct stob_segment_iter {
	struct addb_segment_iter ssi_base;
	struct m0_stob          *ssi_stob;
	/** Trailer buffer size (may be larger than trailer itself) */
	m0_bcount_t              ssi_trlsize;
	/** Cached stob bshift value */
	uint32_t                 ssi_bshift;
	/* following fields are used to perform I/O */
	struct m0_stob_io        ssi_io;
	m0_bcount_t              ssi_buf_v_count;
	void                    *ssi_buf_ov_buf;
	m0_bcount_t              ssi_io_v_count;
	m0_bindex_t              ssi_io_iv_index;

	/* following fields are used to hold trailer and perform trailer I/O */
	struct m0_bufvec         ssi_tbuf;
	struct m0_stob_io        ssi_tio;
	m0_bcount_t              ssi_tbuf_v_count;
	void                    *ssi_tbuf_ov_buf;
	m0_bcount_t              ssi_tio_v_count;
	m0_bindex_t              ssi_tio_iv_index;
};

/**
 * An ADDB segment iterator for use over a file.
 */
struct file_segment_iter {
	struct addb_segment_iter fsi_base;
	FILE                    *fsi_in;
};

/** Common logic to stob and file segment size determination. */
static int addb_segsize_decode(struct m0_bufvec *buf)
{
	struct m0_bufvec_cursor   cur;
	struct m0_addb_seg_header header;
	int                       rc;

	m0_bufvec_cursor_init(&cur, buf);
	rc = stobsink_header_encdec(&header, &cur, M0_XCODE_DECODE);
	M0_ASSERT(rc == 0); /* fail iff short buffer */

	/* AD stob returns zero filled block at EOF */
	if (header.sh_seq_nr == 0 &&
	    header.sh_ver_nr == 0 &&
	    header.sh_segsize == 0) {
		rc = -ENODATA;
	} else if (header.sh_seq_nr == 0 ||
		   header.sh_segsize == 0 ||
		   header.sh_segsize > INT32_MAX ||
		   header.sh_ver_nr != STOBSINK_XCODE_VER_NR)
		rc = -EINVAL;
	else
		rc = header.sh_segsize;
	return rc;
}

/**
 * Helper for determining the segment size of an ADDB stob on first use.
 * @return On success, the positive segment size is returned.
 */
static int stob_retrieval_segsize_get(struct m0_stob *stob)
{
	struct m0_bufvec       sri_buf;
	struct m0_stob_io      sri_io;
	m0_bcount_t            sri_buf_v_count;
	void                  *sri_buf_ov_buf;
	m0_bcount_t            sri_io_v_count;
	m0_bindex_t            sri_io_iv_index;
	struct m0_indexvec    *iv;
	struct m0_bufvec      *obuf;
	struct m0_clink        sri_wait;
	struct m0_stob_domain *dom;
	m0_bcount_t            header_size;
	uint32_t               bshift = stob->so_op->sop_block_shift(stob);
	int                    rc;

	header_size = max64u(sizeof(struct m0_addb_seg_header), 1 << bshift);
	rc = m0_bufvec_alloc_aligned(&sri_buf, 1, header_size, bshift);
	if (rc != 0)
		return rc;

	m0_stob_io_init(&sri_io);
	sri_io.si_opcode = SIO_READ;
	m0_clink_init(&sri_wait, NULL);
	m0_clink_add_lock(&sri_io.si_wait, &sri_wait);

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
	do {
		rc = m0_stob_io_launch(&sri_io, stob, NULL, NULL);
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
		rc = addb_segsize_decode(&sri_buf);
	} while (0);

	m0_clink_del_lock(&sri_wait);
	m0_stob_io_fini(&sri_io);
	m0_bufvec_free_aligned(&sri_buf, bshift);
	M0_POST(rc != 0);
	return rc;
}

static int stob_segment_iter_next(struct m0_addb_segment_iter *iter,
				  struct m0_bufvec_cursor     *cur)
{
	struct stob_segment_iter  *si;
	struct m0_stob            *stob;
	struct m0_stob_domain     *dom;
	struct m0_clink            seg_wait;
	struct m0_addb_seg_header  header;
	struct m0_addb_seg_trailer trailer;
	m0_bindex_t                offset;
	int                        rc;

	M0_PRE(iter != NULL && cur != NULL);
	si = container_of(iter, struct stob_segment_iter, ssi_base.asi_base);
	M0_PRE(si->ssi_base.asi_magic == M0_ADDB_STOBRET_MAGIC);

	stob = si->ssi_stob;
	dom = stob->so_domain;
	offset = si->ssi_base.asi_segsize >> si->ssi_bshift;
	m0_clink_init(&seg_wait, NULL);

	while (1) {
		/* read trailer first */
		si->ssi_tio.si_obj = NULL;
		si->ssi_tio.si_rc = 0;
		si->ssi_tio.si_count = 0;
		m0_clink_add_lock(&si->ssi_tio.si_wait, &seg_wait);
		rc = m0_stob_io_launch(&si->ssi_tio, stob, NULL, NULL);
		if (rc < 0) {
			m0_clink_del_lock(&seg_wait);
			break;
		}
		while (si->ssi_tio.si_state != SIS_IDLE)
			m0_chan_wait(&seg_wait);
		m0_clink_del_lock(&seg_wait);
		rc = si->ssi_tio.si_rc;
		if (rc < 0)
			break;
		if (si->ssi_tio.si_count < si->ssi_tio_v_count) {
			rc = 0;
			break;
		}
		m0_bufvec_cursor_init(cur, &si->ssi_tbuf);
		m0_bufvec_cursor_move(cur, si->ssi_trlsize - sizeof trailer);
		rc = stobsink_trailer_encdec(&trailer, cur, M0_XCODE_DECODE);
		M0_ASSERT(rc == 0); /* fail iff short buffer */

		/* now read the whole segment */
		si->ssi_io.si_obj = NULL;
		si->ssi_io.si_rc = 0;
		si->ssi_io.si_count = 0;
		m0_clink_add_lock(&si->ssi_io.si_wait, &seg_wait);
		rc = m0_stob_io_launch(&si->ssi_io, stob, NULL, NULL);
		if (rc < 0) {
			m0_clink_del_lock(&seg_wait);
			break;
		}
		while (si->ssi_io.si_state != SIS_IDLE)
			m0_chan_wait(&seg_wait);
		m0_clink_del_lock(&seg_wait);
		rc = si->ssi_io.si_rc;
		if (rc < 0)
			break;
		if (si->ssi_io.si_count < si->ssi_io_v_count) {
			rc = 0;
			break;
		}

		m0_bufvec_cursor_init(cur, &si->ssi_base.asi_buf);
		rc = stobsink_header_encdec(&header, cur, M0_XCODE_DECODE);
		M0_ASSERT(rc == 0); /* fail iff short buffer */

		/* AD stob returns zero filled block at EOF */
		if (header.sh_seq_nr == 0 &&
		    header.sh_ver_nr == 0 &&
		    header.sh_segsize == 0) {
			rc = 0;
			break;
		}
		if (header.sh_seq_nr == 0 ||
		    header.sh_segsize != si->ssi_base.asi_segsize ||
		    header.sh_ver_nr != STOBSINK_XCODE_VER_NR) {
			rc = -EINVAL;
			break;
		}

		si->ssi_io_iv_index += offset;
		si->ssi_tio_iv_index += offset;
		if (header.sh_seq_nr == trailer.st_seq_nr &&
		    trailer.st_rec_nr != 0 &&
		    header.sh_seq_nr >= si->ssi_base.asi_min_seq_nr) {
			si->ssi_base.asi_seq_nr = header.sh_seq_nr;
			return trailer.st_rec_nr;
		}
	}

	si->ssi_base.asi_seq_nr = 0;
	M0_POST(rc <= 0);
	return rc;
}

static uint64_t stob_segment_iter_seq_get(struct m0_addb_segment_iter *iter)
{
	struct addb_segment_iter *ai =
	    container_of(iter, struct addb_segment_iter, asi_base);

	M0_PRE(iter != NULL && ai->asi_magic == M0_ADDB_STOBRET_MAGIC);
	return ai->asi_seq_nr;
}

static void stob_segment_iter_seq_set(struct m0_addb_segment_iter *iter,
				      uint64_t seq_nr)
{
	struct addb_segment_iter *ai =
	    container_of(iter, struct addb_segment_iter, asi_base);

	M0_PRE(iter != NULL && ai->asi_magic == M0_ADDB_STOBRET_MAGIC);
	ai->asi_min_seq_nr = seq_nr;
	ai->asi_seq_nr = 0;
}

static void stob_segment_iter_free(struct m0_addb_segment_iter *iter)
{
	struct stob_segment_iter *si =
	    container_of(iter, struct stob_segment_iter, ssi_base.asi_base);

	M0_ASSERT(iter != NULL &&
		  si->ssi_base.asi_magic == M0_ADDB_STOBRET_MAGIC);
	m0_stob_put(si->ssi_stob);
	m0_stob_io_fini(&si->ssi_io);
	m0_bufvec_free_aligned(&si->ssi_base.asi_buf, si->ssi_bshift);
	m0_stob_io_fini(&si->ssi_tio);
	m0_bufvec_free_aligned(&si->ssi_tbuf, si->ssi_bshift);
	si->ssi_base.asi_magic = 0;
	m0_free(si);
}

/**
 * Helper for determining the segment size of an ADDB file on first use.
 * @return On success, the positive segment size is returned.
 */
static int file_retrieval_segsize_get(FILE *infile)
{
	m0_bcount_t      header_size = sizeof(struct m0_addb_seg_header);
	char             buf[sizeof(struct m0_addb_seg_header)];
	void            *bp = buf;
	struct m0_bufvec fbuf = M0_BUFVEC_INIT_BUF(&bp, &header_size);
	int              rc;

	rc = fread(buf, header_size, 1, infile);
	if (rc != 1)
		return feof(infile) != 0 ? -ENODATA : -errno;
	rc = fseek(infile, 0L, SEEK_SET);
	if (rc < 0)
		return -errno;
	return addb_segsize_decode(&fbuf);
}

static int file_segment_iter_next(struct m0_addb_segment_iter *iter,
				  struct m0_bufvec_cursor     *cur)
{
	struct file_segment_iter  *fi;
	struct m0_addb_seg_header  header;
	struct m0_addb_seg_trailer trailer;
	m0_bcount_t                offset;
	int                        rc;

	M0_PRE(iter != NULL && cur != NULL);
	fi = container_of(iter, struct file_segment_iter, fsi_base.asi_base);
	M0_PRE(fi->fsi_base.asi_magic == M0_ADDB_FILERET_MAGIC);
	offset = fi->fsi_base.asi_segsize - sizeof trailer;

	while (1) {
		/* no concurrent writer, can read whole segment */
		rc = fread(fi->fsi_base.asi_buf.ov_buf[0],
			   fi->fsi_base.asi_segsize, 1, fi->fsi_in);
		if (rc != 1) {
			rc = feof(fi->fsi_in) != 0 ? 0 : -errno;
			break;
		}
		m0_bufvec_cursor_init(cur, &fi->fsi_base.asi_buf);
		m0_bufvec_cursor_move(cur, offset);
		rc = stobsink_trailer_encdec(&trailer, cur, M0_XCODE_DECODE);
		M0_ASSERT(rc == 0); /* fail iff short buffer */

		m0_bufvec_cursor_init(cur, &fi->fsi_base.asi_buf);
		rc = stobsink_header_encdec(&header, cur, M0_XCODE_DECODE);
		M0_ASSERT(rc == 0); /* fail iff short buffer */

		if (header.sh_seq_nr == 0 ||
		    header.sh_segsize != fi->fsi_base.asi_segsize ||
		    header.sh_ver_nr != STOBSINK_XCODE_VER_NR) {
			rc = -EINVAL;
			break;
		}
		if (header.sh_seq_nr == trailer.st_seq_nr &&
		    trailer.st_rec_nr != 0 &&
		    header.sh_seq_nr >= fi->fsi_base.asi_min_seq_nr) {
			fi->fsi_base.asi_seq_nr = header.sh_seq_nr;
			return trailer.st_rec_nr;
		}
	}

	fi->fsi_base.asi_seq_nr = 0;
	M0_POST(rc <= 0);
	return rc;
}

static uint64_t file_segment_iter_seq_get(struct m0_addb_segment_iter *iter)
{
	struct addb_segment_iter *ai =
	    container_of(iter, struct addb_segment_iter, asi_base);

	M0_PRE(iter != NULL && ai->asi_magic == M0_ADDB_FILERET_MAGIC);
	return ai->asi_seq_nr;
}

static void file_segment_iter_seq_set(struct m0_addb_segment_iter *iter,
				      uint64_t seq_nr)
{
	struct addb_segment_iter *ai =
	    container_of(iter, struct addb_segment_iter, asi_base);

	M0_PRE(iter != NULL && ai->asi_magic == M0_ADDB_FILERET_MAGIC);
	ai->asi_min_seq_nr = seq_nr;
}

static void file_segment_iter_free(struct m0_addb_segment_iter *iter)
{
	struct file_segment_iter *fi =
	    container_of(iter, struct file_segment_iter, fsi_base.asi_base);

	M0_ASSERT(iter != NULL &&
		  fi->fsi_base.asi_magic == M0_ADDB_FILERET_MAGIC);
	fclose(fi->fsi_in);
	m0_bufvec_free(&fi->fsi_base.asi_buf);
	fi->fsi_base.asi_magic = 0;
	m0_free(fi);
}

static int addb_segment_iter_nextbuf(struct m0_addb_segment_iter *iter,
				     const struct m0_bufvec     **bv)
{
	struct m0_bufvec_cursor   cur;
	struct addb_segment_iter *ai;
	int                       rc;

	M0_PRE(iter != NULL && bv != NULL);
	rc = iter->asi_next(iter, &cur);
	if (rc == 0)
		rc = -ENODATA;
	if (rc < 0)
		return rc;

	ai = container_of(iter, struct addb_segment_iter, asi_base);
	*bv = &ai->asi_buf;
	return 0;
}

/** Helper function to free the m0_addb_rec allocated by xcode decode. */
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
				     M0_XCODE_DECODE);
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
	struct stob_segment_iter *si;
	uint32_t                  bshift;
	struct m0_indexvec       *iv;
	struct m0_bufvec         *obuf;
	m0_bcount_t               trailer_size;
	int                       rc;

	M0_PRE(iter != NULL && stob != NULL);
	M0_ALLOC_PTR(si);
	if (si == NULL)
		return -ENOMEM;
	si->ssi_base.asi_base.asi_next = stob_segment_iter_next;
	si->ssi_base.asi_base.asi_nextbuf = addb_segment_iter_nextbuf;
	si->ssi_base.asi_base.asi_seq_get = stob_segment_iter_seq_get;
	si->ssi_base.asi_base.asi_seq_set = stob_segment_iter_seq_set;
	si->ssi_base.asi_base.asi_free = stob_segment_iter_free;
	rc = stob_retrieval_segsize_get(stob);
	if (rc < 0) {
		m0_free(si);
		return rc;
	}
	si->ssi_base.asi_segsize = rc;
	bshift = stob->so_op->sop_block_shift(stob);
	si->ssi_bshift = bshift;
	si->ssi_stob = stob;
	rc = m0_bufvec_alloc_aligned(&si->ssi_base.asi_buf, 1,
				     si->ssi_base.asi_segsize, bshift);
	if (rc != 0) {
		m0_free(si);
		return rc;
	}

	trailer_size = max64u(sizeof(struct m0_addb_seg_trailer), 1 << bshift);
	si->ssi_trlsize = trailer_size;
	rc = m0_bufvec_alloc_aligned(&si->ssi_tbuf, 1, trailer_size, bshift);
	if (rc != 0) {
		m0_bufvec_free_aligned(&si->ssi_base.asi_buf, bshift);
		m0_free(si);
		return rc;
	}

	m0_stob_get(stob);

	m0_stob_io_init(&si->ssi_io);
	si->ssi_io.si_opcode = SIO_READ;

	iv = &si->ssi_io.si_stob;
	iv->iv_vec.v_nr = 1;
	iv->iv_vec.v_count = &si->ssi_io_v_count;
	si->ssi_io_v_count = si->ssi_base.asi_segsize >> bshift;
	iv->iv_index = &si->ssi_io_iv_index;
	si->ssi_io_iv_index = 0;

	obuf = &si->ssi_io.si_user;
	obuf->ov_vec.v_nr = 1;
	obuf->ov_vec.v_count = &si->ssi_buf_v_count;
	si->ssi_buf_v_count = si->ssi_io_v_count;
	obuf->ov_buf = &si->ssi_buf_ov_buf;
	si->ssi_buf_ov_buf =
	    m0_stob_addr_pack(si->ssi_base.asi_buf.ov_buf[0], bshift);

	m0_stob_io_init(&si->ssi_tio);
	si->ssi_tio.si_opcode = SIO_READ;

	iv = &si->ssi_tio.si_stob;
	iv->iv_vec.v_nr = 1;
	iv->iv_vec.v_count = &si->ssi_tio_v_count;
	si->ssi_tio_v_count = si->ssi_trlsize >> bshift;
	iv->iv_index = &si->ssi_tio_iv_index;
	si->ssi_tio_iv_index =
	    (si->ssi_base.asi_segsize - si->ssi_trlsize) >> bshift;
	M0_ASSERT((si->ssi_tio_iv_index << bshift) ==
		  (si->ssi_base.asi_segsize - si->ssi_trlsize));

	obuf = &si->ssi_tio.si_user;
	obuf->ov_vec.v_nr = 1;
	obuf->ov_vec.v_count = &si->ssi_tbuf_v_count;
	si->ssi_tbuf_v_count = si->ssi_tio_v_count;
	obuf->ov_buf = &si->ssi_tbuf_ov_buf;
	si->ssi_tbuf_ov_buf = m0_stob_addr_pack(si->ssi_tbuf.ov_buf[0], bshift);

	si->ssi_base.asi_magic = M0_ADDB_STOBRET_MAGIC;
	*iter = &si->ssi_base.asi_base;

	return 0;
}

M0_INTERNAL int m0_addb_file_iter_alloc(struct m0_addb_segment_iter **iter,
					const char *path)
{
	struct file_segment_iter *fi;
	FILE                     *infile;
	int                       rc;

	M0_PRE(iter != NULL && path != NULL);
	infile = fopen(path, "r");
	if (infile == NULL)
		return -errno;

	M0_ALLOC_PTR(fi);
	if (fi == NULL) {
		fclose(infile);
		return -ENOMEM;
	}
	fi->fsi_base.asi_base.asi_next = file_segment_iter_next;
	fi->fsi_base.asi_base.asi_nextbuf = addb_segment_iter_nextbuf;
	fi->fsi_base.asi_base.asi_seq_get = file_segment_iter_seq_get;
	fi->fsi_base.asi_base.asi_seq_set = file_segment_iter_seq_set;
	fi->fsi_base.asi_base.asi_free = file_segment_iter_free;
	fi->fsi_in = infile;

	rc = file_retrieval_segsize_get(infile);
	if (rc < 0) {
		fclose(infile);
		m0_free(fi);
		return rc;
	}
	fi->fsi_base.asi_segsize = rc;
	rc = m0_bufvec_alloc(&fi->fsi_base.asi_buf, 1, rc);
	if (rc != 0) {
		fclose(infile);
		m0_free(fi);
	} else {
		fi->fsi_base.asi_magic = M0_ADDB_FILERET_MAGIC;
		*iter = &fi->fsi_base.asi_base;
	}
	return rc;
}

M0_INTERNAL void m0_addb_segment_iter_free(struct m0_addb_segment_iter *iter)
{
	if (iter != NULL)
		iter->asi_free(iter);
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
