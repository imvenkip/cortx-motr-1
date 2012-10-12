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
#include <linux/fs.h>       /* struct file_operations */

#include "lib/memory.h"     /* c2_alloc(), c2_free() */
#include "lib/misc.h"       /* c2_round_{up/down} */
#include "lib/bob.h"        /* c2_bob_type */
#include "lib/ext.h"        /* c2_ext */
#include "lib/bitmap.h"     /* c2_bitmap */
#include "lib/arith.h"      /* min_type() */
#include "layout/pdclust.h" /* C2_PUT_*, c2_layout_to_pdl(),
			     * c2_pdclust_instance_map */
#include "c2t1fs/linux_kernel/c2t1fs.h" /* c2t1fs_sb */
#include "lib/bob.h"        /* c2_bob_type */
#include "ioservice/io_fops.h"    /* c2_io_fop */
#include "ioservice/io_fops_ff.h" /* c2_fop_cob_rw */
#include "colibri/magic.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"      /* C2_LOG, C2_ENTRY */

const struct inode_operations c2t1fs_reg_inode_operations;
void iov_iter_advance(struct iov_iter *i, size_t bytes);

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
   Then data is changed as per the user request and later sent for write to
   server.
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
   spanned by the incoming IO requests. For write requests, The data pages
   which are completely spanned will be populated by copying data
   from user-space buffers.

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
   @n delta - @f$Parity(D_o, D_n)@f$   (Difference between 2 versions of
                                        data block)
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
   as per the user request.

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
   such buffers do not change parity.

   - Ergo, parity will be calculated from valid data blocks only (falling
   within end-of-file)

   Since sending network requests for IO is under control of client, these
   changes can be accommodated for irrespective of layout type.
   This way, the wastage of storage space in small IO can be restricted to
   block granularity.

   @todo In future, optimizations could be done in order to speed up small file
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
   struct c2t1fs_sb.
   All c2_sm structures are associated with this group.
   @see struct c2t1fs_sb.

   io_req_state - Represents state of IO request call.
   c2_sm_state_descr structure will be defined for description of all
   states mentioned below.

   io_req_type - Represents type of IO request.

   @todo IO types like fault IO are not supported yet.

   io_request_ops - Operation vector for struct io_request.

   nw_xfer_request - Structure representing the network transfer part for
   an IO request. This structure keeps track of request IO fops as well as
   individual completion callbacks and status of whole request.
   Typically, all IO requests are broken down into multiple fixed-size
   requests.

   nw_xfer_state - State of struct nw_xfer_request.

   nw_xfer_ops - Operation vector for struct nw_xfer_request.

   pargrp_iomap - Represents a map of io extents in given parity group.
   Struct io_request contains as many pargrp_iomap structures as the number
   of parity groups spanned by original index vector.
   Typically, the segments from pargrp_iomap::pi_ivec are round_{up/down}
   to nearest page boundary for segments from io_request::ir_ivec.

   pargrp_iomap_rmwtype - Type of read approach used by pargrp_iomap
   in case of rmw IO.

   pargrp_iomap_ops - Operation vector for struct pargrp_iomap.

   data_buf - Represents a simple data buffer wrapper object. The embedded
   c2_buf::b_addr points to a kernel page or a user-space buffer in case of
   direct IO.

   target_ioreq - Collection of IO extents and buffers, directed towards each
   of the component objects in a parity group.
   These structures are created by struct io_request dividing the incoming
   struct iovec into members of parity group.

   ast thread - A thread will be maintained per super block instance
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

    - the "ast" thread to execute ASTs from io requests and to wake up
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
           if (!sb->csb_active && c2_atomic64_get(&sb->csb_pending_io_nr) == 0){
                   c2_chan_signal(&sb->csb_iowait);
                   break;
           }
   }

   @endcode

   So, while c2t1fs is mounted, the thread will keep running in loop waiting
   for ASTs.

   When unmount is triggered, new io requests will be returned with an error
   code as c2t1fs_sb::csb_active flag is reset.
   For pending io requests, the ast thread will wait until callbacks
   from all outstanding io requests are acknowledged and executed.

   Once all pending io requests are dealt with, the ast thread will exit
   waking up the unmount thread.

   Abort is not supported at the moment in c2t1fs io code as it needs same
   functionality from layer beneath.

   When bottom halves for c2_sm_ast structures are run, it updates
   target_ioreq::tir_rc and target_ioreq::tir_bytes with data from
   IO reply fop.
   Then the bottom half decrements nw_xfer_request::nxr_iofops_nr,
   number of IO fops.  When this count reaches zero, io_request::ir_sm
   changes its state.

   A callback function will be used to associate with c2_rpc_item::ri_ops::
   rio_replied(). This callback will be invoked once rpc layer receives the
   reply rpc item. This callback will post the io_req_fop::irf_ast and
   will thus wake up the thread which executes the ASTs.

   io_req_fop - Represents a wrapper over generic IO fop and its callback
   to keep track of such IO fops issued by same nw_xfer_request.

   io_rpc_item_cb - Callback used to receive fop reply events.

   io_bottom_half - Bottom-half function for IO request.

   Magic value to verify sanity of struct io_request, struct nw_xfer_request,
   struct target_ioreq and struct io_req_fop.
   The magic values are used along with static c2_bob_type structures to
   assert run-time type identification.

   @subsubsection rmw-lspec-sub1 Subcomponent Subroutines

   In order to satisfy the requirement of an ioctl API for fully vectored
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

   @ref io_request_invariant - Invariant for structure io_request.

   @ref nw_xfer_request_invariant - Invariant for structure nw_xfer_request.

   @ref data_buf_invariant_nr - A helper function to invoke invariant() for
   all data_buf structures in array pargrp_iomap::tir_databufs.
   It is on similar lines of c2_tl_forall() API.

   @ref target_ioreq_invariant - Invariant for structure target_ioreq.

   @ref pargrp_iomap_invariant - Invariant for structure pargrp_iomap.

   @ref pargrp_iomap_invariant_nr - A helper function to invoke
   pargrp_iomap_invariant() for all such structures in io_request::ir_iomaps.

   @ref data_buf_invariant - Invariant for structure data_buf.

   @ref io_req_fop_invariant - Invariant for structure io_req_fop.

   @ref io_request_init - Initializes a newly allocated io_request structure.

   @ref io_request_fini - Finalizes the io_request structure.

   @ref nw_xfer_req_prepare - Creates and populates target_ioreq structures
   for each member in the parity group.
   Each target_ioreq structure will allocate necessary pages to store
   file data and will create IO fops out of it.

   @ref nw_xfer_req_dispatch - Dispatches the network transfer request and
   does asynchronous wait for completion. Typically, IO fops from all
   target_ioreq::tir_iofops contained in nw_xfer_request::nxr_tioreqs list
   are dispatched at once.
   The network transfer request is considered as complete when callbacks for
   all IO fops from all target_ioreq structures are acknowledged and
   processed.

   @ref nw_xfer_req_complete - Does post processing for network request
   completion which is usually notifying struct io_request about completion.

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
   and reading the rest of parity group units is more economical (in terms of io
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

        /**
         * pargrp_iomap::pi_ivec is shared for read and write state for an
         * rmw IO request. Read IO can not go past EOF but write IO can.
         * In such case, changing pargrp_iomap::pi_ivec is costly because
         * it has to be reassessed and changed after read IO is complete.
         * Instead, we use a flag to notify last valid page of a read
         * request and store the count of bytes in last page in
	 * data_buf::db_buf::b_nob.
         */
        PA_READ_EOF        = (1 << 4),

        PA_NR              = 6,
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
        /**
         * Prepares subsequent target_ioreq objects as needed and populates
         * target_ioreq::ir_ivec and target_ioreq::ti_bufvec.
	 * @pre   nw_xfer_request_invariant(xfer).
	 * @post  !tioreqs_list_is_empty(xfer->nxr_tioreqs).
         */
        int  (*nxo_prepare)    (struct nw_xfer_request  *xfer);

        /**
         * Does post processing of a network transfer request.
         * Primarily all IO fops submitted by this network transfer request
         * are finalized so that new fops can be created for same request.
	 * @param rmw  Boolean telling if current IO request is rmw or not.
	 * @pre   nw_xfer_request_invariant(xfer).
	 * @post  xfer->nxr_state == NXS_COMPLETE.
         */
        void (*nxo_complete)   (struct nw_xfer_request  *xfer,
                                bool                     rmw);

        /**
         * Dispatches the IO fops created by all member target_ioreq objects
         * and sends them to server for processing.
         * The whole process is done in an asynchronous manner and does not
         * block the thread during processing.
	 * @pre   nw_xfer_request_invariant(xfer).
	 * @post  xfer->nxr_state == NXS_INFLIGHT.
         */
        int  (*nxo_dispatch)   (struct nw_xfer_request  *xfer);

        /**
         * Locates or creates a target_iroeq object which maps to the given
         * target address.
	 * @param src  Source address comprising of parity group number
	 * and unit number in parity group.
	 * @param tgt  Target address comprising of frame number and
	 * target object number.
	 * @param out  Out parameter containing target_ioreq object.
	 * @pre   nw_xfer_request_invariant(xfer).
         */
        int  (*nxo_tioreq_map) (struct nw_xfer_request      *xfer,
                                struct c2_pdclust_src_addr  *src,
                                struct c2_pdclust_tgt_addr  *tgt,
                                struct target_ioreq        **out);
};

/**
 * Structure representing the network transfer part for an IO request.
 * This structure keeps track of request IO fops as well as individual
 * completion callbacks and status of whole request.
 * Typically, all IO requests are broken down into multiple fixed-size
 * requests.
 */
struct nw_xfer_request {
        /** Holds C2_T1FS_NWREQ_MAGIC */
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
	IRS_FAILED,
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
        /**
         * Prepares pargrp_iomap structures for the parity groups spanned
         * by io_request::ir_ivec.
	 * @pre   req->ir_iomaps == NULL && req->ir_iomap_nr == 0.
	 * @post  req->ir_iomaps != NULL && req->ir_iomap_nr > 0.
         */
        int (*iro_iomaps_prepare) (struct io_request  *req);

        /**
         * Copies data from/to user-space to/from kernel-space according
         * to given direction and page filter.
	 * @param dir    Direction of copy.
	 * @param filter Only copy pages that match the filter.
	 * @pre   io_request_invariant(req).
         */
        int (*iro_user_data_copy) (struct io_request  *req,
                                   enum copy_direction dir,
                                   enum page_attr      filter);

        /**
         * Locates the pargrp_iomap structure corresponding to given
         * parity group id from io_request::ir_iomaps.
         * For a valid parity group id, a pargrp_iomap structure must
         * be present.
	 * @param grpid Parity group id.
	 * @param map   Out paramter to return pargrp_iomap.
         */
        void (*iro_iomap_locate)  (struct io_request    *req,
                                   uint64_t              grpid,
                                   struct pargrp_iomap **map);

        /**
         * Recalculates parity for all pargrp_iomap structures in
         * given io_request.
         * Basically, invokes parity_recalc() routine for every
         * pargrp_iomap in io_request::ir_iomaps.
	 * @pre  io_request_invariant(req) && req->ir_type == IRT_WRITE.
	 * @post io_request_invariant(req).
         */
        int (*iro_parity_recalc)  (struct io_request  *req);

        /**
         * Handles the state transition, status of request and the
         * intermediate copy_{from/to}_user.
	 * @pre io_request_invariant(req).
         */
        int (*iro_iosm_handle)    (struct io_request  *req);
};

/**
 * io_request - Represents an IO request call. It contains the IO extent,
 * struct nw_xfer_request and the state of IO request. This structure is
 * primarily used to track progress of an IO request.
 * The state transitions of io_request structure are handled by c2_sm
 * structure and its support for chained state transitions.
 */
struct io_request {
        /** Holds C2_T1FS_IOREQ_MAGIC */
        uint64_t                     ir_magic;

        int                          ir_rc;

        /**
         * struct file* can point to c2t1fs inode and hence its
         * associated c2_fid structure.
         */
        struct file                 *ir_file;

        /**
	 * Index vector describing file extents and their lengths.
	 * This vector is in sync with the array of iovec structures
	 * below.
	 */
        struct c2_indexvec           ir_ivec;

        /**
         * Array of struct pargrp_iomap pointers.
         * Each pargrp_iomap structure describes the part of parity group
         * spanned by segments from ::ir_ivec.
         */
        struct pargrp_iomap        **ir_iomaps;

        /** Number of pargrp_iomap structures. */
        uint64_t                     ir_iomap_nr;

        /**
         * Array of iovec structures containing user-space buffers.
         * It is used as is since using a new structure would require
         * conversion.
         */
        struct iovec                *ir_iovec;

        /** Async state machine to handle state transitions and callbacks. */
        struct c2_sm                 ir_sm;

        enum io_req_type             ir_type;

        const struct io_request_ops *ir_ops;

        struct nw_xfer_request       ir_nwxfer;
};

/**
 * Represents a simple data buffer wrapper object. The embedded
 * c2_buf::b_addr points to a kernel page.
 */
struct data_buf {
        /** Holds C2_T1FS_DTBUF_MAGIC. */
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
 * Represents a map of io extents in a given parity group. Struct io_request
 * contains as many pargrp_iomap structures as the number of parity groups
 * spanned by io_request::ir_ivec.
 * Typically, the segments from pargrp_iomap::pi_ivec are round_{up/down}
 * to nearest page boundary for respective segments from
 * io_requets::ir_ivec.
 */
struct pargrp_iomap {
        /** Holds C2_T1FS_PGROUP_MAGIC. */
        uint64_t                        pi_magic;

        /** Parity group id. */
        uint64_t                        pi_grpid;

        /**
         * Part of io_request::ir_ivec which falls in ::pi_grpid
         * parity group.
         * All segments are in increasing order of file offset.
	 * Segment counts in this index vector are multiple of
	 * PAGE_CACHE_SIZE.
         */
        struct c2_indexvec              pi_ivec;

        /**
         * Type of read approach used only in case of rmw IO.
         * Either read-old or read-rest.
         */
        enum pargrp_iomap_rmwtype       pi_rtype;

        /**
         * Data units in a parity group.
         * Unit size should be multiple of PAGE_CACHE_SIZE.
         * This is basically a matrix with
         * - number of rows    = Unit_size / PAGE_CACHE_SIZE and
         * - number of columns = N.
         * Each element of matrix is worth PAGE_CACHE_SIZE;
         * A unit size worth of data holds a contiguous chunk of file data.
         * The file offset grows vertically first and then to the next
         * data unit.
         */
        struct data_buf              ***pi_databufs;

        /**
         * Parity units in a parity group.
         * Unit size should be multiple of PAGE_CACHE_SIZE.
         * This is a matrix with
         * - number of rows    = Unit_size / PAGE_CACHE_SIZE and
         * - number of columns = K.
         * Each element of matrix is worth PAGE_CACHE_SIZE;
         */
        struct data_buf              ***pi_paritybufs;

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
         * pargrp_iomap::pi_rtype will be set to PIR_READOLD or
         * PIR_READREST accordingly.
	 * @param ivec   Source index vector from which pargrp_iomap::pi_ivec
	 * will be populated. Typically, this is io_request::ir_ivec.
	 * @param cursor Index vector cursor associated with ivec.
	 * @pre iomap != NULL && ivec != NULL &&
	 * c2_vec_count(&ivec->iv_vec) > 0 && cursor != NULL &&
	 * c2_vec_count(&iomap->iv_vec) == 0
	 * @post  c2_vec_count(&iomap->iv_vec) > 0 &&
	 * iomap->pi_databufs != NULL.
         */
        int (*pi_populate)  (struct pargrp_iomap      *iomap,
                             const struct c2_indexvec *ivec,
                             struct c2_ivec_cursor    *cursor);

        /**
         * Returns true if the given segment is spanned by existing segments
         * in pargrp_iomap::pi_ivec.
	 * @param index Starting index of incoming segment.
	 * @param count Count of incoming segment.
	 * @pre   pargrp_iomap_invariant(iomap).
	 * @ret   true if segment is found in pargrp_iomap::pi_ivec,
	 * false otherwise.
         */
        bool (*pi_spans_seg) (struct pargrp_iomap *iomap,
                              c2_bindex_t          index,
			      c2_bcount_t          count);

        /**
         * Changes pargrp_iomap::pi_ivec to suit read-rest approach
         * for an RMW IO request.
	 * @pre  pargrp_iomap_invariant(iomap).
	 * @post pargrp_iomap_invariant(iomap).
         */
        int (*pi_readrest)   (struct pargrp_iomap *iomap);

        /**
         * Finds out the number of pages _completely_ spanned by incoming
         * io vector. Used only in case of read-modify-write IO.
         * This is needed in order to decide the type of read approach
         * {read_old, read_rest} for the given parity group.
	 * @pre pargrp_iomap_invariant(map).
	 * @ret Number of pages _completely_ spanned by pargrp_iomap::pi_ivec.
         */
        uint64_t (*pi_fullpages_find) (struct pargrp_iomap *map);

        /**
         * Process segment pointed to by segid in pargrp_iomap::pi_ivec and
         * allocate data_buf structures correspondingly.
	 * It also populates data_buf::db_flags for pargrp_iomap::pi_databufs.
	 * @param segid Segment id which needs to be processed.
	 * @param rmw   If given pargrp_iomap structure needs rmw.
	 * @pre   map != NULL.
	 * @post  pargrp_iomap_invariant(map).
	 *
         */
        int (*pi_seg_process)    (struct pargrp_iomap *map,
                                  uint64_t             segid,
                                  bool                 rmw);

        /**
         * Process the data buffers in pargrp_iomap::pi_databufs
         * when read-old approach is chosen.
	 * Auxiliary buffers are allocated here.
	 * @pre pargrp_iomap_invariant(map) && map->pi_rtype == PIR_READOLD.
         */
        int (*pi_readold_auxbuf_alloc) (struct pargrp_iomap *map);

        /**
	 * Recalculates parity for given pargrp_iomap.
	 * @pre map != NULL && map->pi_ioreq->ir_type == IRT_WRITE.
	 */
        int (*pi_parity_recalc)   (struct pargrp_iomap *map);

        /**
         * Allocates data_buf structures for pargrp_iomap::pi_paritybufs
         * and populate db_flags accordingly.
	 * @pre   map->pi_paritybufs == NULL.
	 * @post  map->pi_paritybufs != NULL && pargrp_iomap_invariant(map).
         */
        int (*pi_paritybufs_alloc)(struct pargrp_iomap *map);
};

/** Operations vector for struct target_ioreq. */
struct target_ioreq_ops {
        /**
         * Adds an io segment to index vector and buffer vector in
         * target_ioreq structure.
	 * @param frame      Frame number of target object.
	 * @param gob_offset Offset in global file.
	 * @param count      Number of bytes in this segment.
	 * @param unit       Unit id in parity group.
	 * @pre   ti != NULL && count > 0.
	 * @post  c2_vec_count(&ti->ti_ivec.iv_vec) > 0.
         */
        void (*tio_seg_add)        (struct target_ioreq *ti,
                                    uint64_t             frame,
                                    c2_bindex_t          gob_offset,
                                    c2_bcount_t          count,
                                    uint64_t             unit);

        /**
	 * Prepares io fops from index vector and buffer vector.
	 * This API uses rpc bulk API to store net buffer descriptors
	 * in IO fops.
	 * @pre   iofops_tlist_is_empty(ti->ti_iofops).
	 * @post  !iofops_tlist_is_empty(ti->ti_iofops).
	 */
        int  (*tio_iofops_prepare) (struct target_ioreq *ti);
};

/**
 * Collection of IO extents and buffers, directed towards each
 * of target objects (data_unit / parity_unit) in a parity group.
 * These structures are created by struct io_request dividing the incoming
 * struct iovec into members of a parity group.
 */
struct target_ioreq {
        /** Holds C2_T1FS_TIOREQ_MAGIC */
        uint64_t                       ti_magic;

        /** Fid of component object. */
        struct c2_fid                  ti_fid;

        /** Status code for io operation done for this target_ioreq. */
        int                            ti_rc;

        /** Number of bytes read/written for this target_ioreq. */
        uint64_t                       ti_bytes;

        /** List of io_req_fop structures issued on this target object. */
        struct c2_tl                   ti_iofops;

        /** Resulting IO fops are sent on this rpc session. */
        struct c2_rpc_session         *ti_session;

        /** Linkage to link in to nw_xfer_request::nxr_tioreqs list. */
        struct c2_tlink                ti_link;

        /**
         * Index vector containing IO segments with cob offsets and
         * their length.
	 * Each segment in this vector is worth PAGE_CACHE_SIZE.
         */
        struct c2_indexvec             ti_ivec;

        /**
	 * Buffer vector corresponding to index vector above.
	 * This buffer is in sync with ::ti_ivec.
	 */
        struct c2_bufvec               ti_bufvec;

        /** Array of page attributes. */
        enum page_attr                *ti_pageattrs;

        /** target_ioreq operation vector. */
        const struct target_ioreq_ops *ti_ops;

        /* Backlink to parent structure nw_xfer_request. */
        struct nw_xfer_request        *ti_nwxfer;
};

/**
 * Represents a wrapper over generic IO fop and its callback
 * to keep track of such IO fops issued by the same target_ioreq structure.
 *
 * When bottom halves for c2_sm_ast structures are run, it updates
 * target_ioreq::ti_rc and target_ioreq::ti_bytes with data from
 * IO reply fop.
 * Then it decrements nw_xfer_request::nxr_iofop_nr, number of IO fops.
 * When this count reaches zero, io_request::ir_sm changes its state.
 */
struct io_req_fop {
        /** Holds C2_T1FS_IOFOP_MAGIC */
        uint64_t                irf_magic;

        /**
         * In-memory handle for IO fop.
         */
        struct c2_io_fop        irf_iofop;

        /** Callback per IO fop. */
        struct c2_sm_ast        irf_ast;

        /** Linkage to link in to target_ioreq::ti_iofops list. */
        struct c2_tlink         irf_link;

        /**
         * Backlink to target_ioreq object where rc and number of bytes
         * are updated.
         */
        struct target_ioreq    *irf_tioreq;
};

C2_TL_DESCR_DEFINE(tioreqs, "List of target_ioreq objects", static,
                   struct target_ioreq, ti_link, ti_magic,
                   C2_T1FS_TIOREQ_MAGIC, C2_T1FS_NWREQ_MAGIC);

C2_TL_DESCR_DEFINE(iofops, "List of IO fops", static,
                   struct io_req_fop, irf_link, irf_magic,
                   C2_T1FS_IOFOP_MAGIC, C2_T1FS_TIOREQ_MAGIC);

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

static struct c2_bob_type ioreq_bobtype = {
        .bt_name         = "io_request_bobtype",
        .bt_magix_offset = offsetof(struct io_request, ir_magic),
        .bt_magix        = C2_T1FS_IOREQ_MAGIC,
        .bt_check        = NULL,
};

static struct c2_bob_type pgiomap_bobtype = {
        .bt_name         = "pargrp_iomap_bobtype",
        .bt_magix_offset = offsetof(struct pargrp_iomap, pi_magic),
        .bt_magix        = C2_T1FS_PGROUP_MAGIC,
        .bt_check        = NULL,
};

static struct c2_bob_type nwxfer_bobtype = {
        .bt_name         = "nw_xfer_request_bobtype",
        .bt_magix_offset = offsetof(struct nw_xfer_request, nxr_magic),
        .bt_magix        = C2_T1FS_NWREQ_MAGIC,
        .bt_check        = NULL,
};

static struct c2_bob_type dtbuf_bobtype = {
        .bt_name         = "data_buf_bobtype",
        .bt_magix_offset = offsetof(struct data_buf, db_magic),
        .bt_magix        = C2_T1FS_DTBUF_MAGIC,
        .bt_check        = NULL,
};

/*
 * These are used as macros since they are used as lvalues which is
 * not possible by using static inline functions.
 */
#define INDEX(ivec, i) ((ivec)->iv_index[(i)])

#define COUNT(ivec, i) ((ivec)->iv_vec.v_count[(i)])

#define SEG_NR(vec)    ((vec)->iv_vec.v_nr)

static inline struct inode *file_to_inode(struct file *file)
{
        return file->f_dentry->d_inode;
}

static inline struct c2t1fs_inode *file_to_c2inode(struct file *file)
{
        return C2T1FS_I(file_to_inode(file));
}

static inline struct c2_fid *file_to_fid(struct file *file)
{
        return &file_to_c2inode(file)->ci_fid;
}

static inline struct c2t1fs_sb *file_to_sb(struct file *file)
{
        return C2T1FS_SB(file_to_inode(file)->i_sb);
}

static inline struct c2_sm_group *file_to_smgroup(struct file *file)
{
        return &file_to_sb(file)->csb_iogroup;
}

static inline uint64_t page_nr(c2_bcount_t size)
{
        return size >> PAGE_CACHE_SHIFT;
}

static inline struct c2_layout_instance *layout_instance(struct io_request *req)
{
        return file_to_c2inode(req->ir_file)->ci_layout_instance;
}

static inline struct c2_pdclust_instance *pdlayout_instance(struct
                c2_layout_instance *li)
{
        return c2_layout_instance_to_pdi(li);
}

static inline struct c2_pdclust_layout *pdlayout_get(struct io_request *req)
{
        return pdlayout_instance(layout_instance(req))->pi_layout;
}

static inline uint32_t layout_n(struct c2_pdclust_layout *play)
{
        return play->pl_attr.pa_N;
}

static inline uint32_t layout_k(struct c2_pdclust_layout *play)
{
        return play->pl_attr.pa_K;
}

static inline uint64_t layout_unit_size(struct c2_pdclust_layout *play)
{
        return play->pl_attr.pa_unit_size;
}

static inline uint64_t parity_units_page_nr(struct c2_pdclust_layout *play)
{
        return page_nr(layout_unit_size(play)) * layout_k(play);
}

static inline uint64_t indexvec_page_nr(struct c2_vec *vec)
{
        return page_nr(c2_vec_count(vec));
}

static inline uint64_t data_size(struct c2_pdclust_layout *play)
{
        return layout_n(play) * layout_unit_size(play);
}

static inline struct c2_parity_math *parity_math(struct io_request *req)
{
        return &pdlayout_instance(layout_instance(req))->pi_math;
}

static inline uint64_t group_id(c2_bindex_t index, c2_bcount_t dtsize)
{
        return index / dtsize;
}

static inline uint64_t target_offset(uint64_t    frame,
                                     uint64_t    unit_size,
                                     c2_bindex_t gob_offset)
{
        return frame * unit_size + (gob_offset % unit_size);
}

static inline struct c2_fid target_fid(struct io_request          *req,
                                       struct c2_pdclust_tgt_addr *tgt)
{
        return c2t1fs_cob_fid(file_to_c2inode(req->ir_file), tgt->ta_obj + 1);
}

static inline struct c2_rpc_session *target_session(struct io_request *req,
                                                    struct c2_fid      tfid)
{
        return c2t1fs_container_id_to_session(file_to_sb(req->ir_file),
                                              tfid.f_container);
}

static inline uint64_t page_id(c2_bindex_t offset)
{
        return offset >> PAGE_CACHE_SHIFT;
}

static inline uint32_t data_row_nr(struct c2_pdclust_layout *play)
{
        return page_nr(layout_unit_size(play));
}

static inline uint32_t data_col_nr(struct c2_pdclust_layout *play)
{
        return layout_n(play);
}

static inline uint32_t parity_col_nr(struct c2_pdclust_layout *play)
{
        return layout_k(play);
}

static inline uint32_t parity_row_nr(struct c2_pdclust_layout *play)
{
        return data_row_nr(play);
}

/**
 * Returns the position of page in matrix of data buffers.
 * @param map - Concerned parity group data structure.
 * @param index - Current file index.
 * @param row - Out parameter for row id.
 * @param col - Out parameter for column id.
 * */
static void page_pos_get(struct pargrp_iomap *map, c2_bindex_t index,
                         uint32_t *row, uint32_t *col)
{
        uint64_t                  pg_id;
        struct c2_pdclust_layout *play;

        C2_PRE(map != NULL);
        C2_PRE(row != NULL);
        C2_PRE(col != NULL);

        play = pdlayout_get(map->pi_ioreq);

        pg_id = page_id(index - data_size(play) * map->pi_grpid);
        *row  = pg_id % data_row_nr(play);
        *col  = pg_id / data_row_nr(play);
}

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

static void io_rpc_item_cb(struct c2_rpc_item *item);

/**
 * io_rpc_item_cb can not be directly invoked from io fops code since it
 * leads to build dependency of ioservice code over kernel-only code (c2t1fs).
 * Hence, a new c2_rpc_item_ops structure is used for fops dispatched
 * by c2t1fs io requests.
 */
static const struct c2_rpc_item_ops c2t1fs_item_ops = {
        .rio_sent    = NULL,
        .rio_replied = io_rpc_item_cb,
        .rio_free    = c2_io_item_free,
};

static bool nw_xfer_request_invariant(const struct nw_xfer_request *xfer);

static int  nw_xfer_io_prepare  (struct nw_xfer_request *xfer);
static void nw_xfer_req_complete(struct nw_xfer_request *xfer,
                                 bool                    rmw);
static int  nw_xfer_req_dispatch(struct nw_xfer_request *xfer);

static int nw_xfer_tioreq_map   (struct nw_xfer_request      *xfer,
                                 struct c2_pdclust_src_addr  *src,
                                 struct c2_pdclust_tgt_addr  *tgt,
                                 struct target_ioreq        **out);

static int nw_xfer_tioreq_get   (struct nw_xfer_request *xfer,
                                 struct c2_fid          *fid,
                                 struct c2_rpc_session  *session,
                                 uint64_t                size,
                                 struct target_ioreq   **out);

static const struct nw_xfer_ops xfer_ops = {
        .nxo_prepare     = nw_xfer_io_prepare,
        .nxo_complete    = nw_xfer_req_complete,
        .nxo_dispatch    = nw_xfer_req_dispatch,
        .nxo_tioreq_map  = nw_xfer_tioreq_map,
};

static int  pargrp_iomap_populate     (struct pargrp_iomap      *map,
                                       const struct c2_indexvec *ivec,
                                       struct c2_ivec_cursor    *cursor);

static bool pargrp_iomap_spans_seg    (struct pargrp_iomap *map,
                                       c2_bindex_t          index,
                                       c2_bcount_t          count);

static int  pargrp_iomap_readrest     (struct pargrp_iomap *map);


static int  pargrp_iomap_seg_process  (struct pargrp_iomap *map,
                                       uint64_t             seg,
                                       bool                 rmw);

static int  pargrp_iomap_parity_recalc(struct pargrp_iomap *map);

static uint64_t pargrp_iomap_fullpages_count(struct pargrp_iomap *map);

static int pargrp_iomap_readold_auxbuf_alloc(struct pargrp_iomap *map);

static int pargrp_iomap_paritybufs_alloc(struct pargrp_iomap *map);

static const struct pargrp_iomap_ops iomap_ops = {
        .pi_populate             = pargrp_iomap_populate,
        .pi_spans_seg            = pargrp_iomap_spans_seg,
        .pi_readrest             = pargrp_iomap_readrest,
        .pi_fullpages_find       = pargrp_iomap_fullpages_count,
        .pi_seg_process          = pargrp_iomap_seg_process,
        .pi_readold_auxbuf_alloc = pargrp_iomap_readold_auxbuf_alloc,
        .pi_parity_recalc        = pargrp_iomap_parity_recalc,
        .pi_paritybufs_alloc     = pargrp_iomap_paritybufs_alloc,
};

static bool pargrp_iomap_invariant_nr(const struct io_request *req);
static bool target_ioreq_invariant(const struct target_ioreq *ti);

static void target_ioreq_fini         (struct target_ioreq *ti);

static int target_ioreq_iofops_prepare(struct target_ioreq *ti);

static void target_ioreq_seg_add      (struct target_ioreq *ti,
                                       uint64_t             frame,
                                       c2_bindex_t          gob_offset,
                                       c2_bcount_t          count,
                                       uint64_t             unit);

static const struct target_ioreq_ops tioreq_ops = {
        .tio_seg_add        = target_ioreq_seg_add,
        .tio_iofops_prepare = target_ioreq_iofops_prepare,
};

static struct data_buf *data_buf_alloc_init(enum page_attr pattr);

static void data_buf_dealloc_fini(struct data_buf *buf);

static void io_bottom_half(struct c2_sm_group *grp, struct c2_sm_ast *ast);

static int  ioreq_iomaps_prepare(struct io_request *req);

static void ioreq_iomap_locate  (struct io_request    *req,
                                 uint64_t              grpid,
                                 struct pargrp_iomap **map);

static int ioreq_user_data_copy (struct io_request   *req,
                                 enum copy_direction  dir,
                                 enum page_attr       filter);

static int ioreq_parity_recalc  (struct io_request *req);

static int ioreq_iosm_handle    (struct io_request *req);

static const struct io_request_ops ioreq_ops = {
        .iro_iomaps_prepare = ioreq_iomaps_prepare,
        .iro_user_data_copy = ioreq_user_data_copy,
        .iro_iomap_locate   = ioreq_iomap_locate,
        .iro_parity_recalc  = ioreq_parity_recalc,
        .iro_iosm_handle    = ioreq_iosm_handle,
};

static int ioreq_sm_state(const struct io_request *req)
{
	return req->ir_sm.sm_state;
}

static void ioreq_sm_state_set(struct io_request *req, int state)
{
        c2_mutex_lock(&req->ir_sm.sm_grp->s_lock);
	c2_sm_state_set(&req->ir_sm, state);
        c2_mutex_unlock(&req->ir_sm.sm_grp->s_lock);
}

static void ioreq_sm_failed(struct io_request *req, int rc)
{
        c2_mutex_lock(&req->ir_sm.sm_grp->s_lock);
	c2_sm_fail(&req->ir_sm, IRS_FAILED, rc);
        c2_mutex_unlock(&req->ir_sm.sm_grp->s_lock);
}

static const struct c2_sm_state_descr io_states[] = {
        [IRS_INITIALIZED]    = {
                .sd_flags     = C2_SDF_INITIAL,
                .sd_name      = "IO_initial",
                .sd_allowed   = (1 << IRS_READING) | (1 << IRS_WRITING) |
				(1 << IRS_FAILED)
        },
        [IRS_READING]        = {
                .sd_name      = "IO_reading",
                .sd_allowed   = (1 << IRS_READ_COMPLETE) |
				(1 << IRS_FAILED)
        },
        [IRS_READ_COMPLETE]  = {
                .sd_name      = "IO_read_complete",
                .sd_allowed   = (1 << IRS_WRITING) | (1 << IRS_REQ_COMPLETE) |
				(1 << IRS_FAILED)
        },
        [IRS_WRITING] = {
                .sd_name      = "IO_writing",
                .sd_allowed   = (1 << IRS_WRITE_COMPLETE) | (1 << IRS_FAILED)
        },
        [IRS_WRITE_COMPLETE] = {
                .sd_name      = "IO_write_complete",
                .sd_allowed   = (1 << IRS_REQ_COMPLETE) | (1 << IRS_FAILED)
        },
        [IRS_FAILED]   = {
                .sd_flags     = C2_SDF_FAILURE,
                .sd_name      = "IO_req_failed",
                .sd_allowed   = (1 << IRS_REQ_COMPLETE)
        },
        [IRS_REQ_COMPLETE]   = {
                .sd_flags     = C2_SDF_TERMINAL,
                .sd_name      = "IO_req_complete",
        },
};

static const struct c2_sm_conf io_sm_conf = {
        .scf_name      = "IO request state machine configuration",
        .scf_nr_states = ARRAY_SIZE(io_states),
        .scf_state     = io_states,
};

static bool io_request_invariant(const struct io_request *req)
{
        C2_PRE(req != NULL);

        return
               io_request_bob_check(req) &&
               req->ir_type   <= IRT_TYPE_NR &&
               req->ir_iovec  != NULL &&
               req->ir_ops    != NULL &&
               c2_fid_is_valid(file_to_fid(req->ir_file)) &&

               ergo(ioreq_sm_state(req) == IRS_READING,
                    !tioreqs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

               ergo(ioreq_sm_state(req) == IRS_WRITING,
                    !tioreqs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

               ergo(ioreq_sm_state(req) == IRS_READ_COMPLETE,
                    ergo(req->ir_type == IRT_READ,
                         tioreqs_tlist_is_empty(
                                 &req->ir_nwxfer.nxr_tioreqs))) &&

               ergo(ioreq_sm_state(req) == IRS_WRITE_COMPLETE,
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

static bool data_buf_invariant_nr(const struct pargrp_iomap *map, int index)
{
        return c2_forall(i, page_nr(layout_n(pdlayout_get(map->pi_ioreq))),
                         ergo(map->pi_databufs[i] != NULL,
                              data_buf_invariant(map->pi_databufs[index][i])));
}

static bool io_req_fop_invariant(const struct io_req_fop *fop)
{
        return
               fop != NULL &&
               io_req_fop_bob_check(fop) &&
               fop->irf_tioreq != NULL &&
               fop->irf_ast.sa_cb != NULL &&
               iofops_tlink_is_in(fop);
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
               c2_tl_forall(iofops, iofop, &ti->ti_iofops,
                            io_req_fop_invariant(iofop));
}

static bool pargrp_iomap_invariant(const struct pargrp_iomap *map)
{
        return
               map != NULL &&
               pargrp_iomap_bob_check(map) &&
               map->pi_ops      != NULL &&
               map->pi_rtype     <  PIR_NR &&
               map->pi_databufs != NULL &&
               ergo(c2_vec_count(&map->pi_ivec.iv_vec) > 0,
                    c2_forall(i, map->pi_ivec.iv_vec.v_nr - 1,
                              map->pi_ivec.iv_index[i] +
                              map->pi_ivec.iv_vec.v_count[i] <
                              map->pi_ivec.iv_index[i+1])) &&
               c2_forall(i, page_nr(layout_unit_size(
                         pdlayout_get(map->pi_ioreq))),
                         data_buf_invariant_nr(map, i));
}

static bool pargrp_iomap_invariant_nr(const struct io_request *req)
{
        return c2_forall(i, req->ir_iomap_nr,
                         pargrp_iomap_invariant(req->ir_iomaps[i]));
}

static void nw_xfer_request_init(struct nw_xfer_request *xfer)
{
        C2_ENTRY("nw_xfer_request : %p", xfer);
        C2_PRE(xfer != NULL);

        nw_xfer_request_bob_init(xfer);
        xfer->nxr_rc    = 0;
        xfer->nxr_bytes = xfer->nxr_iofop_nr = 0;
        xfer->nxr_state = NXS_INITIALIZED;
        xfer->nxr_ops   = &xfer_ops;
        tioreqs_tlist_init(&xfer->nxr_tioreqs);

        C2_POST(nw_xfer_request_invariant(xfer));
        C2_LEAVE();
}

static void nw_xfer_request_fini(struct nw_xfer_request *xfer)
{
        C2_ENTRY("nw_xfer_request : %p", xfer);
        C2_PRE(xfer != NULL && xfer->nxr_state == NXS_COMPLETE);
        C2_PRE(nw_xfer_request_invariant(xfer));

        xfer->nxr_ops = NULL;
        nw_xfer_request_bob_fini(xfer);
        tioreqs_tlist_fini(&xfer->nxr_tioreqs);
        C2_LEAVE();
}

static void ioreq_iomap_locate(struct io_request *req, uint64_t grpid,
                               struct pargrp_iomap **iomap)
{
        uint64_t map;

        C2_ENTRY("Locate map with grpid = %llu", grpid);
        C2_PRE(io_request_invariant(req));
        C2_PRE(iomap != NULL);

        for (map = 0; map < req->ir_iomap_nr; ++map) {
                if (req->ir_iomaps[map]->pi_grpid == grpid) {
                        *iomap = req->ir_iomaps[map];
                        break;
                }
        }
        C2_POST(map < req->ir_iomap_nr);
        C2_LEAVE("Map = %p", *iomap);
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
        } else
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
        uint64_t                  bytes;
        uint32_t                  row;
        uint32_t                  col;
        struct page              *page;
        struct c2_pdclust_layout *play;

        C2_ENTRY("Copy %s user-space, start = %llu, end = %llu",
                 dir == CD_COPY_FROM_USER ? (char *)"from" : (char *)"to",
                 start, end);
        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(it != NULL);
        C2_PRE(C2_IN(dir, (CD_COPY_FROM_USER, CD_COPY_TO_USER)));

        /* Finds out the page from pargrp_iomap::pi_databufs. */
        play = pdlayout_get(map->pi_ioreq);
        page_pos_get(map, start, &row, &col);
        C2_ASSERT(map->pi_databufs[row][col] != NULL);
        page = virt_to_page(map->pi_databufs[row][col]->db_buf.b_addr);

        if (dir == CD_COPY_FROM_USER) {
                if (page_copy_filter(start, end, filter)) {
                        C2_ASSERT(map->pi_databufs[row][col]->db_flags &
                                  filter);

                        /*
                         * Copies page to auxiliary buffer before it gets
                         * overwritten by user data. This is needed in order
                         * to calculate delta parity in case of read-old
                         * approach.
                         */
                        if (map->pi_databufs[row][col]->db_auxbuf.b_addr !=
                            NULL && map->pi_rtype == PIR_READOLD)
                                memcpy(map->pi_databufs[row][col]->db_auxbuf.
                                       b_addr,
                                       map->pi_databufs[row][col]->db_buf.
                                       b_addr, PAGE_CACHE_SIZE);

                        pagefault_disable();
                        /* Copies to appropriate offset within page. */
                        bytes = iov_iter_copy_from_user_atomic(page, it,
                                        start & (PAGE_CACHE_SIZE - 1),
                                        end - start);
                        pagefault_enable();

                        if (bytes != end - start)
                                C2_RETERR(-EFAULT, "Failed to copy_from_user");
                }
        } else {
                bytes = copy_to_user(it->iov->iov_base + it->iov_offset,
                                     map->pi_databufs[row][col]->
                                     db_buf.b_addr +
                                     (start & (PAGE_CACHE_SIZE - 1)),
                                     end - start);
                if (bytes != 0)
                        C2_RETERR(-EFAULT, "Failed to copy_to_user");
        }

        C2_RETURN(0);
}

static int pargrp_iomap_parity_recalc(struct pargrp_iomap *map)
{
        int                       rc;
        uint32_t                  u;
        uint32_t                  id;
        uint32_t                  row;
        uint32_t                  col;
        struct c2_buf            *dbufs;
        struct c2_buf            *pbufs;
        struct c2_pdclust_layout *play;

        C2_ENTRY("map = %p", map);
        C2_PRE(pargrp_iomap_invariant(map));

        play = pdlayout_get(map->pi_ioreq);
        C2_ALLOC_ARR_ADDB(dbufs, layout_n(play), &c2t1fs_addb,
                          &io_addb_loc);
        C2_ALLOC_ARR_ADDB(pbufs, layout_k(play), &c2t1fs_addb,
                          &io_addb_loc);

        if (dbufs == NULL || pbufs == NULL) {
                rc = -ENOMEM;
                goto last;
        }

        if ((map->pi_ioreq->ir_type == IRT_WRITE && map->pi_rtype == PIR_NONE)
            || map->pi_rtype == PIR_READREST) {

                for (row = 0; row < data_row_nr(play); ++row) {
                        for (u = 0, col = 0; col < data_col_nr(play);
                             ++u, ++col)
                                dbufs[u] = map->pi_databufs[row][col]->db_buf;

                        for (col = 0; col < layout_k(play); ++col)
                                pbufs[col] = map->pi_paritybufs[row][col]->
                                             db_buf;

                        c2_parity_math_calculate(parity_math(map->pi_ioreq),
                                                 dbufs, pbufs);
                }

        } else {
                struct data_buf *buf;
                struct c2_buf    zbuf;
                /* Array of spare buffers. */
                struct c2_buf   *spbufs;
                /* Buffers to store delta parities. */
                struct c2_buf   *deltabufs;

                C2_ALLOC_ARR_ADDB(deltabufs, layout_n(play), &c2t1fs_addb,
                                  &io_addb_loc);
                if (deltabufs == NULL) {
                        rc = -ENOMEM;
                        goto last;
                }

                zbuf.b_addr = (void *)get_zeroed_page(GFP_KERNEL);
                if (zbuf.b_addr == NULL) {
                        c2_free(deltabufs);
                        rc = -ENOMEM;
                        goto last;
                }
                zbuf.b_nob  = PAGE_CACHE_SIZE;

                C2_ALLOC_ARR_ADDB(spbufs, layout_k(play), &c2t1fs_addb,
                                  &io_addb_loc);
                if (spbufs == NULL) {
                        c2_free(deltabufs);
                        free_page((unsigned long)zbuf.b_addr);
                        rc = -ENOMEM;
                        goto last;
                }

                for (id = 0; id < layout_k(play); ++id) {
                        spbufs[id].b_addr = (void *)get_zeroed_page(GFP_KERNEL);
                        if (spbufs[id].b_addr == NULL) {
                                rc = -ENOMEM;
                                break;
                        }
                        spbufs[id].b_nob = PAGE_CACHE_SIZE;
                }

                if (rc != 0) {
                        for (id = 0; id < layout_k(play); ++id) {
                                free_page((unsigned long)spbufs[id].b_addr);
                                spbufs[id].b_addr = NULL;
                        }
                        c2_free(deltabufs);
                        free_page((unsigned long)zbuf.b_addr);
                        goto last;
                }

                for (row = 0; row < data_row_nr(play); ++row) {
                        /*
                         * Calculates parity between old version and
                         * new version of every data block.
                         */
                        for (col = 0; col < data_col_nr(play); ++col) {

                                buf = map->pi_databufs[row][col];

                                if (buf != NULL && C2_IN(buf->db_flags,
                                    (PA_FULLPAGE_MODIFY, PA_PARTPAGE_MODIFY))) {

                                        dbufs[0].b_addr = buf->db_auxbuf.b_addr;
                                        dbufs[0].b_nob  = PAGE_CACHE_SIZE;
                                        dbufs[1].b_addr = buf->db_buf.b_addr;
                                        dbufs[1].b_nob  = PAGE_CACHE_SIZE;

                                        for (id = 2; id < data_col_nr(play); ++id)
                                                dbufs[id] = zbuf;

                                        /*
                                         * Reuses the data_buf::db_auxbuf to
                                         * store delta parity.
                                         */
                                        deltabufs[col]  = buf->db_auxbuf;
                                        c2_parity_math_calculate(parity_math
                                                        (map->pi_ioreq), dbufs,
                                                        &deltabufs[col]);
                                        C2_LOG(C2_INFO, "Parity calculated for"
                                               "row: %d, col: %d", row, col);
                                }
                        }

                        /* Calculates parity amongst delta parity buffers. */
                        for (col = 0; col < data_col_nr(play); ++col) {
                                if (map->pi_databufs[row][col]->
                                    db_auxbuf.b_addr != NULL)
                                        dbufs[col] = map->pi_databufs
                                                     [row][col]->db_auxbuf;
                                else
                                        dbufs[col] = zbuf;
                        }
                        c2_parity_math_calculate(parity_math(map->
                                                 pi_ioreq), dbufs, spbufs);
                        C2_LOG(C2_INFO, "Calculated parity amongst delta parities");

                        /*
                         * Calculates parity amongst spbufs array and old
                         * version of parity block.
                         */
                        for (col = 0; col < parity_col_nr(play); ++col)
                                pbufs[col] = map->pi_paritybufs[row][col]->
                                             db_buf;

                        /*
                         * Assigns valid buffers (spbufs array and old version
                         * of parity block) and use zero buffers elsewhere.
                         * Zero buffers are used to satisafy requirement
                         * of c2_parity_math_calculate() API.
                         * Zero buffers have no effect on parity.
                         */
                        for (id = 0; id < parity_col_nr(play); ++id) {
                                dbufs[0] = spbufs[id];
                                dbufs[1] = map->pi_paritybufs[row][id]->db_buf;
                                for (col = 2; col < data_col_nr(play); ++col)
                                        dbufs[col] = zbuf;

                                c2_parity_math_calculate(parity_math(map->
                                                         pi_ioreq), dbufs,
                                                         &pbufs[id]);
                        }
                        C2_LOG(C2_INFO, "Calculated parity with"
                               "old version of parity block");
                }

                free_page((unsigned long)zbuf.b_addr);
                for (id = 0; id < parity_col_nr(play); ++id)
                        free_page((unsigned long)spbufs[id].b_addr);

                c2_free(spbufs);
                c2_free(deltabufs);
        }
last:
        c2_free(dbufs);
        c2_free(pbufs);
        C2_RETURN(rc);
}

static int ioreq_parity_recalc(struct io_request *req)
{
        int      rc;
        uint64_t map;

        C2_ENTRY("io_request : %p", req);
        C2_PRE(io_request_invariant(req));

        for (map = 0; map < req->ir_iomap_nr; ++map) {
                rc = req->ir_iomaps[map]->pi_ops->pi_parity_recalc(req->
                                ir_iomaps[map]);
                if (rc != 0)
                        C2_RETERR(rc, "Parity recalc failed for grpid : %llu",
                                  req->ir_iomaps[map]->pi_grpid);
        }
        C2_RETURN(0);
}

static int ioreq_user_data_copy(struct io_request   *req,
                                enum copy_direction  dir,
                                enum page_attr       filter)
{
        int                       rc;
        uint64_t                  map;
        c2_bindex_t               grpstart;
        c2_bindex_t               grpend;
        c2_bindex_t               pgstart;
        c2_bindex_t               pgend;
        c2_bcount_t               count = 0;
        struct iov_iter           it;
        struct c2_ivec_cursor     srccur;
        struct c2_pdclust_layout *play;

        C2_ENTRY("io_request : %p, %s filter = %d", req,
                 dir == CD_COPY_FROM_USER ? (char *)"from" : (char *)"to",
                 filter);
        C2_PRE(io_request_invariant(req));
        C2_PRE(dir < CD_NR);

        iov_iter_init(&it, req->ir_iovec, req->ir_ivec.iv_vec.v_nr,
                      c2_vec_count(&req->ir_ivec.iv_vec), 0);
        c2_ivec_cursor_init(&srccur, &req->ir_ivec);
        play = pdlayout_get(req);

        for (map = 0; map < req->ir_iomap_nr; ++map) {

                C2_ASSERT(pargrp_iomap_invariant(req->ir_iomaps[map]));

                grpstart = data_size(play) * req->ir_iomaps[map]->pi_grpid;
                grpend   = grpstart + data_size(play);
                pgstart  = c2_ivec_cursor_index(&srccur);

                while (c2_ivec_cursor_move(&srccur, count) &&
                       c2_ivec_cursor_index(&srccur) < grpend) {

                        pgend = pgstart + min64u(c2_ivec_cursor_step(&srccur),
                                                 PAGE_CACHE_SIZE);
                        count = pgend - pgstart;

                        /*
                         * This takes care of finding correct page from
                         * current pargrp_iomap structure from pgstart
                         * and pgend.
                         */
                        rc = user_data_copy(req->ir_iomaps[map], pgstart, pgend,
                                            &it, dir, filter);
                        if (rc != 0)
                                C2_RETERR(rc, "Copy failed");

                        iov_iter_advance(&it, count);
                        pgstart += count;
                }
        }

        C2_RETURN(0);
}

static void indexvec_sort(struct c2_indexvec *ivec)
{
        uint64_t i;
        uint64_t j;

        C2_ENTRY("indexvec = %p", ivec);
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
        C2_LEAVE();
}

static int pargrp_iomap_init(struct pargrp_iomap *map,
                             struct io_request   *req,
                             uint64_t             grpid)
{
        int                       pg;
        struct c2_pdclust_layout *play;

        C2_ENTRY("map = %p, ioreq = %p, grpid = %llu", map, req, grpid);
        C2_PRE(map != NULL);
        C2_PRE(req != NULL);

        pargrp_iomap_bob_init(map);
        play                  = pdlayout_get(req);
        map->pi_ops           = &iomap_ops;
        map->pi_rtype         = PIR_NONE;
        map->pi_grpid         = grpid;
        map->pi_ioreq         = req;
        map->pi_paritybufs    = NULL;
        SEG_NR(&map->pi_ivec) = data_size(play);

        C2_ALLOC_ARR_ADDB(map->pi_ivec.iv_index, SEG_NR(&map->pi_ivec),
                          &c2t1fs_addb, &io_addb_loc);
        C2_ALLOC_ARR_ADDB(map->pi_ivec.iv_vec.v_count, SEG_NR(&map->pi_ivec),
                          &c2t1fs_addb, &io_addb_loc);

        if (map->pi_ivec.iv_index       == NULL ||
            map->pi_ivec.iv_vec.v_count == NULL)
                goto fail;

        C2_ALLOC_ARR_ADDB(map->pi_databufs, data_row_nr(play),
                          &c2t1fs_addb, &io_addb_loc);
        if (map->pi_databufs == NULL)
                goto fail;

        for (pg = 0; pg < data_row_nr(play); ++pg) {
                C2_ALLOC_ARR_ADDB(map->pi_databufs[pg], layout_n(play),
                                  &c2t1fs_addb, &io_addb_loc);
                if (map->pi_databufs[pg] == NULL)
                        goto fail;
        }

        if (req->ir_type == IRT_WRITE) {
                C2_ALLOC_ARR_ADDB(map->pi_paritybufs, parity_row_nr(play),
                                  &c2t1fs_addb, &io_addb_loc);
                if (map->pi_paritybufs == NULL)
                        goto fail;

                for (pg = 0; pg < parity_row_nr(play); ++pg) {
                        C2_ALLOC_ARR_ADDB(map->pi_paritybufs[pg],
                                          parity_col_nr(play),
                                          &c2t1fs_addb, &io_addb_loc);
                        if (map->pi_paritybufs[pg] == NULL)
                                goto fail;
                }
        }
        C2_POST(pargrp_iomap_invariant(map));
        C2_RETURN(0);

fail:
        c2_free(map->pi_ivec.iv_index);
        c2_free(map->pi_ivec.iv_vec.v_count);

        if (map->pi_databufs != NULL) {
                for (pg = 0; pg < data_row_nr(play); ++pg) {
                        c2_free(map->pi_databufs[pg]);
                        map->pi_databufs[pg] = NULL;
                }
                c2_free(map->pi_databufs);
        }

        if (map->pi_paritybufs != NULL) {
                for (pg = 0; pg < parity_row_nr(play); ++pg) {
                        c2_free(map->pi_paritybufs[pg]);
                        map->pi_paritybufs[pg] = NULL;
                }
                c2_free(map->pi_paritybufs);
        }
        C2_RETERR(-ENOMEM, "Memory allocation failed");
}

static void pargrp_iomap_fini(struct pargrp_iomap *map)
{
        uint32_t                  row;
        uint32_t		  col;
        struct c2_pdclust_layout *play;

        C2_ENTRY("map %p", map);
        C2_PRE(pargrp_iomap_invariant(map));

        play         = pdlayout_get(map->pi_ioreq);
        map->pi_ops  = NULL;
        map->pi_rtype = PIR_NONE;

        pargrp_iomap_bob_fini(map);
        c2_free(map->pi_ivec.iv_index);
        c2_free(map->pi_ivec.iv_vec.v_count);

        for (row = 0; row < data_row_nr(play); ++row) {
                for (col = 0; col < data_col_nr(play); ++col) {
                        if (map->pi_databufs[row][col] != NULL) {
                                data_buf_dealloc_fini(map->
                                                pi_databufs[row][col]);
                                map->pi_databufs[row][col] = NULL;
                        }
                }
                c2_free(map->pi_databufs[row]);
                map->pi_databufs[row] = NULL;
        }

        if (map->pi_ioreq->ir_type == IRT_WRITE) {
                for (row = 0; row < parity_row_nr(play); ++row) {
                        for (col = 0; col < parity_col_nr(play); ++col) {
                                if (map->pi_paritybufs[row][col] != NULL) {
                                        data_buf_dealloc_fini(map->
                                                        pi_paritybufs[row][col]);
                                        map->pi_paritybufs[row][col] = NULL;
                                }
                        }
                        c2_free(map->pi_paritybufs[row]);
                        map->pi_paritybufs[row] = NULL;
                }
        }

        c2_free(map->pi_databufs);
        c2_free(map->pi_paritybufs);
        map->pi_ioreq               = NULL;
        map->pi_databufs            = NULL;
        map->pi_paritybufs          = NULL;
        map->pi_ivec.iv_index       = NULL;
        map->pi_ivec.iv_vec.v_count = NULL;
        C2_LEAVE();
}

static bool pargrp_iomap_spans_seg(struct pargrp_iomap *map,
                                   c2_bindex_t index, c2_bcount_t count)
{
	uint32_t seg;

        C2_PRE(pargrp_iomap_invariant(map));

	for (seg = 0; seg < map->pi_ivec.iv_vec.v_nr; ++seg) {
		if (index >= INDEX(&map->pi_ivec, seg) &&
		    count <= COUNT(&map->pi_ivec, seg))
			return true;
	}
	return false;
}

static int pargrp_iomap_databuf_alloc(struct pargrp_iomap *map,
                                      uint32_t             row,
                                      uint32_t             col)
{
        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(map->pi_databufs[row][col] == NULL);

        map->pi_databufs[row][col] = data_buf_alloc_init(0);

        return map->pi_databufs[row][col] == NULL ?  -ENOMEM : 0;
}

/* Allocates data_buf structures as needed and populates the buffer flags. */
static int pargrp_iomap_seg_process(struct pargrp_iomap *map,
                                    uint64_t             seg,
                                    bool                 rmw)
{
        int                       rc;
        uint32_t                  row;
        uint32_t                  col;
        uint64_t                  count = 0;
        c2_bindex_t               start;
        c2_bindex_t               end;
        struct inode             *inode;
        struct data_buf          *dbuf;
        struct c2_ivec_cursor     cur;
        struct c2_pdclust_layout *play;

        C2_ENTRY("map %p, seg %llu", map, seg);
        C2_PRE(pargrp_iomap_invariant(map));

        play  = pdlayout_get(map->pi_ioreq);
        inode = map->pi_ioreq->ir_file->f_dentry->d_inode;
        c2_ivec_cursor_init(&cur, &map->pi_ivec);

        while (!c2_ivec_cursor_move(&cur, count)) {

                start = c2_ivec_cursor_index(&cur);
                end   = min64u(PAGE_CACHE_SIZE, c2_ivec_cursor_step(&cur)) +
                        start;
                count = end - start;
                page_pos_get(map, start, &row, &col);

                rc = pargrp_iomap_databuf_alloc(map, row, col);
                if (rc != 0)
                        goto err;

                dbuf = map->pi_databufs[row][col];

                if (map->pi_ioreq->ir_type == IRT_WRITE) {
                        dbuf->db_flags |= PA_WRITE;

                        dbuf->db_flags |= count == PAGE_CACHE_SIZE ?
                                PA_FULLPAGE_MODIFY : PA_PARTPAGE_MODIFY;

                        /*
                         * Even if PA_PARTPAGE_MODIFY flag is set in
                         * this buffer, the auxiliary buffer can not be
                         * allocated until ::pi_rtype is selected.
                         */
                        if (rmw) {
                                if (end <= inode->i_size) {
                                        if (dbuf->db_flags & PA_PARTPAGE_MODIFY)
                                                dbuf->db_flags |= PA_READ;
                                } else if (page_id(end) ==
                                           page_id(inode->i_size)) {
                                        /*
                                         * Note the actual length of page
                                         * till EOF in data_buf::c2_buf::b_nob.
                                         */
                                        dbuf->db_buf.b_nob =
                                                min64u(inode->i_size, end) -
                                                start;
                                        dbuf->db_flags |= PA_READ_EOF;
                                }
                        }
                } else
                        /*
                         * For read IO requests, file_aio_read() has already
			 * delimited the index vector to EOF boundary.
                         */
                        dbuf->db_flags |= PA_READ;
        }

        C2_RETURN(0);
err:
        for (row = 0; row < data_row_nr(play); ++row) {
                for (col = 0; col < data_col_nr(play); ++col) {
                        if (map->pi_databufs[row][col] != NULL) {
                                data_buf_dealloc_fini(map->pi_databufs
                                                      [row][col]);
                                map->pi_databufs[row][col] = NULL;
                        }
                }
        }
        C2_RETERR(rc, "databuf_alloc failed");
}

static uint64_t pargrp_iomap_fullpages_count(struct pargrp_iomap *map)
{
        uint32_t                  row;
        uint32_t                  col;
        uint64_t                  nr = 0;
        struct c2_pdclust_layout *play;

        C2_ENTRY("map %p", map);
        C2_PRE(pargrp_iomap_invariant(map));

        play = pdlayout_get(map->pi_ioreq);

        for (row = 0; row < data_row_nr(play); ++row) {
                for (col = 0; col < data_col_nr(play); ++col) {

                        if (map->pi_databufs[row][col] &&
                            map->pi_databufs[row][col]->db_flags &
                            PA_FULLPAGE_MODIFY)
                                ++nr;
                }
        }
        C2_LEAVE();
        return nr;
}

static uint64_t pargrp_iomap_auxbuf_alloc(struct pargrp_iomap *map,
                                          uint32_t             row,
                                          uint32_t             col)
{
        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(C2_IN(map->pi_databufs[row][col]->db_flags,
               (PA_PARTPAGE_MODIFY, PA_FULLPAGE_MODIFY)));
        C2_PRE(map->pi_rtype == PIR_READOLD);

        map->pi_databufs[row][col]->db_auxbuf.b_addr = (void *)
                get_zeroed_page(GFP_KERNEL);

        if (map->pi_databufs[row][col]->db_auxbuf.b_addr == NULL)
                return -ENOMEM;
        map->pi_databufs[row][col]->db_auxbuf.b_nob = PAGE_CACHE_SIZE;

        return 0;
}

/*
 * Allocates auxiliary buffer for data_buf structures in
 * pargrp_iomap structure.
 */
static int pargrp_iomap_readold_auxbuf_alloc(struct pargrp_iomap *map)
{
        int                       rc;
        uint64_t                  start;
        uint64_t                  end;
        uint64_t                  count = 0;
        uint32_t                  row;
        uint32_t                  col;
        struct c2_ivec_cursor     cur;
        struct c2_pdclust_layout *play;

        C2_ENTRY("map %p", map);
        C2_PRE(pargrp_iomap_invariant(map));
        C2_PRE(map->pi_rtype == PIR_READOLD);

        play  = pdlayout_get(map->pi_ioreq);
        c2_ivec_cursor_init(&cur, &map->pi_ivec);

        while (!c2_ivec_cursor_move(&cur, count)) {
                start = c2_ivec_cursor_index(&cur);
                end   = min64u(PAGE_CACHE_SIZE, c2_ivec_cursor_step(&cur)) +
                        start;
                count = end - start;
                page_pos_get(map, start, &row, &col);

                if (count < PAGE_CACHE_SIZE) {
                        map->pi_databufs[row][col]->db_flags |=
                                PA_PARTPAGE_MODIFY;
                        rc = pargrp_iomap_auxbuf_alloc(map, row, col);
                        if (rc != 0)
                                C2_RETERR(rc, "auxbuf_alloc failed");
                }
        }
        C2_RETURN(rc);
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
 * read and written from server is selected.
 *
 * @verbatim
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
 *  Read-rest approach
 *
 *   a     x
 *   +---+---+---+---+---+---+---+
 *   |   | P'| F | F | F | # | * |  PG#0
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | F | F | # | * |  PG#1
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | P'|   | # | * |  PG#2
 *   +---+---+---+---+---+---+---+
 *     N   N   N   N   N   K   P
 *                 y     b
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
 * @endverbatim
 */
static int pargrp_iomap_readrest(struct pargrp_iomap *map)
{
        int                       rc;
        uint32_t                  row;
        uint32_t                  seg;
        uint32_t                  seg_nr;
        c2_bindex_t               grpstart;
        c2_bindex_t               grpend;
        struct c2_pdclust_layout *play;
        struct c2_indexvec       *ivec;

        C2_ENTRY("map %p", map);
        C2_PRE(pargrp_iomap_invariant(map));

        map->pi_rtype = PIR_READREST;

        play     = pdlayout_get(map->pi_ioreq);
        ivec     = &map->pi_ivec;
        seg_nr   = map->pi_ivec.iv_vec.v_nr;
        grpstart = data_size(play) * map->pi_grpid;
        grpend   = grpstart + data_size(play);

	/* Extends first segment to align with start of parity group. */
        COUNT(ivec, 0) += (INDEX(ivec, 0) - grpstart);
        INDEX(ivec, 0)  = grpstart;

	/* Extends last segment to align with end of parity group. */
        COUNT(ivec, seg_nr - 1) += grpend - INDEX(ivec, seg_nr - 1);

        /*
         * All io extents _not_ spanned by pargrp_iomap::pi_ivec
         * need to be included so that _all_ pages from parity group
         * are available to do IO.
         */
        for (seg = 1; seg_nr > 2 && seg <= seg_nr - 2; ++seg) {
                if (INDEX(ivec, seg) + COUNT(ivec, seg) < INDEX(ivec, seg + 1))
                        COUNT(ivec, seg) += INDEX(ivec, seg + 1) -
                                            (INDEX(ivec, seg) +
					     COUNT(ivec, seg));
        }

        /*
         * For read-rest approach, all data units from a parity group
         * are read. Ergo, mark them as to be read.
         */
        for (row = 0; row < data_row_nr(play); ++row) {
                for (seg_nr = 0; seg_nr < data_col_nr(play); ++seg_nr) {
                        if (map->pi_databufs[row][seg_nr] == NULL) {
                                rc = pargrp_iomap_databuf_alloc(map, row,
                                                                seg_nr);
                                if (rc != 0)
                                        C2_RETERR(rc, "databuf_alloc failed");

                                map->pi_databufs[row][seg_nr]->db_flags |=
                                        PA_READ;
                        }
                }
        }

        C2_RETURN(0);
}

static int pargrp_iomap_paritybufs_alloc(struct pargrp_iomap *map)
{
        uint32_t                  row;
        uint32_t                  col;
        struct c2_pdclust_layout *play;

        C2_ENTRY("map %p", map);
        C2_PRE(pargrp_iomap_invariant(map));

        play = pdlayout_get(map->pi_ioreq);
        for (row = 0; row < parity_row_nr(play); ++row) {
                for (col = 0; col < parity_col_nr(play); ++col) {

                        map->pi_paritybufs[row][col] = data_buf_alloc_init(0);
                        if (map->pi_paritybufs[row][col] == NULL)
                                goto err;

                        map->pi_paritybufs[row][col]->db_flags |= PA_WRITE;

                        if (map->pi_rtype == PIR_READOLD)
                                map->pi_paritybufs[row][col]->db_flags |=
                                        PA_READ;
                }
        }
        C2_RETURN(0);
err:
        for (row = 0; row < parity_row_nr(play); ++row) {
                for (col = 0; col < parity_col_nr(play); ++col) {
                        c2_free(map->pi_paritybufs[row][col]);
                        map->pi_paritybufs[row][col] = NULL;
                }
        }
        C2_RETERR(-ENOMEM, "Memory allocation failed for data_buf.");
}

static int pargrp_iomap_populate(struct pargrp_iomap      *map,
                                 const struct c2_indexvec *ivec,
                                 struct c2_ivec_cursor    *cursor)
{
        int                       rc;
        bool                      rmw;
        uint64_t                  seg;
        uint64_t                  size;
        uint64_t                  grpsize;
        c2_bcount_t               count = 0;
        /* Number of pages _completely_ spanned by incoming io vector. */
        uint64_t                  nr = 0;
        /* Number of pages to be read + written for read-old approach. */
        uint64_t                  ro_page_nr;
        /* Number of pages to be read + written for read-rest approach. */
        uint64_t                  rr_page_nr;
        c2_bindex_t               grpstart;
        c2_bindex_t               grpend;
        struct c2_pdclust_layout *play;

        C2_ENTRY("map %p, indexvec %p", map, ivec);
        C2_PRE(map  != NULL);
        C2_PRE(ivec != NULL);

        play     = pdlayout_get(map->pi_ioreq);
        grpsize  = data_size(play);
        grpstart = grpsize * map->pi_grpid;
        grpend   = grpstart + grpsize;

        for (size = 0, seg = cursor->ic_cur.vc_seg; INDEX(ivec, seg) < grpend;
             ++seg)
                size += min64u(grpend - INDEX(ivec, seg), COUNT(ivec, seg));

        rmw = size < grpsize && map->pi_ioreq->ir_type == IRT_WRITE;

        size = map->pi_ioreq->ir_file->f_dentry->d_inode->i_size;

        for (seg = 0; !c2_ivec_cursor_move(cursor, count) &&
             c2_ivec_cursor_index(cursor) < grpend; ++seg) {

		c2_bindex_t endpos;

                /*
                 * Skips the current segment if it is already spanned by
                 * rounding up/down of earlier segment.
                 */
                if (map->pi_ops->pi_spans_seg(map, c2_ivec_cursor_index(cursor),
                                              c2_ivec_cursor_step(cursor))) {
                        count = c2_ivec_cursor_step(cursor);
                        continue;
                }

                INDEX(&map->pi_ivec, seg) =
			c2_round_down(c2_ivec_cursor_index(cursor),
				      PAGE_CACHE_SIZE);

                endpos = min64u(grpend, c2_ivec_cursor_index(cursor) +
                                c2_ivec_cursor_step(cursor));

                COUNT(&map->pi_ivec, seg) =
			c2_round_up(endpos, PAGE_CACHE_SIZE) -
			INDEX(&map->pi_ivec, seg);

                /* For read IO request, IO should not go beyond EOF. */
                if (map->pi_ioreq->ir_type == IRT_READ &&
                    INDEX(&map->pi_ivec, seg) + COUNT(&map->pi_ivec, seg) >
                    size)
                        COUNT(&map->pi_ivec, seg) = size - INDEX(&map->pi_ivec,
                                                                 seg);

                rc = map->pi_ops->pi_seg_process(map, seg, rmw);
                if (rc != 0)
                        C2_RETERR(rc, "seg_process failed");
                count = endpos - c2_ivec_cursor_index(cursor);
                ++map->pi_ivec.iv_vec.v_nr;
        }
        C2_LOG(C2_DEBUG, "%llu segments processed", seg);

        /*
         * Decides whether to undertake read-old approach or read-rest for
         * an rmw IO request.
         * By default, the segments in index vector pargrp_iomap::pi_ivec
         * are suitable for read-old approach.
         * Hence the index vector is changed only if read-rest approach
         * is selected.
         */
        if (rmw) {
                nr = pargrp_iomap_fullpages_count(map);

                /* can use number of data_buf structures instead of using
                 * indexvec_page_nr(). */
                ro_page_nr = /* Number of pages to be read. */
                             indexvec_page_nr(&map->pi_ivec.iv_vec) - nr +
                             parity_units_page_nr(play) +
                             /* Number of pages to be written. */
                             indexvec_page_nr(&map->pi_ivec.iv_vec) +
                             parity_units_page_nr(play);

                rr_page_nr = /* Number of pages to be read. */
                             page_nr(grpend - grpstart) - nr +
                             /* Number of pages to be written. */
                             indexvec_page_nr(&map->pi_ivec.iv_vec) +
                             parity_units_page_nr(play);

                if (rr_page_nr < ro_page_nr) {
                        rc = map->pi_ops->pi_readrest(map);
                        if (rc != 0)
                                C2_RETERR(rc, "readrest failed");
                } else {
                        map->pi_rtype = PIR_READOLD;
                        rc = map->pi_ops->pi_readold_auxbuf_alloc(map);
                }
        }

        if (map->pi_ioreq->ir_type == IRT_WRITE)
                rc = pargrp_iomap_paritybufs_alloc(map);

        C2_POST(ergo(rc == 0, pargrp_iomap_invariant(map)));

        C2_RETURN(rc);
}

static int ioreq_iomaps_prepare(struct io_request *req)
{
        int                       rc;
        uint64_t                  seg;
        uint64_t                  grp;
        uint64_t                  id;
        uint64_t                  grpstart;
        uint64_t                  grpend;
        uint64_t                 *grparray;
        struct c2_pdclust_layout *play;
        struct c2_ivec_cursor     cursor;

        C2_ENTRY("io_request %p", req);
        C2_PRE(req != NULL);

        play = pdlayout_get(req);

        C2_ALLOC_ARR_ADDB(grparray, max64u(c2_vec_count(&req->ir_ivec.iv_vec) /
                          data_size(play) + 1, SEG_NR(&req->ir_ivec)),
                          &c2t1fs_addb, &io_addb_loc);
        if (grparray == NULL)
                C2_RETERR(-ENOMEM, "Failed to allocate memory for int array");

        /*
         * Finds out total number of parity groups spanned by
         * io_request::ir_ivec.
         */
        for (seg = 0; seg < SEG_NR(&req->ir_ivec); ++seg) {
                grpstart = group_id(INDEX(&req->ir_ivec, seg), data_size(play));
                grpend   = group_id(INDEX(&req->ir_ivec, seg) +
                                    COUNT(&req->ir_ivec, seg), data_size(play));
                for (grp = grpstart; grp < grpend; ++grp) {
                        for (id = 0; id < req->ir_iomap_nr; ++id)
                                if (grparray[id] == grp)
                                        break;
                        if (id == req->ir_iomap_nr) {
                                grparray[id] = grp;
                                ++req->ir_iomap_nr;
                        }
                }
        }
        c2_free(grparray);

        C2_LOG(C2_DEBUG, "Number of pargrp_iomap structures : %llu",
               req->ir_iomap_nr);

        /* req->ir_iomaps is zeroed out on allocation. */
        C2_ALLOC_ARR_ADDB(req->ir_iomaps, req->ir_iomap_nr, &c2t1fs_addb,
                          &io_addb_loc);
        if (req->ir_iomaps == NULL) {
                rc = -ENOMEM;
                goto failed;
        }

        c2_ivec_cursor_init(&cursor, &req->ir_ivec);

        /*
         * cursor is advanced maximum by parity group size in one iteration
         * of this loop.
         * This is done by pargrp_iomap::pi_ops::pi_populate().
         */
        for (id = 0; !c2_ivec_cursor_move(&cursor, 0); ++id) {

                C2_ASSERT(id < req->ir_iomap_nr);
                C2_ASSERT(req->ir_iomaps[id] == NULL);
                C2_ALLOC_PTR_ADDB(req->ir_iomaps[id], &c2t1fs_addb,
                                  &io_addb_loc);
                if (req->ir_iomaps[id] == NULL) {
                        rc = -ENOMEM;
                        goto failed;
                }

                rc = pargrp_iomap_init(req->ir_iomaps[id], req,
                                       group_id(c2_ivec_cursor_index(&cursor),
                                                data_size(play)));
                if (rc != 0)
                        goto failed;

                rc = req->ir_iomaps[id]->pi_ops->pi_populate(req->
                                ir_iomaps[id], &req->ir_ivec, &cursor);
                if (rc != 0)
                        goto failed;
                C2_LOG(C2_DEBUG, "pargrp_iomap id : %llu populated", id);
        }

        C2_RETURN(0);
failed:

        for (id = 0; id < req->ir_iomap_nr ; ++id) {
                if (req->ir_iomaps[id] != NULL) {
                        pargrp_iomap_fini(req->ir_iomaps[id]);
                        c2_free(req->ir_iomaps[id]);
                        req->ir_iomaps[id] = NULL;
                }
        }
        c2_free(req->ir_iomaps);
        req->ir_iomaps = NULL;

        C2_RETERR(rc, "iomaps_prepare failed");
}

/*
 * Creates target_ioreq objects as required and populates
 * target_ioreq::ti_ivec and target_ioreq::ti_bufvec.
 */
static int nw_xfer_io_prepare(struct nw_xfer_request *xfer)
{
        int                         rc;
        uint64_t                    map;
        uint64_t                    unit;
        uint64_t                    unit_size;
        uint64_t                    count = 0;
        uint64_t                    pgstart;
        uint64_t                    pgend;
	/* Extent representing a data unit. */
        struct c2_ext               u_ext;
	/* Extent representing resultant extent. */
        struct c2_ext               r_ext;
	/* Extent representing a segment from index vector. */
        struct c2_ext               v_ext;
        struct io_request          *req;
        struct target_ioreq        *ti;
        struct c2_ivec_cursor       cursor;
        struct c2_pdclust_layout   *play;
        enum c2_pdclust_unit_type   unit_type;
        struct c2_pdclust_src_addr  src;
        struct c2_pdclust_tgt_addr  tgt;

        C2_ENTRY("nw_xfer_request %p", xfer);
        C2_PRE(nw_xfer_request_invariant(xfer));

        req       = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
        play      = pdlayout_get(req);
        unit_size = layout_unit_size(play);

        for (map = 0; map < req->ir_iomap_nr; ++map) {

                pgstart      = data_size(play) * req->ir_iomaps[map]->
                               pi_grpid;
                pgend        = pgstart + data_size(play);
                src.sa_group = req->ir_iomaps[map]->pi_grpid;

                /* Cursor for pargrp_iomap::pi_ivec. */
                c2_ivec_cursor_init(&cursor, &req->ir_iomaps[map]->pi_ivec);

                while (!c2_ivec_cursor_move(&cursor, count)) {

                        unit = (c2_ivec_cursor_index(&cursor) - pgstart) /
                               unit_size;

                        u_ext.e_start = pgstart + unit * unit_size;
                        u_ext.e_end   = u_ext.e_start + unit_size;

                        v_ext.e_start  = c2_ivec_cursor_index(&cursor);
                        v_ext.e_end    = v_ext.e_start +
                                          c2_ivec_cursor_step(&cursor);

                        c2_ext_intersection(&u_ext, &v_ext, &r_ext);
                        if (!c2_ext_is_valid(&r_ext))
                                continue;

                        count     = c2_ext_length(&r_ext);
                        unit_type = c2_pdclust_unit_classify(play, unit);
                        if (unit_type == C2_PUT_SPARE ||
                            unit_type == C2_PUT_PARITY)
                                continue;

                        src.sa_unit = unit;
                        rc = xfer->nxr_ops->nxo_tioreq_map(xfer, &src, &tgt,
                                                           &ti);
                        if (rc != 0)
                                goto err;

                        ti->ti_ops->tio_seg_add(ti, tgt.ta_frame, r_ext.e_start,
                                                c2_ext_length(&r_ext),
                                                unit_type);
                }

                /* Maps parity units. */
                for (unit = 0; unit < layout_k(play); ++unit) {

                        src.sa_unit = layout_n(play) + unit;
                        rc = xfer->nxr_ops->nxo_tioreq_map(xfer, &src, &tgt,
                                                           &ti);
                        if (rc != 0)
                                goto err;

                        /* This call doesn't deal with global file
                         * offset and counts. */
                        ti->ti_ops->tio_seg_add(ti, tgt.ta_frame, 0,
                                                layout_unit_size(play),
                                                src.sa_unit);
                }
        }

        C2_RETURN(0);
err:
        c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
                tioreqs_tlist_del(ti);
                target_ioreq_fini(ti);
                c2_free(ti);
                ti = NULL;
        } c2_tl_endfor;

        C2_RETERR(rc, "io_prepare failed");
}

static int ioreq_iosm_handle(struct io_request *req)
{
	int		   rc;
	int		   res;
        uint64_t           map;

        C2_ENTRY("io_request %p", req);
        C2_PRE(io_request_invariant(req));

        for (map = 0; map < req->ir_iomap_nr; ++map) {
                if (C2_IN(req->ir_iomaps[map]->pi_rtype,
                          (PIR_READOLD, PIR_READREST)))
                        break;
        }

        /*
         * Since c2_sm is part of io_request, for any parity group
         * which is partial, read-modify-write state transition is followed
         * for all parity groups.
         */
        if (map == req->ir_iomap_nr) {
		enum io_req_state state;

		state = req->ir_type == IRT_READ ? IRS_READING :
						   IRS_WRITING;
		if (state == IRS_WRITING) {
			rc = req->ir_ops->iro_user_data_copy(req,
                                        CD_COPY_FROM_USER, 0);
			if (rc != 0)
				goto fail;

                        rc = req->ir_ops->iro_parity_recalc(req);
                        if (rc != 0)
                                goto fail;
		}
		ioreq_sm_state_set(req, state);
		rc = nw_xfer_req_dispatch(&req->ir_nwxfer);
		if (rc != 0)
			goto fail;

		state = req->ir_type == IRT_READ ? IRS_READ_COMPLETE:
						   IRS_WRITE_COMPLETE;
		rc    = c2_sm_timedwait(&req->ir_sm, state, C2_TIME_NEVER);
                if (rc != 0)
                        ioreq_sm_failed(req, rc);

		C2_ASSERT(ioreq_sm_state(req) == state);

                if (state == IRS_READ_COMPLETE) {
                        rc = req->ir_ops->iro_user_data_copy(req,
                                        CD_COPY_TO_USER, 0);
                        if (rc != 0)
                                ioreq_sm_failed(req, rc);
                }
        } else {
		ioreq_sm_state_set(req, IRS_READING);
		rc = nw_xfer_req_dispatch(&req->ir_nwxfer);
		if (rc != 0)
			goto fail;

                /*
                 * If fops dispatch fails, we need to wait till all io fop
                 * callbacks are acked since IO fops have already been
                 * dispatched.
                 */
		res = req->ir_ops->iro_user_data_copy(req, CD_COPY_FROM_USER,
						      PA_FULLPAGE_MODIFY);

		rc = c2_sm_timedwait(&req->ir_sm, IRS_READ_COMPLETE,
				     C2_TIME_NEVER);

		if (res != 0 || rc != 0) {
                        rc = res != 0 ? res : rc;
			goto fail;
                }

		rc = req->ir_ops->iro_user_data_copy(req, CD_COPY_FROM_USER,
						     PA_PARTPAGE_MODIFY);
		if (rc != 0)
			goto fail;

                /* Finalizes the old read fops. */
                req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer, true);

		ioreq_sm_state_set(req, IRS_WRITING);
                rc = req->ir_ops->iro_parity_recalc(req);
                if (rc != 0)
                        goto fail;

		rc = nw_xfer_req_dispatch(&req->ir_nwxfer);
		if (rc != 0)
			goto fail;

		rc = c2_sm_timedwait(&req->ir_sm, IRS_WRITE_COMPLETE,
				     C2_TIME_NEVER);
		if (rc != 0)
			goto fail;
        }

        req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer, false);
	C2_RETURN(0);
fail:
        ioreq_sm_failed(req, rc);
        req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer, false);
        C2_RETERR(rc, "ioreq_iosm_handle failed");
}

static int io_request_init(struct io_request  *req,
                           struct file        *file,
                           struct iovec       *iov,
                           struct c2_indexvec *ivec,
                           enum io_req_type    rw)
{
        int                 rc;
        struct c2_indexvec *riv = &req->ir_ivec;

        C2_ENTRY("io_request %p, rw %d", req, rw);
        C2_PRE(req  != NULL);
        C2_PRE(file != NULL);
        C2_PRE(iov  != NULL);
        C2_PRE(ivec != NULL);
        C2_PRE(C2_IN(rw, (IRT_READ, IRT_WRITE)));

        req->ir_rc       = 0;
        req->ir_ops      = &ioreq_ops;
        req->ir_file     = file;
        req->ir_type     = rw;
        req->ir_iovec    = iov;
        req->ir_iomap_nr = 0;

        io_request_bob_init(req);
        nw_xfer_request_init(&req->ir_nwxfer);

	c2_sm_init(&req->ir_sm, &io_sm_conf, IRS_INITIALIZED,
		   file_to_smgroup(req->ir_file), &c2t1fs_addb);

        C2_ALLOC_ARR_ADDB(riv->iv_index, ivec->iv_vec.v_nr, &c2t1fs_addb,
                          &io_addb_loc);

        C2_ALLOC_ARR_ADDB(riv->iv_vec.v_count, ivec->iv_vec.v_nr, &c2t1fs_addb,
                          &io_addb_loc);

        riv->iv_vec.v_nr = ivec->iv_vec.v_nr;

        if (riv->iv_index == NULL || riv->iv_vec.v_count == NULL) {
                c2_free(riv->iv_index);
                c2_free(riv->iv_vec.v_count);
                C2_RETERR(-ENOMEM, "Allocation failed for c2_indexvec");
        }

        memcpy(riv->iv_index, ivec->iv_index, ivec->iv_vec.v_nr *
               sizeof(c2_bindex_t));
        memcpy(riv->iv_vec.v_count, ivec->iv_vec.v_count, ivec->iv_vec.v_nr *
               sizeof(c2_bcount_t));

        /* Sorts the index vector in increasing order of file offset. */
        indexvec_sort(riv);

        /*
         * Prepares io maps for each parity group/s spanned by segments
         * in index vector.
         */
        rc = req->ir_ops->iro_iomaps_prepare(req);

        C2_POST(ergo(rc == 0, io_request_invariant(req)));
        C2_RETURN(rc);
}

static void io_request_fini(struct io_request *req)
{
        uint64_t             i;
        struct target_ioreq *ti;

        C2_ENTRY("io_request %p", req);
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
        req->ir_iomap_nr = 0;
        req->ir_iomaps   = NULL;
        req->ir_ops      = NULL;

        c2_tl_for (tioreqs, &req->ir_nwxfer.nxr_tioreqs, ti) {
                   tioreqs_tlist_del(ti);
                /*
                 * All io_req_fop structures in list target_ioreq::ti_iofops
                 * are already finalized in nw_xfer_req_complete().
                 */
                target_ioreq_fini(ti);
        } c2_tl_endfor;

        nw_xfer_request_fini(&req->ir_nwxfer);
        C2_LEAVE();
}

static void data_buf_init(struct data_buf *buf, void *addr, uint64_t flags)
{
        C2_PRE(buf  != NULL);
        C2_PRE(addr != NULL);

        data_buf_bob_init(buf);
        buf->db_flags = flags;
        c2_buf_init(&buf->db_buf, addr, PAGE_CACHE_SIZE);
}

static void data_buf_fini(struct data_buf *buf)
{
        C2_PRE(buf != NULL);

        data_buf_bob_fini(buf);
        buf->db_flags = PA_NONE;
}

static int nw_xfer_tioreq_map(struct nw_xfer_request      *xfer,
                              struct c2_pdclust_src_addr  *src,
                              struct c2_pdclust_tgt_addr  *tgt,
                              struct target_ioreq        **out)
{
        int                       rc;
        struct c2_fid             tfid;
        struct io_request        *req;
        struct c2_rpc_session    *session;
        struct c2_pdclust_layout *play;

        C2_ENTRY("nw_xfer_request %p", xfer);
        C2_PRE(nw_xfer_request_invariant(xfer));
        C2_PRE(src != NULL);
        C2_PRE(tgt != NULL);

        req  = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
        play = pdlayout_get(req);

        c2_pdclust_instance_map(pdlayout_instance(layout_instance(req)),
                                src, tgt);
        tfid    = target_fid(req, tgt);
        session = target_session(req, tfid);

        rc = nw_xfer_tioreq_get(xfer, &tfid, session,
                                layout_unit_size(play)* req->ir_iomap_nr,
                                out);
        C2_RETURN(rc);
}

static int target_ioreq_init(struct target_ioreq    *ti,
                             struct nw_xfer_request *xfer,
                             const struct c2_fid    *cobfid,
                             struct c2_rpc_session  *session,
                             uint64_t                size)
{
        C2_ENTRY("target_ioreq %p, nw_xfer_request %p, fid %p",
                 ti, xfer, cobfid);
        C2_PRE(ti      != NULL);
        C2_PRE(xfer    != NULL);
        C2_PRE(cobfid  != NULL);
        C2_PRE(session != NULL);
        C2_PRE(size    >  0);

        ti->ti_rc      = 0;
        ti->ti_ops     = &tioreq_ops;
        ti->ti_fid     = *cobfid;
        ti->ti_bytes   = 0;
        ti->ti_nwxfer  = xfer;
        ti->ti_session = session;

        iofops_tlist_init(&ti->ti_iofops);
        tioreqs_tlink_init(ti);
        target_ioreq_bob_init(ti);

        /*
         * This value is incremented when new segments are added to the
         * index vector.
         */
        ti->ti_ivec.iv_vec.v_nr = 0;
        ti->ti_bufvec.ov_vec.v_nr = page_nr(size);

        C2_ALLOC_ARR_ADDB(ti->ti_ivec.iv_index, ti->ti_bufvec.ov_vec.v_nr,
                          &c2t1fs_addb, &io_addb_loc);
        if (ti->ti_ivec.iv_index == NULL)
                goto fail;

        C2_ALLOC_ARR_ADDB(ti->ti_ivec.iv_vec.v_count, ti->ti_bufvec.ov_vec.v_nr,
                          &c2t1fs_addb, &io_addb_loc);
        if (ti->ti_ivec.iv_index == NULL)
                goto fail;

        C2_ALLOC_ARR_ADDB(ti->ti_bufvec.ov_vec.v_count,
                          ti->ti_bufvec.ov_vec.v_nr, &c2t1fs_addb,
                          &io_addb_loc);
        if (ti->ti_bufvec.ov_vec.v_count == NULL)
                goto fail;

        C2_ALLOC_ARR_ADDB(ti->ti_bufvec.ov_buf, ti->ti_bufvec.ov_vec.v_nr,
                          &c2t1fs_addb, &io_addb_loc);
        if (ti->ti_bufvec.ov_buf == NULL)
                goto fail;

        C2_ALLOC_ARR_ADDB(ti->ti_pageattrs, ti->ti_bufvec.ov_vec.v_nr,
                          &c2t1fs_addb, &io_addb_loc);
        if (ti->ti_pageattrs == NULL)
                goto fail;

        C2_POST(target_ioreq_invariant(ti));
        C2_RETURN(0);
fail:
        c2_free(ti->ti_ivec.iv_index);
        c2_free(ti->ti_ivec.iv_vec.v_count);
        c2_free(ti->ti_bufvec.ov_vec.v_count);
        c2_free(ti->ti_bufvec.ov_buf);

        C2_RETERR(-ENOMEM, "Failed to allocate memory in target_ioreq_init");
}

static void target_ioreq_fini(struct target_ioreq *ti)
{
        C2_ENTRY("target_ioreq %p", ti);
        C2_PRE(target_ioreq_invariant(ti));

        target_ioreq_bob_fini(ti);
        tioreqs_tlink_fini(ti);
        iofops_tlist_fini(&ti->ti_iofops);
        ti->ti_ops     = NULL;
        ti->ti_session = NULL;
        ti->ti_nwxfer  = NULL;

        c2_free(ti->ti_ivec.iv_index);
        c2_free(ti->ti_ivec.iv_vec.v_count);
        c2_free(ti->ti_bufvec.ov_buf);
        c2_free(ti->ti_bufvec.ov_vec.v_count);
        c2_free(ti->ti_pageattrs);

        ti->ti_ivec.iv_index         = NULL;
        ti->ti_ivec.iv_vec.v_count   = NULL;
        ti->ti_bufvec.ov_buf         = NULL;
        ti->ti_bufvec.ov_vec.v_count = NULL;
        ti->ti_pageattrs             = NULL;
        C2_LEAVE();
}

static struct target_ioreq *target_ioreq_locate(struct nw_xfer_request *xfer,
                                                struct c2_fid          *fid)
{
        struct target_ioreq *ti;

        C2_ENTRY("nw_xfer_request %p, fid %p", xfer, fid);
        C2_PRE(nw_xfer_request_invariant(xfer));
        C2_PRE(fid != NULL);

        c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
                if (c2_fid_eq(&ti->ti_fid, fid))
                        break;
        } c2_tl_endfor;

        C2_LEAVE();
        return ti;
}

static int nw_xfer_tioreq_get(struct nw_xfer_request *xfer,
                              struct c2_fid          *fid,
                              struct c2_rpc_session  *session,
                              uint64_t                size,
                              struct target_ioreq   **out)
{
        int                  rc = 0;
        struct target_ioreq *ti;

        C2_ENTRY("nw_xfer_request %p, fid %p", xfer, fid);
        C2_PRE(nw_xfer_request_invariant(xfer));
        C2_PRE(fid     != NULL);
        C2_PRE(session != NULL);
        C2_PRE(out     != NULL);

        ti = target_ioreq_locate(xfer, fid);
        if (ti == NULL) {
                C2_ALLOC_PTR_ADDB(ti, &c2t1fs_addb, &io_addb_loc);
                if (ti == NULL)
                        C2_RETERR(-ENOMEM, "Failed to allocate memory"
                                  "for target_ioreq");

                rc = target_ioreq_init(ti, xfer, fid, session, size);
                rc == 0 ? tioreqs_tlist_add(&xfer->nxr_tioreqs, ti) :
                          c2_free(ti);
        }
        *out = ti;
        C2_RETURN(rc);
}

static struct data_buf *data_buf_alloc_init(enum page_attr pattr)
{
        struct data_buf *buf;
        unsigned long    addr;

        C2_ENTRY();
        addr = get_zeroed_page(GFP_KERNEL);
        if (addr == 0) {
                C2_LOG(C2_ERROR, "Failed to get free page");
                return NULL;
        }

        C2_ALLOC_PTR_ADDB(buf, &c2t1fs_addb, &io_addb_loc);
        if (buf == NULL) {
                free_page(addr);
                C2_LOG(C2_ERROR, "Failed to allocate data_buf");
                return NULL;
        }

        data_buf_init(buf, (void *)addr, pattr);
        C2_POST(data_buf_invariant(buf));
        C2_LEAVE();
        return buf;
}

static void data_buf_dealloc_fini(struct data_buf *buf)
{
        C2_ENTRY("data_buf %p", buf);
        C2_PRE(data_buf_invariant(buf));

        if (buf->db_buf.b_addr != NULL) {
                free_page((unsigned long)buf->db_buf.b_addr);
                buf->db_buf.b_addr = NULL;
                buf->db_buf.b_nob  = 0;
        }
        data_buf_fini(buf);
        c2_free(buf);
        C2_LEAVE();
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
        struct data_buf           *buf;
        struct io_request         *req;
        struct pargrp_iomap       *map;
        struct c2_pdclust_layout  *play;
        enum c2_pdclust_unit_type unit_type;

        C2_ENTRY("target_ioreq %p, gob_offset %llu, count %llu",
                 ti, gob_offset, count);
        C2_PRE(target_ioreq_invariant(ti));

        req     = bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
                         &ioreq_bobtype);
        play    = pdlayout_get(req);
        toff    = target_offset(frame, layout_unit_size(play), gob_offset);
        dtsize  = data_size(play);
        req->ir_ops->iro_iomap_locate(req, group_id(gob_offset, dtsize),
                                      &map);
        C2_ASSERT(map != NULL);
        pgstart = toff;
        goff    = gob_offset;

        unit_type = c2_pdclust_unit_classify(play, unit);
        C2_ASSERT(unit_type == C2_PUT_DATA || unit_type == C2_PUT_PARITY);

        while (pgstart < toff + count) {
                pgend = min64u(pgstart + PAGE_CACHE_SIZE, toff + count);
                seg   = SEG_NR(&ti->ti_ivec);

                INDEX(&ti->ti_ivec, seg) = pgstart;
                COUNT(&ti->ti_ivec, seg) = pgend - pgstart;

                ti->ti_bufvec.ov_vec.v_count[seg] = pgend - pgstart;

                if (unit_type == C2_PUT_DATA) {
                        uint32_t row;
                        uint32_t col;

                        page_pos_get(map, goff, &row, &col);
                        buf = map->pi_databufs[row][col];

                        if (buf->db_flags & PA_READ_EOF)
                                COUNT(&ti->ti_ivec, seg) =
					ti->ti_bufvec.ov_vec.v_count[seg] =
					buf->db_buf.b_nob;
                }
                else
                        buf = map->pi_paritybufs[page_id(goff)]
				                [unit % data_col_nr(play)];

                ti->ti_bufvec.ov_buf[seg] = buf->db_buf.b_addr;
                ti->ti_pageattrs[seg]     = buf->db_flags;

                goff += COUNT(&ti->ti_ivec, seg);
                ++ti->ti_ivec.iv_vec.v_nr;
                pgstart = pgend;
        }
        C2_LEAVE();
}

static int io_req_fop_init(struct io_req_fop   *fop,
                           struct target_ioreq *ti)
{
        int                rc;
        struct io_request *req;

        C2_ENTRY("io_req_fop %p, target_ioreq %p", fop, ti);
        C2_PRE(fop != NULL);
        C2_PRE(ti  != NULL);

        io_req_fop_bob_init(fop);
        iofops_tlink_init(fop);
        fop->irf_tioreq    = ti;
        fop->irf_ast.sa_cb = io_bottom_half;

        req = bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
                     &ioreq_bobtype);

        rc  = c2_io_fop_init(&fop->irf_iofop, req->ir_sm.sm_state ==
                             IRS_READING ? &c2_fop_cob_readv_fopt :
                             &c2_fop_cob_writev_fopt);
        /*
         * Change ri_ops of rpc item so as to execute c2t1fs's own
         * callback on receiving a reply.
         */
        fop->irf_iofop.if_fop.f_item.ri_ops = &c2t1fs_item_ops;

        C2_POST(ergo(rc == 0, io_req_fop_invariant(fop)));
        C2_RETURN(rc);
}

static void io_req_fop_fini(struct io_req_fop *fop)
{
        C2_ENTRY("io_req_fop %p", fop);
        C2_PRE(io_req_fop_invariant(fop));

        /**
         * IO fop is finalized (c2_io_fop_fini()) through rpc sessions code
         * using c2_rpc_item::c2_rpc_item_ops::rio_free().
         * @see c2_io_item_free().
         */

        iofops_tlink_fini(fop);
        io_req_fop_bob_fini(fop);
        fop->irf_tioreq = NULL;
        C2_LEAVE();
}

static void irfop_fini(struct io_req_fop *irfop)
{
        C2_ENTRY("io_req_fop %p", irfop);
        C2_PRE(irfop != NULL);

        c2_rpc_bulk_buflist_empty(&irfop->irf_iofop.if_rbulk);
        io_req_fop_fini(irfop);
        c2_free(irfop);
        C2_LEAVE();
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

        C2_ENTRY("indexvec %p, rw %d", ivec, rw);
        C2_PRE(kcb  != NULL);
        C2_PRE(iov  != NULL);
        C2_PRE(ivec != NULL);
        C2_PRE(C2_IN(rw, (IRT_READ, IRT_WRITE)));

        C2_ALLOC_PTR_ADDB(req, &c2t1fs_addb, &io_addb_loc);
        if (req == NULL)
                C2_RETERR(-ENOMEM, "Failed to allocate memory for io_request");

        rc = io_request_init(req, kcb->ki_filp, (struct iovec *)iov, ivec, rw);
        if (rc != 0) {
                count = rc;
                goto last;
        }

        rc = req->ir_nwxfer.nxr_ops->nxo_prepare(&req->ir_nwxfer);
        if (rc != 0) {
                io_request_fini(req);
                count = rc;
                goto last;
        }

        rc = req->ir_ops->iro_iosm_handle(req);
        count = req->ir_nwxfer.nxr_bytes;
	if (req->ir_rc != 0)
                count = rc;

	io_request_fini(req);
last:
	c2_free(req);
        C2_LEAVE();

        return count;
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
        C2_ENTRY("seg_nr %lu position %llu", seg_nr, pos);
        C2_ALLOC_PTR_ADDB(ivec, &c2t1fs_addb, &io_addb_loc);
        if (ivec == NULL) {
                C2_LEAVE();
                return NULL;
        }

        ivec->iv_vec.v_nr    = seg_nr;
        C2_ALLOC_ARR_ADDB(ivec->iv_index, seg_nr, &c2t1fs_addb, &io_addb_loc);
        C2_ALLOC_ARR_ADDB(ivec->iv_vec.v_count, seg_nr, &c2t1fs_addb,
                          &io_addb_loc);

        if (ivec->iv_index == NULL || ivec->iv_vec.v_count == NULL) {
                c2_free(ivec->iv_index);
                c2_free(ivec->iv_vec.v_count);
                c2_free(ivec);
                C2_LEAVE();
                return NULL;
        }

        for (i = 0; i < seg_nr; ++i) {
                ivec->iv_index[i] = pos;
                ivec->iv_vec.v_count[i] = iov[i].iov_len;
                pos += iov[i].iov_len;
        }
        C2_POST(c2_vec_count(&ivec->iv_vec) > 0);

        C2_LEAVE();
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

        C2_ENTRY("struct iovec %p position %llu", iov, pos);
        C2_PRE(kcb != NULL);
        C2_PRE(iov != NULL);
        C2_PRE(seg_nr > 0);

        if (!file_to_sb(kcb->ki_filp)->csb_active) {
                C2_LEAVE();
                return -EINVAL;
        }

	rc = generic_segment_checks(iov, &seg_nr, &count, VERIFY_READ);
	if (rc != 0) {
                C2_LEAVE();
                return 0;
        }

	saved_count = count;
	rc = generic_write_checks(kcb->ki_filp, &pos, &count, 0);
	if (rc != 0 || count == 0) {
                C2_LEAVE();
                return 0;
        }

	if (count != saved_count)
		seg_nr = iov_shorten((struct iovec *)iov, seg_nr, count);

        ivec = indexvec_create(seg_nr, iov, pos);
        if (ivec == NULL) {
                C2_LEAVE();
                return 0;
        }

        C2_LEAVE();
        return c2t1fs_aio(kcb, iov, ivec, IRT_WRITE);
}

static ssize_t file_aio_read(struct kiocb       *kcb,
                             const struct iovec *iov,
                             unsigned long       seg_nr,
                             loff_t              pos)
{
        int                 seg;
        ssize_t             count = 0;
        ssize_t             res;
        struct inode       *inode;
        struct c2_indexvec *ivec;

        C2_ENTRY("struct iovec %p position %llu", iov, pos);
        C2_PRE(kcb != NULL);
        C2_PRE(iov != NULL);
        C2_PRE(seg_nr > 0);

        /* Returns if super block is inactive. */
        if (!file_to_sb(kcb->ki_filp)->csb_active) {
                C2_LEAVE();
                return -EINVAL;
        }

        /*
         * Checks for access privileges and adjusts all segments
         * for proper count and total number of segments.
         */
        res = generic_segment_checks(iov, &seg_nr, &count, VERIFY_WRITE);
        if (res != 0) {
                C2_LEAVE();
                return res;
        }

        /* Index vector has to be created before io_request is created. */
        ivec = indexvec_create(seg_nr, iov, pos);
        if (ivec == NULL) {
                C2_LEAVE();
                return 0;
        }

        /*
         * For read IO, if any segment from index vector goes beyond EOF,
         * they are dropped and the index vector is truncated to EOF boundary.
         */
        inode = kcb->ki_filp->f_dentry->d_inode;
        for (seg = 0; seg < SEG_NR(ivec); ++seg) {
                if (INDEX(ivec, seg) > inode->i_size)
                        break;
                if (INDEX(ivec, seg) + COUNT(ivec, seg) > inode->i_size) {
                        COUNT(ivec, seg) = inode->i_size - INDEX(ivec, seg);
                        break;
                }
        }
        ivec->iv_vec.v_nr = seg + 1;

        C2_LEAVE();
        return c2t1fs_aio(kcb, iov, ivec, IRT_READ);
}

const struct file_operations c2t1fs_reg_file_operations = {
	.llseek    = generic_file_llseek,
	.aio_read  = file_aio_read,
	.aio_write = file_aio_write,
	.read      = do_sync_read,
	.write     = do_sync_write,
};

static int io_fops_async_submit(struct c2_io_fop      *iofop,
		                struct c2_rpc_session *session)
{
	int		      rc;
	struct c2_fop_cob_rw *rwfop;

	C2_ENTRY("c2_io_fop %p c2_rpc_session %p", iofop, session);
	C2_PRE(iofop   != NULL);
        C2_PRE(session != NULL);

	rwfop = io_rw_get(&iofop->if_fop);
	C2_ASSERT(rwfop != NULL);

	rc = c2_rpc_bulk_store(&iofop->if_rbulk, session->s_conn,
			       rwfop->crw_desc.id_descs);
	if (rc != 0)
		goto out;

	iofop->if_fop.f_item.ri_session = session;
        rc = c2_rpc_post(&iofop->if_fop.f_item);

out:
	C2_RETURN(rc);
}

static void io_rpc_item_cb(struct c2_rpc_item *item)
{
        struct c2_fop     *fop;
        struct c2_io_fop  *iofop;
	struct io_req_fop *reqfop;
	struct io_request *ioreq;

        C2_ENTRY("rpc_item %p", item);
        C2_PRE(item != NULL);

        fop    = container_of(item, struct c2_fop, f_item);
        iofop  = container_of(fop, struct c2_io_fop, if_fop);
	reqfop = bob_of(iofop, struct io_req_fop, irf_iofop, &iofop_bobtype);
	ioreq  = bob_of(reqfop->irf_tioreq->ti_nwxfer, struct io_request,
                        ir_nwxfer, &ioreq_bobtype);

	c2_sm_ast_post(ioreq->ir_sm.sm_grp, &reqfop->irf_ast);
        C2_LEAVE();
}

static void io_bottom_half(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct io_req_fop   *irfop;
        struct io_request   *req;
	struct target_ioreq *tioreq;

        C2_ENTRY("sm_group %p sm_ast %p", grp, ast);
        C2_PRE(grp != NULL);
        C2_PRE(ast != NULL);

	irfop  = bob_of(ast, struct io_req_fop, irf_ast, &iofop_bobtype);
	tioreq = irfop->irf_tioreq;
        req    = bob_of(tioreq->ti_nwxfer, struct io_request, ir_nwxfer,
                        &ioreq_bobtype);

	if (tioreq->ti_rc == 0)
                tioreq->ti_rc = irfop->irf_iofop.if_rbulk.rb_rc;

	tioreq->ti_bytes += irfop->irf_iofop.if_rbulk.rb_bytes;
	C2_CNT_DEC(tioreq->ti_nwxfer->nxr_iofop_nr);

	if (tioreq->ti_nwxfer->nxr_iofop_nr == 0) {
                uint32_t state;

                state = ioreq_sm_state(req) == IRS_READING ? IRS_READ_COMPLETE :
                                                            IRS_WRITE_COMPLETE;
                c2_sm_state_set(&req->ir_sm, state);
        }
        C2_LEAVE();
}

static int nw_xfer_req_dispatch(struct nw_xfer_request *xfer)
{
	int		     rc = 0;
        struct io_req_fop   *irfop;
        struct target_ioreq *ti;

        C2_PRE(xfer != NULL);

	c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
		rc = ti->ti_ops->tio_iofops_prepare(ti);
		if (rc != 0)
			C2_RETERR(rc, "iofops_prepare failed");
	} c2_tl_endfor;

	c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
		c2_tl_for(iofops, &ti->ti_iofops, irfop) {

			rc = io_fops_async_submit(&irfop->irf_iofop,
                                                  ti->ti_session);
			if (rc != 0);
				goto out;
		} c2_tl_endfor;

	} c2_tl_endfor;

out:
        xfer->nxr_state = NXS_INFLIGHT;
	C2_RETURN(rc);
}

static void nw_xfer_req_complete(struct nw_xfer_request *xfer, bool rmw)
{
        struct io_request   *req;
        struct target_ioreq *ti;

        C2_ENTRY("nw_xfer_request %p, rmw %s", xfer,
                 rmw ? (char *)"true" : (char *)"false");
        C2_PRE(xfer != NULL);
        xfer->nxr_state = NXS_COMPLETE;

	req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
        C2_ASSERT(io_request_invariant(req));

	c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
                struct io_req_fop *irfop;

		c2_tl_for(iofops, &ti->ti_iofops, irfop) {
			io_req_fop_fini(irfop);
                } c2_tl_endfor;
        } c2_tl_endfor;

	if (!rmw)
                ioreq_sm_state_set(req, IRS_REQ_COMPLETE);

	req->ir_rc = xfer->nxr_rc;
        C2_LEAVE();
}

/*
 * Used in precomputing of io fop size while adding rpc bulk buffer and
 * data buffers.
 */
static inline uint32_t io_desc_size(struct c2_net_domain *ndom)
{
	return
                /* size of variables ci_nr and nbd_len */
		sizeof(uint32_t) * 2  +
                /* size of nbd_data */
		c2_net_domain_get_buffer_desc_size(ndom);
}

static inline uint32_t io_seg_size(void)
{
	return sizeof(struct c2_ioseg);
}

static int bulk_buffer_add(struct io_req_fop       *irfop,
                           struct c2_net_domain    *dom,
                           struct c2_rpc_bulk_buf **rbuf,
                           uint32_t                *delta,
                           uint32_t                 maxsize)
{
        int                rc;
        int                seg_nr;
        struct io_request *req;

        C2_ENTRY("io_req_fop %p net_domain %p delta_size %d",
                 irfop, dom, *delta);
        C2_PRE(irfop  != NULL);
        C2_PRE(dom    != NULL);
        C2_PRE(rbuf   != NULL);
        C2_PRE(delta  != NULL);
        C2_PRE(maxsize > 0);

        req    = bob_of(irfop->irf_tioreq->ti_nwxfer, struct io_request,
                        ir_nwxfer, &ioreq_bobtype);
        seg_nr = min32(c2_net_domain_get_max_buffer_segments(dom),
                       SEG_NR(&irfop->irf_tioreq->ti_ivec));

        *delta += io_desc_size(dom);
        if (c2_io_fop_size_get(&irfop->irf_iofop.if_fop) + *delta < maxsize) {

                rc = c2_rpc_bulk_buf_add(&irfop->irf_iofop.if_rbulk, seg_nr,
                                         dom, NULL, rbuf);
                if (rc != 0) {
                        *delta -= io_desc_size(dom);
                        C2_RETERR(rc, "Failed to add rpc_bulk_buffer");
                }
        }

        C2_POST(*rbuf != NULL);
        C2_RETURN(rc);
}

static int target_ioreq_iofops_prepare(struct target_ioreq *ti)
{
        int                     rc;
        uint32_t                buf = 0;
        uint32_t                maxsize;
        uint32_t                delta = 0;
        enum page_attr          pattr;
        struct io_request      *req;
        struct io_req_fop      *irfop;
        struct c2_net_domain   *ndom;
        struct c2_rpc_bulk_buf *rbuf;

        C2_ENTRY("target_ioreq %p", ti);
        C2_PRE(target_ioreq_invariant(ti));

        req     = bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
                         &ioreq_bobtype);
        C2_ASSERT(C2_IN(ioreq_sm_state(req), (IRS_READING, IRS_WRITING)));

        ndom    = ti->ti_session->s_conn->c_rpc_machine->rm_tm.ntm_dom;
        pattr   = ioreq_sm_state(req) == IRS_READING ? PA_READ : PA_WRITE;
        maxsize = c2_max_fop_size(ti->ti_session->s_conn->c_rpc_machine);

        while (buf < SEG_NR(&ti->ti_ivec)) {

                C2_ALLOC_PTR_ADDB(irfop, &c2t1fs_addb, &io_addb_loc);
                if (irfop == NULL)
                        goto err;

                rc = io_req_fop_init(irfop, ti);
                if (rc != 0) {
                        c2_free(irfop);
                        goto err;
                }

                rc = bulk_buffer_add(irfop, ndom, &rbuf, &delta, maxsize);
                if (rc != 0) {
                        io_req_fop_fini(irfop);
                        c2_free(irfop);
                        goto err;
                }
                delta += io_seg_size();

                /*
                 * Adds io segments and io descriptor only if it fits within
                 * permitted size.
                 */
                while (buf < SEG_NR(&ti->ti_ivec) &&
                       c2_io_fop_size_get(&irfop->irf_iofop.if_fop) + delta <
                       maxsize) {

                        delta += io_seg_size();
                        rc = c2_rpc_bulk_buf_databuf_add(rbuf,
                                                ti->ti_bufvec.ov_buf[buf],
                                                COUNT(&ti->ti_ivec, buf),
                                                INDEX(&ti->ti_ivec, buf), ndom);

                        if (rc == -EMSGSIZE) {
                                delta -= io_seg_size();
                                rc     = bulk_buffer_add(irfop, ndom, &rbuf,
                                                         &delta, maxsize);
                                if (rc != 0)
                                        goto fini_fop;

                                continue;
                        }
                        ++buf;
                }

                rc = c2_io_fop_prepare(&irfop->irf_iofop.if_fop);
                if (rc != 0)
                        goto fini_fop;

		C2_CNT_INC(ti->ti_nwxfer->nxr_iofop_nr);
                iofops_tlist_add(&ti->ti_iofops, irfop);
        }

        C2_RETURN(0);
fini_fop:
        irfop_fini(irfop);
err:
        c2_tlist_for (&iofops_tl, &ti->ti_iofops, irfop) {
                iofops_tlist_del(irfop);
                irfop_fini(irfop);
        } c2_tlist_endfor;

        C2_RETERR(rc, "iofops_prepare failed");
}
