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
   - @subpage rmw-fspec "Read-modify-write IO Func Spec"
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

   rmw_io=>layout   [ label = "fetch constituent cobs" ];
   rmw_io=>rpc_bulk [ label = "issue async read IO" ];
   rpc_bulk=>rmw_io [ label = "read complete" ];
   rmw_io=>rmw_io   [ label = "modify data buffers" ];
   rmw_io=>rpc_bulk [ label = "issue async write IO" ];
   rpc_bulk=>rmw_io [ label = "write complete" ];

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
   };
   @endcode

   In an IO request, the io_req_rootcb is allocated first and its reference
   count is initialized. Then subsequent io_req_leafcbs are allocated and
   attached to the root node.
   On receiving IO completion, the leaf nodes decrement the parent's
   reference count atomically and then destroy themselves.
   The origin thread which spawns the rootcb waits on its embedded channel
   and is woken up when the reference count goes down to zero.

   io_request - Represents an IO request call. It contains the IO extent,
   struct io_req_rootcb and the state of IO request. This structure is
   primarily used to track progress of IO request.

   @code
   struct io_request {
	int                  ir_rc;
	uint64_t             ir_magic;
	struct rw_desc       ir_desc;
	enum io_req_type     ir_type;
	enum io_req_state    ir_state;
	struct io_req_ops    ir_ops;
	struct io_req_rootcb ir_rcb;
   };
   @endcode

   io_req_state - Represents state of IO request call. The possible states
   can be
   UNINITIALIZED, INITIALIZED, INTRANSIT, COMPLETE

   io_req_type - Represents type of IO request. Possible values are READ or
   WRITE.

   io_req_ops - Operation vector for struct io_request.
   @code
   struct io_req_ops {
	int (*iro_dispatch)     (struct io_request *req);
	int (*iro_readpage)     (struct io_request *req, struct page *page);
	int (*iro_prepare_write)(struct io_request *req, struct page *pages,
				 int page_nr);
	int (*iro_commit_write) (struct io_request *req);
   };
   @endcode
   @todo Do the ops vector talk in terms of pages or in rw_desc?

   @subsubsection rmw-lspec-sub1 Subcomponent Subroutines

   An existing API io_req_spans_full_stripe() can be reused to check if
   incoming IO request spans a full stripe or not.

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

   Also, corresponding APIs will be written to perform operations from
   struct io_req_ops vector.

   @subsection rmw-lspec-state State Specification

   The structure io_request can be in one the states, namely UNINITIALIZED,
   INITIALIZED, INTRANSIT, COMPLETE.

   @dot
   digraph io_req_st {
	size  = "3,4"
	label = "States of IO request"
	node   [ shape=record, fontsize=10]
	S0     [ label = "", shape="plaintext" ]
	S1     [ label = "UNINITIALIZED" ]
	S2     [ label = "INITIALIZED"]
	S3     [ label = "INTRANSIT"]
	S4     [ label = "COMPLETE"]
	S0->S1 [ label = "allocate" ]
	S1->S2 [ label = "init" ]
	S2->S3 [ label = "Dispatch all IO requests" ]
	S3->S4 [ label = "Replies received" ]
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
   would be read-modify-write IO. The missing data will be read first
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
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section rmw-ref References

   - <a href="https://docs.google.com/a/xyratex.com/
Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjQ3Z3NraDI4ZG0&hl=en_US">
Detailed level design HOWTO</a>,
   an older document on which this style guide is partially based.

   <hr>
   @section rmw-impl-plan Implementation Plan
   <i>Mandatory.  Describe the steps that should be taken to implement this
   design.</i>

   The plan should take into account:
   - The need for early exposure of new interfaces to potential consumers.
   Should parts of the interfaces be landed early with stub support?
   Does consumer code have to be updated with such stubs?
   - Modular and functional decomposition.  Identify pieces that can be
   developed (coded and unit tested) in smaller sub-tasks.  Smaller tasks
   demonstrate progress to management, as well as reduce the inspection
   overhead.  It may be necessary to plan on re-factoring pieces of code
   to support this incremental development approach.
   - Understand how long it will take to implement the design. If it is
   significantly long (more than a few weeks), then task decomposition
   becomes even more essential, because it allows for merging of changes
   from master into your feature branch, if necessary.
   Task decomposition should be reflected in the GSP task plan for the sprint.
   - Determine how to maximize the overlap of inspection and ongoing
   development.
   The smaller the inspection task the faster it could complete.
   Good planning can reduce programmer idle time during inspection;
   being able to overlap development of the next coding sub-task while the
   current one is being inspected is ideal!
   It is useful to anticipate how you would need organize your GIT code
   source branches to handle this efficiently.
   Remember that you should only present modified code for inspection,
   and not the changes you picked up with periodic merges from another branch.
   - The software development process used by Colibri provides sufficient
   flexibility to decompose tasks, consolidate phases, etc.  For example,
   you may prefer to develop code and UT together, and present both for
   inspection at the same time.  This would require consolidation of the
   CINSP-PREUT phase into the CINSP-POSTUT phase.
   Involve your inspector in such decisions.
   Document such changes in this plan, update the task spreadsheet for both
   yourself and your inspector.

   The implementation plan should be deleted from the DLD when the feature
   is landed into master.

 */


/*#include "doc/dld_template.h"*/

/**
   @defgroup rmwDFSInternal Colibri Sample Module Internals
   @brief Detailed functional specification of the internals of the
   sample module.

   This example is part of the DLD Template and Style Guide. It illustrates
   how to keep internal documentation separate from external documentation
   by using multiple @@defgroup commands in different files.

   Please make sure that the module cross-reference the DLD, as shown below.

   @see @ref DLD and @ref rmw-lspec

   @{
 */

/** @} */ /* end internal */

/**
   External documentation can be continued if need be - usually it should
   be fully documented in the header only.
   @addtogroup DLDFS
   @{
 */

/**
 * This is an example of bad documentation, where an external symbol is
 * not documented in the externally visible header in which it is declared.
 * This also results in Doxygen not being able to automatically reference
 * it in the Functional Specification.
 */
unsigned int dld_bad_example;
/** @} */ /* end-of-DLDFS */


/**
   @page rmw-fspec DLD Functional Specification Template
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref rmw-fspec-ds
   - @ref rmw-fspec-sub
   - @ref rmw-fspec-cli
   - @ref rmw-fspec-usecases
   - @ref rmwDFS "Detailed Functional Specification" <!-- Note link -->

   The Functional Specification section of the DLD shall be placed in a
   separate Doxygen page, identified as a @@subpage of the main specification
   document through the table of contents in the main document page.  The
   purpose of this separation is to co-locate the Functional Specification in
   the same source file as the Detailed Functional Specification.

   A table of contents should be created for the major sections in this page,
   as illustrated above.  It should also contain references to other
   @b external Detailed Functional Specification sections, which even
   though may be present in the same source file, would not be visibly linked
   in the Doxygen output.

   @section rmw-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and @b brief description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   For example:
<table border="0">
<tr><td>&nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp;</td><td>
   The @c dld_sample_ds1 structure tracks the density of the
   electro-magnetic field with the following:
@code
struct dld_sample_ds1 {
  ...
  int dsd_flux_density;
  ...
};
@endcode
   The value of this field is inversely proportional to the square of the
   number of lines of comments in the DLD.
</td></tr></table>
   Note the indentation above, accomplished by means of an HTML table
   is purely for visual effect in the Doxygen output of the style guide.
   A real DLD should not use such constructs.

   Simple lists can also suffice:
   - dld_sample_ds1
   - dld_bad_example

   The section could also describe what use it makes of data structures
   described elsewhere.

   Note that data structures are defined in the
   @ref rmwDFS "Detailed Functional Specification"
   so <b>do not duplicate the definitions</b>!
   Do not describe internal data structures here either - they can be described
   in the @ref DLD-lspec "Logical Specification" if necessary.

   @section rmw-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   Externally visible interfaces should be enumerated and categorized by
   function.  <b>Do not provide details.</b> They will be fully documented in
   the @ref rmwDFS "Detailed Functional Specification".
   Do not describe internal interfaces - they can be described in the
   @ref DLD-lspec "Logical Specification" if necessary.

   @subsection rmw-fspec-sub-cons Constructors and Destructors

   @subsection rmw-fspec-sub-acc Accessors and Invariants

   @subsection rmw-fspec-sub-opi Operational Interfaces
   - dld_sample_sub1()

   @section rmw-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   @section rmw-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   Note the following references to the Detailed Functional Specification
   sections at the end of these Functional Specifications, created using the
   Doxygen @@see command:

   @see @ref rmwDFS "Sample Detailed Functional Specification"
 */

/**
   @defgroup rmwDFS Colibri Sample Module
   @brief Detailed functional specification template.

   This page is part of the DLD style template.  Detailed functional
   specifications go into a module described by the Doxygen @@defgroup command.
   Note that you cannot use a hyphen (-) in the tag of a @@defgroup.

   Module documentation may spread across multiple source files.  Make sure
   that the @@addtogroup Doxygen command is used in the other files to merge
   their documentation into the main group.  When doing so, it is important to
   ensure that the material flows logically when read through Doxygen.

   You are not constrained to have only one module in the design.  If multiple
   modules are present you may use multiple @@defgroup commands to create
   individual documentation pages for each such module, though it is good idea
   to use separate header files for the additional modules.  In particular, it
   is a good idea to separate the internal detailed documentation from the
   external documentation in this header file.  Please make sure that the DLD
   and the modules cross-reference each other, as shown below.

   @see The @ref DLD "Colibri Sample DLD" its
   @ref rmw-fspec "Functional Specification"
   and its @ref rmw-lspec-thread

   @{
 */


/** Data structure to do something. */
struct dld_sample_ds1 {
	/** The z field */
	int dsd_z_field;
	/** Flux density */
	int dsd_flux_density;
};

/*
 * Documented elsewhere to illustrate bad documentation of an external symbol.
 * Doxygen cannot automatically reference it from elsewhere, as can be seen in
 * the Doxygen output for the reference in the Functional Specification above.
 */
extern unsigned int dld_bad_example;

/**
   Subroutine1 opens a foo for access.

   Some particulars:
   - Proper grammar, punctuation and spelling is required
   in all documentation.
   This requirement is not relaxed in the detailed functional specification.
   - Function documentation should be in the 3rd person, singular, present
   tense, indicative mood, active voice.  For example, "creates",
   "initializes", "finds", etc.
   - Functional parameters should not trivialize the
   documentation by repeating what is already clear from the function
   prototype.  For example it would be wrong to say, <tt>"@param read_only
   A boolean parameter."</tt>.
   - The default return convention (0 for success and @c -errno
   on failure) should not be repeated.
   - The @@pre and @@post conditions are preferably expressed in code.

   @param param1 Parameter 1 must be locked before use.
   @param read_only This controls the modifiability of the foo object.
   Set to @c true to prevent modification.
   @retval return value
   @pre Pre-condition, preferably expressed in code.
   @post Post-condition, preferably expressed in code.
 */
int dld_sample_sub1(struct dld_sample_ds1 *param1, bool read_only);

/** @} */ /* rmwDFS end group */

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
