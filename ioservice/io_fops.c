/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ioservice/io_fops.h"

#ifdef __KERNEL__
#include "ioservice/linux_kernel/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "xcode/bufvec_xcode.h" /* c2_xcode_fop_size_get() */
#include "fop/fop_format_def.h"
#include "rpc/rpc_base.h"
#include "rpc/rpc_opcodes.h"
#include "lib/vec.h"	/* c2_0vec */
#include "lib/memory.h"
#include "rpc/rpc2.h"
#include "fop/fop_onwire.h"
#include "lib/tlist.h"

extern struct c2_fop_type_format c2_net_buf_desc_tfmt;
extern struct c2_fop_type_format c2_addb_record_tfmt;

#include "ioservice/io_fops.ff"

C2_TL_DESCR_DECLARE(rpcbulk, extern);
C2_TL_DESCR_DECLARE(rpcitem, extern);

/* Forward declarations. */
static struct c2_fop_file_fid *io_fop_fid_get(struct c2_fop *fop);
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);

static struct c2_fop_type_format *ioservice_fmts[] = {
	&c2_fop_file_fid_tfmt,
	&c2_fop_io_buf_tfmt,
	&c2_fop_io_seg_tfmt,
	&c2_fop_io_vec_tfmt,
	&c2_fop_cob_rw_reply_tfmt,
	&c2_ioseg_tfmt,
	&c2_io_indexvec_tfmt,
	&c2_io_descs_tfmt,
	&c2_io_indexvec_seq_tfmt,
	&c2_fop_cob_rw_tfmt,
};

static struct c2_fop_type *ioservice_fops[] = {
      &c2_fop_cob_readv_fopt,
      &c2_fop_cob_writev_fopt,
      &c2_fop_cob_readv_rep_fopt,
      &c2_fop_cob_writev_rep_fopt,
};


extern const struct c2_rpc_item_ops      rpc_item_iov_ops;
extern const struct c2_rpc_item_type_ops rpc_item_iov_type_ops;

/**
   @page io_bulk_client IO bulk transfer Detailed Level Design.

   - @ref bulkclient-ovw
   - @ref bulkclient-def
   - @ref bulkclient-req
   - @ref bulkclient-depends
   - @ref bulkclient-highlights
   - @subpage bulkclient-fspec "IO bulk client Func Spec"
   - @ref bulkclient-lspec
      - @ref bulkclient-lspec-comps
      - @ref bulkclient-lspec-sc1
      - @ref bulkclient-lspec-state
      - @ref bulkclient-lspec-thread
      - @ref bulkclient-lspec-numa
   - @ref bulkclient-conformance
   - @ref bulkclient-ut
   - @ref bulkclient-st
   - @ref bulkclient-O
   - @ref bulkclient-ref

   <hr>
   @section bulkclient-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   This document describes the working of client side of io bulk transfer.
   This functionality is used only for io path.
   IO bulk client constitues the client side of bulk IO carried out between
   Colibri client file system and data server. Colibri network layer
   incorporates a bulk transport mechanism to transfer user buffers in
   zero-copy fashion.
   The generic io fop contains a network buffer descriptor which refers to a
   network buffer.
   The bulk client creates IO fops and attaches the kernel pages to net
   buffer associated with io fop and submits it to rpc layer.
   The rpc layer populates the net buffer descriptor from io fop and sends
   the fop over wire.
   The receiver starts the zero-copy of buffers using the net buffer
   descriptor from io fop.

   <hr>
   @section bulkclient-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   - c2t1fs Colibri client file system. It works as a kernel module.
   - Bulk transport Event based, asynchronous message passing functionality
   of Colibri network layer.
   - io fop A generic io fop that is used for read and write.
   - rpc bulk An interface to abstract the usage of network buffers by
   client and server programs.
   - ioservice A service providing io routines in Colibri. It runs only
   on server side.

   <hr>
   @section bulkclient-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   - R.bulkclient.rpcbulk The bulk client should use rpc bulk abstraction
   while enqueueing buffers for bulk transfer.
   - R.bulkclient.fopcreation The bulk client should create io fops as needed
   if pages overrun the existing rpc bulk structure.
   - R.bulkclient.netbufdesc The generic io fop should contain a network
   buffer descriptor which points to an in-memory network buffer.
   - R.bulkclient.iocoalescing The IO coalescing code should conform to
   new format of io fop. This is actually a side-effect and not a core
   part of functionality. Since the format of IO fop changes, the IO
   coalescing code which depends on it, needs to be restructured.

   <hr>
   @section bulkclient-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   - r.misc.net_rpc_convert Bulk Client needs Colibri client file system to be    using new network layer apis which include c2_net_domain and c2_net_buffer.
   - r.fop.referring_another_fop With introduction of a net buffer
   descriptor in io fop, a mechanism needs to be introduced so that fop
   definitions from one component can refer to definitions from another
   component. c2_net_buf_desc is a fop used to represent on-wire
   representation of a c2_net_buffer. @see c2_net_buf_desc.

   <hr>
   @section bulkclient-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   IO bulk client uses a generic in-memory structure representing an io fop
   and its associated network buffer.
   This in-memory io fop contains another abstract structure to represent
   the network buffer associated with the fop.
   The bulk client creates c2_io_fop structures as necessary and attaches
   kernel pages to associated c2_rpc_bulk structure and submits the fop
   to rpc layer.
   Rpc layer populates the network buffer descriptor embedded in the io fop
   and sends the fop over wire. The associated network buffer is added to
   appropriate buffer queue of transfer machine owned by rpc layer.
   Once, the receiver side receives the io fop, it acquires a local network
   buffer and calls a c2_rpc_bulk apis to start the zero-copy. @see

   <hr>
   @section bulkclient-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref bulkclient-lspec-comps
   - @ref bulkclient-lspec-sc1
      - @ref bulkclient-lspec-ds1
      - @ref bulkclient-lspec-sub1
      - @ref bulkclientDFSInternal
   - @ref bulkclient-lspec-state
   - @ref bulkclient-lspec-thread
   - @ref bulkclient-lspec-numa


   @subsection bulkclient-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   The following @@dot diagram shows the interaction of bulk client
   program with rpc layer and net layer.
   @dot
   digraph {
     node [style=box];
     label = "IO bulk client interaction with rpc and net layer";
     io_bulk_client [label = "IO bulk client"];
     Rpc_bulk [label = "RPC bulk abstraction"];
     IO_fop [label = "IO fop"];
     nwlayer [label = "Network layer"];
     zerovec [label = "Zero vector"];
     io_bulk_client -> IO_fop;
     IO_fop -> Rpc_bulk;
     IO_fop -> zerovec;
     Rpc_bulk -> zerovec;
     Rpc_bulk -> nwlayer;
   }
   @enddot

   @subsection bulkclient-lspec-sc1 Subcomponent design
   <i>Such sections briefly describes the purpose and design of each
   sub-component. Feel free to add multiple such sections, and any additional
   sub-sectioning within.</i>

   Ioservice subsystem primarily comprises of 2 sub-components
   - IO client (comprises of IO coalescing code)
   - Io server (server part of io routines)
   The IO client subsystem under which IO requests belonging to same fid
   and intent (read/write) are clubbed together in one fop and this resultant
   fop is sent instead of member io fops.

   @subsubsection bulkclient-lspec-ds1 Subcomponent Data Structures
   <i>This section briefly describes the internal data structures that are
   significant to the design of the sub-component. These should not be a part
   of the Functional Specification.</i>

   The IO coalescing subsystem from ioservice primarily works on IO segments.
   IO segment is in-memory structure that represents a contiguous chunk of
   IO data along with extent information.
   An internal data structure ioseg represents the IO segment.
   - ioseg An in-memory structure used to represent a segment of IO data.

   @subsubsection bulkclient-lspec-sub1 Subcomponent Subroutines
   <i>This section briefly describes the interfaces of the sub-component that
   are of significance to the design.</i>

   - ioseg_get Retrieves an ioseg given its index in zero
   vector.

   @subsection bulkclient-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   @dot
   digraph bulk_io_client_states {
	size = "5,6"
	label = "States encountered during io from bulk client"
	node [shape = record, fontname=Helvetica, fontsize=12]
	S0 [label = "", shape="plaintext", layer=""]
	S1 [label = "IO fop initialized"]
	S2 [label = "Rpc bulk structure initialized"]
	S3 [label = "Pages added to rpc bulk structure"]
	S4 [label = "Rpc item posted to rpc layer"]
	S5 [label = "Client waiting for reply"]
	S6 [label = "Net buf desc populated in IO fop & net buffer enqueued"
	"transfer machine."]
	S7 [label = "Reply received"]
	S8 [label = "Terminate"]
	S0 -> S1 [label = "Allocate"]
	S1 -> S2 [label = "c2_rpc_bulk_init()"]
	S1 -> S8 [label = "Failed"]
	S2 -> S8 [label = "Failed"]
	S2 -> S3 [label = "c2_rpc_bulk_page_add()"]
	S3 -> S8 [label = "Failed"]
	S3 -> S4 [label = "c2_rpc_item_post()"]
	S4 -> S5 [label = "c2_chan_wait(rpc_bulk->rb_chan)"]
	S5 -> S6 [label = "rpc_item->rit_ops->rito_io_desc_store(item)"]
	S6 -> S7 [label = "c2_chan_signal(&rpc_bulk->rb_chan)"]
	S7 -> S8 [label = "c2_rpc_bulk_fini(rpc_bulk)"]
   }
   @enddot

   @subsection bulkclient-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   No need of explicit locking for structures like c2_io_fop and ioseg
   since they are taken care by locking at upper layers like locking at
   the c2t1fs part for dispatching IO requests.

   @subsection bulkclient-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   The performance need not be optimized by associating the incoming thread
   to a particular processor. However, keeping in sync with the design of
   request handler which tries to protect the locality of threads executing
   in a particular context by establishing affinity to some designated
   processor, this can be achieved. But this is still at a level higher than
   the io fop processing.

   <hr>
   @section bulkclient-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref bulkclient-req section,
   and explains briefly how the DLD meets the requirement.</i>

   Note the subtle difference in that <b>I</b> tags are used instead of
   the <b>R</b> tags of the requirements section.  The @b I of course,
   stands for "implements":

   - I.bulkclient.rpcbulk The bulk client uses rpc bulk APIs to enqueue
   kernel pages to the network buffer.
   - I.bulkclient.fopcreation bulk client creates new io fops until all
   kernel pages are enqueued.
   - I.bulkclient.netbufdesc The on-wire definition of io_fop contains a
   net buffer descriptor. @see c2_net_buf_desc
   - I.bulkclient.iocoalescing Since all IO coalescing code is built around
   the definition of IO fop, it will conform to new format of io fop.

   <hr>
   @section bulkclient-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   All external interfaces based on c2_io_fop and c2_rpc_bulk will be
   unit tested. All unit tests will stress success and failure conditions.
   Boundary condition testing is also included.
   - The c2_io_fop* and c2_rpc_bulk* interfaces will be unit tested
   first in the order
	- c2_io_fop_init Check if the inline c2_fop and c2_rpc_bulk are
	initialized properly.
	- c2_rpc_bulk_page_add/c2_rpc_bulk_buffer_add to add pages/buffers
	to the rpc_bulk structure and cross check if they are actually added
	or not.
	- Add more pages/buffers to rpc_bulk structure to check if they
	return proper error code.
	- Try c2_io_fop_fini to check if an initialized c2_io_fop and
	the inline c2_rpc_bulk get properly finalized.
	- Initialize and start a network transport and a transfer machine.
	Invoke c2_rpc_bulk_store on rpc_bulk structure and cross check if
	the net buffer descriptor is properly populated in the io fop.
	- Tweak the parameters of transfer machine so that it goes into
	degraded/failed state and invoke c2_rpc_bulk_store and check if
	c2_rpc_bulk_store returns proper error code.
	- Start another transfer machine and invoke c2_rpc_bulk_load to
	check if it recognizes the net buf descriptor and starts buffer
	transfer properly.
	- Tweak the parameters of second transfer machine so that it goes
	into degraded/failed state and invoke c2_rpc_bulk_load and check if
	it returns proper error code.

   <hr>
   @section bulkclient-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   Not applicable.

   <hr>
   @section bulkclient-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   - m denotes the number of IO fops with same fid and intent (read/write).
   - n denotes the total number of IO segments in m IO fops.
   - Memory consumption O(n) During IO coalescing, n number of IO segments
   are allocated and subsequently deallocated, once the resulting IO fop
   is created.
   - Processor cycles O(n) During IO coalescing, all n segments are traversed
   and resultant IO fop is created.
   - Locks Minimal locks since locking is mostly taken care by upper layers.
   - Messages Not applicable.

   <hr>
   @section bulkclient-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/edit?hl=en_US">RPC Bulk Transfer Task Plan</a>
   - <a href="https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjQ3Z3NraDI4ZG0&hl=en_US">Detailed level design HOWTO</a>,
   an older document on which this style guide is partially based.

 */

/**
   @defgroup bulkclientDFSInternal IO bulk client Detailed Function Spec
   @brief Detailed Function Specification for IO bulk client.

   @{
 */

enum {
	IO_SEGMENT_MAGIC = 0x293925012f191354ULL,
	IO_SEGMENT_SET_MAGIC = 0x2ac196c1ee1a1239ULL,
};

/**
   Generic io segment that represents a contiguous stream of bytes
   along with io extent. This structure is typically used by io coalescing
   code from ioservice.
 */
struct ioseg {
	/** Magic constant to verify sanity of structure. */
	uint64_t		 is_magic;
	/* Index in target object to start io from. */
	c2_bindex_t		 is_index;
	/* Number of bytes in io segment. */
	c2_bcount_t		 is_count;
	/* Starting address of buffer. */
	void			*is_buf;
	/* Linkage to have such IO segments in a list hanging off
	   io_seg_set::iss_list. */
	struct c2_tlink		 is_linkage;
};

/**
   Represents coalesced set of io segments.
 */
struct io_seg_set {
	/** Magic constant to verify sanity of structure. */
	uint64_t	iss_magic;
	/** List of struct ioseg. */
	struct c2_tl	iss_list;
};

C2_TL_DESCR_DEFINE(iosegset, "list of coalesced io segments", static,
		   struct ioseg, is_linkage, is_magic,
		   IO_SEGMENT_MAGIC, IO_SEGMENT_SET_MAGIC);

static void ioseg_get(const struct c2_0vec *zvec, uint32_t seg_index,
		      struct ioseg *seg)
{
	C2_PRE(seg != NULL);
	C2_PRE(seg_index < zvec->z_bvec.ov_vec.v_nr);

	seg->is_index = zvec->z_index[seg_index];
	seg->is_count = zvec->z_bvec.ov_vec.v_count[seg_index];
	seg->is_buf = zvec->z_bvec.ov_buf[seg_index];
}

static bool io_fop_invariant(struct c2_io_fop *iofop)
{
	int i;

	if (iofop == NULL || iofop->if_magic != C2_IO_FOP_MAGIC)
		return false;

	for (i = 0; i < ARRAY_SIZE(ioservice_fops); ++i)
		if (iofop->if_fop.f_type == ioservice_fops[i])
			break;

	return i != ARRAY_SIZE(ioservice_fops);
}

int c2_io_fop_init(struct c2_io_fop *iofop, struct c2_fop_type *ftype)
{
	int rc;

	C2_PRE(iofop != NULL);
	C2_PRE(ftype != NULL);

	iofop->if_magic = C2_IO_FOP_MAGIC;
	rc = c2_fop_init(&iofop->if_fop, ftype, NULL);
	if (rc != 0)
		return rc;

	c2_rpc_bulk_init(&iofop->if_rbulk);
	C2_POST(io_fop_invariant(iofop));
	return rc;
}

void c2_io_fop_fini(struct c2_io_fop *iofop)
{
	C2_PRE(io_fop_invariant(iofop));

	c2_rpc_bulk_fini(&iofop->if_rbulk);
	c2_fop_fini(&iofop->if_fop);
}

struct c2_rpc_bulk *c2_fop_to_rpcbulk(const struct c2_fop *fop)
{
	struct c2_io_fop *iofop;

	C2_PRE(fop != NULL);

	iofop = container_of(fop, struct c2_io_fop, if_fop);
	return &iofop->if_rbulk;
}

/** @} end of bulkclientDFSInternal */

bool is_read(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_readv_fopt;
}

bool is_write(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_writev_fopt;
}

bool is_io(const struct c2_fop *fop)
{
	return is_read(fop) || is_write(fop);
}

bool is_read_rep(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_readv_rep_fopt;
}

bool is_write_rep(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_writev_rep_fopt;
}

bool is_io_rep(const struct c2_fop *fop)
{
	return is_read_rep(fop) || is_write_rep(fop);
}

struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop)
{
	struct c2_fop_cob_readv  *rfop;
	struct c2_fop_cob_writev *wfop;

	C2_PRE(fop != NULL);
	C2_PRE(is_io(fop));

	if (is_read(fop)) {
		rfop = c2_fop_data(fop);
		return &rfop->c_rwv;
	} else {
		wfop = c2_fop_data(fop);
		return &wfop->c_rwv;
	}
}

struct c2_fop_cob_rw_reply *io_rw_rep_get(struct c2_fop *fop)
{
	struct c2_fop_cob_readv_rep	*rfop;
	struct c2_fop_cob_writev_rep	*wfop;

	C2_PRE(fop != NULL);
	C2_PRE(is_io_rep(fop));

	if (is_read_rep(fop)) {
		rfop = c2_fop_data(fop);
		return &rfop->c_rep;
	} else {
		wfop = c2_fop_data(fop);
		return &wfop->c_rep;
	}
}

static struct c2_0vec *io_0vec_get(struct c2_rpc_bulk_buf *rbuf)
{
	C2_PRE(rbuf != NULL);

	return &rbuf->bb_zerovec;
}

static void ioseg_unlink_free(struct ioseg *ioseg)
{
	C2_PRE(ioseg != NULL);
	C2_PRE(c2_tlink_is_in(&iosegset_tl, ioseg));

	c2_tlist_del(&iosegset_tl, ioseg);
	c2_free(ioseg);
}

static bool io_fop_type_equal(const struct c2_fop *fop1,
			      const struct c2_fop *fop2)
{
	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	return fop1->f_type == fop2->f_type;
}

static uint64_t io_fop_fragments_nr_get(const struct c2_fop *fop)
{
	uint32_t		 i;
	uint64_t		 frag_nr = 1;
	struct c2_0vec		*iovec;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*rbuf;

	C2_PRE(fop != NULL);
	C2_PRE(is_io(fop));

	rbulk = c2_fop_to_rpcbulk(fop);
	c2_tlist_for(&rpcbulk_tl, &rbulk->rb_buflist, rbuf) {
		iovec = io_0vec_get(rbuf);
		for (i = 0; i < iovec->z_bvec.ov_vec.v_nr - 1; ++i)
			if (iovec->z_index[i] +
			    iovec->z_bvec.ov_vec.v_count[i] !=
			    iovec->z_index[i+1])
				frag_nr++;
	} c2_tlist_endfor;
	return frag_nr;
}

static int io_fop_seg_init(struct ioseg **ns, const struct ioseg *cseg)
{
	struct ioseg *new_seg;

	C2_PRE(ns != NULL);

	C2_ALLOC_PTR(new_seg);
	if (new_seg == NULL)
		return -ENOMEM;

	*ns = new_seg;
	*new_seg = *cseg;
	c2_tlink_init(&iosegset_tl, new_seg);
	return 0;
}

static int io_fop_seg_add_cond(struct ioseg *cseg, const struct ioseg *nseg)
{
	int           rc;
	struct ioseg *new_seg;

	C2_PRE(cseg != NULL);
	C2_PRE(nseg != NULL);

	if (nseg->is_index < cseg->is_index) {
		rc = io_fop_seg_init(&new_seg, nseg);
		if (rc < 0)
			return rc;

		c2_tlist_add_before(&iosegset_tl, cseg, new_seg);
	} else
		rc = -EINVAL;

	return rc;
}

static void io_fop_seg_coalesce(const struct ioseg *seg,
				struct io_seg_set *aggr_set)
{
	int           rc;
	struct ioseg *new_seg;
	struct ioseg *ioseg;

	C2_PRE(seg != NULL);
	C2_PRE(aggr_set != NULL);

	/* Coalesces all io segments in increasing order of offset.
	   This will create new net buffer/s which will be associated with
	   only one io fop and it will be sent on wire. While rest of io fops
	   will hang off a list c2_rpc_item::ri_compound_items. */
	c2_tlist_for(&iosegset_tl, &aggr_set->iss_list, ioseg) {
		rc = io_fop_seg_add_cond(ioseg, seg);
		if ( rc == 0 || rc == -ENOMEM)
			return;
	} c2_tlist_endfor;

	rc = io_fop_seg_init(&new_seg, seg);
	if (rc < 0)
		return;
	c2_tlist_add_tail(&iosegset_tl, &aggr_set->iss_list, new_seg);
}

static void io_fop_segments_coalesce(const struct c2_0vec *iovec,
				     struct io_seg_set *aggr_set)
{
	uint32_t     i;
	struct ioseg seg;

	C2_PRE(iovec != NULL);
	C2_PRE(aggr_set != NULL);

	/* For each segment from incoming IO vector, check if it can
	   be merged with any of the existing segments from aggr_set.
	   If yes, merge it else, add a new entry in aggr_set. */
	for (i = 0; i < iovec->z_bvec.ov_vec.v_nr; ++i) {
		ioseg_get(iovec, i, &seg);
		io_fop_seg_coalesce(&seg, aggr_set);
	}
}

/* Creates and populates net buffers as needed using the list of
   coalesced io segments.
 */
static int io_netbufs_prepare(struct c2_fop *coalesced_fop,
			      struct io_seg_set *seg_set,
			      const struct c2_fop *bkp_fop)
{
	int			 rc;
	int32_t			 max_segs_nr;
	int32_t			 min_segs_nr;
	int32_t			 curr_segs_nr;
	c2_bcount_t		 max_bufsize;
	c2_bcount_t		 curr_bufsize;
	c2_bcount_t		 seg_size;
	struct ioseg		*ioseg;
	struct c2_net_domain	*netdom;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*buf;

	C2_PRE(coalesced_fop != NULL);
	C2_PRE(bkp_fop != NULL);
	C2_PRE(seg_set != NULL);
	C2_PRE(!c2_tlist_is_empty(&iosegset_tl, &seg_set->iss_list));

	netdom = coalesced_fop->f_item.ri_session->s_conn->
		 c_rpcmachine->cr_tm.ntm_dom;
	max_bufsize = c2_net_domain_get_max_buffer_size(netdom);
	max_segs_nr = c2_net_domain_get_max_buffer_segments(netdom);
	rbulk = c2_fop_to_rpcbulk(coalesced_fop);
	curr_segs_nr = c2_tlist_length(&iosegset_tl, &seg_set->iss_list);
	ioseg = c2_tlist_head(&iosegset_tl, &seg_set->iss_list);
	seg_size = ioseg->is_count;

	while (curr_segs_nr != 0) {
		min_segs_nr = min32u(curr_segs_nr, max_segs_nr);

		rc = c2_rpc_bulk_buf_add(rbulk, min_segs_nr, seg_size,
				netdom);
		if (rc != 0)
			goto cleanup;
		buf = c2_tlist_tail(&rpcbulk_tl, &rbulk->rb_buflist);

		c2_tlist_for(&iosegset_tl, &seg_set->iss_list, ioseg) {
			curr_bufsize = c2_vec_count(&buf->bb_zerovec.
						    z_bvec.ov_vec);
			if (curr_bufsize + ioseg->is_count < max_bufsize) {
				rc = c2_rpc_bulk_buf_usrbuf_add(buf,
							 ioseg->is_buf,
							 ioseg->is_count,
							 ioseg->is_index);
				if (rc == -EMSGSIZE)
					break;

				ioseg_unlink_free(ioseg);
			} else
				break;
		} c2_tlist_endfor;
		curr_segs_nr -= min_segs_nr;
		C2_POST(c2_vec_count(&buf->bb_zerovec.z_bvec.ov_vec) <=
			max_bufsize);
		C2_POST(buf->bb_zerovec.z_bvec.ov_vec.v_nr <= max_segs_nr);
	};
	return rc;
cleanup:
	C2_ASSERT(rc != 0);
	c2_rpc_bulk_buflist_empty(rbulk);
	return rc;
}

static void io_fop_ivec_dealloc(struct c2_fop *fop)
{
	int			 cnt;
	struct c2_rpc_bulk	*rbulk;
	struct c2_fop_cob_rw	*rw;
	struct c2_io_indexvec	*ivec;
	struct c2_rpc_bulk_buf	*buf;

	C2_PRE(fop != NULL);

	rw = io_rw_get(fop);
	ivec = rw->crw_ivecs.cis_ivecs;
	rbulk = c2_fop_to_rpcbulk(fop);
	cnt = 0;

	c2_tlist_for(&rpcbulk_tl, &rbulk->rb_buflist, buf) {
		c2_free(ivec[cnt].ci_iosegs);
	} c2_tlist_endfor;
	c2_free(ivec);
}

static int io_fop_ivec_alloc(struct c2_fop *fop)
{
	int			 cnt;
	struct c2_rpc_bulk	*rbulk;
	struct c2_fop_cob_rw	*rw;
	struct c2_io_indexvec	*ivec;
	struct c2_rpc_bulk_buf	*rbuf;

	C2_PRE(fop != NULL);

	cnt = 0;
	rbulk = c2_fop_to_rpcbulk(fop);
	rw = io_rw_get(fop);
	ivec = rw->crw_ivecs.cis_ivecs;
	rw->crw_ivecs.cis_nr = c2_tlist_length(&rpcbulk_tl, &rbulk->rb_buflist);
	C2_ALLOC_ARR(ivec, rw->crw_ivecs.cis_nr);
	if (ivec == NULL)
		return -ENOMEM;

	c2_tlist_for(&rpcbulk_tl, &rbulk->rb_buflist, rbuf) {
		C2_ALLOC_ARR(ivec[cnt].ci_iosegs,
			     rbuf->bb_zerovec.z_bvec.ov_vec.v_nr);
		if (ivec[cnt].ci_iosegs == NULL)
			goto cleanup;
	} c2_tlist_endfor;
	return 0;
cleanup:
	io_fop_ivec_dealloc(fop);
	return -ENOMEM;
}

static void io_fop_ivec_prepare(struct c2_fop *res_fop)
{
	int			 cnt;
	uint32_t		 j;
	struct c2_rpc_bulk	*rbulk;
	struct c2_fop_cob_rw	*rw;
	struct c2_io_indexvec	*ivec;
	struct c2_rpc_bulk_buf	*buf;

	C2_PRE(res_fop != NULL);

	rw = io_rw_get(res_fop);
	rbulk = c2_fop_to_rpcbulk(res_fop);
	rw->crw_ivecs.cis_nr = c2_tlist_length(&rpcbulk_tl, &rbulk->rb_buflist);
	cnt = 0;
	ivec = rw->crw_ivecs.cis_ivecs;

	/* Adds same number of index vector in io fop as there are buffers in
	   c2_rpc_bulk::rb_buflist. */
	c2_tlist_for(&rpcbulk_tl, &rbulk->rb_buflist, buf) {
		for (j = 0; j < ivec[cnt].ci_nr ; ++j) {
			ivec[cnt].ci_iosegs[j].ci_index =
				buf->bb_zerovec.z_index[j];
			ivec[cnt].ci_iosegs[j].ci_count =
				buf->bb_zerovec.z_bvec.ov_vec.v_count[j];
		}
		cnt++;
	} c2_tlist_endfor;
}

static void io_fop_bulkbuf_move(struct c2_fop *src, struct c2_fop *dest)
{
	struct c2_rpc_bulk	*sbulk;
	struct c2_rpc_bulk	*dbulk;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_fop_cob_rw	*srw;
	struct c2_fop_cob_rw	*drw;

	C2_PRE(src != NULL);
	C2_PRE(dest != NULL);

	sbulk = c2_fop_to_rpcbulk(src);
	dbulk = c2_fop_to_rpcbulk(dest);
	c2_mutex_lock(&sbulk->rb_mutex);
	c2_tlist_for(&rpcbulk_tl, &sbulk->rb_buflist, rbuf) {
		c2_tlist_del(&rpcbulk_tl, rbuf);
		c2_tlist_add(&rpcbulk_tl, &dbulk->rb_buflist, rbuf);
	} c2_tlist_endfor;
	c2_mutex_unlock(&sbulk->rb_mutex);

	srw = io_rw_get(src);
	drw = io_rw_get(dest);
	drw->crw_desc = srw->crw_desc;
	drw->crw_ivecs = srw->crw_ivecs;
}

static int io_fop_desc_alloc(struct c2_fop *fop)
{
	struct c2_rpc_bulk	*rbulk;
	struct c2_fop_cob_rw	*rw;

	C2_PRE(fop != NULL);

	rbulk = c2_fop_to_rpcbulk(fop);
	rw = io_rw_get(fop);
	rw->crw_desc.id_nr = c2_tlist_length(&rpcbulk_tl, &rbulk->rb_buflist);
	C2_ALLOC_ARR(rw->crw_desc.id_descs, rw->crw_desc.id_nr);
	if (rw->crw_desc.id_descs == NULL)
		return -ENOMEM;
	return 0;
}

static void io_fop_desc_dealloc(struct c2_fop *fop)
{
	struct c2_rpc_bulk	*rbulk;
	struct c2_fop_cob_rw	*rw;

	C2_PRE(fop != NULL);

	rbulk = c2_fop_to_rpcbulk(fop);
	rw = io_rw_get(fop);
	c2_free(rw->crw_desc.id_descs);
}

/* Allocates memory for net buf descriptors array and index vector array
   and populate the array of index vectors.  */
static int io_fop_desc_ivec_prepare(struct c2_fop *fop,
				    struct io_seg_set *aggr_set,
				    struct c2_fop *bkp_fop)
{
	int			 rc;
	struct c2_rpc_bulk	*rbulk;

	C2_PRE(fop != NULL);

	rbulk = c2_fop_to_rpcbulk(fop);

	rc = io_netbufs_prepare(fop, aggr_set, bkp_fop);
	if (rc != 0)
		return rc;

	rc = io_fop_desc_alloc(fop);
	if (rc != 0) {
		c2_rpc_bulk_buflist_empty(rbulk);
		return rc;
	}

	rc = io_fop_ivec_alloc(fop);
	if (rc != 0) {
		c2_rpc_bulk_buflist_empty(rbulk);
		io_fop_desc_dealloc(fop);
		return rc;
	}

	io_fop_ivec_prepare(fop);

	return rc;
}

static void io_fop_desc_ivec_destroy(struct c2_fop *fop)
{
	struct c2_rpc_bulk *rbulk;

	C2_PRE(fop != NULL);

	rbulk = c2_fop_to_rpcbulk(fop);
	c2_rpc_bulk_buflist_empty(rbulk);
	io_fop_desc_dealloc(fop);
	io_fop_ivec_dealloc(fop);
}

/**
   Coalesces the io fops with same fid and intent (read/write). A list of
   coalesced io segments is generated which is attached to a single
   io fop - res_fop (which is already bound to a session) in form of
   one of more network buffers and rest of the io fops hang off a list
   c2_rpc_item::ri_compound_items in resultant fop.
   The index vector array from io fop is also populated from the list of
   coalesced io segments.
   The res_fop contents are backed up and restored on receiving reply
   so that upper layer is transparent of these operations.
   @see item_io_coalesce().
   @see c2_io_fop_init().
   @see c2_rpc_bulk_init().
 */
static int io_fop_coalesce(struct c2_fop *res_fop)
{
	int			 rc;
	struct c2_fop		*fop;
	struct c2_fop		*bkp_fop;
	struct io_seg_set	 aggr_set;
	struct c2_tl		*items_list;
	struct c2_0vec		*iovec;
	struct c2_rpc_item	*item;
	struct ioseg		*ioseg;
	struct c2_io_fop	*cfop;
	struct c2_fop_cob_rw	*rw;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*rbuf;

	C2_PRE(res_fop != NULL);
	C2_PRE(is_io(res_fop));

	/* Compound list is populated by an rpc item type op, namely
	   item_io_coalesce(). */
	items_list = &res_fop->f_item.ri_compound_items;
	C2_PRE(!c2_tlist_is_empty(&rpcitem_tl, items_list));

	cfop = c2_alloc(sizeof(struct c2_io_fop));
	if (cfop == NULL)
		return -ENOMEM;

	bkp_fop = &cfop->if_fop;
	c2_rpc_item_init(&bkp_fop->f_item);
	aggr_set.iss_magic = IO_SEGMENT_SET_MAGIC;
	c2_tlist_init(&iosegset_tl, &aggr_set.iss_list);

	/* Traverses the fop_list, get the IO vector from each fop,
	   pass it to a coalescing routine and get result back
	   in another list. */
	c2_tlist_for(&rpcitem_tl, items_list, item) {
		fop = c2_rpc_item_to_fop(item);
		rbulk = c2_fop_to_rpcbulk(fop);
		c2_tlist_for(&rpcbulk_tl, &rbulk->rb_buflist, rbuf) {
			iovec = io_0vec_get(rbuf);
			io_fop_segments_coalesce(iovec, &aggr_set);
		} c2_tlist_endfor;
	} c2_tlist_endfor;

	rc = c2_io_fop_init(cfop, res_fop->f_type);
	if (rc != 0)
		goto cleanup;

	/* Removes c2_rpc_bulk_buf from the c2_rpc_bulk::rb_buflist and
	   add it to same list belonging to bkp_fop. */
	io_fop_bulkbuf_move(res_fop, bkp_fop);

	/* Prepares net buffers from set of io segments, allocates memory
	   for net buf desriptors and index vectors and populates the index
	   vectors. */
	rc = io_fop_desc_ivec_prepare(res_fop, &aggr_set, bkp_fop);
	if (rc != 0) {
		io_fop_bulkbuf_move(bkp_fop, res_fop);
		goto cleanup;
	}

	rw = io_rw_get(res_fop);
	C2_POST(rw->crw_desc.id_nr == rw->crw_ivecs.cis_nr);
	c2_tlist_add(&rpcitem_tl, &res_fop->f_item.ri_compound_items,
		     &bkp_fop->f_item);
	return rc;
cleanup:
	C2_ASSERT(rc != 0);
	c2_tlist_for(&iosegset_tl, &aggr_set.iss_list, ioseg) {
		ioseg_unlink_free(ioseg);
	} c2_tlist_endfor;
	c2_tlist_fini(&iosegset_tl, &aggr_set.iss_list);
	c2_io_fop_fini(cfop);
	c2_free(cfop);
	return rc;
}

static struct c2_fop_file_fid *io_fop_fid_get(struct c2_fop *fop)
{
	return &(io_rw_get(fop))->crw_fid;
}

static bool io_fop_fid_equal(struct c2_fop *fop1, struct c2_fop *fop2)
{
	struct c2_fop_file_fid *ffid1;
	struct c2_fop_file_fid *ffid2;

	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	ffid1 = io_fop_fid_get(fop1);
	ffid2 = io_fop_fid_get(fop2);

	return (ffid1->f_seq == ffid2->f_seq && ffid1->f_oid == ffid2->f_oid);
}

void io_fop_replied(struct c2_fop *fop)
{
	struct c2_fop		*bkpfop;
	struct c2_rpc_item	*bkpitem;
	struct c2_io_fop	*cfop;
	struct c2_rpc_bulk	*rbulk;

	C2_PRE(fop != NULL);
	C2_PRE(is_io(fop));

	if (!c2_tlist_is_empty(&rpcbulk_tl, &fop->f_item.ri_compound_items)) {
		bkpitem = c2_tlist_head(&rpcitem_tl,
					&fop->f_item.ri_compound_items);
		bkpfop = c2_rpc_item_to_fop(bkpitem);
		rbulk = c2_fop_to_rpcbulk(fop);
		c2_rpc_bulk_buflist_empty(rbulk);
		io_fop_bulkbuf_move(bkpfop, fop);
		cfop = container_of(bkpfop, struct c2_io_fop, if_fop);
		c2_io_fop_fini(cfop);
		c2_free(cfop);
	} else
		c2_tlist_del(&rpcitem_tl, &fop->f_item);
}

void io_fop_desc_get(struct c2_fop *fop, struct c2_net_buf_desc **desc)
{
	struct c2_fop_cob_rw *rw;

	C2_PRE(fop != NULL);
	C2_PRE(desc != NULL);

	rw = io_rw_get(fop);
	*desc = rw->crw_desc.id_descs;
}

/* Dummy definition for kernel mode. */
#ifdef __KERNEL__
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	        return 0;
}
#else
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);
#endif

const struct c2_fop_type_ops io_fop_cob_rwv_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

static int io_fop_cob_rwv_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

const struct c2_fop_type_ops c2_io_rwv_rep_ops = {
	.fto_fom_init = io_fop_cob_rwv_rep_fom_init,
	.fto_size_get = c2_xcode_fop_size_get
};

/* Rpc item type ops for IO operations. */
static void io_item_replied(struct c2_rpc_item *item, int rc)
{
	struct c2_fop *fop;

C2_FOP_TYPE_DECLARE_OPS(c2_fop_cob_readv, "Read request", &c2_io_cob_readv_ops,
			C2_IOSERVICE_READV_OPCODE, C2_RPC_ITEM_TYPE_REQUEST,
			&rpc_item_iov_type_ops);
C2_FOP_TYPE_DECLARE_OPS(c2_fop_cob_writev, "Write request",
			&c2_io_cob_writev_ops,
			C2_IOSERVICE_WRITEV_OPCODE, C2_RPC_ITEM_TYPE_REQUEST,
			&rpc_item_iov_type_ops);

/**
 * FOP definitions of readv and writev reply FOPs.
 */
C2_FOP_TYPE_DECLARE_OPS(c2_fop_cob_writev_rep, "Write reply",
			&c2_io_rwv_rep_ops, C2_IOSERVICE_WRITEV_REP_OPCODE,
			C2_RPC_ITEM_TYPE_REPLY, &rpc_item_iov_type_ops);
C2_FOP_TYPE_DECLARE_OPS(c2_fop_cob_readv_rep, "Read reply",
			&c2_io_rwv_rep_ops, C2_IOSERVICE_READV_REP_OPCODE,
			C2_RPC_ITEM_TYPE_REPLY,  &rpc_item_iov_type_ops);

static size_t io_item_size_get(const struct c2_rpc_item *item)
{
        size_t		 size;
        struct c2_fop   *fop;

        C2_PRE(item != NULL);

        fop = c2_rpc_item_to_fop(item);
        if (fop->f_type->ft_ops->fto_size_get != NULL)
                size = fop->f_type->ft_ops->fto_size_get(fop);

        return size;
}

static uint64_t io_frags_nr_get(const struct c2_rpc_item *item)
{
	struct c2_fop *fop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	return fop->f_type->ft_ops->fto_get_nfragments(fop);
}

void item_io_coalesce(struct c2_rpc_item *head, struct c2_list *list)
{
	int			 rc;
	struct c2_fop		*bfop;
	struct c2_fop		*ufop;
	struct c2_rpc_item	*item;
	struct c2_rpc_item	*item_next;
	struct c2_rpc_session	*session;

	C2_PRE(head != NULL);
	C2_PRE(list != NULL);

	session = container_of(list, struct c2_rpc_session, s_unbound_items);
	C2_ASSERT(session != NULL);
	C2_ASSERT(c2_mutex_is_locked(&session->s_mutex));

	if (c2_list_is_empty(list))
		return;

	/* Traverses through the list and finds out items that match with
	   head on basis of fid and intent (read/write). Matching items
	   are removed from session->s_unbound_items list and added to
	   head->compound_items list. */
	bfop = c2_rpc_item_to_fop(head);
	c2_list_for_each_entry_safe(list, item, item_next, struct c2_rpc_item,
				    ri_unbound_link) {
		ufop = c2_rpc_item_to_fop(item);
		if (io_fop_type_equal(bfop, ufop) &&
		    io_fop_fid_equal(bfop, ufop)) {
			c2_list_del(&item->ri_unbound_link);
			c2_tlist_add(&rpcitem_tl,
				     &head->ri_compound_items, item);
		}
	}

	if (c2_tlist_is_empty(&rpcitem_tl, &head->ri_compound_items))
		return;

	/* Add the bound item to list of compound items as this will
	   include the bound item's io vector in io coalescing. */
	c2_tlist_add(&rpcitem_tl, &head->ri_compound_items, head);

	rc = bfop->f_type->ft_ops->fto_io_coalesce(bfop);

	c2_tlist_del(&rpcitem_tl, head);
}

int io_item_desc_store(struct c2_rpc_item *item)
{
	int			 rc;
	struct c2_fop		*fop;
	struct c2_fop		*bkp_fop;
	struct c2_io_fop	*cfop;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_item	*bkp;
	struct c2_rpc_item	*member;
	struct c2_rpc_session	*session;
	struct c2_net_buf_desc	*desc;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	rbulk = c2_fop_to_rpcbulk(fop);

	fop->f_type->ft_ops->fto_io_desc_get(fop, &desc);
	C2_ASSERT(desc != NULL);
	rc = c2_rpc_bulk_store(rbulk, item, desc);
	if (rc != 0) {
		if (!c2_tlist_is_empty(&rpcitem_tl, &item->ri_compound_items)) {
			bkp = c2_tlist_head(&rpcitem_tl,
					    &item->ri_compound_items);
			bkp_fop = c2_rpc_item_to_fop(item);
			io_fop_desc_ivec_destroy(fop);
			io_fop_bulkbuf_move(bkp_fop, fop);
			c2_tlist_del(&rpcitem_tl, bkp);
			cfop = container_of(bkp_fop, struct c2_io_fop, if_fop);
			c2_free(cfop);
			session = item->ri_session;
			C2_ASSERT(c2_mutex_is_locked(&session->s_mutex));

			/* Move all member items out of c2_rpc_item::
			   ri_compound_items and them back to
			   c2_rpc_session::s_unbound_items list. */
			c2_tlist_for(&rpcitem_tl, &item->ri_compound_items,
				     member) {
				c2_tlist_del(&rpcitem_tl, member);
				c2_list_add(&session->s_unbound_items,
					    &member->ri_unbound_link);
			} c2_tlist_endfor;
		}
	}

	return rc;
}

static const struct c2_rpc_item_type_ops io_item_type_ops = {
        .rito_sent = NULL,
        .rito_added = NULL,
        .rito_replied = io_item_replied,
        .rito_item_size = io_item_size_get,
        .rito_io_frags_nr_get = io_frags_nr_get,
        .rito_io_coalesce = item_io_coalesce,
        .rito_encode = c2_fop_item_type_default_encode,
        .rito_decode = c2_fop_item_type_default_decode,
	.rito_io_desc_store = io_item_desc_store,
};

struct c2_rpc_item_type rpc_item_type_readv = {
        .rit_ops = &io_item_type_ops,
};

struct c2_rpc_item_type rpc_item_type_writev = {
        .rit_ops = &io_item_type_ops,
};

C2_FOP_TYPE_DECLARE(c2_fop_cob_readv, "Read request",
		    &c2_io_cob_readv_ops, C2_IOSERVICE_READV_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST);

C2_FOP_TYPE_DECLARE(c2_fop_cob_writev, "Write request",
		    &c2_io_cob_writev_ops, C2_IOSERVICE_WRITEV_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST);

C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply",
		    &c2_io_rwv_rep_ops, C2_IOSERVICE_WRITEV_REP_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY);

C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply",
		    &c2_io_rwv_rep_ops, C2_IOSERVICE_READV_REP_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY);

int c2_ioservice_fops_nr(void)
{
	return ARRAY_SIZE(ioservice_fops);
}
C2_EXPORTED(c2_ioservice_fops_nr);

void c2_ioservice_fop_fini(void)
{
	c2_fop_type_fini_nr(ioservice_fops, ARRAY_SIZE(ioservice_fops));
	c2_fop_type_format_fini_nr(ioservice_fmts, ARRAY_SIZE(ioservice_fmts));
}
C2_EXPORTED(c2_ioservice_fop_fini);

int c2_ioservice_fop_init(void)
{
	int rc;

	rc = c2_fop_type_format_parse_nr(ioservice_fmts,
			ARRAY_SIZE(ioservice_fmts));
	if (rc == 0)
		rc = c2_fop_type_build_nr(ioservice_fops,
				ARRAY_SIZE(ioservice_fops));
	if (rc != 0)
		c2_ioservice_fop_fini();
	return rc;
}
C2_EXPORTED(c2_ioservice_fop_init);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
