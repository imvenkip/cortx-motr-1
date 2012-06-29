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

#include "lib/memory.h"     /* c2_alloc(), c2_free() */
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

static struct  c2_pdclust_layout * layout_to_pd_layout(struct c2_layout *l)
{
	return container_of(l, struct c2_pdclust_layout, pl_layout);
}

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

   The read-modify-write feature provides support to do partial stripe
   IO requests on a Colibri client.

   Colibri uses notion of layout to represent how file data is spread
   over multiple objects. Colibri client is supposed to work with
   layout independent code for IO requests. Using layouts, multiple RAID
   patterns like RAID5, parity declustered RAID can be supported.

   Often, incoming IO requests do not span whole stripe of file data.
   The file data for such partial stripe requests have to be read first
   so that whole stripe is available with client, then the stripe data
   is changed as per user request and later sent for write to the server.

   <hr>
   @section rmw-def Definitions
   c2t1fs - Colibri client file system.
   layout - A map which decides how to distribute file data over a number
            of objects.
   stripe - A unit of IO request which spans all data units in a parity group.

   <hr>
   @section rmw-req Requirements

   - @b R.c2t1fs.rmw_io.rmw The implementation shall provide support for
   aligned and un-aligned IO requests.
   - @b R.c2t1fs.rmw_io.efficient The implementation shall provide efficient
   way of implementing IO requests. Since Colibri follows async nature of APIs
   as far as possible, the implementation will stick to async APIs and shall
   try to avoid blocking calls.

   <hr>
   @section rmw-depends Dependencies

   - An async way of waiting for reply rpc item from rpc layer.

   <hr>
   @section rmw-highlights Design Highlights

   - The IO path from c2t1fs code will have a check which will find out if
   incoming IO request is a partial stripe IO.
   - In case of partial stripe IO request, a read request will be issued
   if given extent falls within end-of-file. The read request will be issued
   on the concerned parity group and will wait for completion.
   - Once read IO is complete, changes will be made to the data buffers
   (typically these are pages from page cache) and then write requests
   will be dispatched for the whole stripe.

   <hr>
   @section rmw-lspec Logical Specification

   - @ref rmw-lspec-comps
   - @ref rmw-lspec-sc1
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

   This DLD addresses only the rmw_io component.

   - The component will sit in the IO path of Colibri client code.
   - It will detect if any incoming IO request spans a stripe only partially.
   - In case of partial stripes, it issues an inline read request to read
   the whole stripe and get the data in memory.
   - The thread is blocked until read IO is not complete.
   - If incoming IO request was read, the thread will be returned along
   with the status and number of bytes read.
   - If incoming IO request was write, the pages are modified in-place
   as per incoming IO request.
   - And then, write IO is issued for the whole stripe.
   - Completion of write IO will send the status back to the calling thread.

   @note Present implementation of c2t1fs IO path uses get_user_pages() API
   to pin user-space pages in memory. This works just fine with page aligned
   IO requests. But for read-modify-write, APIs like copy_from_user() and
   copy_to_user() will be used which can copy data to/from user-space and
   can work with both aligned and non-aligned IO requests.

   @subsubsection rmw-lspec-ds1 Subcomponent Data Structures

   io_request - Represents an IO request call. It contains the IO extent,
   struct io_req_rootcb and the state of IO request. This structure is
   primarily used to track progress of IO request.

   @code
   struct io_request {
	int                   ir_rc;
	uint64_t              ir_magic;
	struct rw_desc        ir_desc;
	enum io_req_type      ir_type;
	struct c2_mutex       ir_mutex;
	enum io_req_state     ir_state;
	struct io_request_ops ir_ops;
	struct io_req_rootcb  ir_rcb;
	// Can we use struct c2_ext instead of these 2 fields below?
	loff_t                ir_aux_offset;
	size_t                ir_aux_count;
   };
   @endcode

   io_req_state - Represents state of IO request call.

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

   Magic value to verify sanity of struct io_request.

   @code
   enum {
	IR_MAGIC = 0xfea2303e31a3ce10ULL, // fearsomestanceto
   };
   @endcode

   @todo IO types like fault IO are not supported yet.

   io_request_ops - Operation vector for struct io_request.
   @code
   struct io_request_ops {
	int (*iro_submit)       (struct io_request *req);
	int (*iro_readextent)   (struct io_request *req);
	int (*iro_prepare_write)(struct io_request *req);
	int (*iro_commit_write) (struct io_request *req);
	int (*read_complete)    (struct io_request *req);
	int (*write_complete)   (struct io_request *req);
   };
   @endcode

   The following data structures are needed while doing @b async IO
   with file based on a layout.
   Each incoming IO request is typically split into a number of subsequent
   IO requests addressed to each cob as specified by file layout.
   Typically, every rpc item contains a channel to notify caller of its
   completion. But in this case, we need a new mechanism to send multiple
   IO fops to rpc layer and wake up the caller thread only when all IO fops
   complete their job.

   io_req_leafcb - Represents a leaf node IO request callback. A leaf node
   callback has to point to some valid parent callback. On IO completion,
   the leaf node callback notifies the parent.

   @code
   struct io_req_leafcb {
	struct c2_clink       irl_clink;
	struct io_req_rootcb *irl_parent;
   };
   @endcode

   io_req_rootcb - Represents a root node IO request callback. A root node
   callback can be split into multiple leaf node callbacks. Contains a
   channel and a clink along with a refcount. Caller is expected to wait on
   the embedded channel using the clink. Caller is woken up when the refcount
   goes down to zero.

   @code
   struct io_req_rootcb {
	struct c2_clink       irr_clink;
	struct c2_chan        irr_chan;
	struct c2_ref         irr_ref;
	uint64_t              irr_bytes;
   };
   @endcode

   In an IO request, the io_req_rootcb is allocated first and its reference
   count is initialized. Then subsequent io_req_leafcbs are allocated and
   attached to the root node.
   On receiving IO completion, the leaf nodes decrement the parent's
   reference count atomically and then destroy themselves.
   The origin thread which spawns the rootcb waits on its embedded channel
   and is woken up when the reference count goes down to zero.

   @subsubsection rmw-lspec-sub1 Subcomponent Subroutines

   An existing API io_req_spans_full_stripe() can be reused to check if
   incoming IO request spans a full stripe or not.

   The following API will change the stripe unaligned IO request to make it
   align with stripe width. It will populate the auxiliary offset and count
   fields so that further read/write IO will be done according to these
   auxiliary IO extent. The cumulative count of auxiliary extent will always
   be bigger than original extent. While returning from the system call, the
   number of bytes read/written will not exceed the size of original IO extent.

   @param req IO request to be expanded.
   @pre req != NULL && req->ir_magic != IR_MAGIC
   @post req->ir_aux_offset != 0 && req->ir_aux_count != 0 &&
   req->ir_aux_count >= req->ir_desc.rd_count

   @code
   void io_request_expand(struct io_request *req);
   @endcode

   The APIs that work as members of operation vector struct io_request_ops
   for struct io_request are as follows.

   Initializes a newly allocated io_request structure.

   @param req   IO request to be issued.
   @param fid   File identifier.
   @param base  User-space buffer starting address.
   @param pos   File offset.
   @param count Size of IO request in bytes.
   @param rw    Flag indicating Read or write request.
   @pre req != NULL
   @post req->magic == IR_MAGIC && req->ir_type == rw &&
   req->ir_desc.rd_fid == fid && req->ir_desc.rd_offset == pos &&
   req->ir_desc.rd_count == count &&
   ((c2t1fs_buf*)c2_tlist_head(rwd, req->ir_desc.rd_buf_list))->cb_buf.b_addr
   == base && req->ir_state == IRS_INITIALIZED

   @code
   int io_request_init(struct io_request *req,
                       struct c2_fid     *fid,
                       void              *base,
		       loff_t             pos,
                       size_t             count,
                       enum io_req_type   rw);
   @endcode

   Finalizes the io_request structure.

   @param req IO request to be processed.
   @pre req != NULL && req->ir_magic == IR_MAGIC &&
   req->ir_state == IRS_REQ_COMPLETE
   && c2_atomic64_get(&req->ir_ref.ref_cnt) == 0
   @post c2_tlist_is_empty(rwd, req->ir_desc.rd_buf_list) == true &&

   @code
   void io_request_fini(struct io_request *req);
   @endcode

   Reads given extent of file.
   This API can also be used by a write request which needs to read first
   due to its unaligned nature with stripe width.
   In such case, even if the state of struct io_request indicates IRS_READING
   or IRS_READ_COMPLETE, the io_req_type enum suggests it is a write request
   in first place.

   @param req IO request to be issued.
   @pre req != NULL && req->ir_magic == IR_MAGIC &&
   req->ir_state >= IRS_INITIALIZED
   @post req->ir_state == IRS_READING

   @code
   int io_request_readextent(struct io_request *req);
   @endcode

   Make necessary preparations for a write IO request.
   This might involve issuing req->iro_readextent() request to read the
   necessary file extent if the incoming IO request is not stripe aligned.
   If the request is stripe aligned, no read IO is done.
   Instead, file data is copied from user space to kernel space.

   @param req IO request to be issued.
   @pre req != NULL && req->ir_magic == IR_MAGIC &&
   req->ir_state >= IRS_INITIALIZED
   @post req->ir_state == IRS_READING || req->ir_state == IRS_WRITING

   @code
   int io_request_prepare_write(struct io_request *req);
   @endcode

   Commit the transaction for updates made by given write IO request.
   With current code, this function will most likely be a stub. It will be
   implemented when there is a cache in c2t1fs client which will maintain
   dirty data and will commit the data with data servers as a result of
   crossing some threshold (e.g. reaching grant value or timeout).

   @param req IO request to be committed.
   @pre req != NULL && req->magic == IR_MAGIC && req->ir_state == IRS_WRITING
   && !c2_tlist_is_empty(rwd, req->ir_desc.rd_buf_list)
   @post req->ir_state == IRS_WRITE_COMPLETE &&
   c2_tlist_is_empty(rwd, req->ir_desc.rd_buf_list) == true

   @code
   int io_request_write_commit(struct io_request *req);
   @endcode

   Post processing for read IO completion.
   For READ IO operations, this function will copy back the data from
   kernel space to user-space.

   @param req IO request to be processed.
   @pre req != NULL && req->ir_magic == IR_MAGIC && req->ir_state == IRS_READING
   @post req->ir_state == IRS_READ_COMPLETE

   @code
   int io_request_read_complete(struct io_request *req);
   @endcode

   Post processing for write IO completion. This function will simply signal
   the thread blocked for completion of whole IO request.

   @param req IO request to be processed.
   @pre req != NULL && req->ir_magic == IR_MAGIC && req->ir_state == IRS_WRITING
   @post req->ir_state == IRS_WRITE_COMPLETE

   @code
   int io_request_write_complete(struct io_request *req);
   @endcode

   Submits the rpc for given IO fops. Implies "network transfer" of IO
   requests.

   @param req IO request to be submitted.
   @pre req != NULL && req->ir_magic == IR_MAGIC &&
   (req->ir_state == IRS_READING || req->ir_state == IRS_WRITING)
   @post (req->ir_state == IRS_READ_COMPLETE ||
   req->ir_state == IRS_WRITE_COMPLETE)

   @code
   int io_request_submit(struct io_request *req);
   @endcode

   - Callback used for leaf node IO request. This callback is used while
   initializing clink embedded in structure io_req_leafcb.
   This function notifies its parent of IO completion.

   @code
   bool leafio_complete(struct c2_clink *link);
   @endcode

   - Destructor function for c2_ref object embedded in structure io_req_rootcb.
   This function will wake up the caller thread by signalling on the embedded
   channel.

   @code
   void rootio_complete(struct c2_ref *ref);
   @endcode

   @subsection rmw-lspec-state State Specification

   @dot
   digraph io_req_st {
	size    = "4,6"
	label   = "States of IO request"
	node   [ shape=record, fontsize=9 ]
	S0     [ label = "", shape="plaintext" ]
	S1     [ label = "IRS_UNINITIALIZED" ]
	S2     [ label = "IRS_INITIALIZED" ]
	S3     [ label = "IRS_READING" ]
	S4     [ label = "IRS_WRITING" ]
	S5     [ label = "IRS_READ_COMPLETE" ]
	S6     [ label = "IRS_WRITE_COMPLETE" ]
	S7     [ label = "IRS_REQ_COMPLETE" ]
	S0->S1 [ label = "allocate", fontsize=10, weight=8 ]
	S1->S2 [ label = "init", fontsize=10, weight=8 ]
	{
	    rank = same; S4; S3;
	};
	S2->S4 [ label = "io == write()", fontsize=9 ]
	S2->S3 [ label = "io == read()", fontsize=9 ]
	S3->S3 [ label = "!io_spans_full_stripe()", fontsize=9 ]
	{
	    rank = same; S6; S5;
	};
	S4->S6 [ label = "write_complete()", fontsize=9, weight=4 ]
	S3->S5 [ label = "read_complete()", fontsize=9, weight=4 ]
	S4->S3 [ label = "!io_spans_full_stripe()", fontsize=9 ]
	S5->S7 [ label = "io == read()", fontsize=9 ]
	S5->S4 [ label = "io == write()", fontsize=9 ]
	S6->S7 [ label = "io == write()", fontsize=9 ]
   }
   @enddot

   @todo A client cache is missing at the moment. With addition of cache,
   the states of an IO request might add up.

   @subsection rmw-lspec-thread Threading and Concurrency Model

   The incoming IO request waits asynchronously for the completion of
   constituent IO requests. Multiple leaf node IO requests can point to
   same parent but they only decrement the refcount of parent IO request
   which is an atomic operation and does not need any special synchronization.

   @todo In future, with introduction of Resource Manager, distributed extent
   locks have to be acquired/released as needed.

   @subsection rmw-lspec-numa NUMA optimizations

   None.

   <hr>
   @section rmw-conformance Conformance

   - @b I.c2t1fs.rmw_io.rmw The implementation uses an API
   io_req_spans_full_stripe() to find out if the incoming IO request
   would be read-modify-write IO. The IO extent is modified if the request
   is unaligned with stripe width. The missing data will be read first
   from data server synchronously(later from client cache which is missing
   at the moment and then from data server). And then it will be modified
   and send as a full stripe IO request.
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

   @test Issue a full stripe size IO and check if it is successful. This
   test case should assert that full stripe IO is intact with new changes.

   @test Issue a partial stripe read IO and check if it successful. This
   test case should assert the fact that partial stripe read IO is working
   properly.

   @test Issue a partial stripe write IO and check if it is successful.
   This should confirm the fact that partial stripe write IO is working
   properly.

   @test Write very small amount of data (10 - 20 bytes) to a newly created
   file and check if it is successful. This should stress 2 boundary conditions
   - a partial stripe write IO request and
   - unavailability of all data units in a parity group. In this case,
   the non-existing data units will be assumed as zero filled buffers and
   the parity will be calculated accordingly.

   <hr>
   @section rmw-st System Tests

   A bash script will be written to send partial stripe IO requests in
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
     - implementation of an async way of submitting and waiting for multiple
       IO fops. Referred to as subtask-asyncIO henceforth.
     - implementation of primary data structures and interfaces needed to
       support read-modify-write. Referred to as subtask-rwm-support henceforth.

   - New interfaces are consumed by same module (c2t1fs). The primary
   data structures and interfaces have been identified and defined.

   - The subtask-asyncIO can be carved out as an independent task. This will
   be done first. It can be tested using existing c2t1fs ST test scripts.

   - Next, the subtask-rmw-support can be implemented which will refactor
   the IO path code. This can be tested by using a test script which will
   stress the IO path with read-modify-write IO requests.

   - While the patch for subtask-asyncIO is getting inspected, another subtask
   which will implement the read-modify-write support will be undertaken.

 */

const struct file_operations c2t1fs_reg_file_operations = {
	.llseek    = generic_file_llseek,   /* provided by linux kernel */
	.aio_read  = c2t1fs_file_aio_read,
	.aio_write = c2t1fs_file_aio_write,
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

static bool io_req_spans_full_stripe(struct c2t1fs_inode *ci,
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

	if (!io_req_spans_full_stripe(ci, buf, count, pos)) {
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
