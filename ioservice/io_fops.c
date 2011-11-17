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
#include "lib/vec.h"	/* c2_0vec */
#include "lib/memory.h"
#include "rpc/rpccore.h"

extern struct c2_fop_type_format c2_net_buf_desc_tfmt;
extern struct c2_fop_type_format c2_addb_record_tfmt;

#include "ioservice/io_fops.ff"

/* Forward declarations. */
int c2_rpc_fop_default_encode(struct c2_rpc_item_type *item_type,
			      struct c2_rpc_item *item,
			      struct c2_bufvec_cursor *cur);

int c2_rpc_fop_default_decode(struct c2_rpc_item_type *item_type,
			      struct c2_rpc_item **item,
			      struct c2_bufvec_cursor *cur);

static struct c2_fop_file_fid *io_fop_fid_get(struct c2_fop *fop);

/**
   The IO fops code has been generalized to suit both read and write fops
   as well as the kernel implementation.
   The fop for read and write is same.
   Most of the code deals with IO coalescing and fop type ops.
   Ioservice also registers IO fops. This initialization should be done
   explicitly while using code is user mode while kernel module takes care
   of this initialization by itself.
   Most of the IO coalescing is done from client side. RPC layer, typically
   formation module invokes the IO coalescing code.
 */

/**
   A generic IO segment pointing either to read or write segments. This
   is needed to have generic IO coalescing code. During coalescing, lot
   of new io segments are created which need to be tracked using a list.
   This is where the generic io segment is used.
 */

static struct c2_fop_type_format *ioservice_fmts[] = {
	&c2_fop_file_fid_tfmt,
	&c2_fop_io_buf_tfmt,
	&c2_fop_io_seg_tfmt,
	&c2_fop_io_vec_tfmt,
	&c2_fop_cob_rw_tfmt,
	&c2_fop_cob_rw_reply_tfmt,
	&c2_ioseg_tfmt,
	&c2_io_indexvec_tfmt,
};

static struct c2_fop_type *ioservice_fops[] = {
	&c2_fop_cob_readv_fopt,
	&c2_fop_cob_writev_fopt,
	&c2_fop_cob_readv_rep_fopt,
	&c2_fop_cob_writev_rep_fopt,
};

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
   - ioseg_set Set the contents of IO segment referred by given
   index in zero vector to the contents of input IO segment.
   - ioseg_nr_alloc Allocate given number of segments for a new zero vector.

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

/**
   Generic io segment that represents a contiguous stream of bytes
   along with io extent. This structure is typically used by io coalescing
   code from ioservice.
 */
struct ioseg {
	/* Index in target object to start io from. */
	c2_bindex_t		 is_index;
	/* Number of bytes in io segment. */
	c2_bcount_t		 is_count;
	/* Starting address of buffer. */
	void			*is_buf;
	/* Linkage to have such IO segments in a list. */
	struct c2_list_link	 is_linkage;
};

/**
   Get the io segment indexed by index in array of io segments in zerovec.
   @pre zvec != NULL & seg_index < zvec->z_bvec.ov_vec.v_nr.

   @param zvec The c2_0vec io vector from which io segment will be retrieved.
   @param index Index of io segments in array of io segments from zerovec.
   @param seg Out parameter to return io segment.
 */
void ioseg_get(const struct c2_0vec *zvec, uint32_t seg_index,
	       struct ioseg *seg)
{
	C2_PRE(seg != NULL);
	C2_PRE(seg_index < zvec->z_bvec.ov_vec.v_nr);

	seg->is_index = zvec->z_indices[seg_index];
	seg->is_count = zvec->z_bvec.ov_vec.v_count[seg_index];
	seg->is_buf = zvec->z_bvec.ov_buf[seg_index];
}

/**
   Set the io segment referred by index into array of io segments from
   the zero vector.
   @note There is no data copy here. Just buffer pointers are copied since
   this API is supposed to be used in same address space.

   @note The incoming c2_0vec should be allocated and initialized.
   @param zvec The c2_0vec io vector whose io segment will be changed.
   @param seg Target segment for set.
 */
void ioseg_set(struct c2_0vec *zvec, uint32_t seg_index,
	       const struct ioseg *seg)
{
	C2_PRE(seg != NULL);
	C2_PRE(seg_index < zvec->z_bvec.ov_vec.v_nr);

	zvec->z_bvec.ov_buf[seg_index] = seg->is_buf;
	zvec->z_indices[seg_index] = seg->is_index;
	zvec->z_bvec.ov_vec.v_count[seg_index] = seg->is_count;
}

/**
   Allocate the io segments for the given c2_0vec structure.
   @note The incoming c2_0vec should be allocated and initialized.
   @param zvec The c2_0vec structure to which allocated segments
   are attached.
   @param segs_nr Number of io segments to be allocated.
 */
int ioseg_nr_alloc(struct c2_0vec *zvec, uint32_t segs_nr)
{
	C2_PRE(zvec != NULL);
	C2_PRE(segs_nr != 0);

	C2_ALLOC_ARR(zvec->z_bvec.ov_buf, segs_nr);
	return zvec->z_bvec.ov_buf == NULL ? -ENOMEM : 0;
}

bool io_fop_invariant(struct c2_io_fop *iofop)
{
	int i;

	if (iofop == NULL || iofop->if_magic != C2_IO_FOP_MAGIC)
		return false;

	for (i = 0; i < ARRAY_SIZE(ioservice_fops); ++i)
		if (iofop->if_fop.f_type == ioservice_fops[i])
			break;

	return i != ARRAY_SIZE(ioservice_fops);
}

/**
   Initialize a c2_io_fop structure.
   @pre iofop != NULL && ftype != NULL && netdom != NULL
   @param ftype Type of fop to be initialized.
   @param segs_nr Number of IO segments to be contained in the io fop.
   @param seg_size Size of each IO segment.
   @param netdom The network domain under which IO requests are made.
 */
int c2_io_fop_init(struct c2_io_fop *iofop,
		   struct c2_fop_type *ftype,
		   uint32_t segs_nr,
		   c2_bcount_t seg_size,
		   struct c2_net_domain *netdom)
{
	int rc;

	C2_PRE(iofop != NULL);
	C2_PRE(ftype != NULL);
	C2_PRE(netdom != NULL);

	iofop->if_magic = C2_IO_FOP_MAGIC;
	rc = c2_fop_init(&iofop->if_fop, ftype, NULL);
	if (rc != 0)
		return rc;

	rc = c2_rpc_bulk_init(&iofop->if_rbulk, segs_nr, seg_size, netdom);
	if (rc != 0) {
		c2_fop_fini(&iofop->if_fop);
		return rc;
	}

	C2_POST(io_fop_invariant(iofop));
	return rc;
}

void c2_io_fop_fini(struct c2_io_fop *iofop)
{
	C2_PRE(io_fop_invariant(iofop));

	c2_fop_fini(&iofop->if_fop);
	c2_rpc_bulk_fini(&iofop->if_rbulk);
}

struct c2_rpc_bulk *c2_fop_to_rpcbulk(struct c2_fop *fop)
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

static struct c2_bufvec *iovec_get(struct c2_fop *fop)
{
	return &c2_fop_to_rpcbulk(fop)->rb_nbuf.nb_buffer;
}

static struct c2_0vec *io_0vec_get(struct c2_fop *fop)
{
	return &c2_fop_to_rpcbulk(fop)->rb_zerovec;
}

static void ioseg_unlink_free(struct ioseg *ioseg)
{
	C2_PRE(ioseg != NULL);

	c2_list_del(&ioseg->is_linkage);
	c2_free(ioseg);
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

static bool io_fop_type_equal(const struct c2_fop *fop1,
		const struct c2_fop *fop2)
{
	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	return fop1->f_type == fop2->f_type;
}

static uint64_t io_fop_fragments_nr_get(struct c2_fop *fop)
{
	uint32_t	      i;
	uint64_t	      frag_nr = 1;
	struct c2_0vec	     *iovec;

	C2_PRE(fop != NULL);
	C2_PRE(is_io(fop));

	iovec = io_0vec_get(fop);
	for (i = 0; i < iovec->z_bvec.ov_vec.v_nr - 1; ++i)
		if (iovec->z_indices[i] +
		    iovec->z_bvec.ov_vec.v_count[i] !=
		    iovec->z_indices[i+1])
			frag_nr++;
	return frag_nr;
}

static int io_fop_seg_init(struct ioseg **ns, struct ioseg *cseg)
{
	struct ioseg *new_seg;

	C2_PRE(ns != NULL);

	C2_ALLOC_PTR(new_seg);
	if (new_seg == NULL)
		return -ENOMEM;

	c2_list_link_init(&new_seg->is_linkage);
	*ns = new_seg;
	*new_seg = *cseg;
	return 0;
}

static int io_fop_seg_add_cond(struct ioseg *cseg, struct ioseg *nseg)
{
	int           rc;
	struct ioseg *new_seg;

	C2_PRE(cseg != NULL);
	C2_PRE(nseg != NULL);

	if (nseg->is_index < cseg->is_index) {
		rc = io_fop_seg_init(&new_seg, nseg);
		if (rc < 0)
			return rc;

		c2_list_add_before(&cseg->is_linkage, &new_seg->is_linkage);
	} else
		rc = -EINVAL;

	return rc;
}

/**
   Checks if input IO segment from IO vector can fit with existing set of
   segments in aggr_list.
   If yes, change corresponding segment from aggr_list accordingly.
   The segment is added in a sorted manner of starting offset in aggr_list.
   Else, add a new segment to the aggr_list.
   @note This is a best-case effort or an optimization effort. That is why
   return value is void. If something fails, everything is undone and function
   returns.

   @param aggr_list - list of write segments which gets built during
    this operation.
 */
static void io_fop_seg_coalesce(struct ioseg *seg,
				struct c2_list *aggr_list)
{
	int           rc;
	struct ioseg *new_seg;
	struct ioseg *ioseg;
	struct ioseg *ioseg_next;

	C2_PRE(seg != NULL);
	C2_PRE(aggr_list != NULL);

	c2_list_for_each_entry_safe(aggr_list, ioseg, ioseg_next,
				    struct ioseg, is_linkage) {
		/* If given segment fits before some other segment
		   in increasing order of offsets, add it before
		   current segments from aggr_list. */
		rc = io_fop_seg_add_cond(ioseg, seg);
		if ( rc == 0 || rc == -ENOMEM)
			return;
	}

	/* Add a new IO segment unconditionally in aggr_list. */
	rc = io_fop_seg_init(&new_seg, seg);
	if (rc < 0)
		return;
	c2_list_add_tail(aggr_list, &new_seg->is_linkage);
}

static void io_fop_segments_coalesce(struct c2_0vec *iovec,
				     struct c2_list *aggr_list)
{
	uint32_t     i;
	struct ioseg seg;

	C2_PRE(iovec != NULL);
	C2_PRE(aggr_list != NULL);

	/* For each segment from incoming IO vector, check if it can
	   be merged with any of the existing segments from aggr_list.
	   If yes, merge it else, add a new entry in aggr_list. */
	for (i = 0; i < iovec->z_bvec.ov_vec.v_nr; ++i) {
		ioseg_get(iovec, i, &seg);
		io_fop_seg_coalesce(&seg, aggr_list);
	}
}

/**
   Coalesces the IO vectors of a list of read/write fops into IO vector
   of given resultant fop. At a time, all fops in the list are either
   read fops or write fops. Both fop types can not be present simultaneously.

   @param res_fop - resultant fop with which the resulting IO vector is
   associated.
 */
static int io_fop_coalesce(struct c2_fop *res_fop)
{
	int			 rc;
	int			 i = 0;
	uint64_t		 curr_segs;
	struct c2_fop		*fop;
	struct c2_fop		*bkp_fop;
	struct c2_list		 aggr_list;
	struct c2_list		*items_list;
	struct c2_0vec		*iovec;
	struct c2_rpc_item	*item;
	struct ioseg		*ioseg;
	struct ioseg		*ioseg_next;
	struct c2_fop_type	*fopt;
	struct c2_bufvec	*res_bvec;
	struct c2_io_fop	*cfop;
	struct c2_net_buffer	*nbuf;
	struct c2_fop_cob_rw	*rw;

	C2_PRE(res_fop != NULL);
	items_list = &res_fop->f_item.ri_compound_items;
	C2_PRE(!c2_list_is_empty(items_list));

	fopt = res_fop->f_type;
	C2_PRE(is_io(res_fop));

        /* Makes a copy of original IO vector belonging to res_fop and place
           it in input parameter vec which can be used while restoring the
           IO vector. */
	cfop = c2_alloc(sizeof(struct c2_io_fop));
	if (cfop == NULL)
		return -ENOMEM;

	bkp_fop = &cfop->if_fop;
	c2_rpc_item_init(&bkp_fop->f_item);
	io_rw_get(bkp_fop)->crw_fid = *io_fop_fid_get(res_fop);

	c2_list_init(&aggr_list);

	/* Traverses the fop_list, get the IO vector from each fop,
	   pass it to a coalescing routine and get result back
	   in another list. */
	c2_list_for_each_entry(items_list, item, struct c2_rpc_item,
			       ri_field) {
		fop = c2_rpc_item_to_fop(item);
		iovec = io_0vec_get(fop);
		io_fop_segments_coalesce(iovec, &aggr_list);
	}

	curr_segs = c2_list_length(&aggr_list);
	nbuf = &c2_fop_to_rpcbulk(res_fop)->rb_nbuf;
	iovec = io_0vec_get(res_fop);
	res_bvec = iovec_get(res_fop);

	rc = c2_io_fop_init(cfop, fopt, iovec->z_bvec.ov_vec.v_nr,
			    res_bvec->ov_vec.v_count[0], nbuf->nb_dom);

	if (rc != 0)
		goto cleanup;

	*io_0vec_get(bkp_fop) = *iovec;

	iovec->z_bvec.ov_vec.v_count = NULL;
	iovec->z_bvec.ov_buf = NULL;
	C2_ALLOC_ARR(iovec->z_bvec.ov_vec.v_count, curr_segs);
	if (iovec->z_bvec.ov_vec.v_count == NULL)
		goto cleanup;
	C2_ALLOC_ARR(iovec->z_bvec.ov_buf, curr_segs);
	if (iovec->z_bvec.ov_buf == NULL)
		goto cleanup;

	c2_fop_to_rpcbulk(bkp_fop)->rb_nbuf.nb_length = nbuf->nb_length;

	c2_list_for_each_entry_safe(&aggr_list, ioseg, ioseg_next,
				    struct ioseg, is_linkage) {
		ioseg_set(iovec, i, ioseg);
		ioseg_unlink_free(ioseg);
		i++;
	}
	c2_list_fini(&aggr_list);
	*res_bvec = iovec->z_bvec;
	res_bvec->ov_vec.v_nr = i;
	nbuf->nb_length = c2_vec_count(&res_bvec->ov_vec);

	/* Changes the target object index vector according to the
	   coalesced vector. */
	rw = io_rw_get(res_fop);
	io_rw_get(bkp_fop)->crw_ivec = rw->crw_ivec;
	rw->crw_ivec.ci_nr = curr_segs;
	C2_ALLOC_ARR(rw->crw_ivec.ci_iosegs, curr_segs);
	if (rw->crw_ivec.ci_iosegs == NULL)
		goto cleanup;

	for (i = 0; i < curr_segs; ++i) {
		rw->crw_ivec.ci_iosegs[i].ci_index = iovec->z_indices[i];
		rw->crw_ivec.ci_iosegs[i].ci_count =
			iovec->z_bvec.ov_vec.v_count[i];
	}

	c2_list_add(&res_fop->f_item.ri_compound_items,
		    &bkp_fop->f_item.ri_field);
	return rc;
cleanup:
	C2_ASSERT(rc != 0);
	c2_list_for_each_entry_safe(&aggr_list, ioseg, ioseg_next,
				    struct ioseg, is_linkage)
		ioseg_unlink_free(ioseg);
	c2_list_fini(&aggr_list);
	c2_free(iovec->z_bvec.ov_vec.v_count);
	c2_free(iovec->z_bvec.ov_buf);
	c2_io_fop_fini(cfop);
	c2_free(cfop);
	return rc;
}

/**
   Returns the fid of given IO fop.
   @note This method only works for read and write IO fops.
   @retval On-wire fid of given fop.
 */
static struct c2_fop_file_fid *io_fop_fid_get(struct c2_fop *fop)
{
	return &(io_rw_get(fop))->crw_fid;
}

/**
   Returns if given 2 fops refer to same fid. The fids mentioned here
   are on-wire fids.
   @retval true if both fops refer to same fid, false otherwise.
 */
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
	struct c2_0vec		*bkpvec;
	struct c2_io_fop	*cfop;
	struct c2_0vec		*iovec;

	C2_PRE(fop != NULL);
	C2_PRE(is_io(fop));

	if (!c2_list_is_empty(&fop->f_item.ri_compound_items)) {
		bkpitem = c2_list_entry(c2_list_first(
				      &fop->f_item.ri_compound_items),
				      struct c2_rpc_item, ri_field);
		bkpfop = c2_rpc_item_to_fop(bkpitem);
		bkpvec = io_0vec_get(bkpfop);
		iovec = io_0vec_get(fop);
		c2_free(iovec->z_bvec.ov_vec.v_count);
		c2_free(iovec->z_bvec.ov_buf);
		*iovec = *bkpvec;
		c2_fop_to_rpcbulk(fop)->rb_nbuf.nb_length =
			c2_fop_to_rpcbulk(bkpfop)->rb_nbuf.nb_length;
		c2_list_del(&bkpfop->f_item.ri_field);
		cfop = container_of(bkpfop, struct c2_io_fop, if_fop);
		c2_io_fop_fini(cfop);
		c2_free(cfop);
	} else 
		c2_list_del(&fop->f_item.ri_field);
}

/**
 * readv FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_cob_readv_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
};

/**
 * writev FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_cob_writev_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
};

/**
 * Init function to initialize readv and writev reply FOMs.
 * Since there is no client side FOMs as of now, this is empty.
 * @param fop - fop on which this fom_init methods operates.
 * @param m - fom object to be created here.
 */
static int io_fop_cob_rwv_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/**
 * readv and writev reply FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_rwv_rep_ops = {
	.fto_fom_init = io_fop_cob_rwv_rep_fom_init,
	.fto_size_get = c2_xcode_fop_size_get
};

/* Rpc item type ops for IO operations. */
static void io_item_replied(struct c2_rpc_item *item, int rc)
{
	struct c2_fop *fop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	if (fop->f_type->ft_ops->fto_fop_replied != NULL)
		fop->f_type->ft_ops->fto_fop_replied(fop);
}

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

static uint64_t io_frags_nr_get(struct c2_rpc_item *item)
{
	struct c2_fop *fop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	return fop->f_type->ft_ops->fto_get_nfragments(fop);
}

int item_io_coalesce(struct c2_rpc_item *head, struct c2_list *list)
{
	int			 rc;
	struct c2_fop		*bfop;
	struct c2_fop		*ufop;
	struct c2_rpc_item	*item;
	struct c2_rpc_item	*item_next;
	struct c2_rpc_session	*session;
	c2_bcount_t		 max_bufsize;
	int32_t			 max_segs_nr;
	c2_bcount_t		 curr_bufsize = 0;
	c2_bcount_t		 curr_segs_nr = 0;
	struct c2_bufvec	*bvec;
	struct c2_net_domain	*netdom;

	C2_PRE(head != NULL);
	C2_PRE(list != NULL);

	session = container_of(list, struct c2_rpc_session, s_unbound_items);
	C2_ASSERT(session != NULL);
	C2_ASSERT(c2_mutex_is_locked(&session->s_mutex));

	if (c2_list_is_empty(list))
		return -EINVAL;

	if (!head->ri_type->rit_ops->rito_io_coalesce)
		return -EINVAL;

	/* Traverses through the list and finds out items that match with
	   head on basis of fid and intent (read/write). Matching items
	   are removed from session->s_unbound_items list and added to
	   head->compound_items list. */
	netdom = head->ri_session->s_conn->c_rpcmachine->cr_tm.ntm_dom;
	max_bufsize = c2_net_domain_get_max_buffer_size(netdom);
	max_segs_nr = c2_net_domain_get_max_buffer_segments(netdom);
	bfop = c2_rpc_item_to_fop(head);
	c2_list_for_each_entry_safe(list, item, item_next, struct c2_rpc_item,
				    ri_unbound_link) {
		ufop = c2_rpc_item_to_fop(item);
		if (io_fop_type_equal(bfop, ufop) &&
		    io_fop_fid_equal(bfop, ufop)) {
			/* Sends only those many fops so as to coalesce 
			 a new net buffer which can fit into
			 max_bufsize and max_segs_nr thresholds. */
			bvec = iovec_get(ufop);
			curr_bufsize += c2_vec_count(&bvec->ov_vec);
			curr_segs_nr += bvec->ov_vec.v_nr; 

			if (curr_bufsize > max_bufsize ||
			    curr_segs_nr > max_segs_nr)
				break;

			c2_list_del(&item->ri_unbound_link);
			c2_list_add(&head->ri_compound_items, &item->ri_field);
		}
	}

	if (c2_list_is_empty(&head->ri_compound_items))
		return -EINVAL;

	/* Add the bound item to list of compound items as this will
	   include the bound item's io vector in io coalescing. */
	c2_list_add(&head->ri_compound_items, &head->ri_field);

	rc = bfop->f_type->ft_ops->fto_io_coalesce(bfop);

	c2_list_del(&head->ri_field);
	return rc;
}

static const struct c2_rpc_item_type_ops rpc_item_readv_type_ops = {
        .rito_sent = NULL,
        .rito_added = NULL,
        .rito_replied = io_item_replied,
        .rito_item_size = io_item_size_get,
        .rito_io_frags_nr_get = io_frags_nr_get,
        .rito_io_coalesce = item_io_coalesce,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

static const struct c2_rpc_item_type_ops rpc_item_writev_type_ops = {
        .rito_sent = NULL,
        .rito_added = NULL,
        .rito_replied = io_item_replied,
        .rito_item_size = io_item_size_get,
        .rito_io_frags_nr_get = io_frags_nr_get,
        .rito_io_coalesce = item_io_coalesce,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

struct c2_rpc_item_type rpc_item_type_readv = {
        .rit_ops = &rpc_item_readv_type_ops,
};

struct c2_rpc_item_type rpc_item_type_writev = {
        .rit_ops = &rpc_item_writev_type_ops,
};

C2_FOP_TYPE_DECLARE_NEW(c2_fop_cob_readv, "Read request",
			C2_IOSERVICE_READV_OPCODE, &c2_io_cob_readv_ops,
			&rpc_item_type_readv);
C2_FOP_TYPE_DECLARE_NEW(c2_fop_cob_writev, "Write request",
			C2_IOSERVICE_WRITEV_OPCODE, &c2_io_cob_writev_ops,
			&rpc_item_type_writev);

C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply",
		    C2_IOSERVICE_WRITEV_REP_OPCODE, &c2_io_rwv_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply",
		    C2_IOSERVICE_READV_REP_OPCODE, &c2_io_rwv_rep_ops);

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
