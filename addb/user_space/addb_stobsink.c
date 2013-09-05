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
 * Original creation: 09/04/2012
 */

/**
   @page ADDB-DLD-DSINK Stob Sink and Repository Detailed Design

   This is a component DLD and hence not all sections are present.
   Refer to the @ref ADDB-DLD "ADDB Detailed Design"
   for the design requirements.

   - @ref ADDB-DLD-DSINK-ovw
   - @ref ADDB-DLD-DSINK-depends
   - @ref ADDB-DLD-DSINK-fspec
   - @ref ADDB-DLD-DSINK-lspec
      - @ref ADDB-DLD-DSINK-lspec-repo
      - @ref ADDB-DLD-DSINK-lspec-sink
      - @ref ADDB-DLD-DSINK-lspec-reader
      - @ref ADDB-DLD-DSINK-lspec-state
      - @ref ADDB-DLD-DSINK-lspec-thread
      - @ref ADDB-DLD-DSINK-lspec-numa
   - @ref ADDB-DLD-DSINK-ut
   - @ref ADDB-DLD-DSINK-O

   <hr>
   @section ADDB-DLD-DSINK-ovw Overview

   The ADDB Stob Sink is used by an @ref ADDB-DLD-lspec-mc "ADDB Machine"
   to store ADDB records persistently on disk using the @ref stob interface.

   This persistent store, or repository, contains a stream of ADDB records.
   When the repository fills, the stream wraps around to the beginning in
   and the stream continues in a circular manner.  This circular behavior
   continues across instances of the stob sink, i.e. on restart, the stob
   sink continues writing where it left off.

   The structure of an ADDB repository is designed to support a stob sink and
   record retrieval operating simultaneously, and with no synchronization.

   <hr>
   @section ADDB-DLD-DSINK-depends Dependencies

   - The @ref stob "Storage objects" module is used to store data persistently.

   <hr>
   @section ADDB-DLD-DSINK-fspec Functional Specification

   @see @ref addb_stobsink

   <hr>
   @section ADDB-DLD-DSINK-lspec Logical Specification
   - @ref ADDB-DLD-DSINK-lspec-repo
   - @ref ADDB-DLD-DSINK-lspec-sink
   - @ref ADDB-DLD-DSINK-lspec-reader
   - @ref ADDB-DLD-DSINK-lspec-state
   - @ref ADDB-DLD-DSINK-lspec-thread
   - @ref ADDB-DLD-DSINK-lspec-numa

   @subsection ADDB-DLD-DSINK-lspec-repo The Repository Structure and Behavior

   An ADDB repository in a stob is divided into logical segments.  Each
   segment starts at a known offset from the start of the stob.  For simplicity,
   we specify that the interval between the start of each segment be the same
   for all segments.  This interval is configurable, but once chosen for a given
   ADDB repository, it does not change.

   A header is written at these known offsets by the stob sink as it streams
   records to the repository.  The header contains a sequence number and the
   segment size (this segment size need only be written to the header at offset
   0 of the stob, but it is convenient to have headers of the same size in all
   segments).  The sequence number is incremented each time a header is written
   to the repository.  After the header, ADDB records are streamed sequentially
   until the end of the segment is reached (when the next record would cross the
   segment boundary).  In addition, just before the end of a segment, the stob
   sink writes a trailer.  This trailer contains the same sequence number as
   that segment's header and a count of the number of records in that segment.
   There may need to be some padding between the end of the final record in the
   segment and the trailer, so that the trailer is also at a known offset.  This
   results in the structure shown below.

   @verbatim
   +---------+---------+---------+-----+---------+-----+----------+---------+
   | Header0 | Record0 | Record1 | ... | RecordN | pad | Trailer0 | Header1 |...
   +---------+---------+---------+-----+---------+-----+----------+---------+
   ^                                                              ^
   |                                                              |
   offset 0                                        segment interval
   @endverbatim

   The segment size must be large enough to allow an entire ADDB record
   sequence received via the ADDB service to be streamed into a single segment,
   along with the header and trailer overhead.  The upper bound on the size
   of such an ADDB record sequence is the RPC maximum message size, currently
   configured to be 168K.

   During record retrieval, segments are read sequentially from the stob until
   the end of the stob is reached.  To handle a reader and a writer overlapping,
   the reader must first seek forward and read the trailer, then read the whole
   segment and compare the sequence number of the header with that of the
   trailer.  If they differ, an overlap has occurred and the segment is
   discarded.

   In the general case, once wrap-around has occurred, sequence numbers in the
   segment headers of the stob look something like

   @verbatim
   N, N + 1, ..., N + K, M, M + 1, ..., M + L
   @endverbatim

   where N + K is the largest sequence number and N + K is the latest segment
   written.  We also have M + L + 1 == N and M < N.  In practice, it is not
   strictly required that the sequence numbers of each segment be one greater
   than the sequence number of the immediately preceding segment, only that
   they be strictly monotonically increasing and that both M < N and M + L < N.

   @subsection ADDB-DLD-DSINK-lspec-sink The Stob Sink

   There are three main phases to the operation of the stob sink.
   -# Startup: In this phase, the stob sink determines where to continue writing
      records within the circular structure of the repository.
   -# Running: In this phase, the stob sink is ready to store ADDB records
      in the repository.
   -# Shutdown: In this phase, the stob sink writes a trailer for the current
      segment and terminates.

   The Startup phase, completed during m0_addb_mc_configure_stob_sink(),
   allocates resources, verifies that the stob can be used, and finds the
   correct position in the stob where the next ADDB records should be saved.
   First, the header at offset 0 of the stob is read.  When the stob is empty,
   no header will exist, so the sequence number is initialized to 1 and the
   current segment offset is set to 0.  When the stob is not empty, the segment
   size records in the header must match the segment size passed to
   m0_addb_mc_configure_stob_sink().  Then, a binary search of the segments is
   used to find the last segment written to the stob (segment N + K in the
   earlier diagram).  The binary search employs an algorithm like the following
   pseudo-code:

   @verbatim
   left = 0;
   right = stob_size_in_intervals;
   header_left = header_at(left);
   header_right = header_at(right);
   while (left != right) {
        mid = left + (right - left) / 2;
        header_mid = header_at(mid);
        if (header_mid->seqno <= header_right->seqno)
             right = mid;
        else
             left = mid;
   }
   @endverbatim

   Note that in the case where the stob sink has not yet reached the final
   segment in the stob, a binary search is instead used to find the final
   segment written.  In this case, the search is for the largest segment offset
   with a segment header written.

   The stobsink::ss_seq_nr is initialized to one greater than the sequence
   number of the found segment.  The offset for the next segment is set to the
   offset of segment M in the earlier diagram, 0 if the largest segment happens
   to be at the end of the stob.  A pool of segment-sized buffers is allocated
   for use during the Running phase.  A buffer is selected from this pool, in
   round-robin fashion, and made the current buffer.  Finally, the reference
   count on the supplied m0_stob is incremented.

   While in the Running phase, the stob sink saves ADDB records in the
   repository.  Records are saved individually using the
   m0_addb_mc_recsink::rs_save() function, implemented by stobsink_save().
   Sequences received via the ADDB service are saved using the
   m0_addb_mc_recsink::rs_save_seq() function, implemented by
   stobsink_save_seq().

   Records are buffered in a segment-sized buffer.  Buffering is used to improve
   the space utilization of the stob and to improve performance by avoiding a
   many small writes.  Even in the case of record sequences, we expect the size
   of the sequence to typically be small, because ADDB record sequences sent by
   the client are packed into an RPC packet to fill it up.

   When a new buffer is selected to be the current buffer, the stobsink::ss_cur
   is set to point into the buffer, leaving room at the start for the header
   (the header is written just before the buffer is flushed).

   While holding the stobsink::ss_mutex, stobsink_save() and stobsink_save_seq()
   check that the record or record sequence will fit in the current buffer.  If
   so, the record or record sequence is simply appended and the mutex released.

   If the record or record sequence will not fit, the current buffer must be
   first be flushed.  A header is written to the start of the buffer.  A
   trailer, as described above, is written at the very end of the buffer.  The
   buffer's stobsink_poolbuf::spb_busy flag is set.  The stobsink::ss_seq_nr is
   incremented, the next buffer in the pool is selected to be the current
   buffer.  The stobsink::ss_mutex is then released.  Finally, the full buffer
   is flushed to the stob asynchronously.  The stobsink::ss_mutex must be
   re-established before stobsink_save() or stobsink_save_seq() can attempt to
   append the pending record or record sequence to the new current buffer.  The
   logic of checking that the record or record sequence will fit must be
   re-performed, to handle the cases of concurrent use of the global ADDB
   machine and where the stob layer may have posted ADDB records of its own.

   When an asynchronous stob operation completes, the stobsink_chan_cb() is
   called by the stob layer.  This will reset the stobsink_poolbuf::spb_busy,
   also while holding stobsink::ss_mutex.

   @todo merge m0_chan external lock and use ss_mutex to lock the channel

   In an unloaded stob sink, even with a relatively small segment size, it is
   possible that a long time will pass before a segment fills up.  A
   configurable timeout can be used to cause the writer to write the header and
   trailer for the current segment and flush it.  After such a timeout, the
   writer can continue to save additional records to the current segment.
   However, before flushing it again, the sequence number in the header and
   trailer must once again be incremented.

   The stob sink moves to the Shutdown phase when the final reference to the
   stob sink is released using the m0_addb_mc_recsink::rs_put() function,
   implemented by stobsink_put() and stobsink_release().  During shutdown, a
   trailer is written at the end of the current buffer and the buffer is flushed
   to the stob, synchronously.  In the unlikely case that no records are in the
   buffer, the buffer is not flushed.  The buffer pool and the stob are then
   released.

   Note that on abnormal shutdown, the stob sink will not have the opportunity
   to flush the current segment, so any such unflushed records will be lost.
   We expect trace to be used to debug abnormal shutdown, not ADDB, so the loss
   of such records should not be overly detrimental.

   @subsection ADDB-DLD-DSINK-lspec-reader The Repository Reader

   This is not a full specification of the Repository Reader.  Only those
   aspects of the reader that are motivated directly by the stob sink and
   repository layout are covered here.

   - The repository cat be read directly using the @ref stob interface.
     Alternatively, given that the current implementations of the stob API store
     the data in files or devices accessible via a pathname, data in a stob can
     be copied using a ADDB-specific copy tool and processed separately.
   - The reader (or the copy tool) must read an entire segment before processing
     any of it.  More specifically, the reader must first read the trailer, then
     read the whole segment.  The copy tool must have knowledge of this
     requirement; a simple @a cat or @c dd CLI will not suffice for a live ADDB
     repository, as such tools cannot guarantee an ordering of blocks read and
     written simultaneously in the repository.
   - The reader must compare the sequence number of the header and trailer.
     - If they are the same, the records in the segment can be processed.
     - If they differ, the segment must be ignored.

   @see ADDB-DLD-RETRV

   @subsection ADDB-DLD-DSINK-lspec-state State Specification

   The stob sink effectively has a single Running state.  It exits its
   Startup phase on successful return from m0_addb_mc_configure_stob_sink()
   and enters its Shutdown phase when the final reference to the stob is
   removed, causing stobsink_release() to be called.

   @subsection ADDB-DLD-DSINK-lspec-thread Threading and Concurrency Model

   The stob sink is only used with an ADDB machine in a Mero server.  Such an
   ADDB machine is intended to be set up by a request handler and used within
   its scope, with the request handler synchronizing its own use of the ADDB
   machine.  However, one such Mero server ADDB machine is designated as the
   global default ADDB machine, ::m0_addb_gmc.  As such, a mutex,
   stobsink::ss_mutex, is used to serialize access through its implementation of
   the Record-Sink interface.

   The stobsink_poolbuf::spb_busy flag is used to keep the stob sink interfaces
   from using a buffer that is awaiting completion.  After the asynchronous
   completion notification is received, such buffers are available for re-use,
   by resetting the flag, also while holding the stobsink::ss_mutex.  Although
   a buffer pool is allocated when the stob sink is initialized, it can also be
   expanded by the stob sink interfaces in the case where the next buffer in the
   list is still busy, awaiting completion.

   The stobsink::ss_mutex must always be released before launching a stob IO
   request.  This allows the stob layer to use ADDB, which might involve use
   of the same stob sink, to log exceptions.

   @subsection ADDB-DLD-DSINK-lspec-numa NUMA optimizations

   NUMA optimizations are handled at higher layers, specifically at the layer of
   the request handler.  The stob sink creates no threads of its own, using only
   the thread of its caller.  However, there are NUMA implications on the stob
   sink.  The asynchronous callbacks made when a stob IO operation completes may
   not be in the same locality as the locality that launched the stob IO
   operation; this could be controlled by the higher layers such that the stob
   provided to the stob sink is configured to use the same locality as that of
   the request handler.  Also, uses of the global default ADDB machine may occur
   in any locality.  However, the global default ADDB machine should only be
   used for exceptions, and therefore should have a limited impact.

   <hr>
   @section ADDB-DLD-DSINK-ut Unit Tests

   These unit tests cover only the stob sink.  Retrieval of records from
   the stob are part of the @ref ADDB-DLD-RETRV.

   @test Startup with an empty stob correctly sets up the stobsink structure.

   @test Saving individual records and sequences to a non-full buffer succeed,
   and do not write immediately.

   @test Saving individual records and sequences to a full buffer succeeds,
   and causes a write operation to be launched to the stob and the fields of
   the stobsink structure to be positioned correctly for the next buffer.

   @test After a segment is persisted, its header can be read during startup
   conditions.

   @test Binary search for current segment must work for an empty stob, a partly
   full stob (never wrapped around), after wrap-around, and the special case
   where the last written segment is at the end of the stob.

   @test Segments that are persisted have correct headers and trailers and
   contents.  Their records can be decoded and the data corresponds to the
   records and sequences that were saved.  This test does not use the exposed
   retrieval interface.

   <hr>
   @section ADDB-DLD-DSINK-O Analysis

   To allow a repository segment to be processed as a single unit, the segment
   size should be kept relatively small, to avoid excessive buffering during
   record retrieval.  In addition, the larger the segment, the greater the
   chance of an overlap and the need to discard the segment.  Segment sizes of
   256K to 1M seem sufficient to meet both the needs of the writer and the
   reader.

   We assume that in a production environment, the size of the stob will be
   large compare to the size of a segment (stob size on the order of GB to
   TB, segment size <= 1MB).  As such, the overhead of the header, trailer
   and padding, even in the case of shutdown, is not considered significant.

 */

#include "lib/refs.h"		/* m0_ref */
#include "lib/locality.h"	/* m0_locality0_get */
#include "dtm/dtm.h"		/* m0_dtx */
#include "stob/stob.h"

/**
   @defgroup addb_stobsink ADDB Stob Sink Interfaces
   @ingroup addb_pvt
   @see @ref addb "Analysis and Data-Base API"
   @{
 */

struct stobsink;

/**
 * Buffers and their corresponding stob IO operation structures.  These are
 * linked together into a pool rooted at stobsink::ss_pool.
 */
struct stobsink_poolbuf {
	uint64_t                  spb_magic;
	struct m0_tlink           spb_link;
	struct m0_clink           spb_wait;
	/** Back pointer to the stob sink */
	struct stobsink          *spb_sink;

	/** The buffer is awaiting stob io completion callback */
	bool                      spb_busy;
	struct m0_bufvec          spb_buf;
	struct m0_stob_io         spb_io;
	/** The transaction object for spb_io */
	struct m0_dtx             spb_tx;

	/* following fields are used as size[1] arrays of spb_buf and spb_io */
	m0_bcount_t               spb_buf_v_count;
	void                     *spb_buf_ov_buf;
	m0_bcount_t               spb_io_v_count;
	m0_bindex_t               spb_io_iv_index;
	/**
	 * Fol record part representing stob io operations.
	 * It should be pointed by m0_stob_io::si_fol_rec_part.
	 */
        struct m0_fol_rec_part    spb_fol_rec_part;
};

M0_TL_DESCR_DEFINE(stobsink_pool, "stobsink pool", static,
		   struct stobsink_poolbuf, spb_link, spb_magic,
		   M0_ADDB_STOBSINK_BUF_MAGIC, M0_ADDB_STOBSINK_MAGIC);
M0_TL_DEFINE(stobsink_pool, static, struct stobsink_poolbuf);

/**
 * Internal stob sink structure.
 */
struct stobsink {
	/** Reference count */
	struct m0_ref             ss_ref;
	/** Synchronize access to the stobsink in case of global machine */
	struct m0_mutex           ss_mutex;
	/** Segment size of the ADDB stob */
	m0_bcount_t               ss_segsize;
	/** Cached ADDB stob bshift value */
	uint32_t                  ss_bshift;
	/** Maximum number of segments in the ADDB stob */
	size_t                    ss_seg_nr;
	/** Time most recent segment was persisted */
	m0_time_t                 ss_persist_time;
	/** Maximum relative time until a segment is persisted */
	m0_time_t                 ss_timeout;
	/** The stob for this stob sink */
	struct m0_stob           *ss_stob;
	/** Current segment sequence number */
	uint64_t                  ss_seq_nr;
	/** The offset of the current segment in the stob */
	m0_bindex_t               ss_offset;
	/** Number of records in the current segment */
	uint32_t                  ss_record_nr;
	/** Number of records when current segment was last persisted */
	uint32_t                  ss_persist_nr;
	/** stob is synchronous, stobsink_chan_cb() does not consume events */
	bool                      ss_sync;
	/** Re-use space allocated by stobsink_alloc() */
	void                     *ss_rec;
	size_t                    ss_rec_size;
	/** Pool of buffers and stob io operation objects */
	struct m0_tl              ss_pool;
	/** Current buffer in the pool */
        struct stobsink_poolbuf  *ss_current;
	/** Cursor into ss_current->spb_buf */
	struct m0_bufvec_cursor   ss_cur;
	/** The externally visible record sink interface */
	struct m0_addb_mc_recsink ss_sink;
};

enum {
	STOBSINK_XCODE_VER_NR = 1,
	STOBSINK_MINPOOL_NR   = 2,
	STOBSINK_MAXPOOL_NR   = 8, /** @todo make this configurable? */
};

static inline struct stobsink *stobsink_from_mc(struct m0_addb_mc *mc)
{
	M0_PRE(mc != NULL && mc->am_sink != NULL);
	return container_of(mc->am_sink, struct stobsink, ss_sink);
}

/**
 * Stob sink invariant.
 */
static bool stobsink_invariant(const struct stobsink *sink)
{
	return sink != NULL &&
	       sink->ss_sink.rs_magic == M0_ADDB_STOBSINK_MAGIC &&
	       sink->ss_stob != NULL &&
	       sink->ss_seq_nr != 0 &&
	       sink->ss_current != NULL &&
	       sink->ss_persist_nr <= sink->ss_record_nr &&
	       !stobsink_pool_tlist_is_empty(&sink->ss_pool);
}

/**
 * Helper to encode or decode a m0_addb_seg_header.
 */
static int stobsink_header_encdec(struct m0_addb_seg_header *hdr,
				  struct m0_bufvec_cursor   *cur,
				  enum m0_xcode_what         what)
{
	struct m0_xcode_ctx ctx;
	struct m0_xcode_obj obj = {
		.xo_type = m0_addb_seg_header_xc
	};
	int rc;

	M0_PRE(hdr != NULL && cur != NULL);

	obj.xo_ptr = hdr;
	m0_xcode_ctx_init(&ctx, &obj);
	ctx.xcx_buf   = *cur;
	ctx.xcx_alloc = m0_xcode_alloc;

	rc = addb_encdec_op[what](&ctx);
	if (rc == 0)
		*cur = ctx.xcx_buf;
	return rc;
}

/**
 * Helper to encode or decode a m0_addb_seg_trailer.
 */
static int stobsink_trailer_encdec(struct m0_addb_seg_trailer *trl,
				   struct m0_bufvec_cursor    *cur,
				   enum m0_xcode_what          what)
{
	struct m0_xcode_ctx ctx;
	struct m0_xcode_obj obj = {
		.xo_type = m0_addb_seg_trailer_xc
	};
	int rc;

	M0_PRE(trl != NULL && cur != NULL);

	obj.xo_ptr = trl;
	m0_xcode_ctx_init(&ctx, &obj);
	ctx.xcx_buf   = *cur;
	ctx.xcx_alloc = m0_xcode_alloc;

	rc = addb_encdec_op[what](&ctx);
	if (rc == 0)
		*cur = ctx.xcx_buf;
	return rc;
}

/**
 * Function that synchronously reads a header located at the given offset.  A
 * buffer from the sink buffer pool is used to read the data.  On success, the
 * header is decoded into the provided struct m0_addb_seg_header.  Assumes that
 * reading of headers is only performed during stob sink Startup.
 * @pre sink->ss_stob != NULL && sink->ss_seq_nr == 0 && header != NULL
 * @retval -ENODATA requested segment head does not exist (ie. EOF)
 */
static int stobsink_header_read(struct stobsink *sink,
				m0_bindex_t offset,
				struct m0_addb_seg_header *header)
{
	struct stobsink_poolbuf *pb;
	struct m0_stob_domain   *dom;
	struct m0_bufvec_cursor  cur;
	uint32_t                 bshift;
	int                      rc;

	M0_PRE(sink->ss_stob != NULL && sink->ss_seq_nr == 0 &&
	       sink->ss_current != NULL && sink->ss_sync && header != NULL);
	pb = sink->ss_current;
	M0_PRE(!pb->spb_busy);

	dom = sink->ss_stob->so_domain;
	bshift = sink->ss_bshift;
	pb->spb_io_iv_index = offset >> bshift;
	pb->spb_io.si_opcode = SIO_READ;
	pb->spb_io_v_count = max64u(sizeof *header >> bshift, 1);
	pb->spb_buf_v_count = pb->spb_io_v_count;
	pb->spb_busy = true;

	/* m0_stob_io_launch does not reset these fields */
	pb->spb_io.si_obj = NULL;
	pb->spb_io.si_rc = 0;
	pb->spb_io.si_count = 0;
	rc = m0_stob_io_launch(&pb->spb_io, sink->ss_stob, NULL, NULL);
	if (rc != 0) {
		pb->spb_busy = false;
	} else {
		while (pb->spb_busy)
			m0_chan_wait(&pb->spb_wait);
		rc = pb->spb_io.si_rc;
		if (rc == 0) {
			if (pb->spb_io.si_count < pb->spb_io_v_count) {
				rc = -ENODATA;
			} else {
				m0_bufvec_cursor_init(&cur, &pb->spb_buf);
				rc = stobsink_header_encdec(header, &cur,
							    M0_XCODE_DECODE);
				M0_ASSERT(rc == 0); /* fail iff short buffer */
				/* AD stob returns zero filled block at EOF */
				if (header->sh_seq_nr == 0 &&
				    header->sh_ver_nr == 0 &&
				    header->sh_segsize == 0)
					rc = -ENODATA;
			}
		} else {
			M0_LOG(M0_ERROR, "header_read at offset %ld failed %d",
			       (unsigned long) offset, pb->spb_io.si_rc);
		}
	}

	pb->spb_io.si_opcode = SIO_WRITE;
	pb->spb_io_v_count = sink->ss_segsize >> bshift;
	pb->spb_buf_v_count = pb->spb_io_v_count;
	pb->spb_io.si_fol_rec_part = &pb->spb_fol_rec_part;
	return rc;
}

/**
 * Helper function that synchronously reads the header of the given segment
 * number and returns its sequence number.
 * @retval 0 the segment cannot be read
 */
static uint64_t stobsink_seq_at(struct stobsink *sink, uint64_t seg_nr)
{
	struct m0_addb_seg_header header;
	int                       rc;

	rc = stobsink_header_read(sink, seg_nr * sink->ss_segsize, &header);
	if (rc != 0)
		return 0;
	return header.sh_seq_nr;
}

/**
 * Helper function for m0_addb_mc_configure_stob_sink() that uses a binary
 * search on the configured stob to find the correct starting sequence number
 * and offset for writing to the stob.  Assumes that the search is only
 * performed during stob sink Startup.  All I/O is synchronous.
 * Handles both the case where the stob is partially used (not wrapped around)
 * and where it has wrapped around.
 * @param sink The sink to search.  On success, the stobsink::ss_seq_nr,
 * and stobsink::ss_offset are set.
 * @param first_seq_nr sequence number of segment at offset 0
 * @pre sink->ss_stob != NULL && sink->ss_seq_nr == 0 && sink->ss_seg_nr > 0
 * @post sink->ss_seq_nr != 0
 */
static void stobsink_offset_search(struct stobsink *sink, uint64_t first_seq_nr)
{
	uint64_t left;
	uint64_t mid;
	uint64_t right;
	uint64_t seq_mid;
	uint64_t seq_right;

	M0_PRE(sink->ss_stob != NULL && sink->ss_seq_nr == 0 &&
	       sink->ss_seg_nr > 0);

	right = sink->ss_seg_nr - 1;
	seq_right = right != 0 ? stobsink_seq_at(sink, right) : first_seq_nr;
	if (seq_right >= first_seq_nr) {
		/*
		 * special cases:
		 * if equal, stob is empty or contains 1 segment
		 * otherwise, full stob, final segment has largest seq_nr
		 */
		sink->ss_seq_nr = seq_right + 1;
		sink->ss_offset = 0;
		return;
	}

	left = 0;
	while (left != right) {
		mid = left + (right - left) / 2;
		seq_mid = stobsink_seq_at(sink, mid);
		/* also handle rounding: left == mid (right - left == 1) */
		if (seq_mid <= seq_right || left == mid) {
			seq_right = seq_mid;
			right = mid;
		} else {
			left = mid;
		}
	}

	sink->ss_seq_nr = seq_mid + 1;
	++left;
	sink->ss_offset = left * sink->ss_segsize;
}

/**
 * Callback assigned to the m0_clink::cl_cb on the channel that is signaled
 * when asynchronous write operations complete.
 */
static bool stobsink_chan_cb(struct m0_clink *link)
{
	struct stobsink_poolbuf *pb =
	    container_of(link, struct stobsink_poolbuf, spb_wait);
	bool sync;

	M0_PRE(pb->spb_magic == M0_ADDB_STOBSINK_BUF_MAGIC);
	m0_mutex_lock(&pb->spb_sink->ss_mutex);
	sync = pb->spb_sink->ss_sync;
	pb->spb_busy = false;
	if (pb->spb_io.si_rc != 0)
		M0_LOG(M0_ERROR, "segment io at offset %ld failed %d",
		       (unsigned long) pb->spb_io_iv_index <<
		       pb->spb_sink->ss_bshift,
		       pb->spb_io.si_rc);
	/** @todo alert some component if the operation fails */
	if (pb->spb_tx.tx_state == M0_DTX_OPEN)
		m0_dtx_done_sync(&pb->spb_tx);
	m0_mutex_unlock(&pb->spb_sink->ss_mutex);
	return !sync;
}

/**
 * Allocate one additional stobsink_poolbuf and add it to the pool on the given
 * stobsink after the stobsink::ss_current element.  In the case where the
 * stobsink::ss_current is NULL, adds the new buffer to the pool and sets
 * stobsink::ss_current and stobsink::ss_cur.
 * @note Does not check stobsink_invariant(sink), caller should do so.
 * @retval -E2BIG pool has reached its maximum size
 */
static int stobsink_poolbuf_grow(struct stobsink *sink)
{
	struct stobsink_poolbuf *pb;
	uint32_t                 bshift;
	struct m0_indexvec      *iv;
	struct m0_bufvec        *obuf;
	int                      rc;

	if (stobsink_pool_tlist_length(&sink->ss_pool) >= STOBSINK_MAXPOOL_NR)
		return -E2BIG;

	M0_ALLOC_PTR(pb);
	if (pb == NULL) {
		M0_LOG(M0_ERROR, "unable to grow stobsink pool: ENOMEM");
		return -ENOMEM;
	}
	bshift = sink->ss_bshift;
	rc = m0_bufvec_alloc_aligned(&pb->spb_buf, 1, sink->ss_segsize, bshift);
	if (rc != 0) {
		m0_free(pb);
		return rc;
	}

	stobsink_pool_tlink_init(pb);
	pb->spb_sink = sink;
	pb->spb_busy = false;

	m0_stob_io_init(&pb->spb_io);
	pb->spb_io.si_opcode = SIO_WRITE;
	pb->spb_io.si_fol_rec_part = &pb->spb_fol_rec_part;
	m0_clink_init(&pb->spb_wait, stobsink_chan_cb);
	m0_clink_add_lock(&pb->spb_io.si_wait, &pb->spb_wait);

	iv = &pb->spb_io.si_stob;
	iv->iv_vec.v_nr = 1;
	iv->iv_vec.v_count = &pb->spb_io_v_count;
	pb->spb_io_v_count = sink->ss_segsize >> bshift;
	iv->iv_index = &pb->spb_io_iv_index;

	obuf = &pb->spb_io.si_user;
	obuf->ov_vec.v_nr = 1;
	obuf->ov_vec.v_count = &pb->spb_buf_v_count;
	pb->spb_buf_v_count = pb->spb_io_v_count;
	obuf->ov_buf = &pb->spb_buf_ov_buf;
	pb->spb_buf_ov_buf = m0_stob_addr_pack(pb->spb_buf.ov_buf[0], bshift);

	if (sink->ss_current != NULL) {
		stobsink_pool_tlist_add_after(sink->ss_current, pb);
	} else {
		stobsink_pool_tlist_add(&sink->ss_pool, pb);
		sink->ss_current = pb;
		m0_bufvec_cursor_init(&sink->ss_cur, &pb->spb_buf);
		m0_bufvec_cursor_move(&sink->ss_cur,
				      sizeof(struct m0_addb_seg_header));
	}
	return 0;
}

/**
 * Persist the given segment to the stob.  Sets the header and trailer of the
 * current buffer, then increments the stobsink::ss_seq_nr, and launches an
 * asynchronous write operation to be performed on the stob for the buffer. The
 * stobsink::ss_persist_time is updated and the stobsink::ss_seq_nr is
 * incremented.  The offset and record_nr are passed in to allow
 * stobsink_advance() to be called before the previous record is persisted.
 * @pre pb != NULL && m0_mutex_is_locked(&pb->spb_sink->ss_mutex)
 * @note the stobsink::ss_mutex is unlocked while the IO operation is launched
 * and re-locked before returning.
 */
static void stobsink_persist(struct stobsink_poolbuf *pb,
			     m0_bindex_t              offset,
			     uint32_t                 record_nr)
{
	struct m0_sm_group        *grp = m0_locality0_get()->lo_grp;
	struct stobsink           *sink;
	struct m0_stob_domain     *dom;
	struct m0_bufvec_cursor    cur;
	struct m0_addb_seg_header  head;
	struct m0_addb_seg_trailer trailer;
	m0_bcount_t                nr;
	int                        rc;

	M0_PRE(pb != NULL);
	sink = pb->spb_sink;
	M0_PRE(stobsink_invariant(sink));
	M0_PRE(m0_mutex_is_locked(&sink->ss_mutex));
	M0_PRE(ergo(pb->spb_busy, sink->ss_current != pb));
	pb->spb_busy = true;

	dom = sink->ss_stob->so_domain;
	m0_bufvec_cursor_init(&cur, &pb->spb_buf);

	head.sh_seq_nr = sink->ss_seq_nr;
	head.sh_ver_nr = STOBSINK_XCODE_VER_NR;
	head.sh_segsize = sink->ss_segsize;
	rc = stobsink_header_encdec(&head, &cur, M0_XCODE_ENCODE);
	M0_ASSERT(rc == 0); /* only fails if not enough space to encode */

	nr = sink->ss_segsize - sizeof head - sizeof trailer;
	m0_bufvec_cursor_move(&cur, nr);

	trailer.st_seq_nr = sink->ss_seq_nr;
	trailer.st_rec_nr = record_nr;
	trailer.st_reserved = 0;
	rc = stobsink_trailer_encdec(&trailer, &cur, M0_XCODE_ENCODE);
	M0_ASSERT(rc == 0); /* only fails if not enough space to encode */
	M0_ASSERT(m0_bufvec_cursor_move(&cur, 0));
	pb->spb_io_iv_index = offset >> sink->ss_bshift;

	/* Must update sink fields before releasing lock. */
	sink->ss_persist_time = m0_time_now();
	if (sink->ss_current == pb)
		sink->ss_persist_nr = record_nr;
	sink->ss_seq_nr++;

	/* m0_stob_io_launch does not reset these fields */
	pb->spb_io.si_obj = NULL;
	pb->spb_io.si_rc = 0;
	pb->spb_io.si_count = 0;
	m0_mutex_unlock(&sink->ss_mutex);
	if (dom->sd_bedom != NULL) {
		m0_sm_group_lock(grp);
		m0_dtx_init(&pb->spb_tx, dom->sd_bedom, grp);
		rc = dom->sd_ops->sdo_tx_make(dom,
				m0_vec_count(&pb->spb_io.si_user.ov_vec),
				&pb->spb_tx);
		if (rc != 0) {
			m0_dtx_fini(&pb->spb_tx);
			m0_sm_group_unlock(grp);
			m0_mutex_lock(&sink->ss_mutex);
			pb->spb_busy = false;
			M0_LOG(M0_ERROR, "segment tx_make for offset=%ld "
					 "and size=%ld failed: rc=%d",
				(long)offset,
				(long)m0_vec_count(&pb->spb_io.si_user.ov_vec), rc);
			/** @todo alert some component that the db/tx has failed */
			return;
		}
	}

	rc = m0_stob_io_launch(&pb->spb_io, sink->ss_stob, &pb->spb_tx, NULL);
	if (dom->sd_bedom != NULL)
		m0_sm_group_unlock(grp);
	m0_mutex_lock(&sink->ss_mutex);

	if (rc != 0) {
		pb->spb_busy = false;
		M0_LOG(M0_ERROR, "segment persist at offset %ld failed %d",
		       (unsigned long) offset, rc);
		/** @todo alert some component that the stob has failed */
		if (pb->spb_tx.tx_state == M0_DTX_OPEN) {
			m0_sm_group_lock(grp);
			m0_dtx_done_sync(&pb->spb_tx);
			m0_dtx_fini(&pb->spb_tx);
			m0_sm_group_unlock(grp);
		}
	}
	M0_POST(stobsink_invariant(sink));
}

/**
   Subroutine to trigger a persist of the current buffer to the stob if there
   has been no update to it for the duration of the timeout period.
   @pre m0_mutex_is_locked(&sink->ss_mutex)
   @see stobsink_persist()
   @note stobsink_invariant() is not called
 */
static void stobsink_persist_trigger(struct stobsink *sink)
{
	M0_PRE(m0_mutex_is_locked(&sink->ss_mutex));
	if (sink->ss_persist_time + sink->ss_timeout < m0_time_now()) {
		stobsink_persist(sink->ss_current,
				 sink->ss_offset,
				 sink->ss_record_nr);
		/** @todo wait for persist to complete? */
	}
}

/**
 * Advance the stobsink to the next segment.  The stobsink::ss_offset,
 * stobsink::ss_record_nr and stobsink::ss_persist_nr are updated and the next
 * buffer in the pool is selected.  When the stobsink_poolbuf::spb_busy is set
 * on next buffer in the pool, an attempt may be made to grow the pool.
 * However, if the pool cannot grow, the stobsink::ss_current will be left
 * pointing at the busy buffer.
 */
static void stobsink_advance(struct stobsink *sink)
{
	struct stobsink_poolbuf *next;
	int                      rc;

	M0_PRE(stobsink_invariant(sink));

	next = stobsink_pool_tlist_next(&sink->ss_pool, sink->ss_current);
	if (next == NULL)		/* treat as circular list */
		next = stobsink_pool_tlist_head(&sink->ss_pool);
	if (next->spb_busy) {
		rc = stobsink_poolbuf_grow(sink);
		if (rc == 0)
			next = stobsink_pool_tlist_next(&sink->ss_pool,
							sink->ss_current);
	}
	sink->ss_current = next;
	sink->ss_offset += sink->ss_segsize;
	if (sink->ss_offset >= sink->ss_seg_nr * sink->ss_segsize)
		sink->ss_offset = 0;
	sink->ss_record_nr = 0;
	sink->ss_persist_nr = 0;
	m0_bufvec_cursor_init(&sink->ss_cur, &next->spb_buf);
	m0_bufvec_cursor_move(&sink->ss_cur, sizeof(struct m0_addb_seg_header));
}

/**
 * Select a buffer that has enough space for additional record(s) whose encoded
 * size is specified.  If there is not enough space in the sink->ss_current
 * buffer, the next buffer in the pool is chosen using stobsink_advance() and
 * the (former) current buffer is persisted.
 */
static void stobsink_buf_locate(struct stobsink *sink, m0_bcount_t len)
{
	struct stobsink_poolbuf *pb;
	m0_bindex_t              offset;
	uint32_t                 record_nr;
	bool                     persist_required;

	M0_PRE(m0_mutex_is_locked(&sink->ss_mutex));
	M0_PRE(len <= sink->ss_segsize - (sizeof(struct m0_addb_seg_header) +
					  sizeof(struct m0_addb_seg_trailer)));
	while (len + sizeof(struct m0_addb_seg_trailer) >
	       m0_bufvec_cursor_step(&sink->ss_cur)) {
		pb = sink->ss_current;
		/* cache current values so stobsink_advance can update them */
		offset = sink->ss_offset;
		record_nr = sink->ss_record_nr;
		persist_required = record_nr != sink->ss_persist_nr;
		pb->spb_busy = persist_required;

		stobsink_advance(sink);

		if (persist_required)
			stobsink_persist(pb, offset, record_nr);
		if (sink->ss_current->spb_busy) {
			M0_LOG(M0_NOTICE, "dropped ADDB record, buffer busy");
			break;
		}
	}
}

/**
 * Reference count release method to release a stob sink.
 */
static void stobsink_release(struct m0_ref *ref)
{
	struct stobsink         *sink =
	    container_of(ref, struct stobsink, ss_ref);
	struct stobsink_poolbuf *pb;

	m0_mutex_lock(&sink->ss_mutex);
	sink->ss_sync = true;
	if (sink->ss_current != NULL &&
	    sink->ss_record_nr != sink->ss_persist_nr)
		stobsink_persist(sink->ss_current,
				 sink->ss_offset,
				 sink->ss_record_nr);

	m0_tl_for(stobsink_pool, &sink->ss_pool, pb) {
		m0_mutex_unlock(&sink->ss_mutex);
		while (pb->spb_busy)
			m0_chan_wait(&pb->spb_wait);

		m0_mutex_lock(&sink->ss_mutex);
		stobsink_pool_tlink_del_fini(pb);
		m0_clink_del_lock(&pb->spb_wait);
		m0_clink_fini(&pb->spb_wait);
		m0_stob_io_fini(&pb->spb_io);
		m0_bufvec_free_aligned(&pb->spb_buf, sink->ss_bshift);
		m0_free(pb);
	} m0_tl_endfor;
	m0_mutex_unlock(&sink->ss_mutex);

	stobsink_pool_tlist_fini(&sink->ss_pool);
	m0_mutex_fini(&sink->ss_mutex);
	m0_stob_put(sink->ss_stob);
	m0_free(sink->ss_rec);
	sink->ss_sink.rs_magic = 0;
	m0_free(sink);
}

/**
 * Implementation of the m0_addb_mc_recsink::rs_get() method.
 */
static void stobsink_get(struct m0_addb_mc *mc, struct m0_addb_mc_recsink *snk)
{
	struct stobsink *sink = container_of(snk, struct stobsink, ss_sink);

	M0_PRE(stobsink_invariant(sink));
	m0_ref_get(&sink->ss_ref);
}

/**
 * Implementation of the m0_addb_mc_recsink::rs_put() method.
 */
static void stobsink_put(struct m0_addb_mc *mc, struct m0_addb_mc_recsink *snk)
{
	struct stobsink *sink = container_of(snk, struct stobsink, ss_sink);

	M0_PRE(stobsink_invariant(sink));
	m0_ref_put(&sink->ss_ref);
}

/**
 * Implementation of the m0_addb_mc_recsink::rs_rec_alloc() method.
 * @note the stobsink::ss_mutex is locked on success
 */
struct m0_addb_rec *stobsink_alloc(struct m0_addb_mc *mc, size_t len)
{
	struct stobsink    *sink = stobsink_from_mc(mc);
	struct m0_addb_rec *ret;

	M0_PRE(stobsink_invariant(sink));
	m0_mutex_lock(&sink->ss_mutex);
	if (sink->ss_current->spb_busy) {
		m0_mutex_unlock(&sink->ss_mutex);
		M0_LOG(M0_NOTICE, "dropped ADDB record, buffer busy");
		return NULL;
	}

	if (len > sink->ss_rec_size) {
		ret = m0_alloc(len);
		if (ret == NULL) {
			m0_mutex_unlock(&sink->ss_mutex);
			M0_LOG(M0_NOTICE, "dropped ADDB record, ENOMEM");
		} else {
			m0_free(sink->ss_rec);
			sink->ss_rec = ret;
			sink->ss_rec_size = len;
		}
	} else {
		ret = sink->ss_rec;
	}
	return ret;
}

/**
 * Implementation of the m0_addb_mc_recsink::rs_save() method.
 * @note the stobsink::ss_mutex is locked by the required immediately preceding
 * call to stobsink_alloc().
 */
static void stobsink_save(struct m0_addb_mc *mc, struct m0_addb_rec *rec)
{
	struct stobsink         *sink = stobsink_from_mc(mc);
	m0_bcount_t              len;
	int                      rc;

	M0_PRE(stobsink_invariant(sink) && rec != NULL);
	M0_PRE(m0_mutex_is_locked(&sink->ss_mutex));

	len = addb_rec_payload_size(rec);
	stobsink_buf_locate(sink, len);
	if (sink->ss_current->spb_busy)
		goto ret;

	rc = addb_rec_encdec(&rec, &sink->ss_cur, M0_XCODE_ENCODE);
	M0_ASSERT(rc == 0); /* only fails if not enough space to encode */
	sink->ss_record_nr++;
ret:
	m0_mutex_unlock(&sink->ss_mutex);
}

/**
 * Implementation of the m0_addb_mc_recsink::rs_save_seq() method.
 */
static void stobsink_save_seq(struct m0_addb_mc       *mc,
			      struct m0_bufvec_cursor *cur,
			      m0_bcount_t              len)
{
	struct stobsink *sink = stobsink_from_mc(mc);
	m0_bcount_t      nr;
	uint32_t         rec_nr;

	M0_PRE(stobsink_invariant(sink));
	m0_mutex_lock(&sink->ss_mutex);
	if (sink->ss_current->spb_busy) {
		M0_LOG(M0_NOTICE, "dropped ADDB record seq, buffer busy");
		goto ret;
	}

	/* Extract sequence length, add to total, copy the records only */
	nr = m0_bufvec_cursor_copyfrom(cur, &rec_nr, sizeof rec_nr);
	M0_ASSERT(nr == sizeof rec_nr);
	len  -= sizeof rec_nr;
	stobsink_buf_locate(sink, len);
	if (sink->ss_current->spb_busy)
		goto ret;
	nr = m0_bufvec_cursor_copy(&sink->ss_cur, cur, len);
	M0_ASSERT(nr == len);
	sink->ss_record_nr += rec_nr;
ret:
	m0_mutex_unlock(&sink->ss_mutex);
}

static void stobsink_skulk(struct m0_addb_mc *mc)
{
	struct stobsink *sink = stobsink_from_mc(mc);

	M0_PRE(stobsink_invariant(sink));
	m0_mutex_lock(&sink->ss_mutex);
	if (!sink->ss_current->spb_busy)
		stobsink_persist_trigger(sink);
	m0_mutex_unlock(&sink->ss_mutex);
}

static const struct m0_addb_mc_recsink stobsink_ops = {
	.rs_magic     = M0_ADDB_STOBSINK_MAGIC,
	.rs_get       = stobsink_get,
	.rs_put       = stobsink_put,
	.rs_rec_alloc = stobsink_alloc,
	.rs_save      = stobsink_save,
	.rs_save_seq  = stobsink_save_seq,
	.rs_skulk     = stobsink_skulk,
};

/** @} */ /* end of addb_stobsink group */

/** @addtogroup addb_mc */

/* public interface */
M0_INTERNAL int m0_addb_mc_configure_stob_sink(struct m0_addb_mc *mc,
					       struct m0_stob    *stob,
					       m0_bcount_t        segment_size,
					       m0_bcount_t        stob_size,
					       m0_time_t          timeout)
{
	struct stobsink          *sink;
	struct m0_addb_seg_header head;
	int                       i;
	int                       rc;

	M0_PRE(!m0_addb_mc_has_recsink(mc));
	M0_PRE(stob != NULL && segment_size > 0 && stob_size >= segment_size);
	M0_PRE(segment_size < INT32_MAX);
	M0_PRE(timeout > 0);

	M0_ALLOC_PTR(sink);
	if (sink == NULL)
		return -ENOMEM;

	m0_ref_init(&sink->ss_ref, 1, stobsink_release);
	m0_mutex_init(&sink->ss_mutex);
	sink->ss_segsize = segment_size;
	sink->ss_bshift = stob->so_op->sop_block_shift(stob);
	sink->ss_seg_nr = stob_size / segment_size;
	sink->ss_persist_time = m0_time_now();
	sink->ss_timeout = timeout;
	sink->ss_sink = stobsink_ops;
	sink->ss_stob = stob;
	stobsink_pool_tlist_init(&sink->ss_pool);
	for (i = 0; i < STOBSINK_MINPOOL_NR; ++i) {
		rc = stobsink_poolbuf_grow(sink);
		if (rc != 0)
			goto fail;
	}

	sink->ss_sync = true;
	rc = stobsink_header_read(sink, 0, &head);
	if (rc == -ENODATA) {
		sink->ss_seq_nr = 1;
	} else if (rc != 0) {
		goto fail;
	} else if (head.sh_seq_nr == 0 ||
		   head.sh_segsize != segment_size ||
		   head.sh_ver_nr != STOBSINK_XCODE_VER_NR) {
		rc = -EINVAL;
		goto fail;
	} else {
		stobsink_offset_search(sink, head.sh_seq_nr);
	}

	m0_stob_get(stob);
	sink->ss_sync = false;
	mc->am_sink = &sink->ss_sink;
	M0_POST(stobsink_invariant(sink));
	return 0;
fail:
	M0_ASSERT(rc != 0);
	sink->ss_sync = false;
	m0_ref_put(&sink->ss_ref);
	return rc;
}
M0_EXPORTED(m0_addb_mc_configure_stob_sink);

/** @} */ /* end of addb_mc group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
