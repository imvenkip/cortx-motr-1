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
 * Original author: Anand Vidwansa <anand_vidwansa@xyratex.com>
 * Original creation date: 10/09/2012
 */

#pragma once

#ifndef __MERO_M0T1FS_FILE_INTERNAL_H__
#define __MERO_M0T1FS_FILE_INTERNAL_H__

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

   <hr>
   @section rmw-ovw Overview

   The read-modify-write feature provides support to do partial parity group
   IO requests on a Mero client.
   This feature is needed for write IO requests _only_.

   Mero uses notion of layout to represent how file data is spread
   over multiple objects. Mero client is supposed to work with
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
   m0t1fs - Mero client file system.
   layout - A map which decides how to distribute file data over a number
            of objects.

   <hr>
   @section rmw-req Requirements

   - @b R.m0t1fs.rmw_io.rmw The implementation shall provide support for
   partial parity group IO requests.

   - @b R.m0t1fs.fullvectIO The implementation shall support fully vectored
   scatter-gather IO where multiple IO segments could be directed towards
   multiple file offsets. This functionality will be used by cluster library
   writers through an ioctl.

   - @b R.m0t1fs.rmw_io.efficient IO requests should be implemented efficiently.
   Since Mero follows async nature of APIs as far as possible, the
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

   - The component will sit in IO path of Mero client code.

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

   @note Present implementation of m0t1fs IO path uses get_user_pages() API
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
   The state transitions of io_request structure are handled by m0_sm
   structure and its support for chained state transitions.

   The m0_sm_group structure is kept as a member of in-memory superblock,
   struct m0t1fs_sb.
   All m0_sm structures are associated with this group.
   @see struct m0t1fs_sb.

   io_req_state - Represents state of IO request call.
   m0_sm_state_descr structure will be defined for description of all
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
   m0_buf::b_addr points to a kernel page or a user-space buffer in case of
   direct IO.

   target_ioreq - Collection of IO extents and buffers, directed towards each
   of the component objects in a parity group.
   These structures are created by struct io_request dividing the incoming
   struct iovec into members of parity group.

   ast thread - A thread will be maintained per super block instance
   m0t1fs_sb to wake up on receiving ASTs and execute the bottom halves.
   This thread will run as long as m0t1fs is mounted.
   This thread will handle the pending IO requests gracefully when unmount
   is triggered.

   The in-memory super block struct m0t1fs_sb will maintain

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
           m0_chan_wait(&sm_group->s_clink);
           m0_sm_group_lock(sm_group);
           m0_sm_asts_run(sm_group);
           m0_sm_group_unlock(sm_group);
           if (!sb->csb_active && m0_atomic64_get(&sb->csb_pending_io_nr) == 0){
                   m0_chan_signal(&sb->csb_iowait);
                   break;
           }
   }

   @endcode

   So, while m0t1fs is mounted, the thread will keep running in loop waiting
   for ASTs.

   When unmount is triggered, new io requests will be returned with an error
   code as m0t1fs_sb::csb_active flag is reset.
   For pending io requests, the ast thread will wait until callbacks
   from all outstanding io requests are acknowledged and executed.

   Once all pending io requests are dealt with, the ast thread will exit
   waking up the unmount thread.

   Abort is not supported at the moment in m0t1fs io code as it needs same
   functionality from layer beneath.

   When bottom halves for m0_sm_ast structures are run, it updates
   target_ioreq::tir_rc and target_ioreq::tir_bytes with data from
   IO reply fop.
   Then the bottom half decrements nw_xfer_request::nxr_iofops_nr,
   number of IO fops.  When this count reaches zero, io_request::ir_sm
   changes its state.

   A callback function will be used to associate with m0_rpc_item::ri_ops::
   rio_replied(). This callback will be invoked once rpc layer receives the
   reply rpc item. This callback will post the io_req_fop::irf_ast and
   will thus wake up the thread which executes the ASTs.

   io_req_fop - Represents a wrapper over generic IO fop and its callback
   to keep track of such IO fops issued by same nw_xfer_request.

   io_rpc_item_cb - Callback used to receive fop reply events.

   io_bottom_half - Bottom-half function for IO request.

   Magic value to verify sanity of struct io_request, struct nw_xfer_request,
   struct target_ioreq and struct io_req_fop.
   The magic values are used along with static m0_bob_type structures to
   assert run-time type identification.

   @subsubsection rmw-lspec-sub1 Subcomponent Subroutines

   In order to satisfy the requirement of an ioctl API for fully vectored
   scatter-gather IO (where multiple IO segments are directed at multiple
   file offsets), a new API will be introduced that will use a m0_indexvec
   structure to specify multiple file offsets.

   @code
   ssize_t m0t1fs_aio(struct kiocb *iocb, const struct iovec *iov,
                      const struct m0_indexvec *ivec, enum io_req_type type);
   @endcode

   In case of normal file->f_op->aio_{read/write} calls, the m0_indexvec
   will be created and populated by m0t1fs code, while in case of ioctl, it
   has to be provided by user-space.

   @ref io_request_invariant - Invariant for structure io_request.

   @ref nw_xfer_request_invariant - Invariant for structure nw_xfer_request.

   @ref data_buf_invariant_nr - A helper function to invoke invariant() for
   all data_buf structures in array pargrp_iomap::tir_databufs.
   It is on similar lines of m0_tl_forall() API.

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
   state machine (m0_sm) will wait for completion of read or write IO.
   Incoming callbacks will be registered with the m0_sm state machine where
   the bottom half functions will be executed on the "ast" thread.
   The ast thread and the system call thread coordinate by using
   m0_sm_group::s_lock mutex.
   @see sm. Concurrency section of State machine.

   @todo In future, with introduction of Resource Manager, distributed extent
   locks have to be acquired or released as needed.

   @subsection rmw-lspec-numa NUMA optimizations

   None.

   <hr>
   @section rmw-conformance Conformance

   - @b I.m0t1fs.rmw_io.rmw The implementation maintains an io map per parity
   group in a data structure pargrp_iomap. The io map rounds up/down the
   incoming io segments to nearest page boundaries.
   The missing data will be read first from data server (later from client cache
   which is missing at the moment and then from data server).
   Data is copied from user-space at desired file offsets and then it will be
   sent to server as a write IO request.

   - @b I.m0t1fs.rmw_io.efficient The implementation uses an asynchronous
   way of waiting for IO requests and does not send the requests one after
   another as is done with current implementation. This leads in only one
   conditional wait instead of waits proportional to number of IO requests
   as is done with current implementation.

   <hr>
   @section rmw-ut Unit Tests

   The UT will exercise following unit test scenarios.
   @todo However, with all code in kernel and no present UT code for m0t1fs,
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

     - implementation of io fops, m0_sm and all state entry and exit functions.
       Referred to as nwio-sm henceforth.

   - New interfaces are consumed by same module (m0t1fs). The primary
   data structures and interfaces have been identified and defined.

   - The subtasks rwm-support and nwio-sm will commence parallely.

   - The subtask rmw-core can be tested independently of other subtask. Once,
   all it is found to be working properly, it can be integrated with nwio-sm
   subtask.

 */

/* export */
struct io_request;
struct nw_xfer_request;
struct pargrp_iomap;
struct target_ioreq;
struct io_req_fop;

struct io_request_ops;
struct nw_xfer_ops;
struct pargrp_iomap_ops;
struct target_ioreq_ops;

enum   page_attr;
enum   copy_direction;
enum   nw_xfer_state;
enum   io_req_state;
enum   io_req_type;
enum   pargrp_iomap_rmwtype;

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

	/** Page contains file data. */
	PA_DATA		   = (1 << 4),

	/** Page contains parity. */
	PA_PARITY	   = (1 << 5),

        /**
         * pargrp_iomap::pi_ivec is shared for read and write state for an
         * rmw IO request. Read IO can not go past EOF but write IO can.
         * In such case, changing pargrp_iomap::pi_ivec is costly because
         * it has to be reassessed and changed after read IO is complete.
         * Instead, we use a flag to notify last valid page of a read
         * request and store the count of bytes in last page in
	 * data_buf::db_buf::b_nob.
         */
        PA_READ_EOF        = (1 << 6),

        PA_NR              = 7,
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
                                struct m0_pdclust_src_addr  *src,
                                struct m0_pdclust_tgt_addr  *tgt,
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
        /** Holds M0_T1FS_NWREQ_MAGIC */
        uint64_t                  nxr_magic;

        /** Resultant status code for all IO fops issued by this structure. */
        int                       nxr_rc;

        /** Resultant number of bytes read/written by all IO fops. */
        uint64_t                  nxr_bytes;

        enum nw_xfer_state        nxr_state;

        const struct nw_xfer_ops *nxr_ops;

        /** List of all target_ioreq structures. */
        struct m0_tl              nxr_tioreqs;

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
 * m0_sm_state_descr structure will be defined for description of all
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
	 * Finalizes and deallocates pargrp_iomap structures.
	 * @pre  req != NULL && req->ir_iomaps != NULL.
	 * @post req->ir_iomaps == NULL && req->ir_iomap_nr == 0.
	 */
	void (*iro_iomaps_destroy)(struct io_request  *req);

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
 * The state transitions of io_request structure are handled by m0_sm
 * structure and its support for chained state transitions.
 */
struct io_request {
        /** Holds M0_T1FS_IOREQ_MAGIC */
        uint64_t                     ir_magic;

        int                          ir_rc;

	/** Number of data bytes copied to/from user space. */
	m0_bcount_t                  ir_copied_nr;

        /**
         * struct file* can point to m0t1fs inode and hence its
         * associated m0_fid structure.
         */
        struct file                 *ir_file;

        /**
	 * Index vector describing file extents and their lengths.
	 * This vector is in sync with the array of iovec structures
	 * below.
	 */
        struct m0_indexvec           ir_ivec;

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
        struct m0_sm                 ir_sm;

        enum io_req_type             ir_type;

        const struct io_request_ops *ir_ops;

        struct nw_xfer_request       ir_nwxfer;
};

/**
 * Represents a simple data buffer wrapper object. The embedded
 * m0_buf::b_addr points to a kernel page.
 */
struct data_buf {
        /** Holds M0_T1FS_DTBUF_MAGIC. */
        uint64_t       db_magic;

        /** Inline buffer pointing to a kernel page. */
        struct m0_buf  db_buf;

        /**
         * Auxiliary buffer used in case of read-modify-write IO.
         * Used when page pointed to by ::db_buf::b_addr is partially spanned
         * by incoming rmw request.
         */
        struct m0_buf  db_auxbuf;

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
        /** Holds M0_T1FS_PGROUP_MAGIC. */
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
        struct m0_indexvec              pi_ivec;

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
	 * m0_vec_count(&ivec->iv_vec) > 0 && cursor != NULL &&
	 * m0_vec_count(&iomap->iv_vec) == 0
	 * @post  m0_vec_count(&iomap->iv_vec) > 0 &&
	 * iomap->pi_databufs != NULL.
         */
        int (*pi_populate)  (struct pargrp_iomap      *iomap,
                             const struct m0_indexvec *ivec,
                             struct m0_ivec_cursor    *cursor);

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
                              m0_bindex_t          index,
			      m0_bcount_t          count);

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
	 * @param segid Segment id which needs to be processed. Given seg id
	 * should point to last segment in pargrp_iomap::pi_ivec when invoked.
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
	 * @param par_offset Offset in case of parity units.
	 * @param count      Number of bytes in this segment.
	 * @param unit       Unit id in parity group.
	 * @param map        Parent pargrp_iomap structure.
	 * @pre   ti != NULL && count > 0.
	 * @post  m0_vec_count(&ti->ti_ivec.iv_vec) > 0.
         */
        void (*tio_seg_add)        (struct target_ioreq *ti,
                                    uint64_t             frame,
                                    m0_bindex_t          gob_offset,
                                    m0_bindex_t          par_offset,
                                    m0_bcount_t          count,
                                    uint64_t             unit,
				    struct pargrp_iomap *map);

        /**
	 * Prepares io fops from index vector and buffer vector.
	 * This API uses rpc bulk API to store net buffer descriptors
	 * in IO fops.
	 * @pre   iofops_tlist_is_empty(ti->ti_iofops).
	 * @post !iofops_tlist_is_empty(ti->ti_iofops).
	 */
        int  (*tio_iofops_prepare) (struct target_ioreq *ti,
				    enum page_attr       filter);
};

/**
 * Collection of IO extents and buffers, directed towards each
 * of target objects (data_unit / parity_unit) in a parity group.
 * These structures are created by struct io_request dividing the incoming
 * struct iovec into members of a parity group.
 */
struct target_ioreq {
        /** Holds M0_T1FS_TIOREQ_MAGIC */
        uint64_t                       ti_magic;

        /** Fid of component object. */
        struct m0_fid                  ti_fid;

        /** Status code for io operation done for this target_ioreq. */
        int                            ti_rc;

        /** Number of parity bytes read/written for this target_ioreq. */
        uint64_t                       ti_parbytes;

	/** Number of file data bytes read/written for this object. */
	uint64_t                       ti_databytes;

        /** List of io_req_fop structures issued on this target object. */
        struct m0_tl                   ti_iofops;

        /** Resulting IO fops are sent on this rpc session. */
        struct m0_rpc_session         *ti_session;

        /** Linkage to link in to nw_xfer_request::nxr_tioreqs list. */
        struct m0_tlink                ti_link;

        /**
         * Index vector containing IO segments with cob offsets and
         * their length.
	 * Each segment in this vector is worth PAGE_CACHE_SIZE except
	 * the very last one.
         */
        struct m0_indexvec             ti_ivec;

        /**
	 * Buffer vector corresponding to index vector above.
	 * This buffer is in sync with ::ti_ivec.
	 */
        struct m0_bufvec               ti_bufvec;

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
 * When bottom halves for m0_sm_ast structures are run, it updates
 * target_ioreq::ti_rc and target_ioreq::ti_bytes with data from
 * IO reply fop.
 * Then it decrements nw_xfer_request::nxr_iofop_nr, number of IO fops.
 * When this count reaches zero, io_request::ir_sm changes its state.
 */
struct io_req_fop {
        /** Holds M0_T1FS_IOFOP_MAGIC */
        uint64_t                irf_magic;

        /** In-memory handle for IO fop. */
        struct m0_io_fop        irf_iofop;

	/** Type of pages {PA_DATA, PA_PARITY} carried by io fop. */
	enum page_attr		irf_pattr;

        /** Callback per IO fop. */
        struct m0_sm_ast        irf_ast;

        /** Linkage to link in to target_ioreq::ti_iofops list. */
        struct m0_tlink         irf_link;

        /**
         * Backlink to target_ioreq object where rc and number of bytes
         * are updated.
         */
        struct target_ioreq    *irf_tioreq;
};

#endif /* __MERO_M0T1FS_FILE_INTERNAL_H__ */
