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

const struct inode_operations c2t1fs_reg_inode_operations;
void iov_iter_advance(struct iov_iter *i, size_t bytes);

static struct c2_pdclust_layout *layout_to_pd_layout(struct c2_layout *l)
{
	return container_of(l, struct c2_pdclust_layout, pl_layout);
}

/* Imports */
struct c2_net_domain;
extern bool c2t1fs_inode_bob_check(struct c2t1fs_inode *bob);

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

   - @b R.c2t1fs.fullvectIO The implementation shall support fully vectored
   scatter-gather IO where multiple IO segments could be directed towards
   multiple file offsets. This functionality will be used by cluster library
   writers through an ioctl.

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

   - Read IO requests are issued _only_ for units with at least one single byte
   of valid file data along with parity units. Ergo, no read IO is done beyond
   end-of-file.

   - In case of partial parity group write IO request, a read request is issued
   to read necessary data blocks and parity blocks.

   - Read requests will be issued to read the data pages which are partially
   spanned by the incoming IO requests. The data pages which are completely
   spanned by IO request will be populated by copying data from user buffers.

   - Once read is complete, changes are made to partially spanned data buffers
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

	// Index vector describing file extents and their length.
        // All segments are in increasing order of file offsets.
	struct c2_indexvec           ir_ivec;

        // Array of struct pargrp_iomap.
        // Each pargrp_iomap structure describes the part of parity group
        // spanned by segments from ::ir_ivec.
        struct pargrp_iomap        **ir_iomaps;

        // Number of pargrp_iomap structures.
        // Maximum number is equal to number of parity groups spanned by
        // io request.
        uint64_t                     ir_iomap_nr;

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
   requests.

   @code
   struct nw_xfer_request {
	uint64_t                  nxr_magic;

        // Resultant status code for all IO fops issed by this structure.
	int                       nxr_rc;

        // Resultant number of bytes read/written by this structure.
        uint64_t                  nxr_bytes;

	enum nw_xfer_state        nxr_state;

	const struct nw_xfer_ops *nxr_ops;

	// List of target_ioreq structures.
	struct c2_tl              nxr_tioreqs;

        // Number of IO fops issued by all target_ioreq objects belonging
        // to this nw_xfer_request object.
        // This number is updated when bottom halves from ASTs are run.
        // When it reaches zero, state of io_request::ir_sm changes.
        uint64_t                  nxr_iofops_nr;
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
	int  (*nxo_prepare)       (struct nw_xfer_request  *xfer);
	int  (*nxo_dispatch)      (struct nw_xfer_request  *xfer);
	int  (*nxo_complete)      (struct nw_xfer_request  *xfer);
        void (*nxo_tioreq_locate) (struct nw_xfer_request  *xfer,
                                   struct c2_fid           *fid,
                                   struct target_ioreq    **tioreq);
   };
   @endcode

   pargrp_iomap - Represents a map of io extents in given parity group.
   Struct io_request contains as many pargrp_iomap structures as the number
   of parity groups spanned by original index vector.
   Typically, the segments from pargrp_iomap::pi_ivec are round_{up/down}
   to nearest page boundary for segments from io_request::ir_ivec.

   @code
   struct pargrp_iomap {
           uint64_t                       pi_magic;

           // Parity group id.
           uint64_t                       pi_grpid;

           // Part of io_request::ir_ivec which falls in ::pi_grpid
           // parity group.
           // All segments are in increasing order of file offset.
           struct c2_indexvec             pi_ivec;

           // Type of read approach used in case of rmw IO.
           // Either read-old or read-rest.
           enum pargrp_iomap_rmwtype      pi_read;

           // Operation vector.
           const struct pargrp_iomap_ops *pi_ops;

           struct io_request             *pi_ioreq;
   };
   @endcode

   Type of read approach used by pargrp_iomap in case of rmw IO.

   @code
   enum pargrp_iomap_rmwtype {
           PIR_NONE,
           PIR_READOLD,
           PIR_READREST,
           PIR_NR,
   };
   @endcode

   Operation vector for struct pargrp_iomap.

   @code

   struct pargrp_iomap_ops {
           // Populates pargrp_iomap::pi_ivec by deciding whether to follow
           // read-old approach or read-rest approach.
           // pargrp_iomap::pi_read will be set to PIR_READOLD or PIR_READREST
           // accordingly.
           int (*pi_populate)   (struct pargrp_iomap      *iomap,
                                 const struct c2_indexvec *ivec,
                                 uint64_t                  cursor);

           // Returns true if given segment is spanned by existing segments
           // in pargrp_iomap::pi_ivec.
           bool (*pi_spans_seg) (struct pargrp_iomap *iomap,
                                 c2_bindex_t index, c2_bcount_t count);
   };
   @endcode

   data_buf - Represents a simple data buffer wrapper object. The embedded
   c2_buf::b_addr points to a kernel page or a user-space buffer in case of
   direct IO.

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

   target_ioreq - Collection of IO extents and buffers, directed towards each
   of the component objects in a parity group.
   These structures are created by struct io_request dividing the incoming
   struct iovec into members of parity group.

   @code
   struct target_ioreq {
	uint64_t                tir_magic;

	// Fid of component object.
	struct c2_fid           tir_fid;

        // Status code for io operation done for this target_ioreq.
        int                     tir_rc;

        // Number of bytes read/written for this target_ioreq.
        uint64_t                tir_bytes;

        // List of struct io_req_fop issued on this target object.
        struct c2_tl            tir_iofops;

	// Resulting IO fops are sent on this session.
	struct c2_rpc_session  *tir_session;

	// Linkage to link in to nw_xfer_request::nxr_tioreqs list.
	struct c2_tlink         tir_link;

        // Index vector describing starting cob offset and its length.
        struct c2_indexvec      tir_ivec;

        // Number of data_buf structures representing the extent.
        uint64_t                tir_buf_nr;

        // Array of data_buf structures.
        // Each element typically refers to one kernel page.
        // Intentionally kept as array since it makes iterating much easier
        // as compared to a list, especially while calculating parity.
        struct data_buf       **tir_buffers;

        // Back link to parent structure nw_xfer_request.
        struct nw_xfer_request *tir_nwxfer;
   };
   @endcode

   ast thread - A special thread will be maintained per super block instance
   c2t1fs_sb to wake up on receiving ASTs and execute the bottom halves.
   This thread will run as long as c2t1fs is mounted.
   This thread will handle the pending IO requests gracefully when unmount
   is triggered.

   The in-memory super block struct c2t1fs_sb will maintain

    - a boolean indicating if super block is active (mounted). This flag
      will be reset by the unmount thread.
      In addition to this, every io request will check this flag while
      initializing. If this flag is reset, it will return immediately.
      This helps in blocking new io requests from coming in when unmount
      is triggered.

    - atomic count of pending io requests. This will help while handling
      pending io requests when unmount is triggered.

    - the special "ast" thread to execute ASTs from io requests and to wake up
      the unmount thread after completion of pending io requests.

    - a channel for unmount thread to wait on until all pending io requests
      complete.

   The thread will run a loop like this...

   @code

   while (1) {
           c2_chan_wait(&sm_group->s_clink);
           c2_sm_group_lock(sm_group);
           c2_sm_asts_run(sm_group);
           c2_sm_group_unlock(sm_group);
           if (!sb->csb_active && c2_atomic64_get(&sb->csb_pending_io) == 0) {
                   c2_chan_signal(&sb->csb_iowait);
                   break;
           }
   }

   @endcode

   So, while c2t1fs is mounted, the thread will keep running in loop waiting
   for ASTs.

   When unmount is triggered, new io requests will be blocked as
   c2t1fs_sb::csb_active flag is reset.
   For pending io requests, the special thread will wait until callbacks
   from all outstanding io requests are acknowledged and executed.

   Once all pending io requests are dealt with, the special thread will exit
   waking up the unmount thread.

   Abort is not supported at the moment in c2t1fs io code as it needs same
   functionality from layer beneath.

   io_req_fop - Represents a wrapper over generic IO fop and its callback
   to keep track of such IO fops issued by same nw_xfer_request.

   When bottom halves for c2_sm_ast structures are run, it updates
   target_ioreq::tir_rc and target_ioreq::tir_bytes with data from
   IO reply fop.
   Then it decrements nw_xfer_request::nxr_iofops_nr, number of IO fops.
   When this count reaches zero, io_request::ir_sm changes its state.

   A callback function will be used to associate with c2_rpc_item::ri_chan
   channel. This callback will be triggered once rpc layer receives the
   reply rpc item. This callback will post the io_req_fop::irf_ast and
   will thus wake up the thread which executes the ASTs.

   @code
   struct io_req_fop {
        uint64_t                irf_magic;

        struct c2_io_fop        irf_iofop;

        // Callback per IO fop.
        struct c2_sm_ast        irf_ast;

        // Clink registered with c2_rpc_item::ri_chan channel.
        struct c2_clink         irf_clink;

        // Linkage to link in to target_ioreq::tir_iofops list.
        struct c2_tlink         irf_tlink;

        // Backlink to target_ioreq object where rc and number of bytes
        // are updated.
        struct target_ioreq    *irf_tioreq;
   };
   @endcode

   Callback used to tie to io_req_fop::irf_clink which is listening to
   events on c2_rpc_item::ri_chan channel.

   @code

   bool io_rpc_item_cb(struct c2_clink *link)
   {
           struct io_req_fop  *irfop;
           struct io_request  *ioreq;

           irfop = container_of(link, struct io_req_fop, irf_clink);
           ioreq = container_of(irfop->irf_tioreq->tir_nwxfer,
                                struct io_request, ir_nwxfer);

           c2_sm_ast_post(ioreq->ir_sm.sm_grp, &irfop->irf_ast);
   }

   @endcode

   Bottom-half function for IO request.

   @code

   void io_bottom_half(struct c2_sm_group *grp, struct c2_sm_ast *ast)
   {
           struct io_req_fop   *irfop;
           struct target_ioreq *tioreq;

           irfop = container_of(ast, struct io_req_fop, irf_ast);
           tioreq = irfop->irf_tioreq;

           // If rc is non-zero already, don't update it.
           // This way, first error code is not overwritten by subsequent
           // non-zero error codes.
           if (tioreq->tir_rc == 0)
                   tioreq->tir_rc = irfop->irf_iofop.if_rbulk.rb_rc;

           tioreq->tir_bytes += irfop->irf_iofop.if_rbulk.rb_bytes;
           tioreq->tir_nwxfer->nxr_iofops_nr--;

           // Thread executing system call will be waiting on a channel
           // embedded nw_xfer_request::nxr_iowait.
           if (tioreq->tir_nwxfer->nxr_iofops_nr == 0)
                   c2_chan_signal((struct c2_chan *)(ast->sa_datum));
   }

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
        DTBUF_MAGIC  = 0x191e2c09119e91a1ULL, // intercontinental
        PGROUP_MAGIC = 0x19ca9de5ce91b21bULL, // incandescentbulb
   };
   @endcode

   @subsubsection rmw-lspec-sub1 Subcomponent Subroutines

   In order to satisafy the requirement of an ioctl API for fully vectored
   scatter-gather IO (where multiple IO segments are directed at multiple
   file offsets), a new API will be introduced that will use a c2_indexvec
   structure to specify multiple file offsets.

   @code
   ssize_t c2t1fs_aio(struct kiocb *iocb, const struct iovec *iov,
                      const struct c2_indexvec *ivec, enum io_req_type type);
   @endcode

   In case of normal file->f_op->aio_{read/write} calls, the c2_indexvec
   will be created and populated by c2t1fs code, while in case of ioctl, it
   has to be provided by user-space.

   Invariant for structure io_request.

   @code
   static bool io_request_invariant(const struct io_request *req)
   {
        uint64_t i;

	C2_PRE(req != NULL);

	return
		req->ir_magic == IOREQ_MAGIC &&
		req->ir_type  <= IRT_NR &&
		req->ir_state <= IRS_STATE_NR &&
                req->ir_iovec != NULL &&
                req->ir_iomap_nr > 0 &&
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

                c2_vec_count(&req->ir_ivec.iv_vec) > 0 &&

                c2_forall(i, req->ir_ivec.iv_vec.v_nr - 1,
                          req->ir_ivec.iv_index[i] <=
                          req->ir_ivec.iv_index[i+1]) &&

                pargrp_iomap_invariant_nr(req) &&

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

   A helper function to invoke invariant() for all data_buf structures
   in array target_ioreq::tir_buffers.
   It is on similar lines of c2_tl_forall() API.

   @code
   static bool data_buf_invariant_nr(const struct target_ioreq *tir)
   {
           uint64_t i;

           for (i = 0; i < tir->tir_buffers; ++i)
                   if (!data_buf_invariant(tir->tir_buffers))
                           break;
           return i == tir->tir_buffers;
   }
   @endcode

   Invariant for structure target_ioreq.

   @code
   static bool target_ioreq_invariant(const struct target_ioreq *tir)
   {
           uint64_t i;

           return
                  tir != NULL &&
                  tir->tir_magic   == TIOREQ_MAGIC &&
                  tir->tir_session != NULL &&
                  tir->tir_nwxfer  != NULL &&
                  c2_fid_is_valid(tir->tir_fid) &&
                  c2_tlink_is_in(tir->tir_link) &&
                  tir->tir_buf_nr > 0 &&
                  tir->tir_buffers != NULL &&
                  c2_tl_forall(iofops, iofop, &tir->tir_iofops,
                               io_req_fop_invariant(iofop));
   }
   @endcode

   Invariant for structure pargrp_iomap.

   @code
   static bool pargrp_iomap_invariant(const struct pargrp_iomap *map)
   {
           return
                  map != NULL &&
                  map->pi_magic == PGROUP_MAGIC &&
                  map->pi_ops   != NULL &&
                  map->pi_read  <  PIR_NR &&
                  map->pi_ioreq != NULL &&
                  c2_vec_count(&map->pi_ivec.iv_vec) > 0;
   }
   @endcode

   A helper function to invoke pargrp_iomap_invariant() for all such
   structures in io_request::ir_iomaps.

   @code
   static bool pargrp_iomap_invariant_nr(struct io_request *req)
   {
           return c2_forall(i, req->ir_iomap_nr,
                            pargrp_iomap_invariant(req->ir_iomaps[i]));
   }
   @endcode

   Invariant for structure data_buf.

   @code
   static bool data_buf_invariant(const struct data_buf *db)
   {
           return
                  db != NULL &&
                  db->db_magic == DTBUF_MAGIC &&
                  db->db_type  <  PUT_NR &&
                  db->db_buf.b_addr != NULL &&
                  db->db_buf.b_nob  > 0;
   }
   @endcode

   Invariant for structure io_req_fop.

   @code
   static bool io_req_fop_invariant(const struct io_req_fop *irf)
   {
           return
                  irf != NULL &&
                  irf->irf_magic  == IOFOP_MAGIC &&
                  irf->irf_tioreq != NULL &&
                  c2_clink_is_armed(&irf->clink) &&
                  c2_tlink_is_in(irf->irf_tlink);
   }
   @endcode

   The APIs that work as members of operation vector struct io_request_ops
   for struct io_request are as follows.

   Initializes a newly allocated io_request structure.

   @param req    IO request to be issued.
   @param fid    File identifier.
   @param iov    Array of user-space buffers.
   @param pos    Starting file offset.
   @param rw     Flag indicating Read or write request.
   @pre   req != NULL
   @post  io_request_invariant(req) &&
   @n     c2_fid_eq(&req->ir_fid, fid) && req->ir_iovec == iov &&
   @n     && req->ir_type == rw && req->ir_state == IRS_INITIALIZED &&

   @code
   int io_request_init(struct io_request  *req,
                       struct c2_fid      *fid,
                       struct iovec       *iov,
                       struct c2_indexvec *ivec,
                       enum io_req_type    rw);
   @endcode

   Finalizes the io_request structure.

   @param req IO request to be processed.
   @pre   io_request_invariant(req) &&
   @n     req->ir_state == IRS_REQ_COMPLETE &&
   @post  c2_tlist_is_empty(req->ir_nwxfer.nxr_tioreqs)

   @code
   void io_request_fini(struct io_request *req);
   @endcode

   Starts read request for given extent of file. Data read from server
   is kept into io_request::ir_nwxfer::target_ioreq::tir_buffers.
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
   the bottom half functions will be executed on the "ast" thread.
   The ast thread and the system call thread coordinate by using
   c2_sm_group::s_lock mutex.
   @see sm. Concurrency section of State machine.

   @todo In future, with introduction of Resource Manager, distributed extent
   locks have to be acquired or released as needed.

   @subsection rmw-lspec-numa NUMA optimizations

   None.

   <hr>
   @section rmw-conformance Conformance

   - @b I.c2t1fs.rmw_io.rmw The implementation maintains an io map per parity
   group in a data structure pargrp_iomap. The io map rounds up/down the
   incoming io segments to nearest page boundaries.
   The missing data will be read first from data server (later from client cache
   which is missing at the moment and then from data server).
   Data is copied from user-space at desired file offsets and then it will be
   sent to server as a write IO request.

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

   @test Test read-rest test case. If io request spans a parity group partially,
   and reading the rest of parity group units is economical (in terms of io
   requests) than reading the spanned extent, the feature will read rest of
   parity group and calculate new parity.
   For instance, in an 8+1+1 layout, first 5 units are overwritten. In this
   case, the code should read rest of the 3 units and calculate new parity and
   write 9 pages in total.

   @test Test read-old test case. If io request spans a parity group partially
   and reading old units and calculating parity _iteratively_ is more economical
   than reading whole parity group, the feature will read old extent and
   calculate parity iteratively.
   For instance, in an 8+1+1 layout, first 2 units are overwritten. In this
   case, the code should read old data from these 2 units and old parity.
   Then parity is calculated iteratively and 3 units (2 data + 1 parity) are
   written.

   <hr>
   @section rmw-st System Tests

   A bash script will be written to send partial parity group IO requests in
   loop and check the results. This should do some sort of stress testing
   for the code.

   <hr>
   @section rmw-O Analysis

   Only one io_request structure is created for every system call.
   Each IO request creates sub requests proportional to number of blocks
   addressed by io vector.
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
     - implementation of primary data structures and interfaces needed to
       support read-modify-write along with core logic to map global file
       io requests into target io requests. Referred to as rmw-core henceforth.

     - implementation of io fops, c2_sm and all state entry and exit functions.
       Referred to as nwio-sm henceforth.

   - New interfaces are consumed by same module (c2t1fs). The primary
   data structures and interfaces have been identified and defined.

   - The subtasks rwm-support and nwio-sm will commence parallely.

   - The subtask rmw-core can be tested independently of other subtask. Once,
   all it is found to be working properly, it can be integrated with nwio-sm
   subtask.

 */

struct io_request;
struct nw_xfer_request;
struct target_ioreq;
struct io_req_fop;
enum   io_req_type;

static int io_request_init(struct io_request *req, struct file *file,
                           struct iovec      *iov, struct c2_indexvec *ivec,
                           enum io_req_type   rw);

static bool io_request_invariant(const struct io_request *req);

/**
 * Page attributes for all pages spanned by pargrp_iomap::pi_ivec.
 * This enum is also used by data_buf::db_flags.
 */
enum page_attr {
        /** Page not spanned by io vector. */
        PA_NONE            = 0,

        /** Page needs to be read. */
        PA_READ            = (1 << 0),

        /**
         * Page is completely spanned by incoming io vector, which is why
         * file data can be modified while read IO is going on.
         * Such pages need not be read from server.
         * Used only in case of read-modify-write.
         * Mutually exclusive with PA_READ and PA_PARTPAGE_MODIFY.
         */
        PA_FULLPAGE_MODIFY = (1 << 1),

        /**
         * Page is partially spanned by incoming io vector, which is why
         * it has to wait till the whole page (superset) is read first and
         * then it can be modified as per user request.
         * Used only in case of read-modify-write.
         * Mutually exclusive with PA_FULLPAGE_MODIFY.
         */
        PA_PARTPAGE_MODIFY = (1 << 2),

        /** Page needs to be written. */
        PA_WRITE           = (1 << 3),

        PA_NR,
};

/** Enum representing direction of data copy in IO. */
enum copy_direction {
        CD_COPY_FROM_USER,
        CD_COPY_TO_USER,
        CD_NR,
};

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
        int  (*nxo_prepare)       (struct nw_xfer_request  *xfer);
        int  (*nxo_dispatch)      (struct nw_xfer_request  *xfer);
        int  (*nxo_complete)      (struct nw_xfer_request  *xfer);
        void (*nxo_tioreq_locate) (struct nw_xfer_request  *xfer,
                                   struct c2_fid           *fid,
                                   struct target_ioreq    **tioreq);
};

/**
 * Structure representing the network transfer part for an IO request.
 * This structure keeps track of request IO fops as well as individual
 * completion callbacks and status of whole request.
 * Typically, all IO requests are broken down into multiple fixed-size
 * requests.
 */
struct nw_xfer_request {
        /** Holds NWREQ_MAGIC */
        uint64_t                  nxr_magic;

        /** Resultant status code for all IO fops issued by this structure. */
        int                       nxr_rc;

        /** Resultant number of bytes read/written by all IO fops. */
        uint64_t                  nxr_bytes;

        enum nw_xfer_state        nxr_state;

        const struct nw_xfer_ops *nxr_ops;

        /** List of all target_ioreq structures. */
        struct c2_tl              nxr_tioreqs;

        /**
         * Number of IO fops issued by all target_ioreq structures
         * belonging to this nw_xfer_request object.
         * This number is updated when bottom halves from ASTs are run.
         * When it reaches zero, state of io_request::ir_sm changes.
         */
        uint64_t                  nxr_iofop_nr;
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

struct pargrp_iomap;

/** Operation vector for struct io_request. */
struct io_request_ops {
        int (*iro_readextent)     (struct io_request  *req);
        int (*iro_preparewrite)   (struct io_request  *req);
        int (*iro_commit_write)   (struct io_request  *req);
        int (*iro_submit)         (struct io_request  *req);
        int (*iro_read_complete)  (struct io_request  *req);
        int (*iro_write_complete) (struct io_request  *req);
        int (*iro_iomaps_prepare) (struct io_request  *req);
        int (*iro_user_data_copy) (struct io_request  *req,
                                   enum copy_direction dir,
                                   enum page_attr      filter);
        int (*iro_copy_to_user)   (struct io_request  *req);
        void (*iro_iomap_locate)  (struct io_request    *req,
                                   uint64_t              grpid,
                                   struct pargrp_iomap **map);
        int (*iro_parity_recalc)  (struct io_request  *req);
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
        uint64_t                     ir_magic;

        int                          ir_rc;

        /**
         * struct file* can point to c2t1fs inode and hence its
         * associated c2_fid structure.
         */
        struct file                 *ir_file;

        /** Index vector describing file extents and their lengths. */
        struct c2_indexvec           ir_ivec;

        /**
         * Array of struct pargrp_iomap.
         * Each pargrp_iomap structure describes the part of parity group
         * spanned by segments from ::ir_ivec.
         */
        struct pargrp_iomap        **ir_iomaps;

        /** Number of pargrp_iomap structures. */
        uint64_t                     ir_iomap_nr;

        /**
         * iovec structure containing user-space buffers.
         * It is used as it as since using a new structure would require
         * conversion.
         */
        struct iovec                *ir_iovec;

        /** Async state machine to handle state transitions and callbacks. */
        struct c2_sm                 ir_sm;

        enum io_req_type             ir_type;

        enum io_req_state            ir_state;

        const struct io_request_ops *ir_ops;

        struct nw_xfer_request       ir_nwxfer;
};

/**
 * Type of read approach used by pargrp_iomap structure
 * in case of rmw IO.
 */
enum pargrp_iomap_rmwtype {
        PIR_NONE,
        PIR_READOLD,
        PIR_READREST,
        PIR_NR,
};

/**
 * Represents a map of io extents in given parity group. Struct io_request
 * contains as many pargrp_iomap structures as the number of parity groups
 * spanned by io_request::ir_ivec.
 * Typically, the segments from pargrp_iomap::pi_ivec are round_{up/down}
 * to nearest page boundary for respective segments from
 * io_requets::ir_ivec.
 */
struct pargrp_iomap {
        /** Holds PGROUP_MAGIC. */
        uint64_t                        pi_magic;

        /** Parity group id. */
        uint64_t                        pi_grpid;

        /**
         * Part of io_request::ir_ivec which falls in ::pi_grpid
         * parity group.
         * All segments are in increasing order of file offset.
         */
        struct c2_indexvec              pi_ivec;

        /**
         * Type of read approach used only in case of rmw IO.
         * Either read-old or read-rest.
         */
        enum pargrp_iomap_rmwtype       pi_read;

        /** Array of data units in a parity group. */
        struct data_buf               **pi_databufs;

        /** Array of parity units in a parity group. */
        struct data_buf               **pi_paritybufs;

        /** Operations vector. */
        const struct pargrp_iomap_ops  *pi_ops;

        /** Backlink to io_request. */
        struct io_request              *pi_ioreq;
};

/** Operations vector for struct pargrp_iomap. */
struct pargrp_iomap_ops {
        /**
         * Populates pargrp_iomap::pi_ivec by deciding whether to follow
         * read-old approach or read-rest approach.
         * pargrp_iomap::pi_read will be set to PIR_READOLD or
         * PIR_READREST accordingly.
         */
        int (*pi_populate)  (struct pargrp_iomap      *iomap,
                             const struct c2_indexvec *ivec,
                             struct c2_ivec_cursor    *cursor);

        /**
         * Returns true if given segment is spanned by existing segments
         * in pargrp_iomap::pi_ivec.
         */
        bool (*pi_spans_seg) (struct pargrp_iomap *iomap,
                              c2_bindex_t index, c2_bcount_t count);

        /**
         * Changes pargrp_iomap::pi_ivec to suit read-rest approach
         * for an RMW IO request.
         */
        int (*pi_readrest)   (struct pargrp_iomap *iomap);

        /**
         * Finds out the pages _completely_ spanned by incoming io vector.
         * Used only in case of read-modify-write IO.
         * This is needed in order to decide the type of read approach
         * {read_old, read_rest} for given parity group.
         */
        uint64_t (*pi_fullpages_find) (struct pargrp_iomap *map);

        /**
         * Populates attributes for pages in given parity group.
         * @see page_attr.
         */
        int (*pi_pageattr_fill)  (struct pargrp_iomap       *map,
                                  const struct c2_indexvec  *ivec,
                                  bool                       rmw);

        /**
         * Process each segment in pargrp_iomap::pi_ivec and
         * allocate data_buf structures correspondingly.
         */
        int (*pi_seg_process)    (struct pargrp_iomap *map,
                                  uint64_t             segid,
                                  c2_bindex_t          pgstart);

        int (*pi_parity_recalc)  (struct pargrp_iomap *map);
};

/**
 * Represents a simple data buffer wrapper object. The embedded
 * c2_buf::b_addr points to a kernel page.
 */
struct data_buf {
        /** Holds DTBUF_MAGIC. */
        uint64_t       db_magic;

        /** Inline buffer pointing to a kernel page. */
        struct c2_buf  db_buf;

        /**
         * Auxiliary buffer used in case of read-modify-write IO.
         * Used when page pointed to by ::db_buf::b_addr is partially spanned
         * by incoming rmw request.
         */
        struct c2_buf  db_auxbuf;
        /**
         * Miscellaneous flags.
         * Can be used later for caching options.
         */
        enum page_attr db_flags;
};

/**
 * Collection of IO extents and buffers, directed towards each
 * of target objects (data_unit / parity_unit) in a parity group.
 * These structures are created by struct io_request dividing the incoming
 * struct iovec into members of parity group.
 */
struct target_ioreq {
        /** Holds TIOREQ_MAGIC */
        uint64_t                 ti_magic;

        /** Fid of component object. */
        struct c2_fid            ti_fid;

        /** Status code for io operation done for this target_ioreq. */
        int                      ti_rc;

        /** Number of bytes read/written for this target_ioreq. */
        uint64_t                 ti_bytes;

        /** List of io_req_fop structures issued on this target object. */
        struct c2_tl             ti_iofops;

        /** Resulting IO fops are sent on this rpc session. */
        struct c2_rpc_session   *ti_session;

        /** Linkage to link in to nw_xfer_request::nxr_tioreqs list. */
        struct c2_tlink          ti_link;

        /**
         * Index vector containing IO segments with cob offsets and
         * their length.
         */
        struct c2_indexvec       ti_ivec;

        /** Buffer vector corresponding to index vector above. */
        struct c2_bufvec         ti_bufvec;

        /* Back link to parent structure nw_xfer_request. */
        struct nw_xfer_request  *ti_nwxfer;
};

/**
 * Represents a wrapper over generic IO fop and its callback
 * to keep track of such IO fops issued by same target_ioreq structure.
 *
 * When bottom halves for c2_sm_ast structures are run, it updates
 * target_ioreq::ti_rc and target_ioreq::ti_bytes with data from
 * IO reply fop.
 * Then it decrements nw_xfer_request::nxr_iofop_nr, number of IO fops.
 * When this count reaches zero, io_request::ir_sm changes its state.
 */

struct io_req_fop {
        /** Holds IOFOP_MAGIC */
        uint64_t                irf_magic;

        /** In-memory handle for IO fop. */
        struct c2_io_fop        irf_iofop;

        /** Callback per IO fop. */
        struct c2_sm_ast        irf_ast;

        /** Clink registered with c2_rpc_item::ri_chan channel. */
        struct c2_clink         irf_clink;

        /** Linkage to link in to target_ioreq::ti_iofops list. */
        struct c2_tlink         irf_link;

        /**
         * Backlink to target_ioreq object where rc and number of bytes
         * are updated.
         */
        struct target_ioreq    *irf_tioreq;
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
        DTBUF_MAGIC  = 0x19c031960ffe9517ULL, // incomingoffensiv
        PGROUP_MAGIC = 0x19ca9de5ce91b21bULL, // incandescentbulb
};

C2_TL_DESCR_DEFINE(tioreqs, "List of target_ioreq objects", static,
                   struct target_ioreq, ti_link, ti_magic,
                   TIOREQ_MAGIC, NWREQ_MAGIC);

C2_TL_DESCR_DEFINE(iofops, "List of IO fops", static,
                   struct io_req_fop, irf_link, irf_magic,
                   IOFOP_MAGIC, TIOREQ_MAGIC);

C2_TL_DEFINE(tioreqs,  static, struct target_ioreq);
C2_TL_DEFINE(iofops,    static, struct io_req_fop);

static struct c2_bob_type tioreq_bobtype;
static struct c2_bob_type iofop_bobtype;
static struct c2_bob_type ioreq_bobtype;
static struct c2_bob_type pgiomap_bobtype;
static struct c2_bob_type nwxfer_bobtype;
static struct c2_bob_type dtbuf_bobtype;

C2_BOB_DEFINE(static, &tioreq_bobtype,  target_ioreq);
C2_BOB_DEFINE(static, &iofop_bobtype,   io_req_fop);
C2_BOB_DEFINE(static, &pgiomap_bobtype, pargrp_iomap);
C2_BOB_DEFINE(static, &ioreq_bobtype,   io_request);
C2_BOB_DEFINE(static, &nwxfer_bobtype,  nw_xfer_request);
C2_BOB_DEFINE(static, &dtbuf_bobtype,   data_buf);

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

static struct c2_bob_type pgiomap_bobtype = {
        .bt_name         = "pargrp_iomap_bobtype",
        .bt_magix_offset = offsetof(struct pargrp_iomap, pi_magic),
        .bt_magix        = PGROUP_MAGIC,
        .bt_check        = NULL,
};

static struct c2_bob_type nwxfer_bobtype = {
        .bt_name         = "nw_xfer_request_bobtype",
        .bt_magix_offset = offsetof(struct nw_xfer_request, nxr_magic),
        .bt_magix        = NWREQ_MAGIC,
        .bt_check        = NULL,
};

static struct c2_bob_type dtbuf_bobtype = {
        .bt_name         = "data_buf_bobtype",
        .bt_magix_offset = offsetof(struct data_buf, db_magic),
        .bt_magix        = DTBUF_MAGIC,
        .bt_check        = NULL,
};

#define FILE_TO_INODE(file)   ((file)->f_dentry->d_inode)
#define FILE_TO_C2INODE(file) (C2T1FS_I(FILE_TO_INODE((file))))
#define FILE_TO_FID(file)     (&FILE_TO_C2INODE((file))->ci_fid)
#define FILE_TO_SB(file)      (C2T1FS_SB(FILE_TO_INODE((file))->i_sb))
#define FILE_TO_SMGROUP(file) (&FILE_TO_SB((file))->csb_iogroup)

#define INDEX(ivec, i) ((ivec)->iv_index[(i)])

#define COUNT(ivec, i) ((ivec)->iv_vec.v_count[(i)])

#define SEG_NR(vec)    ((vec)->iv_vec.v_nr)

#define PARITY_GROUP_NEEDS_RMW(map, size, grpsize) \
        ((size) < (grpsize) && (map)->pi_ioreq->ir_type == IRT_WRITE)

#define PAGE_NR(size)  ((size) / PAGE_CACHE_SIZE)

#define EXTENT_PAGE_NR(ext) (PAGE_NR(c2_ext_length((ext))) + 1)

#define PARITY_UNITS_PAGE_NR(play) \
        (PAGE_NR((play)->pl_unit_size) * play->pl_K)

#define INDEXVEC_PAGE_NR(ivec) (PAGE_NR(c2_vec_count((ivec))))

#define LAYOUT_GET(ioreq) \
        (layout_to_pd_layout(FILE_TO_C2INODE((ioreq)->ir_file)->ci_layout))

#define GRP_ID_GET(index, dtsize) ((index) / (dtsize))

#define TGT_OFFSET_GET(frame, unit_size, gob_offset) \
        ((frame) * (unit_size) + ((gob_offset) % (unit_size)))

#define TGT_FID_GET(req, tgt) \
        (c2t1fs_cob_fid(FILE_TO_FID((req)->ir_file), tgt->ta_obj + 1))

#define TGT_SESS_GET(req, tfid) \
        (c2t1fs_container_id_to_session(FILE_TO_SB(req->ir_file), \
                                        tfid.f_container))

#define PAGE_ID(offset) ((offset) >> PAGE_CACHE_SHIFT)

#define REQ_TO_FOP_TYPE(req) ((req)->ir_type == IRT_READ ? \
                &c2_fop_cob_readv_fopt : &c2_fop_cob_writev_fopt)

/** Invoked during c2t1fs mount. */
void io_bob_tlists_init(void)
{
        c2_bob_type_tlist_init(&tioreq_bobtype, &tioreqs_tl);
        c2_bob_type_tlist_init(&iofop_bobtype,  &iofops_tl);
}

static const struct c2_addb_loc io_addb_loc = {
        .al_name = "c2t1fs_io_path",
};

struct c2_addb_ctx c2t1fs_addb;

const struct c2_addb_ctx_type c2t1fs_addb_type = {
        .act_name = "c2t1fs",
};

C2_ADDB_EV_DEFINE(io_request_failed, "IO request failed.",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

#if 0
static int nw_xfer_io_prepare (struct nw_xfer_request *xfer);
static int nw_xfer_io_dispatch(struct nw_xfer_request *xfer);
static int nw_xfer_io_complete(struct nw_xfer_request *xfer);

static struct nw_xfer_ops xfer_ops = {
        .nxo_prepare  = nw_xfer_io_prepare,
        .nxo_dispatch = nw_xfer_io_dispatch,
        .nxo_complete = nw_xfer_io_complete,
};
#endif

static void tioreq_locate(struct nw_xfer_request  *xfer,
                          struct c2_fid           *fid,
                          struct target_ioreq    **tioreq);

static int nw_xfer_io_prepare (struct nw_xfer_request *xfer);
static int nw_xfer_io_dispatch(struct nw_xfer_request *xfer);

static struct nw_xfer_ops xfer_ops = {
        .nxo_prepare       = nw_xfer_io_prepare,
        .nxo_dispatch      = nw_xfer_io_dispatch,
        .nxo_complete      = NULL,
        .nxo_tioreq_locate = tioreq_locate,
};

static int pargrp_iomap_populate (struct pargrp_iomap      *map,
                                  const struct c2_indexvec *ivec,
                                  struct c2_ivec_cursor    *cursor);

static bool pargrp_iomap_spans_seg(struct pargrp_iomap *map,
                                   c2_bindex_t index, c2_bcount_t count);

static int pargrp_iomap_readrest (struct pargrp_iomap *map);

static uint64_t pargrp_iomap_fullpages_find(struct pargrp_iomap *map);

static int pargrp_iomap_pageattr_fill(struct pargrp_iomap      *map,
                                      const struct c2_indexvec *ivec,
                                      bool                      rmw);

static int pargrp_iomap_seg_process(struct pargrp_iomap *map,
                                    uint64_t             seg,
                                    c2_bindex_t          pgstart);

static int pargrp_iomap_parity_recalc(struct pargrp_iomap *map);

static const struct pargrp_iomap_ops iomap_ops = {
        .pi_populate       = pargrp_iomap_populate,
        .pi_spans_seg      = pargrp_iomap_spans_seg,
        .pi_readrest       = pargrp_iomap_readrest,
        .pi_fullpages_find = pargrp_iomap_fullpages_find,
        .pi_pageattr_fill  = pargrp_iomap_pageattr_fill,
        .pi_seg_process    = pargrp_iomap_seg_process,
        .pi_parity_recalc  = pargrp_iomap_parity_recalc,
};

static bool pargrp_iomap_invariant_nr(const struct io_request *req);
static bool nw_xfer_request_invariant(const struct nw_xfer_request *xfer);
static bool target_ioreq_invariant(const struct target_ioreq *ti);

static void target_ioreq_seg_add(struct target_ioreq *ti,
                                 uint64_t             frame,
                                 c2_bindex_t          gob_offset,
                                 c2_bcount_t          count,
                                 uint64_t             unit);

static int target_ioreq_get(struct nw_xfer_request *xfer,
                            struct c2_fid          *fid,
                            struct c2_rpc_session  *session,
                            uint64_t                size,
                            struct target_ioreq   **out);

static void target_ioreq_fini(struct target_ioreq *ti);

static int nw_xfer_request_tioreq_map(struct nw_xfer_request      *xfer,
                                      struct c2_pdclust_src_addr  *src,
                                      struct c2_pdclust_tgt_addr  *tgt,
                                      struct target_ioreq        **out);

struct page *target_ioreq_page_map(struct target_ioreq        *ti,
                                   struct c2_pdclust_tgt_addr *tgt,
                                   c2_bindex_t                 gob_offset,
                                   c2_bcount_t                 count);

static struct data_buf *data_buf_alloc_init(enum page_attr pattr);

#if 0
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
#endif

static int ioreq_iomaps_prepare(struct io_request *req);
static void ioreq_iomap_locate (struct io_request *req, uint64_t grpid,
                                struct pargrp_iomap **map);
int ioreq_user_data_copy(struct io_request   *req,
                         enum copy_direction  dir,
                         enum page_attr       filter);

static int ioreq_parity_recalc(struct io_request *req);

static struct io_request_ops ioreq_ops = {
        .iro_readextent     = NULL,
        .iro_preparewrite   = NULL,
        .iro_commit_write   = NULL,
        .iro_submit         = NULL,
        .iro_read_complete  = NULL,
        .iro_write_complete = NULL,
        .iro_iomaps_prepare = ioreq_iomaps_prepare,
        .iro_user_data_copy = ioreq_user_data_copy,
        .iro_iomap_locate   = ioreq_iomap_locate,
        .iro_parity_recalc  = ioreq_parity_recalc,
};

#if 0
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
#endif

static bool io_request_invariant(const struct io_request *req)
{
        C2_PRE(req != NULL);

        return
               io_request_bob_check(req) &&
               req->ir_type   <= IRT_TYPE_NR &&
               req->ir_state  <= IRS_STATE_NR &&
               req->ir_iovec  != NULL &&
               req->ir_ops    != NULL &&
               c2_fid_is_valid(FILE_TO_FID(req->ir_file)) &&

               ergo(req->ir_state == IRS_READING,
                    !tioreqs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

               ergo(req->ir_state == IRS_WRITING,
                    !tioreqs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

               ergo(req->ir_state == IRS_READ_COMPLETE,
                    ergo(req->ir_type == IRT_READ,
                         tioreqs_tlist_is_empty(
                                 &req->ir_nwxfer.nxr_tioreqs))) &&

               ergo(req->ir_state == IRS_WRITE_COMPLETE,
                    tioreqs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

               c2_vec_count(&req->ir_ivec.iv_vec) > 0 &&

               c2_forall(i, req->ir_ivec.iv_vec.v_nr - 1,
                         req->ir_ivec.iv_index[i] +
                         req->ir_ivec.iv_vec.v_count[i] <=
                         req->ir_ivec.iv_index[i+1]) &&

               pargrp_iomap_invariant_nr(req) &&

               nw_xfer_request_invariant(&req->ir_nwxfer);
}

static bool nw_xfer_request_invariant(const struct nw_xfer_request *xfer)
{
        return
               xfer != NULL &&
               nw_xfer_request_bob_check(xfer) &&
               xfer->nxr_state <= NXS_STATE_NR &&

               ergo(xfer->nxr_state == NXS_INITIALIZED,
                    tioreqs_tlist_is_empty(&xfer->nxr_tioreqs) &&
                    (xfer->nxr_rc == xfer->nxr_bytes) ==
                    (xfer->nxr_iofop_nr == 0)) &&

               ergo(xfer->nxr_state == NXS_INFLIGHT,
                    !tioreqs_tlist_is_empty(&xfer->nxr_tioreqs) &&
                    xfer->nxr_iofop_nr > 0) &&

               ergo(xfer->nxr_state == NXS_COMPLETE,
                    tioreqs_tlist_is_empty(&xfer->nxr_tioreqs) &&
                    xfer->nxr_iofop_nr == 0 &&
                    xfer->nxr_bytes > 0) &&

               c2_tl_forall(tioreqs, tioreq, &xfer->nxr_tioreqs,
                            target_ioreq_invariant(tioreq)) &&

               xfer->nxr_iofop_nr == c2_tlist_length(&tioreqs_tl,
                                                     &xfer->nxr_tioreqs);
}

static bool data_buf_invariant(const struct data_buf *db)
{
        return
               db != NULL &&
               data_buf_bob_check(db) &&
               db->db_buf.b_addr != NULL &&
               db->db_buf.b_nob  > 0;
}

static bool data_buf_invariant_nr(const struct pargrp_iomap *map)
{
        uint64_t                  i;
        struct c2_pdclust_layout *play;

        play = LAYOUT_GET(map->pi_ioreq);

        for (i = 0; i < PAGE_NR(play->pl_unit_size * play->pl_N); ++i)
                /* Not all data_buf structures are allocated all the time. */
                if (map->pi_databufs[i] != NULL)
                        if (!data_buf_invariant(map->pi_databufs[i]))
                                break;
        return i == PAGE_NR(play->pl_unit_size * play->pl_N);
}

static bool io_req_fop_invariant(const struct io_req_fop *fop)
{
        return
               fop != NULL &&
               fop->irf_tioreq != NULL &&
               fop->irf_ast.sa_cb != NULL &&
               c2_clink_is_armed(&fop->irf_clink) &&
               iofops_tlink_is_in(fop) &&
               io_req_fop_bob_check(fop);
}

static bool target_ioreq_invariant(const struct target_ioreq *ti)
{
        return
               ti != NULL &&
               target_ioreq_bob_check(ti) &&
               ti->ti_session       != NULL &&
               ti->ti_nwxfer        != NULL &&
               ti->ti_bufvec.ov_buf != NULL &&
               c2_fid_is_valid(&ti->ti_fid) &&
               c2_vec_count(&ti->ti_ivec.iv_vec) > 0 &&
               tioreqs_tlink_is_in(ti) &&
               c2_tl_forall(iofops, iofop, &ti->ti_iofops,
                            io_req_fop_invariant(iofop));
}

static bool pargrp_iomap_invariant(const struct pargrp_iomap *map)
{
        return
               map != NULL &&
               pargrp_iomap_bob_check(map) &&
               map->pi_ops      != NULL &&
               map->pi_read     <  PIR_NR &&
               map->pi_databufs != NULL &&
               data_buf_invariant_nr(map) &&
               ergo(c2_vec_count(&map->pi_ivec.iv_vec) > 0,
                    c2_forall(i, map->pi_ivec.iv_vec.v_nr - 1,
                              map->pi_ivec.iv_index[i] +
                              map->pi_ivec.iv_vec.v_count[i] <
                              map->pi_ivec.iv_index[i+1]));
}

static bool pargrp_iomap_invariant_nr(const struct io_request *req)
{
        return c2_forall(i, req->ir_iomap_nr,
                         pargrp_iomap_invariant(req->ir_iomaps[i]));
}

static void nw_xfer_request_init(struct nw_xfer_request *xfer)
{
        C2_PRE(xfer != NULL);

        nw_xfer_request_bob_init(xfer);
        xfer->nxr_rc    = xfer->nxr_bytes = xfer->nxr_iofop_nr = 0;
        xfer->nxr_state = NXS_INITIALIZED;
        xfer->nxr_ops   = &xfer_ops;
        tioreqs_tlist_init(&xfer->nxr_tioreqs);

        C2_POST(nw_xfer_request_invariant(xfer));
}

static void nw_xfer_request_fini(struct nw_xfer_request *xfer)
{
        C2_PRE(xfer != NULL && xfer->nxr_state == NXS_COMPLETE);
        C2_PRE(nw_xfer_request_invariant(xfer));

        nw_xfer_request_bob_fini(xfer);
        tioreqs_tlist_fini(&xfer->nxr_tioreqs);
}

static void tioreq_locate(struct nw_xfer_request  *xfer,
                          struct c2_fid           *fid,
                          struct target_ioreq    **tioreq)
{
        struct target_ioreq *ti;

        C2_PRE(xfer   != NULL);
        C2_PRE(fid    != NULL);
        C2_PRE(tioreq != NULL);

        c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
                if (c2_fid_eq(&ti->ti_fid, fid))
                        break;
        } c2_tl_endfor;

        /*
         * In case the loop runs till end of list, it will return
         * with ti = NULL.
         */
        *tioreq = ti;
}

static void ioreq_iomap_locate(struct io_request *req, uint64_t grpid,
                               struct pargrp_iomap **map)
{
        uint64_t m;

        C2_PRE(io_request_invariant(req));
        C2_PRE(map != NULL);

        for (m = 0; m < req->ir_iomap_nr; ++m) {
                if (req->ir_iomaps[m]->pi_grpid == grpid) {
                        *map = req->ir_iomaps[m];
                        break;
                }
        }
        C2_POST(m < req->ir_iomap_nr);
}

/* Typically used while copying data to/from user space. */
static bool page_copy_filter(c2_bindex_t start, c2_bindex_t end,
                             enum page_attr filter)
{
        C2_PRE(end - start <= PAGE_CACHE_SIZE);
        C2_PRE(ergo(filter != PA_NONE,
                    filter & (PA_FULLPAGE_MODIFY | PA_PARTPAGE_MODIFY)));

        if (filter & PA_FULLPAGE_MODIFY) {
                return (end - start == PAGE_CACHE_SIZE);
        } else if (filter & PA_PARTPAGE_MODIFY) {
                return (end - start <  PAGE_CACHE_SIZE);
        }
        else
                return true;
}

static int user_data_copy(struct pargrp_iomap *map,
                          c2_bindex_t          start,
                          c2_bindex_t          end,
                          struct iov_iter     *it,
                          enum copy_direction  dir,
                          enum page_attr       filter)
{
        /*
         * iov_iter should be able to be used with copy_to_user() as well
         * since it is as good as a vector cursor.
         * Present kernel 2.6.32 has no support for such requirement.
         */
        uint64_t                  dtsize;
        uint64_t                  copied;
        c2_bindex_t               pgindex;
        struct page              *page;
        struct c2_pdclust_layout *play;

        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(dir == CD_COPY_FROM_USER || dir == CD_COPY_TO_USER);

        play    = LAYOUT_GET(map->pi_ioreq);
        dtsize  = play->pl_unit_size * play->pl_N;
        pgindex = PAGE_ID((start - dtsize * map->pi_grpid));
        page    = map->pi_databufs[pgindex]->db_buf.b_addr;

        if (dir == CD_COPY_FROM_USER) {
                if (page_copy_filter(start, end, filter)) {
                        C2_ASSERT(map->pi_databufs[pgindex]->db_flags & filter);

                        /*
                         * Copies page to auxiliary buffer before it gets
                         * overwritten by user data. This is needed in order
                         * to calculate delta parity in case of read-old
                         * approach.
                         */
                        if (map->pi_databufs[pgindex]->db_auxbuf.b_addr != NULL)
                                memcpy(page_address(map->pi_databufs[pgindex]->
                                       db_buf.b_addr),
                                       page_address(map->pi_databufs[pgindex]->
                                       db_auxbuf.b_addr), PAGE_CACHE_SIZE);
                        pagefault_disable();
                        /* Copies to appropriate offset within page. */
                        copied = iov_iter_copy_from_user_atomic(page, it,
                                        start & (PAGE_CACHE_SIZE - 1),
                                        end - start);
                        pagefault_enable();

                        if (copied != end - start)
                                return -EFAULT;
                }
        } else {
                page   = map->pi_databufs[pgindex]->db_buf.b_addr;
                copied = copy_to_user(it->iov->iov_base + it->iov_offset,
                                      page_address(page) +
                                      (start & (PAGE_CACHE_SIZE - 1)),
                                      end - start);
                if (copied != 0)
                        return -EFAULT;
        }

        return 0;
}

static int pargrp_iomap_parity_recalc(struct pargrp_iomap *map)
{
        uint64_t                  u;
        uint64_t                  j;
        struct c2_buf            *dbufs = NULL;
        struct c2_buf            *pbufs = NULL;
        struct c2_pdclust_layout *play;

        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(map->pi_read == PIR_READREST || map->pi_read == PIR_READOLD);

        play = LAYOUT_GET(map->pi_ioreq);
        C2_ALLOC_ARR(dbufs, play->pl_N);
        C2_ALLOC_ARR(pbufs, play->pl_K);

        if (dbufs == NULL || pbufs == NULL) {
                dbufs != NULL ?: c2_free(dbufs);
                pbufs != NULL ?: c2_free(pbufs);
                return -ENOMEM;
        }

        if (map->pi_read == PIR_READREST) {
                for (u = 0; u < play->pl_N; ++u) {
                        dbufs[u].b_addr = page_address(map->pi_databufs[u]->
                                          db_buf.b_addr);
                        dbufs[u].b_nob  = PAGE_CACHE_SIZE;
                }

                for (u = 0; u < play->pl_K; ++u) {
                        pbufs[u].b_addr = page_address(map->pi_paritybufs[u]->
                                          db_buf.b_addr);
                        pbufs[u].b_nob  = PAGE_CACHE_SIZE;
                }

                c2_parity_math_calculate(&play->pl_math, dbufs, pbufs);
        } else {
                struct c2_buf  zbuf;
                /* Buffers to store delta parities. */
                struct c2_buf *deltabufs = NULL;

                C2_ALLOC_ARR(deltabufs, play->pl_N);
                if (deltabufs == NULL)
                        return -ENOMEM;

                zbuf.b_addr = (void *)get_zeroed_page(GFP_KERNEL);
                if (zbuf.b_addr == NULL) {
                        c2_free(deltabufs);
                        return -ENOMEM;
                }
                zbuf.b_nob  = PAGE_CACHE_SIZE;

                for (u = 0; u < play->pl_N; ++u) {
                        /*
                         * Calculates parity between old version and
                         * new version of data block.
                         */
                        if (map->pi_databufs[u] &&
                            map->pi_databufs[u]->db_flags &
                            (PA_FULLPAGE_MODIFY | PA_PARTPAGE_MODIFY)) {

                                dbufs[0].b_addr = map->pi_databufs[u]->
                                                  db_auxbuf.b_addr;
                                dbufs[0].b_nob  = PAGE_CACHE_SIZE;

                                dbufs[1].b_addr = map->pi_databufs[u]->
                                                  db_buf.b_addr;
                                dbufs[1].b_nob  = PAGE_CACHE_SIZE;

                                for (j = 2; j < play->pl_N; ++j)
                                        dbufs[j] = zbuf;

                                c2_parity_math_calculate(&play->pl_math, dbufs,
                                                         &deltabufs[u]);
                        }
                }

                /* Calculates parity amongst delta parities. */
                for (u = 0; u < play->pl_N; ++u)
                        if (deltabufs[u].b_addr == NULL)
                                deltabufs[u] = zbuf;
                c2_parity_math_calculate(&play->pl_math, deltabufs, pbufs);

                /*
                 * Calculates parity between old version of parity block
                 * and new delta parity.
                 */
                for (u = 0; u < play->pl_K; ++u) {
                        if (u < play->pl_K)
                                dbufs[u] = pbufs[u];
                        else if (u < 2 * play->pl_K)
                                dbufs[u] = map->pi_paritybufs[u]->db_buf;
                        else if (u < play->pl_N)
                                if (dbufs[u].b_addr == NULL)
                                        dbufs[u] = zbuf;
                }

                for (j = 0; j < play->pl_K; ++j)
                        pbufs[j] = map->pi_paritybufs[j]->db_buf;

                c2_parity_math_calculate(&play->pl_math, dbufs, pbufs);
        }

        return 0;
}

static int ioreq_parity_recalc(struct io_request *req)
{
        int      rc;
        uint64_t m;

        C2_PRE(io_request_invariant(req));

        for (m = 0; m < req->ir_iomap_nr; ++m) {
                rc = req->ir_iomaps[m]->pi_ops->pi_parity_recalc(req->
                                ir_iomaps[m]);
                if (rc != 0)
                        return rc;
        }
        return 0;
}

int ioreq_user_data_copy(struct io_request   *req,
                         enum copy_direction  dir,
                         enum page_attr       filter)
{
        int                       rc;
        uint64_t                  dtsize;
        uint64_t                  m;
        c2_bindex_t               grpstart;
        c2_bindex_t               grpend;
        c2_bindex_t               pgstart;
        c2_bindex_t               pgend;
        c2_bcount_t               count = 0;
        struct iov_iter           it;
        struct c2_ivec_cursor     srccur;
        struct c2_ivec_cursor     destcur;
        struct pargrp_iomap      *map;
        struct c2_pdclust_layout *play;

        C2_PRE(io_request_invariant(req));

        iov_iter_init(&it, req->ir_iovec, req->ir_ivec.iv_vec.v_nr,
                      c2_vec_count(&req->ir_ivec.iv_vec), 0);
        c2_ivec_cursor_init(&srccur, &req->ir_ivec);
        play   = LAYOUT_GET(req);
        dtsize = play->pl_unit_size * play->pl_N;

        for (m = 0; m < req->ir_iomap_nr; ++m) {

                map      = req->ir_iomaps[m];
                C2_ASSERT(pargrp_iomap_invariant(map));

                c2_ivec_cursor_init(&destcur, &map->pi_ivec);
                grpstart = dtsize * map->pi_grpid;
                grpend   = grpstart + dtsize;
                pgstart  = c2_ivec_cursor_index(&srccur);

                while (c2_ivec_cursor_move(&srccur, count) &&
                       c2_ivec_cursor_index(&srccur) < grpend) {

                        pgend = min64u(c2_round_up(pgstart, PAGE_CACHE_SIZE),
                                       pgstart + c2_ivec_cursor_step(&srccur));

                        count = pgend - pgstart;
                        C2_ASSERT(count <= PAGE_CACHE_SIZE);

                        /*
                         * Keeps src and dest cursors in sync so as to help
                         * copying of user data.
                         */
                        if (c2_ivec_cursor_move_until(&destcur, pgstart))
                                break;

                        rc = user_data_copy(map, pgstart, pgend, &it, dir,
                                            filter);
                        if (rc != 0)
                                return rc;

                        iov_iter_advance(&it, count);
                        pgstart += count;
                }
        }

        return 0;
}

static void indexvec_sort(struct c2_indexvec *ivec)
{
        uint64_t i;
        uint64_t j;

        C2_PRE(ivec != NULL && c2_vec_count(&ivec->iv_vec) > 0);

        /**
         * @todo Should be replaced by an efficient sorting algorithm,
         * something like heapsort which is fairly inexpensive in kernel
         * mode with the least worst case scenario.
         * Existing heap sort from kernel code can not be used due to
         * apparent disconnect between index vector and its associated
         * count vector for same index.
         */
        for (i = 0; i < SEG_NR(ivec); ++i) {
                for (j = i+1; j < SEG_NR(ivec); ++j) {
                        if (INDEX(ivec, i) > INDEX(ivec, j)) {
                                C2_SWAP(INDEX(ivec, i), INDEX(ivec, j));
                                C2_SWAP(COUNT(ivec, i), COUNT(ivec, j));
                        }
                }
        }
}

static int pargrp_iomap_init(struct pargrp_iomap *map,
                             struct io_request   *req,
                             uint64_t             grpid,
                             uint64_t             page_nr)
{
        int                       rc;
        struct c2_pdclust_layout *play;

        C2_PRE(map    != NULL);
        C2_PRE(req    != NULL);
        C2_PRE(page_nr > 0);

        pargrp_iomap_bob_init(map);
        play                  = LAYOUT_GET(req);
        map->pi_ops           = &iomap_ops;
        map->pi_grpid         = grpid;
        map->pi_ioreq         = req;
        SEG_NR(&map->pi_ivec) = page_nr;

        C2_ALLOC_ARR(map->pi_ivec.iv_index, page_nr);
        C2_ALLOC_ARR(map->pi_ivec.iv_vec.v_count, page_nr);

        if (map->pi_ivec.iv_index       == NULL ||
            map->pi_ivec.iv_vec.v_count == NULL) {

                map->pi_ivec.iv_index != NULL ?:
                        c2_free(map->pi_ivec.iv_index);
                map->pi_ivec.iv_vec.v_count != NULL ?:
                        c2_free(map->pi_ivec.iv_vec.v_count);

                C2_ADDB_ADD(&c2t1fs_addb, &io_addb_loc, io_request_failed,
                            "Failed to allocate memory for pargrp_iomap::"
                            "pi_ivec.", -ENOMEM);
                return -ENOMEM;
        }

        C2_ALLOC_ARR(map->pi_databufs, page_nr);
        if (map->pi_databufs == NULL) {
                c2_free(map->pi_ivec.iv_index);
                c2_free(map->pi_ivec.iv_vec.v_count);
                rc = -ENOMEM;
        }

        if (req->ir_type == IRT_WRITE) {
                C2_ALLOC_ARR(map->pi_paritybufs, PARITY_UNITS_PAGE_NR(play));
                if (map->pi_paritybufs == NULL) {
                        c2_free(map->pi_ivec.iv_index);
                        c2_free(map->pi_ivec.iv_vec.v_count);
                        c2_free(map->pi_databufs);
                }
        }
        C2_POST(ergo(rc == 0, pargrp_iomap_invariant(map)));
        return rc;
}

static void pargrp_iomap_fini(struct pargrp_iomap *map)
{
        C2_PRE(pargrp_iomap_invariant(map));

        pargrp_iomap_bob_fini(map);
        c2_free(map->pi_ivec.iv_index);
        c2_free(map->pi_ivec.iv_vec.v_count);
        c2_free(map->pi_databufs);
        map->pi_ops = NULL;
}

static bool pargrp_iomap_spans_seg(struct pargrp_iomap *map,
                                   c2_bindex_t index, c2_bcount_t count)
{
        C2_PRE(pargrp_iomap_invariant(map));

        return c2_forall(i, map->pi_ivec.iv_vec.v_nr,
                         index >= INDEX(&map->pi_ivec, i) &&
                         count <= COUNT(&map->pi_ivec, i));
}

static int pargrp_iomap_databuf_alloc(struct pargrp_iomap *map,
                                      uint64_t             pgid)
{
        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(map->pi_databufs[pgid] == NULL);

        map->pi_databufs[pgid] = data_buf_alloc_init(0);
        return map->pi_databufs[pgid] == NULL ? -ENOMEM: 0;
}

static int pargrp_iomap_seg_process(struct pargrp_iomap *map,
                                    uint64_t             seg,
                                    c2_bindex_t          pgstart)
{
        int         rc;
        uint64_t    pgid;
        c2_bindex_t start;
        c2_bindex_t end;

        C2_PRE(pargrp_iomap_invariant(map));

        start = map->pi_ivec.iv_index[seg];
        end   = start;

        while (end < map->pi_ivec.iv_index[seg] +
               map->pi_ivec.iv_vec.v_count[seg]) {

                end = start + min64u(PAGE_CACHE_SIZE,
                                     map->pi_ivec.iv_vec.v_count[seg]);
                pgid = PAGE_ID(start - pgstart);
                rc = pargrp_iomap_databuf_alloc(map, pgid);

                if (rc != 0)
                        return rc;

                if (end - start == PAGE_CACHE_SIZE)
                        map->pi_databufs[pgid]->db_flags |=
                                PA_FULLPAGE_MODIFY;
                start = end;
        }

        return rc;
}

static uint64_t pargrp_iomap_fullpages_find(struct pargrp_iomap *map)
{
        uint64_t                  seg;
        uint64_t                  nr = 0;
        struct c2_pdclust_layout *play;

        C2_PRE(pargrp_iomap_invariant(map));

        play = LAYOUT_GET(map->pi_ioreq);

        for (seg = 0; seg < PAGE_NR(play->pl_unit_size * play->pl_N); ++seg) {
                if (map->pi_databufs[seg] && map->pi_databufs[seg]->db_flags &
                                PA_FULLPAGE_MODIFY)
                        ++nr;
        }
        return nr;
}

static uint64_t pargrp_iomap_auxbuf_alloc(struct pargrp_iomap *map, uint64_t pg)
{
        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(map->pi_databufs[pg]->db_flags &
               (PA_PARTPAGE_MODIFY | PA_FULLPAGE_MODIFY));

        if (map->pi_read == PIR_READOLD) {

                map->pi_databufs[pg]->db_auxbuf.b_addr = (void *)
                        __get_free_page(GFP_KERNEL);

                if (map->pi_databufs[pg]->db_auxbuf.b_addr == NULL)
                        return -ENOMEM;
                map->pi_databufs[pg]->db_auxbuf.b_nob = PAGE_CACHE_SIZE;
        }

        return 0;
}

static int pargrp_iomap_pageattr_helper(struct pargrp_iomap      *map,
                                        const struct c2_indexvec *ivec,
                                        uint64_t                  grpsize)
{
        int                       rc;
        uint64_t                  pg;
        uint64_t                  seg;
        uint64_t                  pgstartoff;
        uint64_t                  pgendoff;
        struct c2_ext             pgext;
        struct c2_ext             vecext;
        struct c2_ext             rext;

        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(ivec != NULL);

        pgstartoff = grpsize * map->pi_grpid;
        pgendoff   = pgstartoff + grpsize;

        for (seg = 0; INDEX(ivec, seg) < pgendoff && seg < SEG_NR(ivec);
             ++seg) {

                vecext.e_start = INDEX(ivec, seg);
                vecext.e_end   = vecext.e_start + COUNT(ivec, seg);

                pgext.e_start  = c2_round_down(vecext.e_start, PAGE_CACHE_SIZE);
                pgext.e_end    = pgext.e_start + PAGE_CACHE_SIZE;
                pg             = (pgext.e_start - pgstartoff) / PAGE_CACHE_SIZE;

                for (; pgext.e_start < vecext.e_start; ++pg) {

                        c2_ext_intersection(&pgext, &vecext, &rext);

                        if (!c2_ext_is_valid(&rext))
                                goto next;

                        if (ivec == &map->pi_ioreq->ir_ivec) {
                                map->pi_databufs[pg]->db_flags |= PA_WRITE;
                                if (c2_ext_length(&rext) == PAGE_CACHE_SIZE)
                                        C2_ASSERT(map->pi_databufs[pg]->
                                                 db_flags & PA_FULLPAGE_MODIFY);
                                else
                                        map->pi_databufs[pg]->db_flags |=
                                                PA_PARTPAGE_MODIFY;

                                rc = pargrp_iomap_auxbuf_alloc(map, pg);
                                if (rc != 0)
                                        return rc;
                        } else if (ivec == &map->pi_ivec)
                                if (!(map->pi_databufs[pg]->db_flags &
                                      PA_FULLPAGE_MODIFY))
                                        map->pi_databufs[pg]->db_flags |=
                                                PA_READ;

next:
                        pgext.e_start += PAGE_CACHE_SIZE;
                        pgext.e_end   += PAGE_CACHE_SIZE;
                }
        }
        return 0;
}

static int pargrp_iomap_pageattr_fill(struct pargrp_iomap      *map,
                                      const struct c2_indexvec *vec,
                                      bool                      rmw)
{
        int                       rc;
        uint64_t                  grpsize;
        struct c2_pdclust_layout *play;

        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(vec != NULL && c2_vec_count(&vec->iv_vec) > 0);

        play    = LAYOUT_GET(map->pi_ioreq);
        grpsize = play->pl_unit_size * play->pl_N;

        if (rmw) {
                rc = pargrp_iomap_pageattr_helper(map, vec, grpsize);
                if (rc != 0)
                        return rc;

                rc = pargrp_iomap_pageattr_helper(map, &map->pi_ivec, grpsize);
                if (rc != 0)
                        return rc;
        } else {
                uint64_t       pg;
                enum page_attr attr;

                attr = map->pi_ioreq->ir_type == IRT_READ ? PA_READ : PA_WRITE;
                for (pg = 0; pg < PAGE_NR(play->pl_unit_size * play->pl_N);
                     ++pg)
                        map->pi_pageattr[pg] |= attr;
        }
}

static void pargrp_iomap_readrest(struct pargrp_iomap *map)
{
        uint64_t                  i;
        uint64_t                  seg_nr;
        c2_bindex_t               pgstartoff;
        c2_bindex_t               pgendoff;
        struct c2_pdclust_layout *play;
        struct c2_indexvec       *ivec;

        C2_PRE(pargrp_iomap_invariant(map));

        map->pi_read = PIR_READREST;

        play   = LAYOUT_GET(map->pi_ioreq);
        ivec   = &map->pi_ivec;
        seg_nr = map->pi_ivec.iv_vec.v_nr;
        pgstartoff = play->pl_unit_size * play->pl_N * map->pi_grpid;
        pgendoff   = pgstartoff + (play->pl_unit_size * play->pl_N);

        COUNT(ivec, 0) += INDEX(ivec, 0) - pgstartoff;
        INDEX(ivec, 0)  = pgstartoff;

        COUNT(ivec, seg_nr - 1) += pgendoff - INDEX(ivec, seg_nr - 1);

        /*
         * All io extents _not_ spanned by pargrp_iomap::pi_ivec
         * need to be included so that _all_ pages from parity group
         * are available to do IO.
         */
        for (i = 1; i < seg_nr - 2; ++i) {
                if (INDEX(ivec, i) + COUNT(ivec, i) < INDEX(ivec, i + 1))
                        COUNT(ivec, i) += INDEX(ivec, i + 1) -
                                          (INDEX(ivec, i) + COUNT(ivec, i));
                        map->pi_databufs[pg]->db_flags |= attr;
        }
        return 0;
}

/**
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
 * This approach reads the extent from start of parity group to the
 * point where a page is completely spanned by incoming IO request.
 *
 * Typically, the approach which leads to least size of data to be
 * read from server is selected.
 *
 * @verbatim
 *
 *  Read-rest approach
 *
 *   a     x
 *   +---+---+---+---+---+---+---+
 *   |   | P'| F | F | F | # | * |  PG#0
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | F | F | # | * |  PG#1
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | P |   | # | * |  PG#2
 *   +---+---+---+---+---+---+---+
 *     N   N   N   N   N   K   P
 *                 y     b
 *
 *   N = 5, P = 1, K = 1, unit_size = 4k
 *   F  => Fully occupied
 *   P' => Partially occupied
 *   #  => Parity unit
 *   *  => Spare unit
 *   x  => Start of actual file extent.
 *   y  => End of actual file extent.
 *   a  => Rounded down value of x.
 *   b  => Rounded up value of y.
 *
 *
 *  Read-old approach
 *
 *   a     x
 *   +---+---+---+---+---+---+---+
 *   |   |   |   | P'| F | # | * |  PG#0
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | F | F | # | * |  PG#1
 *   +---+---+---+---+---+---+---+
 *   | F | P'|   |   |   | # | * |  PG#2
 *   +---+---+---+---+---+---+---+
 *     N   N   N   N   N   K   P
 *                 y     b
 *
 *   N = 5, P = 1, K = 1, unit_size = 4k
 *   F  => Fully occupied
 *   P' => Partially occupied
 *   #  => Parity unit
 *   *  => Spare unit
 *   x  => Start of actual file extent.
 *   y  => End of actual file extent.
 *   a  => Rounded down value of x.
 *   b  => Rounded up value of y.
 *
 * @endverbatim
 */
static int pargrp_iomap_readrest(struct pargrp_iomap *map)
{
        int                       rc;
        uint64_t                  i;
        uint64_t                  seg_nr;
        c2_bindex_t               pgstartoff;
        c2_bindex_t               pgendoff;
        struct c2_pdclust_layout *play;
        struct c2_indexvec       *ivec;

        C2_PRE(pargrp_iomap_invariant(map));

        map->pi_read = PIR_READREST;

        play   = LAYOUT_GET(map->pi_ioreq);
        ivec   = &map->pi_ivec;
        seg_nr = map->pi_ivec.iv_vec.v_nr;
        pgstartoff = play->pl_unit_size * play->pl_N * map->pi_grpid;
        pgendoff   = pgstartoff + (play->pl_unit_size * play->pl_N);

        COUNT(ivec, 0) += INDEX(ivec, 0) - pgstartoff;
        INDEX(ivec, 0)  = pgstartoff;

        COUNT(ivec, seg_nr - 1) += pgendoff - INDEX(ivec, seg_nr - 1);

        /*
         * All io extents _not_ spanned by pargrp_iomap::pi_ivec
         * need to be included so that _all_ pages from parity group
         * are available to do IO.
         */
        for (i = 1; i < seg_nr - 2; ++i) {
                if (INDEX(ivec, i) + COUNT(ivec, i) < INDEX(ivec, i + 1))
                        COUNT(ivec, i) += INDEX(ivec, i + 1) -
                                          (INDEX(ivec, i) + COUNT(ivec, i)); 
        }

        /*
         * For read-rest approach, all data units from a parity group
         * are read. Ergo, mark them as to be read.
         */
        for (i = 0; i < PAGE_NR(pgendoff - pgstartoff); ++i) {
                if (map->pi_databufs[i] == NULL) {
                        rc = pargrp_iomap_databuf_alloc(map, i);
                        if (rc != 0)
                                return -ENOMEM;
                }
        }

        return 0;
}

static int pargrp_iomap_populate(struct pargrp_iomap      *map,
                                 const struct c2_indexvec *ivec,
                                 struct c2_ivec_cursor    *cursor)
{
        int                       rc;
        bool                      rmw;
        uint64_t                  i;
        uint64_t                  j;
        uint64_t                  size;
        uint64_t                  grpsize;
        c2_bcount_t               count = 0;
        /* Number of pages _completely_ spanned by incoming io vector. */
        uint64_t                  page_nr = 0;
        /* Number of pages to be read + written for read-old approach. */
        uint64_t                  ro_page_nr;
        /* Number of pages to be read + written for read-rest approach. */
        uint64_t                  rr_page_nr;
        c2_bindex_t               pgstart;
        c2_bindex_t               pgend;
        struct c2_pdclust_layout *play;

        C2_PRE(map  != NULL);
        C2_PRE(ivec != NULL);

        play    = LAYOUT_GET(map->pi_ioreq);
        grpsize = play->pl_unit_size * play->pl_N;
        pgstart = grpsize * map->pi_grpid;
        pgend   = pgstart + grpsize;

        for (size = 0, i = cursor->ic_cur.vc_seg; INDEX(ivec, i) < pgend; ++i)
                size += min64u(pgend - INDEX(ivec, i), COUNT(ivec, i));

        rmw = PARITY_GROUP_NEEDS_RMW(map, size, grpsize);

        for (j = 0; !c2_ivec_cursor_move(cursor, count) &&
             c2_ivec_cursor_index(cursor) < pgend; ++j) {
                /*
                 * Skips the current segment if it is already spanned by
                 * rounding up/down of earlier segment.
                 */
                if (map->pi_ops->pi_spans_seg(map, c2_ivec_cursor_index(cursor),
                                              c2_ivec_cursor_step(cursor)))
                        continue;

                INDEX(&map->pi_ivec, j) = c2_round_down(c2_ivec_cursor_index(
                                          cursor), PAGE_CACHE_SIZE);

                count = min64u(pgend, c2_ivec_cursor_index(cursor) +
                               c2_ivec_cursor_step(cursor));

                COUNT(&map->pi_ivec, j) = c2_round_up(count, PAGE_CACHE_SIZE) -
                                          INDEX(&map->pi_ivec, j);

                if (rmw) {
                        rc = map->pi_ops->pi_seg_process(map, j, pgstart);
                        if (rc != 0)
                                return rc;
                }
                count -= c2_ivec_cursor_index(cursor);
        }
        map->pi_ivec.iv_vec.v_nr = j;

        /*
         * Decides whether to undertake read-old approach or read-rest for
         * an rmw IO request.
         * By default, the segments in index vector pargrp_iomap::pi_ivec
         * are suitable for read-old approach.
         * Hence the index vector is changed only if read-rest approach
         * is selected.
         */
        if (rmw) {
                page_nr    = pargrp_iomap_fullpages_find(map);

                /* can use number of data_buf structures instead of using
                 * INDEXVEC_PAGE_NR(). */
                ro_page_nr = /* Number of pages to be read. */
                             INDEXVEC_PAGE_NR(&map->pi_ivec.iv_vec) - page_nr +
                             PARITY_UNITS_PAGE_NR(play) +
                             /* Number of pages to be written. */
                             INDEXVEC_PAGE_NR(&map->pi_ivec.iv_vec) +
                             PARITY_UNITS_PAGE_NR(play);

                rr_page_nr = /* Number of pages to be read. */
                             PAGE_NR(pgend - pgstart) - page_nr +
                             /* Number of pages to be written. */
                             INDEXVEC_PAGE_NR(&map->pi_ivec.iv_vec) +
                             PARITY_UNITS_PAGE_NR(play);

                if (rr_page_nr > ro_page_nr) {
                        rc = map->pi_ops->pi_readrest(map);
                        if (rc != 0)
                                return rc;
                }
                else
                        map->pi_read = PIR_READOLD;
        }

        rc = map->pi_ops->pi_pageattr_fill(map, ivec, rmw);
        C2_POST(ergo(rc == 0, pargrp_iomap_invariant(map)));

        return rc;
}

static int ioreq_iomaps_prepare(struct io_request *req)
{
        int                       rc;
        uint64_t                  i;
        uint64_t                  j;
        uint64_t                  grp = 0;
        /* Size of all data units in parity group. */
        uint64_t                  dtsize;
        uint64_t                  pgstart;
        uint64_t                  pgend;

        struct c2_indexvec       *ivec = &req->ir_ivec;
        struct c2_pdclust_layout *play;
        struct c2_ivec_cursor     cursor;

        C2_PRE(req != NULL);

        play   = LAYOUT_GET(req);
        dtsize = play->pl_unit_size * play->pl_N;

        for (i = 0; i < SEG_NR(ivec); ++i) {
                pgstart = GRP_ID_GET(INDEX(ivec, i), dtsize);
                pgend   = GRP_ID_GET(INDEX(ivec, i) + COUNT(ivec, i), dtsize);
                req->ir_iomap_nr += pgend - pgstart + 1;
        }

        /* req->ir_iomaps is zeroed out on allocation. */
        C2_ALLOC_ARR(req->ir_iomaps, req->ir_iomap_nr);
        if (req->ir_iomaps == NULL) {
                rc = -ENOMEM;
                C2_ADDB_ADD(&c2t1fs_addb, &io_addb_loc, io_request_failed,
                            "Failed to allocate memory for"
                            "io_request::ir_iomaps.", -ENOMEM);
                goto failed;
        }

        c2_ivec_cursor_init(&cursor, ivec);

        while (!c2_ivec_cursor_move(&cursor, 0)) {
                pgstart = GRP_ID_GET(INDEX(ivec, i), dtsize);
                pgend   = GRP_ID_GET(INDEX(ivec, i) + COUNT(ivec, i), dtsize);

                for (j = pgstart; j <= pgend; ++j, ++grp) {
                        req->ir_iomaps[grp] = c2_alloc(sizeof(struct
                                                       pargrp_iomap));
                        if (req->ir_iomaps[grp] == NULL) {
                                C2_ADDB_ADD(&c2t1fs_addb, &io_addb_loc,
                                            io_request_failed,
                                            "Failed to allocate memory for"
                                            "io_request::ir_iomaps[i].",
                                            -ENOMEM);
                                goto failed;
                        }

                        rc = pargrp_iomap_init(req->ir_iomaps[grp], req, j,
                                               PAGE_NR(dtsize));
                        if (rc != 0)
                                goto failed;

                        rc = req->ir_iomaps[grp]->pi_ops->pi_populate(req->
                                        ir_iomaps[grp], ivec, &cursor);
                        if (rc != 0)
                                goto failed;
                }
        }

        return 0;
failed:

        for (i = 0; i < req->ir_iomap_nr ; ++i) {
                if (req->ir_iomaps[i] != NULL) {
                        pargrp_iomap_fini(req->ir_iomaps[i]);
                        c2_free(req->ir_iomaps[i]);
                        req->ir_iomaps[i] = NULL;
                }
        }
        c2_free(req->ir_iomaps);
        req->ir_iomaps = NULL;

        return rc;
}

static int nw_xfer_io_prepare(struct nw_xfer_request *xfer)
{
        int                         rc;
        uint64_t                    map;
        uint64_t                    unit;
        uint64_t                    unit_size;
        uint64_t                    count = 0;
        uint64_t                    pgstart;
        uint64_t                    pgend;
        struct c2_ext               unitext;
        struct c2_ext               rext;
        struct c2_ext               vecext;
        struct io_request          *req;
        struct target_ioreq        *ti;
        struct c2_ivec_cursor       cursor;
        struct c2_pdclust_layout   *play;
        enum c2_pdclust_unit_type   unit_type;
        struct c2_pdclust_src_addr  src;
        struct c2_pdclust_tgt_addr  tgt;

        C2_PRE(nw_xfer_request_invariant(xfer));

        req       = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
        play      = LAYOUT_GET(req);
        unit_size = play->pl_unit_size;

        for (map = 0; map < req->ir_iomap_nr; ++map) {

                pgstart      = unit_size * play->pl_N * req->ir_iomaps[map]->
                               pi_grpid;
                pgend        = pgstart + unit_size * play->pl_N;
                src.sa_group = req->ir_iomaps[map]->pi_grpid;

                /* Cursor for pargrp_iomap::pi_ivec. */
                c2_ivec_cursor_init(&cursor, &req->ir_iomaps[map]->pi_ivec);

                while (!c2_ivec_cursor_move(&cursor, count)) {

                        unit = (c2_ivec_cursor_index(&cursor) - pgstart) /
                               unit_size;

                        unitext.e_start = pgstart + unit * unit_size;
                        unitext.e_end   = unitext.e_start + unit_size;

                        vecext.e_start  = c2_ivec_cursor_index(&cursor);
                        vecext.e_end    = vecext.e_start +
                                          c2_ivec_cursor_step(&cursor);

                        c2_ext_intersection(&unitext, &vecext, &rext);
                        if (!c2_ext_is_valid(&rext))
                                continue;

                        count     = c2_ext_length(&rext);
                        unit_type = c2_pdclust_unit_classify(play, unit);
                        if (unit_type == PUT_SPARE || unit_type == PUT_PARITY)
                                continue;

                        src.sa_unit = unit;
                        rc = nw_xfer_request_tioreq_map(xfer, &src, &tgt, &ti);
                        if (rc != 0)
                                goto err;

                        target_ioreq_seg_add(ti, tgt.ta_frame, rext.e_start,
                                             c2_ext_length(&rext), unit_type);
                }

                /* Maps parity units. */
                for (unit = 0; unit < play->pl_K; ++unit) {

                        src.sa_unit = play->pl_N + unit;
                        rc = nw_xfer_request_tioreq_map(xfer, &src, &tgt, &ti);
                        if (rc != 0)
                                goto err;

                        /* This call doesn't deal with global file
                         * offset and counts. */
                        target_ioreq_seg_add(ti, tgt.ta_frame, 0,
                                             play->pl_unit_size, src.sa_unit);
                }
        }

        return 0;
err:
        c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
                tioreqs_tlist_del(ti);
                target_ioreq_fini(ti);
                c2_free(ti);
                ti = NULL;
        } c2_tl_endfor;

        return rc;
}

static int nw_xfer_io_dispatch(struct nw_xfer_request *xfer)
{
        uint64_t           m;
        struct io_request *req;

        C2_PRE(nw_xfer_request_invariant(xfer));

        req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);

        for (m = 0; m < req->ir_iomap_nr; ++m) {
                if (req->ir_iomaps[m]->pi_read == PIR_READOLD ||
                    req->ir_iomaps[m]->pi_read == PIR_READREST)
                        break;
        }

        /*
         * Since c2_sm is part of io_request, for any parity group
         * which is partial, read-modify-write is done.
         */
        if (m == req->ir_iomap_nr) {
        } else {
        }

        return 0;
}

static int io_request_init(struct io_request *req, struct file *file,
                           struct iovec *iov, struct c2_indexvec *ivec,
                           enum io_req_type rw)
{
        int                 rc;
        struct c2_indexvec *riv = &req->ir_ivec;

        C2_PRE(req  != NULL);
        C2_PRE(file != NULL);
        C2_PRE(iov  != NULL);
        C2_PRE(ivec != NULL);
        C2_PRE(rw   <  IRT_TYPE_NR);

        req->ir_rc       = 0;
        req->ir_ops      = &ioreq_ops;
        req->ir_file     = file;
        req->ir_type     = rw;
        req->ir_iovec    = iov;
        req->ir_state    = IRS_INITIALIZED;
        req->ir_iomap_nr = 0;

        io_request_bob_init(req);
        nw_xfer_request_init(&req->ir_nwxfer);
        /*
        c2_sm_init(&req->ir_sm, &io_sm_conf, IRS_INITIALIZED,
                   FILE_TO_SMGROUP(req->ir_file), NULL);
                   */

        riv->iv_index       = c2_alloc(ivec->iv_vec.v_nr * sizeof(c2_bindex_t));

        riv->iv_vec.v_count = c2_alloc(ivec->iv_vec.v_nr * sizeof(c2_bcount_t));

        riv->iv_vec.v_nr    = ivec->iv_vec.v_nr;

        if (riv->iv_index == NULL || riv->iv_vec.v_count == NULL) {
                riv->iv_index != NULL ?: c2_free(riv->iv_index);

                riv->iv_vec.v_count != NULL ?: c2_free(riv->iv_vec.v_count);
                return -ENOMEM;
        }

        memcpy(riv->iv_index, ivec->iv_index, ivec->iv_vec.v_nr);
        memcpy(riv->iv_vec.v_count, ivec->iv_vec.v_count, ivec->iv_vec.v_nr);

        /* Sorts the index vector in increasing order of file offset. */
        indexvec_sort(riv);

        /*
         * Prepares io maps for each parity group/s spanned by segments
         * in index vector.
         */
        rc = req->ir_ops->iro_iomaps_prepare(req);
        if (rc != 0)
                return rc;

        C2_POST(io_request_invariant(req));
        return 0;
}

static void io_request_fini(struct io_request *req)
{
        uint64_t i;

        C2_PRE(io_request_invariant(req));

        c2_sm_fini(&req->ir_sm);
        io_request_bob_fini(req);
        req->ir_file  = NULL;
        req->ir_iovec = NULL;
        c2_free(req->ir_ivec.iv_index);
        c2_free(req->ir_ivec.iv_vec.v_count);
        for (i = 0; i < req->ir_iomap_nr; ++i) {
                pargrp_iomap_fini(req->ir_iomaps[i]);
                c2_free(req->ir_iomaps[i]);
                req->ir_iomaps[i] = NULL;
        }
        c2_free(req->ir_iomaps);
        req->ir_iomaps = NULL;
        nw_xfer_request_fini(&req->ir_nwxfer);
}

static void data_buf_init(struct data_buf *buf, void *addr, uint64_t flags)
{
        C2_PRE(buf  != NULL);
        C2_PRE(addr != NULL);

        data_buf_bob_init(buf);
        buf->db_flags = flags;
        c2_buf_init(&buf->db_buf, addr, PAGE_CACHE_SIZE);
}

static void data_buf_freepage(struct c2_buf *buf)
{
        C2_PRE(buf != NULL);

        if (buf->b_addr != NULL) {
                free_page((unsigned long)buf->b_addr);
                buf->b_addr = NULL;
                buf->b_nob  = 0;
        }
}

static void data_buf_fini(struct data_buf *buf)
{
        data_buf_bob_fini(buf);
        buf->db_flags = PA_NONE;
}

static int nw_xfer_request_tioreq_map(struct nw_xfer_request      *xfer,
                                      struct c2_pdclust_src_addr  *src,
                                      struct c2_pdclust_tgt_addr  *tgt,
                                      struct target_ioreq        **out)
{
        struct c2_fid               tfid;
        struct io_request          *req;
        struct c2_rpc_session      *session;
        struct c2_pdclust_layout   *play;

        C2_PRE(nw_xfer_request_invariant(xfer));
        C2_PRE(src != NULL);
        C2_PRE(tgt != NULL);

        req  = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
        play = LAYOUT_GET(req);

        c2_pdclust_layout_map(play, src, tgt);
        tfid    = TGT_FID_GET (req, tgt);
        session = TGT_SESS_GET(req, tfid);

        return target_ioreq_get(xfer, &tfid, session,
                                play->pl_unit_size * req->ir_iomap_nr, out);
}

static int target_ioreq_init(struct target_ioreq    *ti,
                             struct nw_xfer_request *xfer,
                             const struct c2_fid    *cobfid,
                             struct c2_rpc_session  *session,
                             uint64_t                size)
{
        C2_PRE(ti      != NULL);
        C2_PRE(xfer    != NULL);
        C2_PRE(cobfid  != NULL);
        C2_PRE(session != NULL);
        C2_PRE(size    >  0);

        iofops_tlist_init(&ti->ti_iofops);
        tioreqs_tlink_init(ti);
        target_ioreq_bob_init(ti);

        ti->ti_fid     = *cobfid;
        ti->ti_nwxfer  = xfer;
        ti->ti_session = session;

        /*
         * This value is incremented when new segments are added to the
         * index vector.
         */
        ti->ti_ivec.iv_vec.v_nr = 0;
        ti->ti_bufvec.ov_vec.v_nr = PAGE_NR(size);

        C2_ALLOC_ARR(ti->ti_ivec.iv_index, ti->ti_bufvec.ov_vec.v_nr);
        if (ti->ti_ivec.iv_index == NULL)
                goto fail;

        C2_ALLOC_ARR(ti->ti_ivec.iv_vec.v_count, ti->ti_bufvec.ov_vec.v_nr);
        if (ti->ti_ivec.iv_index == NULL)
                goto fail;

        C2_ALLOC_ARR(ti->ti_bufvec.ov_vec.v_count, ti->ti_bufvec.ov_vec.v_nr);
        if (ti->ti_bufvec.ov_vec.v_count == NULL)
                goto fail;

        C2_ALLOC_ARR(ti->ti_bufvec.ov_buf, ti->ti_bufvec.ov_vec.v_nr);
        if (ti->ti_bufvec.ov_buf == NULL)
                goto fail;

        C2_POST(target_ioreq_invariant(ti));
        return 0;
fail:
        C2_ADDB_ADD(&c2t1fs_addb, &io_addb_loc, io_request_failed,
                    "Failed to allocate memory in target_ioreq_init", -ENOMEM);
        ti->ti_ivec.iv_index != NULL ?: c2_free(ti->ti_ivec.iv_index);
        ti->ti_ivec.iv_vec.v_count != NULL ?: c2_free(ti->ti_ivec.
                                                      iv_vec.v_count);
        ti->ti_bufvec.ov_vec.v_count != NULL ?: c2_free(ti->ti_bufvec.
                                                        ov_vec.v_count);
        ti->ti_bufvec.ov_buf != NULL ?: c2_free(ti->ti_bufvec.ov_buf);
        return -ENOMEM;
}

static void target_ioreq_fini(struct target_ioreq *ti)
{
        C2_PRE(target_ioreq_invariant(ti));

        iofops_tlist_fini(&ti->ti_iofops);
        tioreqs_tlink_fini(ti);
        target_ioreq_bob_fini(ti);
        ti->ti_session = NULL;
        ti->ti_nwxfer  = NULL;

        c2_free(ti->ti_ivec.iv_index);
        c2_free(ti->ti_ivec.iv_vec.v_count);
        c2_free(ti->ti_bufvec.ov_buf);
        ti->ti_ivec.iv_index       = NULL;
        ti->ti_ivec.iv_vec.v_count = NULL;
        ti->ti_bufvec.ov_buf       = NULL;
}

static struct target_ioreq *target_ioreq_locate(struct nw_xfer_request *xfer,
                                                struct c2_fid *fid)
{
        struct target_ioreq *ti;

        c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
                if (c2_fid_eq(&ti->ti_fid, fid))
                        break;
        } c2_tl_endfor;

        return ti;
}

static int target_ioreq_get(struct nw_xfer_request *xfer,
                            struct c2_fid          *fid,
                            struct c2_rpc_session  *session,
                            uint64_t                size,
                            struct target_ioreq   **out)
{
        int                  rc = 0;
        struct target_ioreq *ti;

        C2_PRE(nw_xfer_request_invariant(xfer));
        C2_PRE(fid     != NULL);
        C2_PRE(session != NULL);
        C2_PRE(out     != NULL);

        ti = target_ioreq_locate(xfer, fid);
        if (ti == NULL) {
                ti = c2_alloc(sizeof *ti);
                if (ti == NULL) {
                        C2_ADDB_ADD(&c2t1fs_addb, &io_addb_loc,
                                    io_request_failed, "Failed to allocate"
                                    "memory for target_ioreq.", -ENOMEM);
                        return -ENOMEM;
                }
                rc = target_ioreq_init(ti, xfer, fid, session, size);
                if (rc == 0)
                        tioreqs_tlist_add(&xfer->nxr_tioreqs, ti);
                else
                        c2_free(ti);
        }
        *out = ti;
        return rc;
}

static struct data_buf *data_buf_alloc_init(enum page_attr pattr)
{
        struct data_buf *db;
        unsigned long    addr;

        addr = __get_free_page(GFP_KERNEL);
        if (addr == 0)
                return NULL;

        db = c2_alloc(sizeof(struct data_buf));
        if (db == NULL) {
                free_page(addr);
                return NULL;
        }

        data_buf_init(db, (void *)addr, pattr);
        C2_POST(data_buf_invariant(db));
        return db;
}

__attribute__((unused))
static void data_buf_dealloc(struct data_buf *buf)
{
        C2_PRE(data_buf_invariant(buf));

        data_buf_freepage(&buf->db_buf);
        data_buf_fini(buf);
        c2_free(buf);
}

static void target_ioreq_seg_add(struct target_ioreq *ti,
                                 uint64_t             frame,
                                 c2_bindex_t          gob_offset,
                                 c2_bcount_t          count,
                                 uint64_t             unit)
{
        uint64_t                   seg;
        uint64_t                   dtsize;
        c2_bindex_t                toff;
        c2_bindex_t                goff;
        c2_bindex_t                pgstart;
        c2_bindex_t                pgend;
        struct io_request         *req;
        struct pargrp_iomap       *map;
        struct c2_pdclust_layout  *play;
        enum c2_pdclust_unit_type unit_type;

        C2_PRE(target_ioreq_invariant(ti));

        req     = bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
                         &ioreq_bobtype);
        play    = LAYOUT_GET(req);
        toff    = TGT_OFFSET_GET(frame, play->pl_unit_size, gob_offset);
        dtsize  = play->pl_unit_size * play->pl_N;
        req->ir_ops->iro_iomap_locate(req, GRP_ID_GET(gob_offset, dtsize),
                                      &map);
        C2_ASSERT(map != NULL);
        pgstart = toff;
        goff    = gob_offset;

        unit_type = c2_pdclust_unit_classify(play, unit);
        C2_ASSERT(unit_type == PUT_DATA || unit_type == PUT_PARITY);

        while (pgstart < toff + count) {
                pgend = min64u(pgstart + PAGE_CACHE_SIZE, toff + count);
                seg   = SEG_NR(&ti->ti_ivec);

                INDEX(&ti->ti_ivec, seg) = pgstart;
                COUNT(&ti->ti_ivec, seg) = pgend - pgstart;

                ti->ti_bufvec.ov_vec.v_count[seg] = pgend - pgstart;
                if (unit_type == PUT_DATA)
                        ti->ti_bufvec.ov_buf[seg] = map->pi_databufs[PAGE_ID(
                                                    goff - (dtsize *
                                                    map->pi_grpid))]->db_buf.
                                                    b_addr;
                else
                        ti->ti_bufvec.ov_buf[seg] = map->pi_paritybufs[unit %
                                                    play->pl_N]->db_buf.b_addr;

                goff += COUNT(&ti->ti_ivec, seg);
                ++SEG_NR(&ti->ti_ivec);
                pgstart = pgend;
        }
}

__attribute__((unused))
static int io_req_fop_init(struct io_req_fop   *fop,
                           struct target_ioreq *ti)
{
        int                rc;
        struct io_request *req;

        C2_PRE(fop != NULL);
        C2_PRE(ti  != NULL);

        iofops_tlink_init(fop);
        io_req_fop_bob_init(fop);
        fop->irf_tioreq = ti;

        req = bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
                     &ioreq_bobtype);
        rc  = c2_io_fop_init(&fop->irf_iofop, REQ_TO_FOP_TYPE(req));

        C2_POST(io_req_fop_invariant(fop));
        return rc;
}

__attribute__((unused))
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
        fop->irf_tioreq = NULL;
}

/**
 * This function can be used by the ioctl which supports fully vectored
 * scatter-gather IO. The caller is supposed to provide an index vector
 * aligned with user buffers in struct iovec array.
 * This function is also used by file->f_op->aio_{read/write} path.
 */
ssize_t c2t1fs_aio(struct kiocb       *kcb,
                   const struct iovec *iov,
                   struct c2_indexvec *ivec,
                   enum io_req_type    rw)
{
        int                rc;
        ssize_t            count;
        struct io_request *req;

        C2_PRE(kcb  != NULL);
        C2_PRE(iov  != NULL);
        C2_PRE(ivec != NULL);

        req = c2_alloc(sizeof *req);
        if (req == NULL) {
                C2_ADDB_ADD(&c2t1fs_addb, &io_addb_loc, io_request_failed,
                            "Failed to allocate memory for io_request.",
                            -ENOMEM);
                return -ENOMEM;
        }

        rc = io_request_init(req, kcb->ki_filp, (struct iovec *)iov, ivec, rw);
        if (rc != 0)
                return rc;
        
        req->ir_nwxfer.nxr_ops->nxo_prepare(&req->ir_nwxfer);
        req->ir_nwxfer.nxr_ops->nxo_dispatch(&req->ir_nwxfer);
        count = req->ir_nwxfer.nxr_bytes;
        io_request_fini(req);
        c2_free(req);

        return count;
}

__attribute__((unused))
static int io_reading_state(struct c2_sm *sm)
{
        struct io_request *req;

        C2_PRE(sm != NULL);

        req = bob_of(sm, struct io_request, ir_sm, &ioreq_bobtype);
        C2_ASSERT(io_request_invariant(req));


        /*
         * Stub code!
         * Done in order to compile the code.
         */
        return 0;
}

__attribute__((unused))
static int io_writing_state(struct c2_sm *sm)
{
        struct io_request *req;

        C2_PRE(sm != NULL);

        req = bob_of(sm, struct io_request, ir_sm, &ioreq_bobtype);
        C2_ASSERT(io_request_invariant(req));


        /*
         * Stub code!
         * Done in order to compile the code.
         */
        return 0;
}

static struct c2_indexvec *indexvec_create(unsigned long       seg_nr,
                                           const struct iovec *iov,
                                           loff_t              pos)
{
        uint32_t            i;
        struct c2_indexvec *ivec;

        /*
         * Apparently, we need to use a new API to process io request
         * which can accept c2_indexvec so that it can be reused by
         * the ioctl which provides fully vectored scatter-gather IO
         * to cluster library users.
         * For that, we need to prepare a c2_indexvec and supply it
         * to this function.
         */
        ivec = c2_alloc(sizeof *ivec);
        if (ivec == NULL)
                /* Log addb message. */
                return NULL;

        ivec->iv_vec.v_nr    = seg_nr;
        ivec->iv_index       = (c2_bindex_t*)c2_alloc(seg_nr *
                                (sizeof(c2_bindex_t)));
        ivec->iv_vec.v_count = (c2_bcount_t*)c2_alloc(seg_nr *
                                (sizeof(c2_bcount_t)));

        if (ivec->iv_index == NULL || ivec->iv_vec.v_count == NULL) {
                /* Log addb message. */
                ivec->iv_index != NULL ?: c2_free(ivec->iv_index);
                ivec->iv_vec.v_count != NULL ?: c2_free(ivec->iv_vec.v_count);
                c2_free(ivec);
                return NULL;
        }

        for (i = 0; i < ivec->iv_vec.v_nr; ++i) {
                ivec->iv_index[i] = pos;
                ivec->iv_vec.v_count[i] = iov[i].iov_len;
                pos += iov[i].iov_len;
        }
        C2_ASSERT(c2_vec_count(&ivec->iv_vec) > 0);

        return ivec;
}

static ssize_t file_aio_write(struct kiocb       *kcb,
                              const struct iovec *iov,
                              unsigned long       seg_nr,
                              loff_t              pos)
{
        int                 rc;
        size_t              count = 0;
        size_t              saved_count;
        struct c2_indexvec *ivec;

	rc = generic_segment_checks(iov, &seg_nr, &count, VERIFY_READ);
	if (rc != 0)
                return 0;

	saved_count = count;
	rc = generic_write_checks(kcb->ki_filp, &pos, &count, 0);
	if (rc != 0 || count == 0)
                return 0;

	if (count != saved_count)
		seg_nr = iov_shorten((struct iovec *)iov, seg_nr, count);

        ivec = indexvec_create(seg_nr, iov, pos);
        if (ivec == NULL)
                /* Log addb message. */
                return 0;

        return c2t1fs_aio(kcb, iov, ivec, IRT_WRITE);
}

static ssize_t file_aio_read(struct kiocb       *kcb,
                             const struct iovec *iov,
                             unsigned long       seg_nr,
                             loff_t              pos)
{
        ssize_t             count = 0;
        ssize_t             res;
        struct c2_indexvec *ivec;

        res = generic_segment_checks(iov, &seg_nr, &count, VERIFY_WRITE);
        if (res != 0)
                return res;

        ivec = indexvec_create(seg_nr, iov, pos);
        if (ivec == NULL)
                return 0;

        return c2t1fs_aio(kcb, iov, ivec, IRT_READ);
}

const struct file_operations c2t1fs_reg_file_operations = {
	.llseek    = generic_file_llseek,   /* provided by linux kernel */
	.aio_read  = file_aio_read,
	.aio_write = file_aio_write,
	.read      = do_sync_read,          /* provided by linux kernel */
	.write     = do_sync_write,         /* provided by linux kernel */
};
