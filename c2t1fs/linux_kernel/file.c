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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 *                  Huang Hua <hua_huang@xyratex.com>
 *                  Anatoliy Bilenko
 * Original creation date: 05/04/2010
 */

#include <asm/uaccess.h>    /* VERIFY_READ, VERIFY_WRITE */
#include <linux/mm.h>       /* get_user_pages(), get_page(), put_page() */
#include <linux/fs.h>

#include "lib/memory.h"     /* c2_alloc(), c2_free() */
#include "lib/misc.h"       /* c2_round_{up/down} */
#include "lib/bob.h"        /* c2_bob_type */
#include "lib/ext.h"        /* c2_ext */
#include "lib/bitmap.h"     /* c2_bitmap */
#include "lib/arith.h"      /* min_type() */
#include "layout/pdclust.h" /* C2_PUT_*, c2_layout_to_pdl(),
			     * c2_pdclust_instance_map */
#include "c2t1fs/linux_kernel/c2t1fs.h"
#include "rpc/rpclib.h"     /* c2_rpc_client_call() */
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"      /* C2_LOG and C2_ENTRY */
#include "lib/bob.h"
#include "ioservice/io_fops.h"
#include "ioservice/io_fops_ff.h"
#include "colibri/magic.h"

void iov_iter_advance(struct iov_iter *i, size_t bytes);

static struct  c2_pdclust_layout * layout_to_pd_layout(struct c2_layout *l)
{
	return container_of(l, struct c2_pdclust_layout, pl_layout);
}

/* Imports */
struct c2_net_domain;
extern bool c2t1fs_inode_bob_check(struct c2t1fs_inode *bob);

static ssize_t c2t1fs_file_aio_read(struct kiocb       *iocb,
				    const struct iovec *iov,
				    unsigned long       nr_segs,
				    loff_t              pos);

static ssize_t c2t1fs_file_aio_write(struct kiocb       *iocb,
				     const struct iovec *iov,
				     unsigned long       nr_segs,
				     loff_t              pos);

static ssize_t c2t1fs_read_write(struct file *filp,
				 char        *buf,
				 size_t       count,
				 loff_t      *ppos,
				 int          rw);

static ssize_t c2t1fs_internal_read_write(struct c2t1fs_inode *ci,
					  char                *buf,
					  size_t               count,
					  loff_t               pos,
					  int                  rw);

static ssize_t c2t1fs_rpc_rw(const struct c2_tl *rw_desc_list, int rw);

/**
   @page rmw_io_dld Detailed Level Design for read-modify-write IO requests.

   - @ref rmw-ovw
   - @ref rmw-def
   - @ref rmw-req
   - @ref rmw-depends
   - @ref rmw-highlights
   - @ref rmw-lspec
      - @ref rmw-lspec-comps
      - @ref rmw-lspec-sc1
      - @ref rmw-lspec-state
      - @ref rmw-lspec-thread
      - @ref rmw-lspec-numa
   - @ref rmw-conformance
   - @ref rmw-ut
   - @ref rmw-st
   - @ref rmw-O
   - @ref rmw-ref
   - @ref rmw-impl-plan

   @note Since the data structures and interfaces produced by this DLD
   are consumed by c2t1fs itself, no external interfaces are produced by
   this DLD. And hence, the DLD lies in a C file.

   <hr>
   @section rmw-ovw Overview

   The read-modify-write feature provides support to do partial parity group
   IO requests on a Colibri client.
   This feature is needed for write IO requests _only_.

   Colibri uses notion of layout to represent how file data is spread
   over multiple objects. Colibri client is supposed to work with
   layout independent code for IO requests. Using layouts, various RAID
   patterns like RAID5, parity declustered RAID can be supported.

   Often, incoming _write_ IO requests do not span whole parity group of
   file data. In such cases, additional data must be read from the same
   parity group to have enough information to compute new parity.
   Then data is changed as per user request and later sent for write to server.
   Such actions are taken only for write IO path.
   Read IO requests do not need any such support.

   <hr>
   @section rmw-def Definitions
   c2t1fs - Colibri client file system.
   layout - A map which decides how to distribute file data over a number
            of objects.

   <hr>
   @section rmw-req Requirements

   - @b R.c2t1fs.rmw_io.rmw The implementation shall provide support for
   partial parity group IO requests.
   - @b R.c2t1fs.rmw_io.efficient IO requests should be implemented efficiently.
   Since Colibri follows async nature of APIs as far as possible, the
   implementation will stick to async APIs and shall avoid blocking calls.

   <hr>
   @section rmw-depends Dependencies

   - An async way of waiting for reply rpc item from rpc layer.
   - Generic layout code.

   <hr>
   @section rmw-highlights Design Highlights

   - All IO requests (spanning full or partial parity group) follow same
   code path.

   - For IO requests which span whole parity group/s, IO is issued immediately.

   - IO requests are issued _only_ for units with at least one single byte
   of valid file data along with parity units. Ergo, no IO is done beyond
   end-of-file.

   - In case of partial parity group write IO request, a read request is issued
   to read necessary data blocks and parity block.

   - Read requests will be issued to read the data pages which are partially
   spanned by the incoming IO requests. The data pages which are completely
   spanned by IO request will be populated by copying data from user buffers.

   - Once read is complete, changes will be made to the data buffers
   (typically these are pages from page cache) and parity is calculated in
   _iterative_ manner. See the example below.

   - And then write requests are dispatched for the changed data blocks
   and the parity block from parity group/s.

   If
   @n Do    - Data from older version of data block.
   @n Dn    - Data from new version of data block.
   @n Po    - Parity for older version of parity group.

   Then,
   @n delta - @f$Parity(D_o, D_n)@f$   (Difference between 2 version of
                                        data blocks)
   @n    Pn - @f$Parity(delta, P_o)@f$ (Parity for new version of
                                        parity group)

   <hr>
   @section rmw-lspec Logical Specification

   - @ref rmw-lspec-comps
   - @ref rmw-lspec-sc1
   - @ref rmw-lspec-smallIO
      - @ref rmw-lspec-ds1
      - @ref rmw-lspec-sub1
      - @ref rmwDFSInternal
   - @ref rmw-lspec-state
   - @ref rmw-lspec-thread
   - @ref rmw-lspec-numa

   @subsection rmw-lspec-comps Component Overview

   - First, rwm_io component works with generic layout component to retrieve
   list of data units and parity units for given parity group.

   - Then it interacts with rpc bulk API to send IO fops to data server.

   - All IO fops are dispatched asynchronously at once and the caller thread
   sleeps over the condition of all fops getting replies.

   @msc
   rmw_io,layout,rpc_bulk;

   rmw_io   => layout   [ label = "fetch constituent cobs" ];
   rmw_io   => rpc_bulk [ label = "if rmw, issue sync read IO" ];
   rpc_bulk => rmw_io   [ label = "callback" ];
   rmw_io   => rmw_io   [ label = "modify data buffers" ];
   rmw_io   => rpc_bulk [ label = "issue async write IO" ];
   rpc_bulk => rmw_io   [ label = "callback" ];

   @endmsc

   @subsection rmw-lspec-sc1 Subcomponent design

   This DLD addresses only rmw_io component.

   - The component will sit in IO path of Colibri client code.

   - It will detect if any incoming IO request spans a parity group only
   partially.

   - In case of partial groups, it issues an inline read request to read
   the necessary blocks from parity group and get the data in memory.

   - The thread blocks for completion of read IO.

   - If incoming IO request was read IO, the thread will be returned along
   with the status and number of bytes read.

   - If incoming IO request was write, the pages are modified in-place
   as per user request.

   - And then, write IO is issued for the whole parity group.

   - Completion of write IO will send the status back to the calling thread.

   @subsection rmw-lspec-smallIO Small IO requests

   For small IO requests which do not span even a single parity group
   due to end-of-file occurring before parity group boundary,
   - rest of the blocks are assumed to be zero and

   - parity is calculated from valid data blocks only.

   The file size will be updated in file inode and will keep a check on
   actual size.

   For instance, with configuration like

   - parity_group_width  = 20K

   - unit_size           = 4K

   - nr_data_units       = 3

   - nr_parity_units     = 1

   - nr_spare_units      = 1

   When a new file is written to with data worth 1K,

   there will be
   - 1 partial data unit and

   - 2 missing data units
   in the parity group.

   Here, parity is calculated with only one partial data unit (only valid block)
   and data from whole parity group will be written to the server.

   While reading the same file back, IO request for only one block is
   made as mandated by file size.

   The seemingly obvious wastage of storage space in such cases can be
   overcome by
   - issuing IO requests _only_ for units which have at least a single byte
   of valid file data and for parity units.

   - For missing data units, there is no need to use zeroed out buffers since
   such buffers does not change parity.

   - Ergo, parity will be calculated from valid data blocks only (falling
   within end-of-file)

   Since sending network requests for IO is under control of client, these
   changes can be accommodated for irrespective of layout type.
   This way, the wastage of storage space in small IO can be restricted to
   block granularity.

   @todo In future, optimizations could be done in order speed up small file
   IO with the help of client side cache.

   @note Present implementation of c2t1fs IO path uses get_user_pages() API
   to pin user-space pages in memory. This works just fine with page aligned
   IO requests. But read-modify-write IO will use APIs like copy_from_user()
   and copy_to_user() which can copy data to or from user-space for both aligned
   and non-aligned IO requests.
   Although for _direct-IO_ and device IO requests, no copy will be done and
   it uses get_user_pages() API since direct-IO is always page aligned.

   @subsubsection rmw-lspec-ds1 Subcomponent Data Structures

   io_request - Represents an IO request call. It contains the IO extent,
   struct nw_xfer_request and the state of IO request. This structure is
   primarily used to track progress of an IO request.
   The state transitions of io_request structure are handled by c2_sm
   structure and its support for chained state transitions.

   The c2_sm_group structure is kept as a member of in-memory superblock,
   struct c2t1fs_sb. @see struct c2t1fs_sb.
   All c2_sm structures are associated with this group.

   @code
   struct io_request {
	uint64_t                     ir_magic;

	int                          ir_rc;

        // File identifier.
        struct c2_fid                ir_fid;

	// File extent spanned by io_request.
	struct c2_ext                ir_extent;

        // iovec structure containing user-space buffers.
        // iovec structure is used as it is since using a new structure
        // would require conversion.
        struct iovec                *ir_iovec;

        // Number of segments in ::ir_iovec.
        unsigned long                ir_seg_nr;

	// Async state machine to handle state transitions.
	struct c2_sm                 ir_sm;

	enum io_req_type             ir_type;

	enum io_req_state            ir_state;

	const struct io_request_ops *ir_ops;

	struct nw_xfer_request       ir_nwxfer;

	// Expanded file extent. Used in case of read-modify-write.
	struct c2_ext                ir_aux_extent;
   };
   @endcode

   io_req_state - Represents state of IO request call.
   c2_sm_state_descr structure will be defined for description of all
   states mentioned below.

   @code
   enum io_request_state {
	IRS_UNINITIALIZED,
	IRS_INITIALIZED,
	IRS_READING,
	IRS_WRITING,
	IRS_READ_COMPLETE,
	IRS_WRITE_COMPLETE,
	IRS_REQ_COMPLETE,
	IRS_STATE_NR,
   };
   @endcode

   io_req_type - Represents type of IO request.

   @code
   enum io_req_type {
	IRT_READ,
	IRT_WRITE,
	IRT_NR,
   };
   @endcode

   @todo IO types like fault IO are not supported yet.

   io_request_ops - Operation vector for struct io_request.
   @code
   struct io_request_ops {
	int (*iro_readextent)     (struct io_request *req);
	int (*iro_prepare_write)  (struct io_request *req);
	int (*iro_commit_write)   (struct io_request *req);
	int (*iro_submit)         (struct io_request *req);
	int (*iro_read_complete)  (struct io_request *req);
	int (*iro_write_complete) (struct io_request *req);
   };
   @endcode

   Structure representing the network transfer part for an IO request.
   This structure keeps track of request IO fops as well as individual
   completion callbacks and status of whole request.
   Typically, all IO requests are broken down into multiple fixed-size
   requests. struct io_req_rootcb will have multiple children each signifying
   a callback for an individual IO fop.

   @code
   struct nw_xfer_request {
	uint64_t               nxr_magic;

        // Resultant status code for all IO fops issed by this structure.
	int                    nxr_rc;

        // Resultant number of bytes read/written by this structure.
        uint64_t               nxr_bytes;

	enum nw_xfer_state     nxr_state;

	struct nw_xfer_ops    *nxr_ops;

	// List of target_ioreq structures.
	struct c2_tl           nxr_tioreqs;

        // Number of IO fops issued by all target_ioreq objects belonging
        // to this nw_xfer_request object.
        // This number is updated when bottom halves from ASTs are run.
        // When it reaches zero, state of io_request::ir_sm changes.
        uint64_t               nxr_iofops_nr;
   };
   @endcode

   State of struct nw_xfer_request.

   @code
   enum nw_xfer_state {
	NXS_UNINITIALIZED,
	NXS_INITIALIZED,
	NXS_INFLIGHT,
	NXS_COMPLETE,
	NXS_NR
   };
   @endcode

   Operation vector for struct nw_xfer_request.

   @code
   struct nw_xfer_ops {
	int (*nxo_prepare)  (struct nw_xfer_request *xfer);
	int (*nxo_dispatch) (struct nw_xfer_request *xfer);
	int (*nxo_complete) (struct nw_xfer_request *xfer);
   };
   @endcode

   data_buf - Represents a simple data buffer wrapper object. The embedded
   c2_buf::b_addr points to a kernel page.

   @code
   struct data_buf {
           uint64_t                  db_magic;

           // Points to a kernel page.
           struct c2_buf             db_buf;

           // Type of buffer in a parity group (data or parity).
           enum c2_pdclust_unit_type db_type;

           // Parity group id to which this buffer belongs.
           uint64_t                  db_pgid;

           // Flags. Can reuse values from enum io_req_type here.
           // Is used when doing read from read-modify-write since not all
           // buffers from target_ieoxt need to be read from target.
           // Can be used later for caching options.
           uint64_t                  db_flags;
   }
   @endcode

   target_ioext - Represents the frames of file data associated with given
   target. It describes the span of IO request and number of data buffers
   attached with it.

   @code
   struct target_ioext {
        uint64_t          tie_magic;

        // Extent describing starting cob offset and its length.
        // Since all such IO requests read or write the target objects
        // sequentially, one extent is enough to describe span of IO request.
        struct c2_ext     tie_ext;

        // Number of data_buf structures representing the extent.
        uint64_t          tie_buf_nr;

        // Array of data_buf structures.
        // Each element typically refers to one kernel page.
        // Intentionally kept as array since it makes iterating much easier
        // as compared to a list, especially while calculating parity.
        // Also, max number of buffers a target_ioext structure can span
        // for given io_request is constant.
        struct data_buf **tie_buffers;
   };
   @endcode

   target_ioreq - Collection of IO extents and buffers, directed towards each
   of the component objects in a parity group.
   These structures are created by struct io_request dividing the incoming
   struct iovec into members of parity group.

   @code
   struct target_ioreq {
	uint64_t               tir_magic;

	// Fid of component object.
	struct c2_fid          tir_fid;

        // Target object extent representing span of IO request on target object
        // and array of data buffers.
	struct target_ioext    tir_extent;

        // List of struct io_req_fop issued on this target object.
        struct c2_tl           tir_iofops;

	// Resulting IO fops are sent on this session.
	struct c2_rpc_session *tir_session;

	// Linkage to link in to nw_xfer_request::nxr_tioreqs list.
	struct c2_tlink        tir_link;
   };
   @endcode

   io_req_fop - Represents a wrapper over generic IO fop and its callback
   to keep track of such IO fops issued by same nw_xfer_request.

   When bottom halves for c2_sm_ast structures are run, it updates
   nw_xfer_request::nxr_rc and nw_xfer_request::nxr_bytes with data from
   IO reply fop.
   Then it decrements nw_xfer_request::nxr_iofops_nr, number of IO fops.
   When this count reaches zero, io_request::ir_sm changes its state.

   @note c2_fom_callback structure is reused as a wrapper over AST. No lock
   is needed while updating nw_xfer_request structure from ASTs since
   c2_sm_asts_run() executes ASTs in serial fashion.

   @code
   struct io_req_fop {
        uint64_t                irf_magic;

        struct c2_io_fop        irf_iofop;

        // Callback per IO fop.
        struct c2_fom_callback  irf_iocb;

        // Linkage to link in to target_ioreq::tir_iofops list.
        struct c2_tlink         irf_tlink;

        // Backlink to nw_xfer_request object where rc and number of bytes
        // are updated.
        struct nw_xfer_request *irf_nwxfer;
   };
   @endcode

   Magic value to verify sanity of struct io_request, struct nw_xfer_request,
   struct target_ioreq and struct io_req_fop.
   The magic values will be used along with static c2_bob_type structures to
   assert run-time type identification.

   @code
   enum {
	IOREQ_MAGIC  = 0xfea2303e31a3ce10ULL, // fearsomestanceto
	NWREQ_MAGIC  = 0x12ec0ffeea2ab1caULL, // thecoffeearabica
        TIOREQ_MAGIC = 0xfa1afe1611ab2eadULL, // falafelpitabread
        IOFOP_MAGIC  = 0xde514ab11117302eULL, // desirabilitymore
        TIOEXT_MAGIC = 0x19ca9de5ca91b61bULL, // incandescentbulb
        DTBUF_MAGIC  = 0x191e2c09119e91a1ULL, // intercontinental
   };
   @endcode

   @subsubsection rmw-lspec-sub1 Subcomponent Subroutines

   An existing API io_req_spans_full_pg() is reused to check if incoming
   IO request spans a full parity group or not.

   The following API will change the parity unit size unaligned IO request
   to make it align with parity group unit size.
   It will populate io_request::ir_aux_extent so that further read or write IO
   will be done according to auxiliary IO extent.
   The cumulative count of auxiliary extent will always be not less than
   size of original extent. However, while returning from system call, the
   number of bytes read/written will not exceed the size of original IO extent.

   @see   io_request_invariant()
   @param req IO request to be expanded so as to align with parity group
   unit size.
   @pre   io_request_invariant()
   @post  c2_ext_is_partof(&req->ir_aux_extent, &req->ir_extent) &&
   @n     io_request_invariant()

   @code
   void io_request_expand(struct c2t1fs_inode *ci, struct io_request *req)
   {
	void                 *buf = req->ir_iovec[0].iov_base;
        // Unit size of a block in parity group.
	uint64_t              size = layout_to_pd_layout(ci->ci_layout)->
                                     pl_unit_size;
        // Original file extent.
        const struct c2_ext  *oe = &req->ir_extent;
        // Expanded file extent.
        const struct c2_ext  *ee = &req->ir_aux_extent;

	C2_PRE(io_request_invariant(req));

        if (io_req_spans_full_pg(ci, buf, c2_ext_length(oe), oe->e_start);
                return;

	// finds immediate lower number which is aligned to
        // parity group unit size.
	ee->e_start = c2_round_down(oe->e_start, size);

	// finds immediate higher number which is aligned to
        // parity group unit size.
	ee->e_end = ee->e_start + c2_round_up(oe->e_start - ee->e_start +
					      c2_ext_length(oe), size);

	C2_POST(c2_ext_is_partof(ee, oe) && io_request_invariant(req));
   }
   @endcode

   API which tells if given IO request is read-modify-write request or not.

   @code
   bool io_request_is_rmw(const struct io_request *req)
   {
           return req->ir_type == IRT_WRITE &&
                  c2_ext_is_partof(&req->ir_aux_extent, &req->ir_extent);
   };
   @endcode

   Invariant for structure io_request.

   @code
   static bool io_request_invariant(const struct io_request *req)
   {
	C2_PRE(req != NULL);

	return
		req->ir_magic == IOREQ_MAGIC &&
		req->ir_type  <= IRT_NR &&
		req->ir_state <= IRS_STATE_NR &&
                req->ir_iovec != NULL &&
                !c2_ext_is_empty(&req->ir_extent) &&
                c2_fid_is_valid(&req->ir_fid) &&

		ergo(req->ir_state == IRS_READING,
		     !c2_tlist_is_empty(req->ir_nwxfer.nxr_tioreqs)) &&

		ergo(req->ir_state == IRS_WRITING,
		     req->ir_type  == IRT_WRITE &&
		     !c2_tlist_is_empty(req->ir_nwxfer.nxr_tioreqs)) &&

		ergo(req->ir_state == IRS_READ_COMPLETE &&
                     req->ir_type  == IRT_READ,
		     c2_tlist_is_empty(req->ir_nwxfer.nxr_tioreqs)) &&

		ergo(req->ir_state == IRS_WRITE_COMPLETE,
		     c2_tlist_is_empty(req->ir_nwxfer.nxr_tioreqs)) &&

                ergo(io_req_is_rmw(req),
                     c2_ext_length(&req->ir_aux_extent) >
                     c2_ext_length(&req->ir_extent)) &&

                nw_xfer_request_invariant(&req->ir_nwxfer);
   }
   @endcode

   Invariant for structure nw_xfer_request.

   @code
   static bool nw_xfer_request_invariant(const struct nw_xfer_request *xfer)
   {
	return
                xfer != NULL &&
		xfer->nxr_magic == NWREQ_MAGIC &&
		xfer->nxr_state <= NXS_NR &&

		ergo(xfer->nxr_state == NXS_INFLIGHT,
		     !c2_tlist_is_empty(xfer->nxr_tioreqs) &&
                     xfer->nxr_iofops_nr > 0) &&

		ergo(xfer->nxr_state == NXS_COMPLETE,
		     c2_tlist_is_empty(xfer->nxr_tioreqs) &&
                     xfer->nxr_iofops_nr == 0 && xfer->nxr_bytes > 0) &&

                c2_tl_forall(tioreqs, tioreq, &xfer->nxr_tioreqs,
                             target_ioreq_invariant(tioreq)) &&

                xfer->nxr_iofops_nr == c2_tlist_length(&xfer->nxr_tioreqs);
   }
   @endcode

   Invariant for structure target_ioreq.

   @code
   static bool target_ioreq_invariant(const struct target_ioreq *tir)
   {
           return
                  tir != NULL &&
                  tir->tir_magic   == TIOREQ_MAGIC &&
                  tir->tir_session != NULL &&
                  c2_fid_is_valid(tir->tir_fid) &&
                  c2_tlink_is_in(tir->tir_link) &&
                  target_ioext_invariant(&tir->tir_extent) &&
                  c2_tl_forall(iofops, iofop, &tir->tir_iofops,
                               io_req_fop_invariant(iofop));
   }
   @endcode

   Invariant for structure target_ioext.

   @code
   static bool target_ioext_invariant(const struct target_ioext *text)
   {
           uint64_t i;

           return
                  text != NULL &&
                  text->tie_magic  == TIOEXT_MAGIC &&
                  text->tie_buf_nr > 0 &&
                  for (i = 0; i < text->tie_buf_nr; ++i)
                          data_buf_invariant(text->tie_buffers[i])
                  !c2_ext_is_empty(&text->tie_ext);
   }
   @endcode

   Invariant for structure data_buf.

   @code
   static bool data_buf_invariant(const struct data_buf *db)
   {
           return
                  ti->ti_session != NULL &&
                  c2_fid_is_valid(ti->ti_fid) &&
                  c2_tlink_is_in(ti->ti_link) &&
                  ergo(!c2_tlist_is_empty(ti->ti_iofops),
                       !c2_tlist_is_empty(ti->ti_buffers));
   }
   @endcode

   Invariant for structure io_req_fop.

   @code
   static bool io_req_fop_invariant(const struct io_req_fop *irf)
   {
           return
                  irf != NULL &&
                  irf->irf_magic  == IOFOP_MAGIC &&
                  irf->irf_nwxfer != NULL &&
                  c2_tlink_is_in(irf->irf_tlink);
   }
   @endcode

   The APIs that work as members of operation vector struct io_request_ops
   for struct io_request are as follows.

   Initializes a newly allocated io_request structure.

   @param req    IO request to be issued.
   @param fid    File identifier.
   @param ivec   Array of user-space buffers.
   @param seg_nr Number of iovec structures.
   @param pos    Starting file offset.
   @param rw     Flag indicating Read or write request.
   @pre   req != NULL
   @post  io_request_invariant(req) &&
   @n     c2_fid_eq(&req->ir_fid, fid) && req->ir_iovec == ivec &&
   @n     req->ir_extent.e_start == pos && req->ir_type == rw
   @n     req->ir_state == IRS_INITIALIZED && req->ir_seg_nr == seg_nr.

   @code
   int io_request_init(struct io_request *req,
                       struct c2_fid     *fid,
                       struct iovec      *ivec,
		       loff_t             pos,
                       unsigned long      seg_nr,
                       enum io_req_type   rw);
   @endcode

   Finalizes the io_request structure.

   @param req IO request to be processed.
   @pre   io_request_invariant(req) &&
   @n     req->ir_state == IRS_REQ_COMPLETE &&
   @post  c2_tlist_is_empty(req->ir_nwxfer.nxr_tioreqs)

   @code
   void io_request_fini(struct io_request *req);
   @endcode

   Reads given extent of file. Data read from server is kept into
   io_request::ir_nwxfer::target_ioreq::sid_buffers. see @c2t1fs_buf.
   This API can also be used by a write request which needs to read first
   due to its unaligned nature with parity group width.
   In such case, even if the state of struct io_request indicates IRS_READING
   or IRS_READ_COMPLETE, the io_req_type enum suggests it is a write request
   in first place.

   @param req IO request to be issued.
   @pre   io_request_invariant(req) && req->ir_state >= IRS_INITIALIZED
   @post  io_request_invariant(req) && req->ir_state == IRS_READING

   @code
   int io_request_readextent(struct io_request *req);
   @endcode

   Make necessary preparations for a write IO request.
   This might involve issuing req->iro_readextent() request to read the
   necessary file extent if the incoming IO request is not parity group aligned.
   If the request is parity group aligned, no read IO is done.
   Instead, file data is copied from user space to kernel space.

   @param req IO request to be issued.
   @pre   io_request_invariant(req) && req->ir_state >= IRS_INITIALIZED
   @post  io_request_invariant(req) &&
   @n     req->ir_state == IRS_READING || req->ir_state == IRS_WRITING

   @code
   int io_request_prepare_write(struct io_request *req);
   @endcode

   Commit the transaction for updates made by given write IO request.
   With current code, this function will most likely be a stub. It will be
   implemented when there is a cache in c2t1fs client which will maintain
   dirty data and will commit the data with data servers as a result of
   crossing some threshold (e.g. reaching grant value or timeout).

   @param req IO request to be committed.
   @pre   io_request_invariant(req) && req->ir_state == IRS_WRITING &&
   @n     !c2_tlist_is_empty(req->ir_nwxfer.nxr_tioreqs)
   @post  io_request_invariant(req) &&
   @n     req->ir_state == IRS_WRITE_COMPLETE &&
   @n     c2_tlist_is_empty(req->ir_nwxfer.nxr_tioreqs)

   @code
   int io_request_write_commit(struct io_request *req);
   @endcode

   Post processing for read IO completion.
   For READ IO operations, this function will copy back the data from
   kernel space to user-space.
   For read-modify-write IO operation, this function will kick-start the
   copy_from_user() _only_ for partially spanned pages and then proceed with
   write IO request processing.

   @param req IO request to be processed.
   @pre   io_request_invariant(req) && req->ir_state == IRS_READING
   @post  io_request_invariant(req) &&
   @n     req->ir_state == IRS_READ_COMPLETE

   @code
   int io_request_read_complete(struct io_request *req);
   @endcode

   Post processing for write IO completion. This function will simply signal
   the thread blocked for completion of whole IO request.

   @param req IO request to be processed.
   @pre   io_request_invariant(req) && req->ir_state == IRS_WRITING
   @post  io_request_invariant(req) &&
   @n     req->ir_state == IRS_WRITE_COMPLETE

   @code
   int io_request_write_complete(struct io_request *req);
   @endcode

   Submits the rpc for given IO fops. Implies "network transfer" of IO
   requests. This API invokes subsequent operations on the embedded
   struct nw_xfer_request object. @see nw_xer_request.

   @param req IO request to be submitted.
   @pre   io_request_invariant(req) &&
   @n     (req->ir_state == IRS_READING || req->ir_state == IRS_WRITING)
   @post  io_request_invariant(req) &&
   @n     (req->ir_state == IRS_READ_COMPLETE ||
   @n     req->ir_state == IRS_WRITE_COMPLETE)

   @code
   int io_request_submit(struct io_request *req);
   @endcode

   The APIs that work as member functions for operation vector struct
   nw_xfer_ops are as follows.

   Creates and populates target_ioreq structures for each member in the
   parity group.
   Each target_ioreq structure will allocate necessary pages to store
   file data and will create IO fops out of it.

   @param xfer The network transfer request being prepared.
   @pre   nw_xfer_request_invariant(xfer) &&
   @n     xfer->nxr_state == UNINITIALIZED
   @post  nw_xfer_request_invariant(xfer) &&
   @n     !c2_tlist_is_empty(xfer->nxr_tioreqs) &&
   @n     xfer->nxr_state == NXS_INITIALIZED.

   @code
   int nw_xfer_req_prepare(struct nw_xfer_request *xfer);
   @endcode

   Dispatches the network transfer request and does asynchronous wait for
   completion. Typically, IO fops from all target_ioreq::tir_iofops
   contained in nw_xfer_request::nxr_tioreqs list are dispatched at once
   The network transfer request is considered as complete when callbacks for
   all IO fops from all target_ioreq structures are acknowledged and
   processed.

   @param xfer The network transfer request being dispatched.
   @pre   nw_xfer_request_invariant(xfer) &&
   @n     !c2_tlist_is_empty(xfer->nxr_tioreqs) &&
   @n     xfer->nxr_state == NXS_INITIALIZED.
   @post  nw_xfer_request_invariant(xfer)

   @code
   int nw_xfer_req_dispatch(struct nw_xfer_request *xfer);
   @endcode

   Does post processing for network request completion which is usually
   notifying struct io_request about completion.

   @param xfer The network transfer request being processed.
   @pre   nw_xfer_request_invariant(xfer) &&
   @n     xfer->nxr_state == NXS_INFLIGHT
   @post  nw_xfer_request_invariant(xfer) &&
   @n     xfer->nxr_state == NXS_COMPLETE &&
   @n     c2_tlist_is_empty(xfer->nxr_tioreqs).

   @code
   int nw_xfer_req_complete(struct nw_xfer_request *xfer);
   @endcode

   @subsection rmw-lspec-state State Specification

   @dot
   digraph io_req_st {
	size     = "8,6"
	label    = "States of IO request"
	node       [ shape=record, fontsize=9 ]
	Start      [ label = "", shape="plaintext" ]
	Suninit    [ label = "IRS_UNINITIALIZED" ]
	Sinit      [ label = "IRS_INITIALIZED" ]
	Sreading   [ label = "IRS_READING" ]
	Swriting   [ label = "IRS_WRITING" ]
	Sreaddone  [ label = "IRS_READ_COMPLETE" ]
	Swritedone [ label = "IRS_WRITE_COMPLETE" ]
	Sreqdone   [ label = "IRS_REQ_COMPLETE" ]

	Start      -> Suninit    [ label = "allocate", fontsize=10, weight=8 ]
	Suninit    -> Sinit      [ label = "init", fontsize=10, weight=8 ]
	{
	    rank = same; Swriting; Sreading;
	};
	Sinit      -> Swriting   [ label = "io == write()", fontsize=9 ]
	Sinit      -> Sreading   [ label = "io == read()", fontsize=9 ]
	Sreading   -> Sreading   [ label = "!io_spans_full_pg()",
				   fontsize=9 ]
	{
	    rank = same; Swritedone; Sreaddone;
	};
	Swriting   -> Swritedone [ label = "write_complete()", fontsize=9,
			           weight=4 ]
	Sreading   -> Sreaddone  [ label = "read_complete()", fontsize=9,
			           weight=4 ]
	Swriting   -> Sreading   [ label = "! io_spans_full_pg()",
				   fontsize=9 ]
	Sreaddone  -> Sreqdone   [ label = "io == read()", fontsize=9 ]
	Sreaddone  -> Swriting   [ label = "io == write()", fontsize=9 ]
	Swritedone -> Sreqdone   [ label = "io == write()", fontsize=9 ]
   }
   @enddot

   @todo A client cache is missing at the moment. With addition of cache,
   the states of an IO request might add up.

   @subsection rmw-lspec-thread Threading and Concurrency Model

   All IO fops resulting from IO request will be dispatched at once and the
   state machine (c2_sm) will wait for completion of read or write IO.
   Incoming callbacks will be registered with the c2_sm state machine where
   their respective top half functions will be executed on same thread while
   the bottom half functions will be executed when all ASTs are run.
   All processing happens on same thread handling the system call. No new
   threads are created.
   @see sm. Concurrency section of State machine.

   @todo In future, with introduction of Resource Manager, distributed extent
   locks have to be acquired or released as needed.

   @subsection rmw-lspec-numa NUMA optimizations

   None.

   <hr>
   @section rmw-conformance Conformance

   - @b I.c2t1fs.rmw_io.rmw The implementation uses an API io_req_is_rmw()
   to find out if the incoming IO request would be read-modify-write IO.
   The IO extent is modified if the request is unaligned with parity group
   unit size width. The missing data will be read first from data server
   synchronously (later from client cache which is missing at the moment and
   then from data server). And then it will be modified and sent to server
   as a write IO request.

   - @b I.c2t1fs.rmw_io.efficient The implementation uses an asynchronous
   way of waiting for IO requests and does not send the requests one after
   another as is done with current implementation. This leads in only one
   conditional wait instead of waits proportional to number of IO requests
   as is done with current implementation.

   <hr>
   @section rmw-ut Unit Tests

   The UT will exercise following unit test scenarios.
   @todo However, with all code in kernel and no present UT code for c2t1fs,
   it is still to be decided how to write UTs for this component.

   @test Issue a full parity group size IO and check if it is successful. This
   test case should assert that full parity group IO is intact with new changes.

   @test Issue a partial parity group read IO and check if it successful. This
   test case should assert the fact that partial parity group read IO is working
   properly.

   @test Issue a partial parity group write IO and check if it is successful.
   This should confirm the fact that partial parity group write IO is working
   properly.

   @test Write very small amount of data (10 - 20 bytes) to a newly created
   file and check if it is successful. This should stress 2 boundary conditions
   - a partial parity group write IO request and

   - unavailability of all data units in a parity group. In this case,
   the non-existing data units will be assumed as zero filled buffers and
   the parity will be calculated accordingly.

   @test Kernel mode fault injection can be used to inject failure codes into
   IO path and check for results.

   <hr>
   @section rmw-st System Tests

   A bash script will be written to send partial parity group IO requests in
   loop and check the results. This should do some sort of stress testing
   for the code.

   <hr>
   @section rmw-O Analysis

   Number of io_request structures used is directly proportional to the
   number of buffers in incoming iovec structure to AIO calls.
   Each IO request creates sub requests proportional to number of blocks
   addressed by one buffer.
   There is only one mutex used to protect the io_request structure.
   Number of IO fops created is also directly proportional to the number
   of data buffers.

   <hr>
   @section rmw-ref References

   - <a href="https://docs.google.com/a/xyratex.com/
Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjQ3Z3NraDI4ZG0&hl=en_US">
Detailed level design HOWTO</a>,
   an older document on which this style guide is partially based.

   <hr>
   @section rmw-impl-plan Implementation Plan

   - The task can be decomposed into 2 subtasks as follows.
     - implementation of c2_sm and all state entry and exit functions.
       Referred to as subtask-sm-support henceforth.

     - implementation of primary data structures and interfaces needed to
       support read-modify-write. Referred to as subtask-rwm-support henceforth.

   - New interfaces are consumed by same module (c2t1fs). The primary
   data structures and interfaces have been identified and defined.

   - The subtask-sm-support will be done first.

   - Next, the subtask-rmw-support can be implemented which will refactor
   the IO path code. This can be tested by using a test script which will
   stress the IO path with read-modify-write IO requests.

   - While the patch for subtask-sm-support is getting inspected, another
   subtask which will implement the read-modify-write support will be
   undertaken.

 */

struct io_request;
struct nw_xfer_request;
struct target_ioreq;
struct io_req_fop;
enum   io_req_type;

static bool io_req_is_rmw  (const struct io_request *req);
static void io_request_init(struct io_request *req, struct file *file,
                            struct iovec *ivec, loff_t pos,
                            unsigned long seg_nr, enum io_req_type rw);

static bool io_request_invariant(const struct io_request *req);
static bool io_req_spans_full_pg(struct c2t1fs_inode *ci, char *buf,
                                 size_t count, loff_t pos);

/** State of struct nw_xfer_request. */
enum nw_xfer_state {
        NXS_UNINITIALIZED,
        NXS_INITIALIZED,
        NXS_INFLIGHT,
        NXS_COMPLETE,
        NXS_STATE_NR,
};

/** Operation vector for struct nw_xfer_request. */
struct nw_xfer_ops {
        int (*nxo_prepare)  (struct nw_xfer_request *xfer);
        int (*nxo_dispatch) (struct nw_xfer_request *xfer);
        int (*nxo_complete) (struct nw_xfer_request *xfer);
};

/**
 * Structure representing the network transfer part for an IO request.
 * This structure keeps track of request IO fops as well as individual
 * completion callbacks and status of whole request.
 * Typically, all IO requests are broken down into multiple fixed-size
 * requests. struct io_req_rootcb will have multiple children each signifying
 * a callback for an individual IO fop.
 */
struct nw_xfer_request {
        /** Holds NWREQ_MAGIC */
        uint64_t            nxr_magic;

        /** Resultant status code for all IO fops issued by this structure. */
        int                 nxr_rc;

        /** Resultant number of bytes read/written by all IO fops. */
        uint64_t            nxr_bytes;

        enum nw_xfer_state  nxr_state;

        struct nw_xfer_ops *nxr_ops;

        /** List of all target_ioreq structures. */
        struct c2_tl        nxr_tioreqs;

        /**
         * Number of IO fops issued by all target_ioreq structures
         * belonging to this nw_xfer_request object.
         * This number is updated when bottom halves from ASTs are run.
         * When it reaches zero, state of io_request::ir_sm changes.
         */
        uint64_t            nxr_iofop_nr;
};

/**
 * Represents state of IO request call.
 * c2_sm_state_descr structure will be defined for description of all
 * states mentioned below.
 */
enum io_req_state {
        IRS_UNINITIALIZED,
        IRS_INITIALIZED,
        IRS_READING,
        IRS_WRITING,
        IRS_READ_COMPLETE,
        IRS_WRITE_COMPLETE,
        IRS_REQ_COMPLETE,
        IRS_STATE_NR,
};

/** Represents type of IO request. */
enum io_req_type {
        IRT_READ,
        IRT_WRITE,
        IRT_TYPE_NR,
};

/**
 * Operation vector for struct io_request.
 */
struct io_request_ops {
        int (*iro_readextent)     (struct io_request *req);
        int (*iro_preparewrite)   (struct io_request *req);
        int (*iro_commit_write)   (struct io_request *req);
        int (*iro_submit)         (struct io_request *req);
        int (*iro_read_complete)  (struct io_request *req);
        int (*iro_write_complete) (struct io_request *req);
};

/**
 * io_request - Represents an IO request call. It contains the IO extent,
 * struct nw_xfer_request and the state of IO request. This structure is
 * primarily used to track progress of an IO request.
 * The state transitions of io_request structure are handled by c2_sm
 * structure and its support for chained state transitions.
 */
struct io_request {
        /** Holds IOREQ_MAGIC */
        uint64_t                 ir_magic;

        int                      ir_rc;

        /**
         * struct file* can point to c2t1fs inode and hence its
         * associated c2_fid structure.
         */
        struct file             *ir_file;

        /** File extent spanned by io_request. */
        struct c2_ext            ir_extent;

        /**
         * iovec structure containing user-space buffers.
         * It is used as it as since using a new structure would require
         * conversion.
         */
        struct iovec            *ir_iovec;

        /** Number of segments in io_request::ir_iovec. */
        unsigned long            ir_seg_nr;

        /** Async state machine to handle state transitions and callbacks. */
        struct c2_sm             ir_sm;

        enum io_req_type         ir_type;

        enum io_req_state        ir_state;

        struct io_request_ops   *ir_ops;

        struct nw_xfer_request   ir_nwxfer;

        /** Expanded file extent. Used in case of read-modify-write. */
        struct c2_ext            ir_aux_extent;
};

enum dt_buf_flags {
        /**
         * Needs regeneration of parity block. Mostly used in case of
         * partial parity groups.
         */
        DBF_PARITYCALC,

        /** Block needs to be read first from a rmw IO request. */
        DBF_NEEDSREAD,
}

/**
 * Represents a simple data buffer wrapper object. The embedded
 * c2_buf::b_addr points to a kernel page.
 */
struct data_buf {
        /** Holds DTBUF_MAGIC. */
        uint64_t                  db_magic;

        /** Inline buffer pointing to a kernel page. */
        struct c2_buf             db_buf;

        /**
         * Type of buffer in parity group { data or parity }.
         * This is needed where a partial parity group is written to
         * and parity unit has to be read in order to calculate the
         * new parity in _incremental_ fashion.
         */
        enum c2_pdclust_unit_type db_type;

        /** Parity group id to which ::db_buf belongs. */
        uint64_t                  db_pgid;

        /**
         * Buffer holding old version of data needed to calculate parity
         * in _iterative_ fashion for partial parity groups.
         */
        struct c2_buf             db_olddata;

        /**
         * Miscellaneous flags. Can reuse values from enum io_req_type here.
         * Is used when doing read from read-modify-write since not all
         * buffers from target_ioext need to be read from target.
         * Can be used later for caching options.
         */
        uint64_t                  db_flags;
};

/**
 * Represents IO chunk consisting of one/multiple frames meant for a
 * target object in a parity group. All such frames in target_ioext struct
 * are contiguous with respect to target object offset.
 */
struct target_ioext {
        /** Holds TIOEXT_MAGIC */
        uint64_t                  ie_magic;

        /**
         * Extent describing starting offset and its length.
         * Since all such IO requests read/write the target objects
         * sequentially, one extent is enough to describe the span of
         * IO request.
         * This extent represents offsets for target object.
         */
        struct c2_ext             ie_ext;

        /** Number of data_buf structures representing the extent. */
        uint64_t                  ie_buf_nr;

        /**
         * Array of data_buf structures. Each element typically refers to
         * one kernel page.
         * This is intentionally kept as array since it makes iterating much
         * easier as compared to a list, especially while calculating parity.
         * And the max number of buffers an io extent can occupy is
         * constant for a given struct io_request.
         */
        struct data_buf         **ie_buffers;
};

/**
 * Collection of IO extents and buffers, directed towards each
 * of target objects (data_unit / parity_unit) in a parity group.
 * These structures are created by struct io_request dividing the incoming
 * struct iovec into members of parity group.
 */
struct target_ioreq {
        /** Holds TIOREQ_MAGIC */
        uint64_t                ti_magic;

        /** Fid of component object. */
        struct c2_fid           ti_fid;

        /**
         * Extent representing span of IO request on target object and
         * list of data buffers.
         */
        struct target_ioext     ti_extent;

        /** List of io_req_fop structures issued on this target object. */
        struct c2_tl            ti_iofops;

        /** Resulting IO fops are sent on this rpc session. */
        struct c2_rpc_session  *ti_session;

        /** Linkage to link in to nw_xfer_request::nxr_tioreqs list. */
        struct c2_tlink         ti_link;
};

/**
 * Represents a wrapper over generic IO fop and its callback
 * to keep track of such IO fops issued by same nw_xfer_request.
 * When bottom halves for c2_sm_ast structures are run, it updates
 * nw_xfer_request::nxr_rc and nw_xfer_request::nxr_bytes with data from
 * IO reply fop.
 * Then it decrements nw_xfer_request::nxr_iofop_nr, number of IO fops.
 * When this count reaches zero, io_request::ir_sm changes its state.
 */

/**
 * @note c2_fom_callback structure is reused as a wrapper over AST. No lock
 * is needed while updating nw_xfer_request structure from ASTs since
 * c2_sm_asts_run() executes ASTs in serial fashion.
 */
struct io_req_fop {
        /** Holds IOFOP_MAGIC */
        uint64_t                irf_magic;

        /** In-memory handle for IO fop. */
        struct c2_io_fop        irf_iofop;

        /** Callback per IO fop. */
        struct c2_fom_callback  irf_iocb;

        /** Linkage to link in to target_ioreq::ti_iofops list. */
        struct c2_tlink         irf_link;

        /**
         * Backlink to nw_xfer_request object where rc and number of bytes
         * are updated.
         */
        struct nw_xfer_request *irf_nwxfer;
};

/**
 * Magic values to verify sanity of struct
 * - io_request, nw_xfer_request, target_ioreq and io_req_fop.
 */
enum {
        IOREQ_MAGIC  = 0xfea2303e31a3ce10ULL, // fearsomestanceto
        NWREQ_MAGIC  = 0x12ec0ffeea2ab1caULL, // thecoffeearabica
        TIOREQ_MAGIC = 0xfa1afe1611ab2eadULL, // falafelpitabread
        IOFOP_MAGIC  = 0xde514ab11117302eULL, // desirabilitymore
        TIOEXT_MAGIC = 0x19ca9de5ca91b61bULL, // incandescentbulb
        DTBUF_MAGIC  = 0x19c031960ffe9517ULL, // incomingoffensiv
};

C2_TL_DESCR_DEFINE(tiodescs, "List of target_ioreq objects", static,
                   struct target_ioreq, ti_link, ti_magic,
                   TIOREQ_MAGIC, NWREQ_MAGIC);

C2_TL_DESCR_DEFINE(iofops, "List of IO fops", static,
                   struct io_req_fop, irf_link, irf_magic,
                   IOFOP_MAGIC, TIOREQ_MAGIC);

C2_TL_DEFINE(tiodescs,  static, struct target_ioreq);
C2_TL_DEFINE(iofops,    static, struct io_req_fop);

static struct c2_bob_type tiodesc_bob;
static struct c2_bob_type ioext_bob;
static struct c2_bob_type iofop_bob;
static struct c2_bob_type ioreq_bob;

C2_BOB_DEFINE(static, &tiodesc_bob, target_ioreq);
C2_BOB_DEFINE(static, &iofop_bob,   io_req_fop);

/**
 * Bob type for struct io_request is manually initialized since it is
 * not associated with any c2_tl_descr.
 * @todo Replace this with the macro for manual initialization of c2_bob_type.
 */
static struct c2_bob_type ioreq_bobtype = {
        .bt_name         = "io_request_bobtype",
        .bt_magix_offset = offsetof(struct io_request, ir_magic),
        //.bt_magix        = C2_FIELD_VALUE(struct io_request, ir_magic),
        .bt_magix        = IOREQ_MAGIC,
        .bt_check        = NULL,
};

static struct c2_bob_type ioext_bobtype = {
        .bt_name         = "io_extent_bobtype",
        .bt_magix_offset = offsetof(struct target_ioext, ie_magic),
        .bt_magix        = TIOEXT_MAGIC,
        .bt_check        = NULL,
};

/** Invoked during c2t1fs mount. */
void io_bob_tlists_init(void)
{
        c2_bob_type_tlist_init(&tiodesc_bob, &tiodescs_tl);
        c2_bob_type_tlist_init(&iofop_bob,   &iofops_tl);
}

static int nw_xfer_io_prepare (struct nw_xfer_request *xfer);
static int nw_xfer_io_dispatch(struct nw_xfer_request *xfer);
static int nw_xfer_io_complete(struct nw_xfer_request *xfer);

static struct nw_xfer_ops xfer_ops = {
        .nxo_prepare  = nw_xfer_io_prepare,
        .nxo_dispatch = nw_xfer_io_dispatch,
        .nxo_complete = nw_xfer_io_complete,
};

static int io_request_readextent    (struct io_request *req);
static int io_request_prepare_write (struct io_request *req);
static int io_request_write_commit  (struct io_request *req);
static int io_request_submit        (struct io_request *req);
static int io_request_read_complete (struct io_request *req);
static int io_request_write_complete(struct io_request *req);

static struct io_request_ops ioreq_ops = {
        .iro_readextent     = io_request_readextent,
        .iro_preparewrite   = io_request_prepare_write,
        .iro_commit_write   = io_request_write_commit,
        .iro_submit         = io_request_submit,
        .iro_read_complete  = io_request_read_complete,
        .iro_write_complete = io_request_write_commit,
};

static int io_reading_state       (struct c2_sm *sm);
static int io_read_complete_state (struct c2_sm *sm);
static int io_writing_state       (struct c2_sm *sm);
static int io_write_complete_state(struct c2_sm *sm);
static int io_req_complete_state  (struct c2_sm *sm);

static const struct c2_sm_state_descr io_sm_state_descr[IRS_STATE_NR] = {
        [IRS_INITIALIZED]    = {
                .sd_flags     = C2_SDF_INITIAL,
                .sd_name      = "IO_initial",
                .sd_in        = NULL,
                .sd_ex        = NULL,
                .sd_invariant = NULL,
                .sd_allowed   = (1 << IRS_READING) | (1 << IRS_WRITING)
        },
        [IRS_READING]        = {
                .sd_flags     = 0,
                .sd_name      = "IO_reading",
                .sd_in        = io_reading_state,
                .sd_ex        = NULL,
                .sd_invariant = NULL,
                .sd_allowed   = 1 << IRS_READ_COMPLETE,
        },
        [IRS_READ_COMPLETE]  = {
                .sd_flags     = 0,
                .sd_name      = "IO_read_complete",
                .sd_in        = io_read_complete_state,
                .sd_ex        = NULL,
                .sd_invariant = NULL,
                .sd_allowed   = (1 << IRS_WRITING) | (1 << IRS_REQ_COMPLETE),
        },
        [IRS_WRITING] = {
                .sd_flags     = 0,
                .sd_name      = "IO_writing",
                .sd_in        = io_writing_state,
                .sd_ex        = NULL,
                .sd_invariant = NULL,
                .sd_allowed   = 1 << IRS_WRITE_COMPLETE,
        },
        [IRS_WRITE_COMPLETE] = {
                .sd_flags     = 0,
                .sd_name      = "IO_write_complete",
                .sd_in        = io_write_complete_state,
                .sd_ex        = NULL,
                .sd_invariant = NULL,
                .sd_allowed   = 1 << IRS_REQ_COMPLETE,
        },
        [IRS_REQ_COMPLETE]   = {
                .sd_flags     = C2_SDF_TERMINAL,
                .sd_name      = "IO_req_complete",
                .sd_in        = io_req_complete_state,
                .sd_ex        = NULL,
                .sd_invariant = NULL,
                .sd_allowed   = 0,
        },
};

static const struct c2_sm_conf io_sm_conf = {
        .scf_name      = "IO request state machine configuration",
        .scf_nr_states = IRS_STATE_NR - 1,
        .scf_state     = io_sm_state_descr,
};

#define FILE_TO_INODE(file)   ((file)->f_dentry->d_inode)
#define FILE_TO_C2INODE(file) (C2T1FS_I(FILE_TO_INODE(file)))
#define FILE_TO_FID(file)     (&FILE_TO_C2INODE(file)->ci_fid)
#define FILE_TO_SMGROUP(file) \
        (&C2T1FS_SB(FILE_TO_INODE(file)->i_sb)->csb_iogroup)

static bool io_request_invariant(const struct io_request *req)
{
        C2_PRE(req != NULL);

        return
                req->ir_magic  == IOREQ_MAGIC &&
                req->ir_type   <= IRT_TYPE_NR &&
                req->ir_state  <= IRS_STATE_NR &&
                req->ir_iovec  != NULL &&
                req->ir_ops    != NULL &&
                req->ir_seg_nr > 0 &&
                !c2_ext_is_empty(&req->ir_extent) &&
                c2_fid_is_valid(FILE_TO_FID(req->ir_file)) &&

                ergo(req->ir_state == IRS_READING,
                     !tiodescs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

                ergo(req->ir_state == IRS_WRITING,
                     !tiodescs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

                ergo(req->ir_state == IRS_READ_COMPLETE,
                     ergo(req->ir_type == IRT_READ,
                          tiodescs_tlist_is_empty(
                                  &req->ir_nwxfer.nxr_tioreqs))) &&

                ergo(req->ir_state == IRS_WRITE_COMPLETE,
                     tiodescs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

                ergo(io_req_is_rmw(req),
                     c2_ext_is_partof(&req->ir_aux_extent, &req->ir_extent));
}

static bool nw_xfer_request_invariant(const struct nw_xfer_request *xfer)
{
        return
                xfer != NULL &&
                xfer->nxr_magic == NWREQ_MAGIC &&
                xfer->nxr_state <= NXS_STATE_NR &&

                ergo(xfer->nxr_state == NXS_INITIALIZED,
                     tiodescs_tlist_is_empty(&xfer->nxr_tioreqs) &&
                     (xfer->nxr_rc == xfer->nxr_bytes) ==
                     (xfer->nxr_iofop_nr == 0)) &&

                ergo(xfer->nxr_state == NXS_INFLIGHT,
                     !tiodescs_tlist_is_empty(&xfer->nxr_tioreqs) &&
                     xfer->nxr_iofop_nr > 0) &&

                ergo(xfer->nxr_state == NXS_COMPLETE,
                     tiodescs_tlist_is_empty(&xfer->nxr_tioreqs) &&
                     xfer->nxr_iofop_nr == 0 &&
                     xfer->nxr_bytes > 0);
}

static bool target_ioreq_invariant(const struct target_ioreq *ti)
{
        return
                ti != NULL &&
                target_ioreq_bob_check(ti) &&
                ti->ti_session != NULL &&
                c2_fid_is_valid(&ti->ti_fid) &&
                tiodescs_tlink_is_in(ti);
}

static bool target_ioext_invariant(const struct target_ioext *ext)
{
        return
                ext != NULL &&
                c2_bob_check(&ioext_bobtype, ext) &&
                ergo(!c2_ext_is_empty(&ext->ie_ext),
                     ext->ie_buf_nr > 0 && ext->ie_buffers != NULL);
}

static bool io_req_fop_invariant(const struct io_req_fop *fop)
{
        return
                fop != NULL &&
                fop->irf_nwxfer != NULL &&
                fop->irf_iocb.fc_bottom != NULL &&
                io_req_fop_bob_check(fop);
}

static void io_request_expand(struct c2t1fs_inode *ci, struct io_request *req)
{
        void          *buf  = req->ir_iovec[0].iov_base;
        /* Original file extent. */
        struct c2_ext *oe = &req->ir_extent;
        /* Expanded file extent. */
        struct c2_ext *ee = &req->ir_aux_extent;

        C2_PRE(io_request_invariant(req));
        if (io_req_spans_full_pg(ci, buf, c2_ext_length(oe), oe->e_start))
                return;

        /*
         * finds immediate lower number which is aligned to
         * parity group unit size.
         */
        ee->e_start = c2_round_down(oe->e_start, PAGE_CACHE_SIZE);

        /*
         * finds immediate higher number which is aligned to
         * parity group unit size.
         */
        ee->e_end = ee->e_start + c2_round_up(oe->e_start - ee->e_start +
                                              c2_ext_length(oe),
                                              PAGE_CACHE_SIZE);

        C2_POST(c2_ext_is_partof(ee, oe) && io_request_invariant(req));
}

/**
 * Any partial parity group can lead to either "read-old" or "read-rest"
 * of the approaches, which is a part of read-modify-write IO request.
 */
static bool io_req_is_group_unaligned(const struct io_request *req)
{
        struct c2_pdclust_layout *pl = layout_to_pd_layout(FILE_TO_C2INODE(
                                                    req->ir_file));
        uint64_t group_size = pl->pl_unit_size * pl->pl_N;

        return
               req->ir_extent.e_start % group_size != 0 ||
               req->ir_extent.e_end   % group_size != 0;
}

static bool io_req_is_page_unaligned(const struct io_request *req)
{
        return
               req->ir_extent.e_start % PAGE_CACHE_SIZE != 0 ||
               req->ir_extent.e_end   % PAGE_CACHE_SIZE != 0;
}

static bool io_req_is_rmw(const struct io_request *req)
{
        return req->ir_type == IRT_WRITE && io_req_is_group_unaligned(req);
}

static void nw_xfer_request_init(struct nw_xfer_request *xfer)
{
        C2_PRE(xfer != NULL);

        xfer->nxr_magic = NWREQ_MAGIC;
        xfer->nxr_rc    = xfer->nxr_bytes = xfer->nxr_iofop_nr = 0;
        xfer->nxr_state = NXS_INITIALIZED;
        xfer->nxr_ops   = &xfer_ops;
        tiodescs_tlist_init(&xfer->nxr_tioreqs);

        C2_POST(nw_xfer_request_invariant(xfer));
}

static void nw_xfer_request_fini(struct nw_xfer_request *xfer)
{
        C2_PRE(xfer != NULL && xfer->nxr_state == NXS_COMPLETE);
        C2_PRE(nw_xfer_request_invariant(xfer));

        tiodescs_tlist_fini(&xfer->nxr_tioreqs);
}

static void io_request_init(struct io_request *req, struct file *file,
                            struct iovec *ivec, loff_t pos,
                            unsigned long seg_nr, enum io_req_type rw)
{
        C2_PRE(req    != NULL);
        C2_PRE(file   != NULL);
        C2_PRE(ivec   != NULL);
        C2_PRE(rw     < IRT_TYPE_NR);
        C2_PRE(seg_nr > 0);

        req->ir_rc     = 0;
        req->ir_ops    = &ioreq_ops;
        req->ir_file   = file;
        req->ir_iovec  = ivec;
        req->ir_type   = rw;
        req->ir_state  = IRS_INITIALIZED;
        req->ir_seg_nr = seg_nr;

        req->ir_extent.e_start     = pos;
        req->ir_extent.e_end       = iov_length(ivec, seg_nr);
        req->ir_aux_extent.e_start = req->ir_aux_extent.e_end = 0;

        c2_bob_init(&ioreq_bobtype, req);
        nw_xfer_request_init(&req->ir_nwxfer);
        c2_sm_init(&req->ir_sm, &io_sm_conf, IRS_INITIALIZED,
                   FILE_TO_SMGROUP(req->ir_file), NULL);

        C2_POST(io_request_invariant(req));
}

static void io_request_fini(struct io_request *req)
{
        C2_PRE(io_request_invariant(req));

        c2_sm_fini(&req->ir_sm);
        c2_bob_fini(&ioreq_bobtype, req);
        nw_xfer_request_fini(&req->ir_nwxfer);
        req->ir_file  = NULL;
        req->ir_iovec = NULL;
}

static void data_buf_init(struct data_buf *buf,
                          void *addr, c2_bcount_t len,
                          enum c2_pdclust_unit_type type)
{
        buf->db_magic = DTBUF_MAGIC;
        buf->db_type  = type;
        c2_buf_init(&buf->db_buf, addr, len);
}

static void data_buf_fini(struct data_buf *buf)
{
        buf->db_magic      = 0;
        buf->db_buf.b_addr = NULL;
        buf->db_buf.b_nob  = 0;
        buf->db_type       = PUT_NR;
}

static int target_ioext_init(struct target_ioext *ext, uint64_t frame_nr_size)
{
        C2_PRE(ext != NULL);
        C2_PRE(frame_nr_size > 0);
        C2_PRE((frame_nr_size & (PAGE_CACHE_SIZE - 1)) == 0);

        c2_bob_init(&ioext_bobtype, ext);
        ext->ie_ext.e_start = ext->ie_ext.e_end = 0;
        ext->ie_buf_nr  = frame_nr_size / PAGE_CACHE_SIZE;

        /* Contents of ext->ie_buffers is set to NULL after allocation. */
        C2_ALLOC_ARR(ext->ie_buffers, ext->ie_buf_nr);
        C2_POST(target_ioext_invariant(ext));

        return ext->ie_buffers == NULL ? -ENOMEM : 0;
}

#define WRITE_STATE_FROM_RMW(req) \
        io_req_is_rmw(req) && (req)->ir_state == IRS_WRITING

#define READ_STATE_FROM_RMW(req)  \
        io_req_is_rmw(req) && (req)->ir_state == IRS_READING

static uint64_t last_data_buf_id(struct target_ioext *ext)
{
        uint64_t i;

        for (; ext->ie_buffers[i] != NULL; ++i);
        return i;
}

/**
 * Populates the target_ioreq::target_ioext with struct data_buf.
 * @post rc == 0 || rc == -ENOMEM.
 */
static int target_ioext_populate(struct target_ioext      *ext,
                                 c2_bindex_t               tgtpos,
                                 c2_bindex_t               filepos,
                                 c2_bcount_t               count,
                                 enum c2_pdclust_unit_type type,
                                 struct io_request        *req)
{
        int                       rc;
        size_t                    written = 0;
        uint64_t                  pg = 0;
        uint64_t                  i = 0;
        struct page              *page;
        struct data_buf          *buf;
        uint64_t                  anchor;

        C2_PRE(target_ioext_invariant(ext));
        C2_PRE(type < PUT_NR);
        C2_PRE(io_request_invariant(req));

        if (c2_ext_is_empty(&ext->ie_ext))
                ext->ie_ext.e_start = tgtpos;

        ext->ie_ext.e_end = tgtpos + count;
        anchor            = last_data_buf_id(ext);

        /*
         * Write state from rmw IO operation (read + update + write)
         * does not need to allocate new data buffers. Its data buffers
         * are already allocated and populated by preceding read request.
         * Rest all permutations like {un/}aligned read IO and
         * aligned write IO need to allocate pages.
         */
        if (!WRITE_STATE_FROM_RMW(req)) {
                do {
                        page = (struct page *)__get_free_page(GFP_KERNEL);
                        if (page == NULL) {
                                rc = -ENOMEM;
                                goto fail;
                        }

                        buf = c2_alloc(sizeof *buf);
                        if (buf == NULL) {
                                __free_page(page);
                                rc = -ENOMEM;
                                goto fail;
                        }

                        data_buf_init(buf, page_address(page),
                                      min64u(count, PAGE_CACHE_SIZE), type);

                        ext->ie_buffers[anchor + i] = buf;
                        ++i;
                        pg += PAGE_CACHE_SIZE;
                } while (pg < count / PAGE_CACHE_SIZE);
        }

        /**
         * For aligned write IO and rmw IO requests, data needs to be
         * copied from user_space buffers.
         */
        if (type == PUT_DATA && req->ir_state == IRS_WRITING) {
                struct iov_iter it;
                iov_iter_init(&it, req->ir_iovec, req->ir_seg_nr, count,
                              written);
                i = 0;

                do {
                        c2_bindex_t poffset;
                        c2_bindex_t pbytes;
                        size_t      copied;

                        C2_PRE(anchor + i < ext->ie_buf_nr);

                        buf     = ext->ie_buffers[anchor + i];
                        poffset = filepos & (PAGE_CACHE_SIZE - 1);
                        pbytes  = min_type(c2_bindex_t,
                                           PAGE_CACHE_SIZE - poffset,
                                           iov_iter_count(&it));

                        pagefault_disable();
                        copied = iov_iter_copy_from_user_atomic(
                                        buf->db_buf.b_addr, &it, poffset,
                                        pbytes);
                        pagefault_enable();

                        if (copied != pbytes) {
                                rc = -EFAULT;
                                goto fail;
                        }

                        iov_iter_advance(&it, copied);
                        written += copied;
                        filepos += copied;
                        ++i;
                } while (buf != NULL);
        }
        C2_POST(written == count);
        return 0;
fail:
        if (!WRITE_STATE_FROM_RMW(req)) {
                i = 0;

                do {
                        buf = ext->ie_buffers[anchor + i];
                        __free_page(buf->db_buf.b_addr);
                        data_buf_fini(buf);
                        c2_free(buf);
                        ext->ie_buffers[anchor + i] = NULL;
                        ++i;
                } while (buf != NULL);
        }
        return rc;
}

/** Assumes associated buffer is already deallocated before fini. */
static void target_ioext_fini(struct target_ioext *ext)
{
        C2_PRE(ext != NULL);
        C2_PRE(!c2_ext_is_empty(&ext->ie_ext));

        c2_bob_fini(&ioext_bobtype, ext);
        c2_free(ext->ie_buffers);
        ext->ie_buffers = NULL;
        ext->ie_buf_nr  = 0;
}

void target_ioext_parity_get(struct target_ioext *ext, struct c2_bufvec *in)
{
        uint64_t i;
        uint64_t id;

        C2_PRE(target_ioext_invariant(ext));
        C2_PRE(in != NULL);

        for (i = 0, id = 0; i < ext->ie_buf_nr; ++i) {
                if (ext->ie_buffers[i] &&
                    ext->ie_buffers[i]->db_type == PUT_PARITY) {
                        in->ov_buf[id] = ext->ie_buffers[i]->db_buf.b_addr;
                        in->ov_vec.v_count[id] = ext->ie_buffers[i]->
                                                 db_buf.b_nob;
                        ++id;
                        in->ov_vec.v_nr = id;
                }
        }
}

static int target_ioreq_init(struct target_ioreq   *ti,
                             const struct c2_fid   *cobfid,
                             struct c2_rpc_session *session,
                             uint64_t               frame_nr_size)
{
        int rc;

        C2_PRE(ti      != NULL);
        C2_PRE(cobfid  != NULL);
        C2_PRE(session != NULL);
        C2_PRE(frame_nr_size > 0);

        iofops_tlist_init(&ti->ti_iofops);
        tiodescs_tlink_init(ti);
        target_ioreq_bob_init(ti);

        ti->ti_fid     = *cobfid;
        ti->ti_session = session;
        rc = target_ioext_init(&ti->ti_extent, frame_nr_size);

        C2_POST(target_ioreq_invariant(ti));
        return rc;
}

static void target_ioreq_fini(struct target_ioreq *ti)
{
        C2_PRE(target_ioreq_invariant(ti));

        iofops_tlist_fini(&ti->ti_iofops);
        tiodescs_tlink_fini(ti);
        target_ioreq_bob_fini(ti);
        ti->ti_session = NULL;
}

static int target_object_locate(struct nw_xfer_request  *xfer,
                                struct c2_fid           *fid,
                                struct c2_rpc_session   *session,
                                uint64_t                 frame_nr_size,
                                struct target_ioreq    **out)
{
        int                  rc = 0;
        struct target_ioreq *ti;

        C2_PRE(nw_xfer_request_invariant(xfer));
        C2_PRE(session != NULL);
        C2_PRE(fid     != NULL);
        C2_PRE(out     != NULL);
        C2_PRE(frame_nr_size > 0);

        c2_tl_for (tiodescs, &xfer->nxr_tioreqs, ti) {
                if (c2_fid_eq(&ti->ti_fid, fid))
                        goto last;
        } c2_tl_endfor;

        ti = c2_alloc(sizeof *ti);
        if (ti == NULL) {
                rc = -ENOMEM;
                goto last;
        }

        rc = target_ioreq_init(ti, fid, session, frame_nr_size);
        tiodescs_tlist_add(&xfer->nxr_tioreqs, ti);
last:
        *out = ti;
        return rc;
}

#define REQ_TO_FOP_TYPE(req) ((req)->ir_type == IRT_READ ? \
                &c2_fop_cob_readv_fopt : &c2_fop_cob_writev_fopt)

static int io_req_fop_init(struct io_req_fop      *fop,
                           struct nw_xfer_request *xfer)
{
        int                rc;
        struct io_request *req;

        C2_PRE(fop  != NULL);
        C2_PRE(xfer != NULL);

        iofops_tlink_init(fop);
        io_req_fop_bob_init(fop);
        fop->irf_nwxfer = xfer;

        fop->irf_iocb.fc_fom = NULL;
        /** @todo assign bottom half function. */
        fop->irf_iocb.fc_bottom = NULL;

        req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
        rc  = c2_io_fop_init(&fop->irf_iofop, REQ_TO_FOP_TYPE(req));

        C2_POST(io_req_fop_invariant(fop));
        return rc;
}

static void io_req_fop_fini(struct io_req_fop *fop)
{
        C2_PRE(io_req_fop_invariant(fop));

        /**
         * IO fop is fini-ed (c2_io_fop_fini()) through rpc sessions code
         * using c2_rpc_item::c2_rpc_item_ops::rio_free().
         * @see io_item_free().
         */

        iofops_tlink_fini(fop);
        io_req_fop_bob_fini(fop);
        c2_fom_callback_fini(&fop->irf_iocb);
        fop->irf_nwxfer = NULL;
}

/*
 * A read request from rmw IO request can lead to either
 *
 * read_old - Read the old data for the extent spanned by current
 * IO request, along with the old parity unit. This approach needs
 * to calculate new parity in _iterative_ manner. This approach is
 * selected only if current IO extent lies within file size.
 *
 * read_rest - Read rest of the parity group, which is _not_ spanned
 * by current IO request, so that data for whole parity group can
 * be availble for parity calculation.
 *
 * Typically, the approach which leads to least size of data to be
 * read from server is selected.
 */
static void nw_xfer_io_read(struct nw_xfer_request *xfer)
{
        bool                      readold;
        /* Size of IO request in bytes. */
        uint64_t                  read_old;
        /* Size of IO request in bytes. */
        uint64_t                  read_rest;
        uint64_t                  grpstart;
        uint64_t                  grpend;
        struct c2_ext            *ext;
        struct io_request        *req;
        struct c2_pdclust_layout *play;

        C2_PRE(nw_xfer_request_invariant(xfer));

        req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
        C2_ASSERT(io_request_invariant(req));
        C2_ASSERT(io_req_is_rmw(req));

        ci         = FILE_TO_C2INODE(req->ir_file);
        play       = layout_to_pd_layout(ci->ci_layout);
        ext        = io_req_is_page_unaligned(req) ? &req->ir_aux_extent :
                     &req->ir_extent;
        filepos    = ext->e_start;
        unit_size  = play->pl_unit_size;
        unit_nr    = play->pl_N + 2 * play->pl_K;
        group_size = play->pl_N * unit_size;
        group_nr   = c2_ext_length(ext) / group_size + 1;
        grpstart   = ext->e_start / group_size;
        groupend   = ext->e_end   / group_size;

        /*
         * - Iterate over number of groups.
         * - Skip full parity groups. Current read requests apply only to
         *   partial parity groups.
         * - For every partial parity group, chose if read-old or read-rest
         *   will be used.
         * - Prepare target_ioreq structures.
         * - Remember to read old parity unit in case of read-old.
         */

        for (; grpstart <= grpend; ++grpstart) {

                if (filepos == grpstart * group_size &&
                    filepos + group_size < ext->e_end)
                        continue;

                read_old  = c2_round_up(filepos, group_size) - filepos;
                read_rest = filepos - c2_round_down(filepos, group_size);

                if (read_old + unit_size < read_rest &&
                    c2_round_up(filepos, group_size) <= ci->ci_inode.i_size) {
                        unit = (filepos % group_size) / unit_size;
                        readold = true;
                } else
                        unit = 0;

                for (; unit < unit_nr; ++unit) {

                        c2_bcount_t bcount;

                        unit_type = c2_pdclust_unit_classify(play, unit);

                        if (unit_type == PUT_SPARE ||
                            (!readold && unit_type == PUT_PARITY))
                                continue;

                        src.sa_unit = unit;
                        c2_pdclust_layout_map(play, &src, &tgt);

                        tfid    = c2t1fs_cob_fid(&ci->ci_fid, tgt.ta_obj + 1);
                        session = c2t1fs_container_id_to_session(csb,
                                        tfid.f_container);
                        bcount  = min_type(c2_bcount_t, c2_ext_length(ext),
                                           unit_size);

                        rc = target_object_locate(xfer, &tfid, session,
                                                  unit_size * group_nr, &ti);

                        if (rc != 0)
                                goto fail;

                        rc = target_ioext_populate(&ti->tir_extent,
                                                   tgt.ta_frame * unit_size,
                                                   filepos, bcount, unit_type,
                                                   req);
                        if (rc != 0)
                                goto fail;

                        filepos += bcount;

                }
        }
}

static int nw_xfer_io_prepare(struct nw_xfer_request *xfer)
{
        int                          rc;
        struct io_request           *req;
        /** Extent in file linear address space which is read/written to. */
        struct c2_ext               *ext;
        /** Fid of component object. */
        struct c2_fid                tfid;
        struct c2_pdclust_layout    *play;
        enum c2_pdclust_unit_type    unit_type;
        struct c2_pdclust_src_addr   src;
        struct c2_pdclust_tgt_addr   tgt;
        /** Number of parity groups to read/write. */
        uint32_t                     group_nr;
        uint32_t                     grp = 0;
        /** Number of units in a parity group. */
        uint32_t                     unit_nr;
        uint32_t                     unit;
        uint64_t                     unit_size;
        uint64_t                     i;
        /** Size of all data units in parity group. */
        uint64_t                     group_size;
        /** Offset in global file. */
        c2_bindex_t                  filepos;
        /*
         * File offset corresponding to {start/end} of a parity group spanned
         * by current io request.
         */
        c2_bindex_t                  pgspos;

        struct target_ioreq         *ti;
        struct c2t1fs_inode         *ci;
        struct c2t1fs_sb            *csb;
        struct c2_bufvec            *data_vecs;
        struct c2_bufvec            *parity_vecs;
        struct c2_rpc_session       *session;

        C2_PRE(nw_xfer_request_invariant(xfer));

        req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
        C2_ASSERT(io_request_invariant(req));

        /*
         * If this is reading state from rmw IO request, IO should be
         * issued to read the unit_size unaligned data buffers first
         * so that rest of the computation can be while data is being
         * read from server.
         */
        if (READ_STATE_FROM_RMW(req))
                nw_xfer_io_read(xfer);

        ci   = FILE_TO_C2INODE(req->ir_file);
        play = layout_to_pd_layout(ci->ci_layout);
        csb  = C2T1FS_SB(ci->ci_inode.i_sb);
        ext  = io_req_is_page_unaligned(req) ? &req->ir_aux_extent :
               &req->ir_extent;

        filepos    = ext->e_start;
        unit_size  = play->pl_unit_size;
        unit_nr    = play->pl_N + 2 * play->pl_K;
        group_size = play->pl_N * unit_size;
        group_nr   = c2_ext_length(ext) / group_size + 1;

        C2_ALLOC_ARR(data_vecs,   unit_size / PAGE_CACHE_SIZE);
        C2_ALLOC_ARR(parity_vecs, unit_size / PAGE_CACHE_SIZE);

        if (data_vecs == NULL || parity_vecs == NULL) {
                rc = -ENOMEM;
                goto fail;
        }

        /** @todo Allocate memory for bufvec::ov_bufs and
         * bufvec::ov_vec::v_count */

        C2_ASSERT((unit_size & (PAGE_CACHE_SIZE - 1)) == 0);

        unit         = (ext->e_start % group_size) / unit_size;
        src.sa_group = ext->e_start / group_size;

        do {

                for (pgspos = filepos; unit < unit_nr; ++unit) {

                        c2_bcount_t bcount;
                        unit_type = c2_pdclust_unit_classify(play, unit);

                        /**
                         * @todo For spare units, a fop should be sent
                         * which will allocate a block on the stob.
                         */
                        if (unit_type == PUT_SPARE)
                                continue;

                        src.sa_unit = unit;

                        c2_pdclust_layout_map(play, &src, &tgt);
                        tfid    = c2t1fs_cob_fid(&ci->ci_fid, tgt.ta_obj + 1);
                        session = c2t1fs_container_id_to_session(csb,
                                        tfid.f_container);
                        bcount  = min_type(c2_bcount_t, c2_ext_length(ext),
                                           unit_size);


                        rc = target_object_locate(xfer, &tfid, session,
                                                  unit_size * group_nr, &ti);
                        if (ti == NULL) {
                                rc = -ENOMEM;
                                goto fail;
                        }

                        rc = target_ioext_populate(&ti->ti_extent,
                                                   tgt.ta_frame * unit_size,
                                                   filepos, bcount, unit_type,
                                                   req);
                        if (rc != 0)
                                goto fail;

                        if (unit_type == PUT_DATA) {
                                filepos += bcount;
                                for (i = 0; i < unit_size / PAGE_CACHE_SIZE;
                                     ++i)
                                        data_vecs[i].ov_buf[unit] = ti->
                                                ti_extent.ie_buffers[grp *
                                                unit_size / PAGE_CACHE_SIZE
                                                + i];
                        } else if (unit_type == PUT_PARITY) {
                                for (i = 0; i < unit_size / PAGE_CACHE_SIZE;
                                     ++i)
                                        parity_vecs[i].ov_buf[unit] = ti->
                                                ti_extent.ie_buffers[grp *
                                                unit_size / PAGE_CACHE_SIZE
                                                + i];
                        }
                        /**
                         * Calculate parity unit for partial parity groups
                         * as well.
                         */
                        if (req->ir_state == IRS_WRITING &&
                            ((ext->e_end % group_size) / unit_size) + 1 <
                             unit_nr && (grp + 1 == group_nr))
                                unit = play->pl_N;
                }

                /** Parity need not be calculated for read requests. */
                if (req->ir_state == IRS_WRITING) {
                        /**
                         * 1. If part/whole of current parity group lies
                         * within EOF, there must be a pre-existing
                         * parity unit for this group. Calculate delta
                         * parity in this case.
                         *
                         * 2. If current parity group lies beyond EOF,
                         * this is append operation and parity must be
                         * calculated afresh.
                         */
                        if (pgspos > ci->ci_inode.i_size - 1) {
                                uint32_t i;

                                for (i = 0; i < unit_size / PAGE_CACHE_SIZE;
                                     ++i)
                                        c2_parity_math_calculate_bvec(
                                                &play->pl_math,
                                                data_vecs[i], parity_vecs);
                        } else {
                                //struct c2_bufvec bvec;

                                //target_ioext_parity_get();
                        }
                }

                ++grp;
                ++src.sa_group;
                unit = 0;
        } while (grp < group_nr);
}

static ssize_t file_aio_write(struct kiocb *kcb, const struct iovec *ivec,
                              unsigned long seg_nr, loff_t pos)
{
        int                  rc;
        size_t               count = 0;
        size_t               saved_count;
        struct io_request   *req;

	rc = generic_segment_checks(ivec, &seg_nr, &count, VERIFY_READ);
	if (rc != 0)
                return 0;

	saved_count = count;
	rc = generic_write_checks(kcb->ki_filp, &pos, &count, 0);
	if (rc != 0 || count == 0)
                return 0;

	if (count != saved_count)
		seg_nr = iov_shorten((struct iovec *)ivec, seg_nr, count);

        req = c2_alloc(sizeof(struct io_request));
        if (req == NULL)
                return 0;

        io_request_init(req, kcb->ki_filp, (struct iovec *)ivec,
                        pos, seg_nr, IRT_WRITE);

        c2_sm_state_set(&req->ir_sm, IRS_WRITING);
        C2_POST(req->ir_sm.sm_state == IRS_REQ_COMPLETE);

        count = req->ir_nwxfer.nxr_bytes;
        io_request_fini(req);
        c2_free(req);
        return count;
}

static int io_reading_state(struct c2_sm *sm)
{
        struct io_request *req;

        C2_PRE(sm != NULL);

        req = bob_of(sm, struct io_request, ir_sm, &ioreq_bobtype);
        C2_ASSERT(io_request_invariant(req));

        if (io_req_is_page_unaligned(req))
                io_request_expand(FILE_TO_C2INODE(req->ir_file), req);
}

static int io_writing_state(struct c2_sm *sm)
{
        struct io_request *req;

        C2_PRE(sm != NULL);

        req = bob_of(sm, struct io_request, ir_sm, &ioreq_bobtype);
        C2_ASSERT(io_request_invariant(req));

        if (io_req_is_rmw(req))
                io_request_expand(FILE_TO_C2INODE(req->ir_file), req);
}

const struct file_operations c2t1fs_reg_file_operations = {
	.llseek    = generic_file_llseek,   /* provided by linux kernel */
	.aio_read  = c2t1fs_file_aio_read,
	//.aio_write = c2t1fs_file_aio_write,
	.aio_write = file_aio_write,
	.read      = do_sync_read,          /* provided by linux kernel */
	.write     = do_sync_write,         /* provided by linux kernel */
};

const struct inode_operations c2t1fs_reg_inode_operations;

#define KIOCB_TO_FILE_NAME(iocb) ((iocb)->ki_filp->f_path.dentry->d_name.name)

static ssize_t c2t1fs_file_aio_read(struct kiocb       *iocb,
				    const struct iovec *iov,
				    unsigned long       nr_segs,
				    loff_t              pos)
{
	unsigned long i;
	ssize_t       result = 0;
	ssize_t       nr_bytes_read = 0;
	ssize_t       count;

	C2_ENTRY();

	C2_LOG(C2_DEBUG, "Read req: file \"%s\" pos %lu nr_segs %lu iov_len %lu",
					KIOCB_TO_FILE_NAME(iocb),
					(unsigned long)pos,
					nr_segs,
					iov_length(iov, nr_segs));

	if (nr_segs == 0)
		goto out;

	result = generic_segment_checks(iov, &nr_segs, &count, VERIFY_WRITE);
	if (result != 0) {
		C2_LOG(C2_ERROR, "Generic segment checks failed: %lu",
						(unsigned long)result);
		goto out;
	}

	if (count == 0)
		goto out;

	for (i = 0; i < nr_segs; i++) {
		const struct iovec *vec = &iov[i];

		C2_LOG(C2_DEBUG, "iov: base %p len %lu pos %lu", vec->iov_base,
					(unsigned long)vec->iov_len,
					(unsigned long)iocb->ki_pos);

		result = c2t1fs_read_write(iocb->ki_filp, vec->iov_base,
					   vec->iov_len, &iocb->ki_pos, READ);

		C2_LOG(C2_DEBUG, "result: %ld", (long)result);
		if (result <= 0)
			break;

		nr_bytes_read += result;

		if ((size_t)result < vec->iov_len)
			break;
	}
out:
	C2_LEAVE("bytes_read: %ld", nr_bytes_read ?: result);
	return nr_bytes_read ?: result;
}

static ssize_t c2t1fs_file_aio_write(struct kiocb       *iocb,
				     const struct iovec *iov,
				     unsigned long       nr_segs,
				     loff_t              pos)
{
	unsigned long i;
	ssize_t       result = 0;
	ssize_t       nr_bytes_written = 0;
	size_t        count = 0;
	size_t        saved_count;

	C2_ENTRY();

	C2_LOG(C2_DEBUG, "WRITE req: file %s pos %lu nr_segs %lu iov_len %lu",
			KIOCB_TO_FILE_NAME(iocb),
			(unsigned long)pos,
			nr_segs,
			iov_length(iov, nr_segs));

	result = generic_segment_checks(iov, &nr_segs, &count, VERIFY_READ);
	if (result != 0) {
		C2_LOG(C2_ERROR, "Generic segment checks failed: %lu",
						(unsigned long)result);
		goto out;
	}

	saved_count = count;
	result = generic_write_checks(iocb->ki_filp, &pos, &count, 0);
	if (result != 0) {
		C2_LOG(C2_ERROR, "generic_write_checks() failed %lu",
						(unsigned long)result);
		goto out;
	}

	if (count == 0)
		goto out;

	if (count != saved_count) {
		nr_segs = iov_shorten((struct iovec *)iov, nr_segs, count);
		C2_LOG(C2_WARN, "write size changed to %lu",
				(unsigned long)count);
	}

	for (i = 0; i < nr_segs; i++) {
		const struct iovec *vec = &iov[i];

		C2_LOG(C2_DEBUG, "iov: base %p len %lu pos %lu", vec->iov_base,
					(unsigned long)vec->iov_len,
					(unsigned long)iocb->ki_pos);

		result = c2t1fs_read_write(iocb->ki_filp, vec->iov_base,
					   vec->iov_len, &iocb->ki_pos, WRITE);

		C2_LOG(C2_DEBUG, "result: %ld", (long)result);
		if (result <= 0)
			break;

		nr_bytes_written += result;

		if ((size_t)result < vec->iov_len)
			break;
	}
out:
	C2_LEAVE("bytes_written: %ld", nr_bytes_written ?: result);
	return nr_bytes_written ?: result;
}

static bool address_is_page_aligned(unsigned long addr)
{
	C2_LOG(C2_DEBUG, "addr %lx mask %lx", addr, PAGE_CACHE_SIZE - 1);
	return (addr & (PAGE_CACHE_SIZE - 1)) == 0;
}

static bool io_req_spans_full_pg(struct c2t1fs_inode *ci,
			         char                *buf,
				 size_t               count,
				 loff_t               pos)
{
	struct c2_pdclust_instance *pi;
	struct c2_pdclust_layout   *pl;
	uint64_t                    stripe_width;
	unsigned long               addr;
	bool                        result;

	C2_ENTRY();

	addr = (unsigned long)buf;

	pi = c2_layout_instance_to_pdi(ci->ci_layout_instance);
	pl = pi->pi_layout;

	/* stripe width = number of data units * size of each unit */
	stripe_width = c2_pdclust_N(pl) * c2_pdclust_unit_size(pl);

	/*
	 * Requested IO size and position within file must be
	 * multiple of stripe width.
	 * Buffer address must be page aligned.
	 */
	C2_LOG(C2_DEBUG, "count = %lu", (unsigned long)count);
	C2_LOG(C2_DEBUG, "width %lu count %% width %lu pos %% width %lu",
			(unsigned long)stripe_width,
			(unsigned long)(count % stripe_width),
			(unsigned long)(pos % stripe_width));
	result = count % stripe_width == 0 &&
		 pos   % stripe_width == 0 &&
		 address_is_page_aligned(addr);

	C2_LEAVE("result: %d", result);
	return result;
}

static int c2t1fs_pin_memory_area(char          *buf,
				  size_t         count,
				  int            rw,
				  struct page ***pinned_pages,
				  int           *nr_pinned_pages)
{
	struct page   **pages;
	unsigned long   addr;
	unsigned long   va;
	int             off;
	int             nr_pages;
	int             nr_pinned;
	int             i;
	int             rc = 0;

	C2_ENTRY();

	addr = (unsigned long)buf & PAGE_CACHE_MASK;
	off  = (unsigned long)buf & (PAGE_CACHE_SIZE - 1);
	/* as we've already confirmed that buf is page aligned,
		should always be 0 */
	C2_PRE(off == 0);

	nr_pages = (off + count + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	C2_ALLOC_ARR(pages, nr_pages);
	if (pages == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	C2_LOG(C2_DEBUG, "addr 0x%lx off %d nr_pages %d", addr, off, nr_pages);

	if (current->mm != NULL && access_ok(rw == READ, addr, count)) {

		/* addr points in user space */
		down_read(&current->mm->mmap_sem);
		nr_pinned = get_user_pages(current, current->mm, addr, nr_pages,
				    rw == READ, 0, pages, NULL);
		up_read(&current->mm->mmap_sem);

	} else {

		/* addr points in kernel space */
		for (i = 0, va = addr; i < nr_pages; i++,
						     va += PAGE_CACHE_SIZE) {
			pages[i] = virt_to_page(va);
			get_page(pages[i]);
		}
		nr_pinned = nr_pages;

	}

	if (nr_pinned != nr_pages) {
		C2_LOG(C2_ERROR, "Failed: could pin only %d pages out of %d",
				rc, nr_pages);

		for (i = 0; i < nr_pinned; i++)
			put_page(pages[i]);

		c2_free(pages);
		rc = -EFAULT;
		goto out;
	}
	for (i = 0; i < nr_pages; i++)
		C2_LOG(C2_DEBUG, "Pinned page[0x%p] buf [0x%p] count [%lu]",
				pages[i], buf, (unsigned long)count);

	*pinned_pages    = pages;
	*nr_pinned_pages = nr_pinned;

	C2_LEAVE("rc: 0");
	return 0;
out:
	*pinned_pages    = NULL;
	*nr_pinned_pages = 0;

	C2_LEAVE("rc: %d", rc);
	return rc;
}

static ssize_t c2t1fs_read_write(struct file *file,
				 char        *buf,
				 size_t       count,
				 loff_t      *ppos,
				 int          rw)
{
	struct inode         *inode;
	struct c2t1fs_inode  *ci;
	struct page         **pinned_pages;
	int                   nr_pinned_pages;
	loff_t                pos = *ppos;
	ssize_t               rc;
	int                   i;

	C2_ENTRY();

	C2_PRE(count != 0);

	inode = file->f_dentry->d_inode;
	ci    = C2T1FS_I(inode);
	C2_PRE(c2t1fs_inode_bob_check(ci));

	if (rw == READ) {
		if (pos > inode->i_size) {
			rc = 0;
			goto out;
		}

		/* check if io spans beyond file size */
		if (pos + count > inode->i_size) {
			count = inode->i_size - pos;
			C2_LOG(C2_DEBUG, "i_size %lu, read truncated to %lu",
					(unsigned long)inode->i_size,
					(unsigned long)count);
		}
	}

	C2_LOG(C2_DEBUG, "%s %lu bytes at pos %lu to %p",
				(char *)(rw == READ ? "Read" : "Write"),
				(unsigned long)count,
				(unsigned long)pos, buf);

	if (!io_req_spans_full_pg(ci, buf, count, pos)) {
		rc = -EINVAL;
		goto out;
	}

	rc = c2t1fs_pin_memory_area(buf, count, rw, &pinned_pages,
						&nr_pinned_pages);
	if (rc != 0)
		goto out;

	rc = c2t1fs_internal_read_write(ci, buf, count, pos, rw);
	if (rc > 0) {
		pos += rc;
		if (rw == WRITE && pos > inode->i_size)
			inode->i_size = pos;
		*ppos = pos;
	}

	for (i = 0; i < nr_pinned_pages; i++)
		put_page(pinned_pages[i]);
out:
	C2_LEAVE("rc: %ld", rc);
	return rc;
}

/**
   Read/write descriptor that describes io on cob identified by rd_fid.
 */
struct rw_desc {
	/** io fop should be sent on this session */
	struct c2_rpc_session *rd_session;

	/** fid of component object */
	struct c2_fid          rd_fid;

	/** number of bytes to [read from|write to] */
	size_t                 rd_count;

	/** List of c2t1fs_buf objects hanging off cb_link */
	struct c2_tl           rd_buf_list;

	/** link within a local list created by c2t1fs_internal_read_write() */
	struct c2_tlink        rd_link;

	/** magic = C2_T1FS_RW_DESC_MAGIC */
	uint64_t               rd_magic;
};

C2_TL_DESCR_DEFINE(rwd, "rw descriptors", static, struct rw_desc, rd_link,
		   rd_magic, C2_T1FS_RW_DESC_MAGIC, C2_T1FS_RW_DESC_HEAD_MAGIC);

C2_TL_DEFINE(rwd, static, struct rw_desc);

struct c2t1fs_buf {
	/** <addr, len> giving memory area, target location for read operation,
	    and source for write operation */
	struct c2_buf             cb_buf;

	/** Offset within stob */
	c2_bindex_t               cb_off;

	/** type of contents in the cb_buf data, parity or spare */
	enum c2_pdclust_unit_type cb_type;

	/** link in rw_desc::rd_buf_list */
	struct c2_tlink           cb_link;

	/** magic = C2_T1FS_BUF_MAGIC */
	uint64_t                  cb_magic;
};

C2_TL_DESCR_DEFINE(bufs, "buf list", static, struct c2t1fs_buf, cb_link,
		   cb_magic, C2_T1FS_BUF_MAGIC, C2_T1FS_BUF_HEAD_MAGIC);

C2_TL_DEFINE(bufs, static, struct c2t1fs_buf);

static void c2t1fs_buf_init(struct c2t1fs_buf *buf,
			    char              *addr,
			    size_t             len,
			    c2_bindex_t        off,
			    enum c2_pdclust_unit_type unit_type)
{
	C2_LOG(C2_DEBUG, "buf %p addr %p len %lu", buf, addr, (unsigned long)len);

	c2_buf_init(&buf->cb_buf, addr, len);
	bufs_tlink_init(buf);
	buf->cb_off   = off;
	buf->cb_type  = unit_type;
	buf->cb_magic = C2_T1FS_BUF_MAGIC;
}

static void c2t1fs_buf_fini(struct c2t1fs_buf *buf)
{
	if (buf->cb_type == C2_PUT_PARITY || buf->cb_type == C2_PUT_SPARE)
		c2_free(buf->cb_buf.b_addr);

	bufs_tlink_fini(buf);
	buf->cb_magic = 0;
}

static struct rw_desc * rw_desc_get(struct c2_tl        *list,
				    const struct c2_fid *fid)
{
	struct rw_desc *rw_desc;

	C2_ENTRY("fid [%lu:%lu]", (unsigned long)fid->f_container,
	                          (unsigned long)fid->f_key);

	c2_tl_for(rwd, list, rw_desc) {

		if (c2_fid_eq(fid, &rw_desc->rd_fid))
			goto out;

	} c2_tl_endfor;

	C2_ALLOC_PTR(rw_desc);
	if (rw_desc == NULL)
		goto out;

	rw_desc->rd_fid     = *fid;
	rw_desc->rd_count   = 0;
	rw_desc->rd_session = NULL;
	rw_desc->rd_magic   = C2_T1FS_RW_DESC_MAGIC;

	bufs_tlist_init(&rw_desc->rd_buf_list);

	rwd_tlink_init_at_tail(rw_desc, list);
out:
	C2_LEAVE("rw_desc: %p", rw_desc);
	return rw_desc;
}

static void rw_desc_fini(struct rw_desc *rw_desc)
{
	struct c2t1fs_buf   *buf;

	C2_ENTRY();

	c2_tl_for(bufs, &rw_desc->rd_buf_list, buf) {

		bufs_tlist_del(buf);
		c2t1fs_buf_fini(buf);
		c2_free(buf);

	} c2_tl_endfor;
	bufs_tlist_fini(&rw_desc->rd_buf_list);

	rwd_tlink_fini(rw_desc);
	rw_desc->rd_magic = 0;

	C2_LEAVE();
}

static int rw_desc_add(struct rw_desc           *rw_desc,
		       char                     *addr,
		       size_t                    len,
		       c2_bindex_t               off,
		       enum c2_pdclust_unit_type type)
{
	struct c2t1fs_buf *buf;

	C2_ENTRY();

	C2_ALLOC_PTR(buf);
	if (buf == NULL) {
		C2_LEAVE("rc: %d", -ENOMEM);
		return -ENOMEM;
	}
	c2t1fs_buf_init(buf, addr, len, off, type);

	bufs_tlist_add_tail(&rw_desc->rd_buf_list, buf);

	C2_LEAVE("rc: 0");
	return 0;
}

static ssize_t c2t1fs_internal_read_write(struct c2t1fs_inode *ci,
					  char                *buf,
					  size_t               count,
					  loff_t               gob_pos,
					  int                  rw)
{
	enum   c2_pdclust_unit_type  unit_type;
	struct c2_pdclust_src_addr   src_addr;
	struct c2_pdclust_tgt_addr   tgt_addr;
	struct c2_pdclust_instance  *pi;
	struct c2_pdclust_layout    *pl;
	struct rw_desc              *rw_desc;
	struct c2t1fs_sb            *csb;
	struct c2_tl                 rw_desc_list;
	struct c2_fid                tgt_fid;
	struct c2_buf               *data_bufs;
	struct c2_buf               *parity_bufs;
	loff_t                       pos;
	size_t                       offset_in_buf;
	uint64_t                     unit_size;
	uint64_t                     nr_data_bytes_per_group;
	ssize_t                      rc = 0;
	char                        *ptr;
	uint32_t                     nr_groups_to_rw;
	uint32_t                     nr_units_per_group;
	uint32_t                     nr_data_units;
	uint32_t                     nr_parity_units;
	int                          parity_index;
	int                          unit;
	int                          i;

	C2_ENTRY();

	csb = C2T1FS_SB(ci->ci_inode.i_sb);

	pi = c2_layout_instance_to_pdi(ci->ci_layout_instance);
	pl = pi->pi_layout;

	unit_size = c2_pdclust_unit_size(pl);

	C2_LOG(C2_DEBUG, "Unit size: %lu", (unsigned long)unit_size);

	/* unit_size should be multiple of PAGE_CACHE_SIZE */
	C2_ASSERT((unit_size & (PAGE_CACHE_SIZE - 1)) == 0);

	nr_data_units           = c2_pdclust_N(pl);
	nr_parity_units         = c2_pdclust_K(pl);
	nr_units_per_group      = nr_data_units + 2 * nr_parity_units;
	nr_data_bytes_per_group = nr_data_units * unit_size;
	/* only full stripe read write */
	nr_groups_to_rw         = count / nr_data_bytes_per_group;

	C2_ALLOC_ARR(data_bufs, nr_data_units);
	C2_ALLOC_ARR(parity_bufs, nr_parity_units);

	rwd_tlist_init(&rw_desc_list);

	src_addr.sa_group = gob_pos / nr_data_bytes_per_group;
	offset_in_buf = 0;

	for (i = 0; i < nr_groups_to_rw; i++, src_addr.sa_group++) {

		for (unit = 0; unit < nr_units_per_group; unit++) {

			unit_type = c2_pdclust_unit_classify(pl, unit);
			if (unit_type == C2_PUT_SPARE) {
				/* No need to read/write spare units */
				C2_LOG(C2_DEBUG, "Skipped spare unit %d", unit);
				continue;
			}

			src_addr.sa_unit = unit;

			c2_pdclust_instance_map(pi, &src_addr, &tgt_addr);

			C2_LOG(C2_DEBUG, "src [%lu:%lu] maps to tgt [0:%lu]",
					(unsigned long)src_addr.sa_group,
					(unsigned long)src_addr.sa_unit,
					(unsigned long)tgt_addr.ta_obj);

			pos = tgt_addr.ta_frame * unit_size;

			tgt_fid = c2t1fs_cob_fid(ci, tgt_addr.ta_obj);

			rw_desc = rw_desc_get(&rw_desc_list, &tgt_fid);
			if (rw_desc == NULL) {
				rc = -ENOMEM;
				goto cleanup;
			}
			rw_desc->rd_count  += unit_size;
			rw_desc->rd_session = c2t1fs_container_id_to_session(
						     csb, tgt_fid.f_container);

			switch (unit_type) {
			case C2_PUT_DATA:
				/* add data buffer to rw_desc */
				rc = rw_desc_add(rw_desc, buf + offset_in_buf,
						     unit_size, pos,
						     C2_PUT_DATA);
				if (rc != 0)
					goto cleanup;

				c2_buf_init(&data_bufs[unit],
					    buf + offset_in_buf,
					    unit_size);

				offset_in_buf += unit_size;
				break;

			case C2_PUT_PARITY:
				/* Allocate buffer for parity and add it to
				   rw_desc */
				parity_index = unit - nr_data_units;
				ptr = c2_alloc(unit_size);
				/* rpc bulk api require page aligned addr */
				C2_ASSERT(
				  address_is_page_aligned((unsigned long)ptr));

				rc = rw_desc_add(rw_desc, ptr, unit_size,
							pos, C2_PUT_PARITY);
				if (rc != 0) {
					c2_free(ptr);
					goto cleanup;
				}

				c2_buf_init(&parity_bufs[parity_index], ptr,
						unit_size);
				/*
				 * If this is last parity buffer and operation
				 * is write, then compute all parity buffers
				 * for the entire current group.
				 */
				if (parity_index == nr_parity_units - 1 &&
						rw == WRITE) {
					C2_LOG(C2_DEBUG, "Compute parity of"
							 " grp %lu",
					     (unsigned long)src_addr.sa_group);
					c2_parity_math_calculate(&pi->pi_math,
								 data_bufs,
								 parity_bufs);
				}
				break;

			case C2_PUT_SPARE:
				/* we've decided to skip spare units. So we
				   shouldn't reach here */
				C2_ASSERT(0);
				break;

			default:
				C2_ASSERT(0);
			}
		}
	}

	rc = c2t1fs_rpc_rw(&rw_desc_list, rw);

cleanup:
	c2_tl_for(rwd, &rw_desc_list, rw_desc) {

		rwd_tlist_del(rw_desc);

		rw_desc_fini(rw_desc);
		c2_free(rw_desc);

	} c2_tl_endfor;
	rwd_tlist_fini(&rw_desc_list);

	C2_LEAVE("rc: %ld", rc);
	return rc;
}

static struct page *addr_to_page(void *addr)
{
	struct page   *pg = NULL;
	unsigned long  ul_addr;
	int            nr_pinned;

	enum {
		NR_PAGES      = 1,
		WRITABLE      = 1,
		FORCE         = 0,
	};

	C2_ENTRY();
	C2_LOG(C2_DEBUG, "addr: %p", addr);

	ul_addr = (unsigned long)addr;
	C2_ASSERT(address_is_page_aligned(ul_addr));

	if (current->mm != NULL &&
	    access_ok(VERIFY_READ, addr, PAGE_CACHE_SIZE)) {

		/* addr points in user space */
		down_read(&current->mm->mmap_sem);
		nr_pinned = get_user_pages(current, current->mm, ul_addr,
					   NR_PAGES, !WRITABLE, FORCE, &pg,
					   NULL);
		up_read(&current->mm->mmap_sem);

		if (nr_pinned <= 0) {
			C2_LOG(C2_WARN, "get_user_pages() failed: [%d]",
					nr_pinned);
			pg = NULL;
		} else {
			/*
			 * The page is already pinned by
			 * c2t1fs_pin_memory_area(). we're only interested in
			 * page*, so drop the ref
			 */
			put_page(pg);
		}

	} else {

		/* addr points in kernel space */
		pg = virt_to_page(addr);
	}

	C2_LEAVE("pg: %p", pg);
	return pg;
}

int rw_desc_to_io_fop(const struct rw_desc *rw_desc,
		      int                   rw,
		      struct c2_io_fop    **out)
{
	struct c2_net_domain   *ndom;
	struct c2_fop_type     *fopt;
	struct c2_fop_cob_rw   *rwfop;
	struct c2t1fs_buf      *cbuf;
	struct c2_io_fop       *iofop;
	struct c2_rpc_bulk     *rbulk;
	struct c2_rpc_bulk_buf *rbuf;
	struct page            *page;
	void                   *addr;
	uint64_t                buf_size;
	uint64_t                offset_in_stob;
	uint64_t                count;
	int                     nr_segments;
	int                     remaining_segments;
	int                     seg;
	int                     nr_pages_per_buf;
	int                     rc;
	int                     i;

#define SESSION_TO_NDOM(session) \
	(session)->s_conn->c_rpc_machine->rm_tm.ntm_dom

	int add_rpc_buffer(void)
	{
		int      max_nr_segments;
		uint64_t max_buf_size;

		C2_ENTRY();

		max_nr_segments = c2_net_domain_get_max_buffer_segments(ndom);
		max_buf_size    = c2_net_domain_get_max_buffer_size(ndom);

		/* Assuming each segment is of size PAGE_CACHE_SIZE */
		nr_segments = min_type(int, max_nr_segments,
					max_buf_size / PAGE_CACHE_SIZE);
		nr_segments = min_type(int, nr_segments, remaining_segments);

		C2_LOG(C2_DEBUG, "max_nr_seg [%d] remaining [%d]",
					max_nr_segments, remaining_segments);

		rbuf = NULL;
		rc = c2_rpc_bulk_buf_add(rbulk, nr_segments, ndom, NULL, &rbuf);
		if (rc == 0) {
			C2_ASSERT(rbuf != NULL);
			rbuf->bb_nbuf->nb_qtype = (rw == READ)
						? C2_NET_QT_PASSIVE_BULK_RECV
					        : C2_NET_QT_PASSIVE_BULK_SEND;
		}

		C2_LEAVE("rc: %d", rc);
		return rc;
	}

	C2_ENTRY();

	C2_ASSERT(rw_desc != NULL && out != NULL);
	*out = NULL;

	C2_ALLOC_PTR(iofop);
	if (iofop == NULL) {
		C2_LOG(C2_ERROR, "iofop allocation failed");
		rc = -ENOMEM;
		goto out;
	}

	fopt = (rw == READ) ? &c2_fop_cob_readv_fopt : &c2_fop_cob_writev_fopt;

	rc = c2_io_fop_init(iofop, fopt);
	if (rc != 0) {
		C2_LOG(C2_ERROR, "io_fop_init() failed rc [%d]", rc);
		goto iofop_free;
	}

	rwfop = io_rw_get(&iofop->if_fop);
	C2_ASSERT(rwfop != NULL);

	rwfop->crw_fid.f_seq = rw_desc->rd_fid.f_container;
	rwfop->crw_fid.f_oid = rw_desc->rd_fid.f_key;

	cbuf = bufs_tlist_head(&rw_desc->rd_buf_list);

	/*
	 * ASSUMING all c2t1fs_buf objects in rw_desc->rd_buf_list have same
	 * number of bytes i.e. c2t1fs_buf::cb_buf.b_nob. This holds true for
	 * now because, only full-stripe width io is supported. So each
	 * c2t1fs_buf is representing in-memory location of one stripe unit.
	 */

	buf_size = cbuf->cb_buf.b_nob;

	/*
	 * Make sure, buf_size is multiple of page size. Because cbuf is
	 * stripe unit and this implementation assumes stripe unit is
	 * multiple of PAGE_CACHE_SIZE.
	 */

	C2_ASSERT((buf_size & (PAGE_CACHE_SIZE - 1)) == 0);
	nr_pages_per_buf = buf_size >> PAGE_CACHE_SHIFT;

	remaining_segments = bufs_tlist_length(&rw_desc->rd_buf_list) *
				nr_pages_per_buf;
	C2_ASSERT(remaining_segments > 0);

	C2_LOG(C2_DEBUG, "bufsize [%lu] pg/buf [%d] nr_bufs [%d]"
			 " rem_segments [%d]",
			(unsigned long)buf_size, nr_pages_per_buf,
			(int)bufs_tlist_length(&rw_desc->rd_buf_list),
			remaining_segments);

	count = 0;
	seg   = 0;

	ndom            = SESSION_TO_NDOM(rw_desc->rd_session);
	rbulk           = &iofop->if_rbulk;

	rc = add_rpc_buffer();
	if (rc != 0)
		goto buflist_empty;

	c2_tl_for(bufs, &rw_desc->rd_buf_list, cbuf) {
		addr            = cbuf->cb_buf.b_addr;
		offset_in_stob  = cbuf->cb_off;

		/* See comments earlier in this function, to understand
		   following assertion. */
		C2_ASSERT(nr_pages_per_buf * PAGE_CACHE_SIZE ==
				cbuf->cb_buf.b_nob);

		for (i = 0; i < nr_pages_per_buf; i++) {

			page = addr_to_page(addr);
			C2_ASSERT(page != NULL);

retry:
			rc = c2_rpc_bulk_buf_databuf_add(rbuf,
						page_address(page),
						PAGE_CACHE_SIZE,
						offset_in_stob, ndom);

			if (rc == -EMSGSIZE) {
				/* add_rpc_buffer() is nested function.
				   add_rpc_buffer() modifies rbuf */
				rc = add_rpc_buffer();
				if (rc != 0)
					goto buflist_empty;
				C2_LOG(C2_DEBUG, "rpc buffer added");
				goto retry;
			}

			if (rc != 0)
				goto buflist_empty;

			offset_in_stob += PAGE_CACHE_SIZE;
			count          += PAGE_CACHE_SIZE;
			addr           += PAGE_CACHE_SIZE;

			seg++;
			remaining_segments--;

			C2_LOG(C2_DEBUG, "Added: pg [0x%p] addr [0x%p] off [%lu]"
				 " count [%lu] seg [%d] remaining [%d]",
				 page, addr,
				(unsigned long)offset_in_stob,
				(unsigned long)count, seg, remaining_segments);

		}
	} c2_tl_endfor;

	C2_ASSERT(count == rw_desc->rd_count);

        rc = c2_io_fop_prepare(&iofop->if_fop);
	if (rc != 0) {
		C2_LOG(C2_ERROR, "io_fop_prepare() failed: rc [%d]", rc);
		goto buflist_empty;
	}

	*out = iofop;

	C2_LEAVE("rc: %d", rc);
	return rc;

buflist_empty:
	c2_rpc_bulk_buflist_empty(rbulk);

	c2_io_fop_fini(iofop);

iofop_free:
	c2_free(iofop);

out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int io_fop_do_sync_io(struct c2_io_fop     *iofop,
			     struct c2_rpc_session *session)
{
	struct c2_fop_cob_rw_reply *rw_reply;
	struct c2_fop_cob_rw       *rwfop;
	int                         rc;

	C2_ENTRY();

	C2_ASSERT(iofop != NULL && session != NULL);

	rwfop = io_rw_get(&iofop->if_fop);
	C2_ASSERT(rwfop != NULL);

	rc = c2_rpc_bulk_store(&iofop->if_rbulk, session->s_conn,
					rwfop->crw_desc.id_descs);
	if (rc != 0)
		goto out;

	/*
	 * XXX For simplicity, doing IO to multiple cobs, one after other,
	 * serially. This should be modified, so that io requests on
	 * cobs are processed parallely.
	 */

	rc = c2_rpc_client_call(&iofop->if_fop, session,
					iofop->if_fop.f_item.ri_ops,
					C2T1FS_RPC_TIMEOUT);
	if (rc != 0)
		goto out;

	rw_reply = io_rw_rep_get(
			c2_rpc_item_to_fop(iofop->if_fop.f_item.ri_reply));
	rc = rw_reply->rwr_rc;

out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static ssize_t c2t1fs_rpc_rw(const struct c2_tl *rw_desc_list, int rw)
{
	struct rw_desc        *rw_desc;
	struct c2t1fs_buf     *buf;
	struct c2_io_fop      *iofop;
	ssize_t                count = 0;
	int                    rc;

	C2_ENTRY();

	C2_LOG(C2_DEBUG, "Operation: %s", (char *)(rw == READ ? "READ" : "WRITE"));

	if (rwd_tlist_is_empty(rw_desc_list))
		C2_LOG(C2_DEBUG, "rw_desc_list is empty");

	c2_tl_for(rwd, rw_desc_list, rw_desc) {

		C2_LOG(C2_DEBUG, "fid: [%lu:%lu] count: %lu",
				(unsigned long)rw_desc->rd_fid.f_container,
				(unsigned long)rw_desc->rd_fid.f_key,
				(unsigned long)rw_desc->rd_count);

		C2_LOG(C2_DEBUG, "Buf list");

		c2_tl_for(bufs, &rw_desc->rd_buf_list, buf) {

			C2_LOG(C2_DEBUG, "addr %p len %lu type %s",
				buf->cb_buf.b_addr,
				(unsigned long)buf->cb_buf.b_nob,
				(char *)(
				(buf->cb_type == C2_PUT_DATA) ? "DATA" :
				 (buf->cb_type == C2_PUT_PARITY) ? "PARITY" :
				 (buf->cb_type == C2_PUT_SPARE) ? "SPARE" :
					"UNKNOWN"));

			if (buf->cb_type == C2_PUT_DATA)
				count += buf->cb_buf.b_nob;

		} c2_tl_endfor;

		rc = rw_desc_to_io_fop(rw_desc, rw, &iofop);
		if (rc != 0) {
			/* For now, if one io fails, fail entire IO. */
			C2_LOG(C2_ERROR, "rw_desc_to_io_fop() failed: rc [%d]",
					rc);
			C2_LEAVE("%d", rc);
			return rc;
		}

		rc = io_fop_do_sync_io(iofop, rw_desc->rd_session);
		if (rc != 0) {
			/* For now, if one io fails, fail entire IO. */
			C2_LOG(C2_ERROR, "io_fop_do_sync_io() failed: rc [%d]",
					rc);
			C2_LEAVE("%d", rc);
			return rc;
		}
	} c2_tl_endfor;

	C2_LEAVE("count: %ld", count);
	return count;
}
