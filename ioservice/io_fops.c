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
#include "fop/fop.h"
#include "xcode/bufvec_xcode.h" /* c2_xcode_fop_size_get() */
#include "fop/fop_format_def.h"
#include "ioservice/io_fops.ff"

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

/**
   @page fop_io_bulk_client DLD for client side of io bulk transfer.

   - @ref DLD-ovw
   - @ref DLD-def
   - @ref DLD-req
   - @ref DLD-depends
   - @ref DLD-highlights
   - @subpage DLD-fspec "Functional Specification" <!-- Note @subpage -->
   - @ref DLD-lspec
      - @ref DLD-lspec-comps
      - @ref DLD-lspec-sc1
      - @ref DLD-lspec-state
      - @ref DLD-lspec-thread
      - @ref DLD-lspec-numa
   - @ref DLD-conformance
   - @ref DLD-ut
   - @ref DLD-st
   - @ref DLD-O
   - @ref DLD-ref


   <hr>
   @section DLD-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   This document describes the working of client side of io bulk transfer.
   This functionality is used only for io path. Colibri network layer
   incorporates a bulk transport mechanism to transfer user buffers
   in zero-copy fashion.
   The generic io fop contains a network buffer descriptor which refers to a
   network buffer. The Colibri client attaches the kernel pages to the net
   buffer associated with io fop and submits it to rpc layer.
   Rpc layer populates the net buffer descriptor from io fop and sends
   the fop over wire.
   The receiver starts the zero-copy of buffers using the net buffer
   descriptor from io fop.

   It is <i>required</i> that the <b>Functional Specification</b> and
   the <b>Detailed Functional Specification</b> be located in the
   primary header file - this is the header file with the declaration
   of the external interfaces that consumers of the component's API
   would include.  In case of stand alone components, an appropriate
   alternative header file should be chosen.

   <b>Structure of the DLD</b><br>
   The DLD specification is <b>required</b> to be sectioned in the
   specific manner illustrated by the <tt>dld-sample.c</tt> and
   <tt>dld-sample.h</tt> files.  This is similar in structure and
   purpose to the sectioning found in a High Level Design.

   It is probably desirable to split the Detailed Functional
   Specifications into separate header files for each sub-module of
   the component.  This example illustrates a component with a single
   module.

   <b>Formatting language</b><br>
   Doxygen is the formatting tool of choice.  The Doxygen @@page
   format is used to define a separate top-level browsable element
   that contains the body of the DLD. The @@section, @@subsection and
   @@subsubsection formatting commands are used to provide internal
   structure.  The page title will be visible in the <b>Related
   Pages</b> tab in the main browser window, as well as displayed as a
   top-level element in the explorer side-bar.

   The Functional Specification is to be located in the primary header
   file of the component in a Doxygen @@page that is referenced as a
   @@subpage from the table of contents of the main DLD specification.
   This sub-page shows up as leaf element of the DLD in the explorer
   side-bar.

   Detailed functional specifications follow the Functional
   Specification, using Doxygen @@defgroup commands for each component
   module.

   <hr>
   @section DLD-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   c2t1fs Colibri client program. Works as a kernel module.
   bulk transport Event based, asynchronous message passing functionality
   of Colibri network layer.
   io fop A generic io fop that is used for read and write.
   rpc bulk An interface to abstract usage of network buffers by client
   and server.

   <hr>
   @section DLD-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   They should be expressed in a list, thusly:
   - <b>R.DLD.Structured</b> The DLD shall be decomposed into a standard
   set of section.  Sub-sections may be used to further decompose the
   material of a section into logically disjoint units.
   - <b>R.DLD.What</b> The DLD shall describe the externally visible
   data structures and interfaces of the component through a
   functional specification section.
   - <b>R.DLD.How</b> The DLD shall explain its inner algorithms through
   a logical specification section.
   - <b>R.DLD.Maintainable</b> The DLD shall be easily maintainable during
   the lifetime of the code.

   <hr>
   @section DLD-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   The DLD specification style guide depends on the HLD and AR
   specifications as they identify requirements, use cases, <i>\&c.</i>.

   <hr>
   @section DLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - The DLD specification requires formal sectioning to address specific
   aspects of the design.
   - The DLD is with the code, and the specification is designed to be
   viewed either as text or through Doxygen.
   - This document can be used as a template.

   <hr>
   @section DLD-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref DLD-lspec-comps
   - @ref DLD-lspec-sc1
      - @ref DLD-lspec-ds1
      - @ref DLD-lspec-sub1
      - @ref DLDDFSInternal  <!-- Note link -->
   - @ref DLD-lspec-state
   - @ref DLD-lspec-thread
   - @ref DLD-lspec-numa


   @subsection DLD-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   Doxygen is limited in its internal support for diagrams. It has built in
   support for @c dot and @c mscgen, and examples of both are provided in this
   template.  Please remember that every diagram <i>must</i> be accompanied by
   an explanation.

   The following @@dot diagram shows the internal components of the Network
   layer, and also illustrates its primary consumer, the RPC layer.
   @dot
   digraph {
     node [style=box];
     label = "Network Layer Components and Interactions";
     subgraph cluster_rpc {
         label = "RPC Layer";
         rpcC [label="Connectivity"];
	 rpcO [label="Output"];
     }
     subgraph cluster_net {
         label = "Network Layer";
	 netM [label="Messaging"];
	 netT [label="Transport"];
	 netL [label="Legacy RPC emulation", style="filled"];
	 netM -> netT;
	 netL -> netM;
     }
     rpcC -> netM;
     rpcO -> netM;
   }
   @enddot

   The @@msc command is used to invoke @c mscgen, which creates sequence
   diagrams. For example:
   @msc
   a,b,c;

   a->b [ label = "ab()" ] ;
   b->c [ label = "bc(TRUE)"];
   c=>c [ label = "process(1)" ];
   c=>c [ label = "process(2)" ];
   ...;
   c=>c [ label = "process(n)" ];
   c=>c [ label = "process(END)" ];
   a<<=c [ label = "callback()"];
   ---  [ label = "If more to run", ID="*" ];
   a->a [ label = "next()"];
   a->c [ label = "ac1()\nac2()"];
   b<-c [ label = "cb(TRUE)"];
   b->b [ label = "stalled(...)"];
   a<-b [ label = "ab() = FALSE"];
   @endmsc
   Note that when entering commands for @c mscgen, do not include the
   <tt>msc { ... }</tt> block delimiters.
   You need the @c mscgen program installed on your system - it is part
   of the Scientific Linux based DevVM.

   UML and sequence diagrams often illustrate points better than any written
   explanation.  However, you have to resort to an external tool to generate
   the diagram, save the image in a file, and load it into your DLD.

   An image is relatively easy to load provided you remember that the
   Doxygen output is viewed from the @c doc/html directory, so all paths
   should be relative to that frame of reference.  For example:
   <img src="../../doc/dld-sample-uml.png">
   I found that a PNG format image from Visio shows up with the correct
   image size while a GIF image was wrongly sized.  Your experience may
   be different, so please ensure that you validate the Doxygen output
   for correct image rendering.

   If an external tool, such as Visio or @c dia, is used to create an
   image, the source of that image (e.g. the Visio <tt>.vsd</tt> or
   the <tt>.dia</tt> file) should be checked into the source tree so
   that future maintainers can modify the figure.  This applies to all
   non-embedded image source files, not just Visio or @c dia.

   @subsection DLD-lspec-sc1 Subcomponent design
   <i>Such sections briefly describes the purpose and design of each
   sub-component. Feel free to add multiple such sections, and any additional
   sub-sectioning within.</i>

   Sample non-standard sub-section to illustrate that it is possible to
   document the design of a sub-component.  This contrived example demonstrates
   @@subsubsections for the sub-component's data structures and subroutines.

   @subsubsection DLD-lspec-ds1 Subcomponent Data Structures
   <i>This section briefly describes the internal data structures that are
   significant to the design of the sub-component. These should not be a part
   of the Functional Specification.</i>

   Describe <i>briefly</i> the internal data structures that are significant to
   the design.  These should not be described in the Functional Specification
   as they are not part of the external interface.  It is <b>not necessary</b>
   to describe all the internal data structures here.  They should, however, be
   documented in Detailed Functional Specifications, though separate from the
   external interfaces.  See @ref DLDDFSInternal for example.

   - dld_sample_internal

   @subsubsection DLD-lspec-sub1 Subcomponent Subroutines
   <i>This section briefly describes the interfaces of the sub-component that
   are of significance to the design.</i>

   Describe <i>briefly</i> the internal subroutines that are significant to the
   design.  These should not be described in the Functional Specification as
   they are not part of the external interface.  It is <b>not necessary</b> to
   describe all the internal subroutines here.  They should, however, be
   documented in Detailed Functional Specifications, though separate from the
   external interfaces.  See @ref DLDDFSInternal for example.

   - dld_sample_internal_invariant()

   @subsection DLD-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   Diagrams are almost essential here. The @@dot tool is the easiest way to
   create state diagrams, and is very readable in text form too.  Here, for
   example, is a @@dot version of a figure from the "rpc/session.h" file:
   @dot
   digraph example {
       size = "5,6"
       label = "RPC Session States"
       node [shape=record, fontname=Helvetica, fontsize=10]
       S0 [label="", shape="plaintext", layer=""]
       S1 [label="Uninitialized"]
       S2 [label="Initialized"]
       S3 [label="Connecting"]
       S4 [label="Active"]
       S5 [label="Terminating"]
       S6 [label="Terminated"]
       S7 [label="Uninitialized"]
       S8 [label="Failed"]
       S0 -> S1 [label="allocate"]
       S1 -> S2 [label="c2_rpc_conn_init()"]
       S2 -> S3 [label="c2_rpc_conn_established()"]
       S3 -> S4 [label="c2_rpc_conn_establish_reply_received()"]
       S4 -> S5 [label="c2_rpc_conn_terminate()"]
       S5 -> S6 [label="c2_rpc_conn_terminate_reply_received()"]
       S6 -> S7 [label="c2_rpc_conn_fini()"]
       S2 -> S8 [label="failed"]
       S3 -> S8 [label="timeout or failed"]
       S5 -> S8 [label="timeout or failed"]
       S8 -> S7 [label="c2_rpc_conn_fini()"]
   }
   @enddot
   The @c dot program is part of the Scientific Linux DevVM.

   @subsection DLD-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   This section must explain all aspects of synchronization, including locking
   order protocols, existential protection of objects by their state, etc.
   A diagram illustrating lock scope would be very useful here.
   For example, here is a @@dot illustration of the scope and locking order
   of the mutexes in the Networking Layer:
   @dot
   digraph {
      node [shape=plaintext];
      subgraph cluster_m1 { // represents mutex scope
         // sorted R-L so put mutex name last to align on the left
         rank = same;
	 n1_2 [label="dom_fini()"];  // procedure using mutex
	 n1_1 [label="dom_init()"];
         n1_0 [label="c2_net_mutex"];// mutex name
      }
      subgraph cluster_m2 {
         rank = same;
	 n2_2 [label="tm_fini()"];
         n2_1 [label="tm_init()"];
         n2_4 [label="buf_deregister()"];
	 n2_3 [label="buf_register()"];
         n2_0 [label="nd_mutex"];
      }
      subgraph cluster_m3 {
         rank = same;
	 n3_2 [label="tm_stop()"];
         n3_1 [label="tm_start()"];
	 n3_6 [label="ep_put()"];
	 n3_5 [label="ep_create()"];
	 n3_4 [label="buf_del()"];
	 n3_3 [label="buf_add()"];
         n3_0 [label="ntm_mutex"];
      }
      label="Mutex usage and locking order in the Network Layer";
      n1_0 -> n2_0;  // locking order
      n2_0 -> n3_0;
   }
   @enddot

   @subsection DLD-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   Conversely, it can describe if sub-optimal behavior arises due
   to contention for shared component resources by multiple processors.

   The section is marked mandatory because it forces the designer to
   consider these aspects of concurrency.

   <hr>
   @section DLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref DLD-req section,
   and explains briefly how the DLD meets the requirement.</i>

   Note the subtle difference in that <b>I</b> tags are used instead of
   the <b>R</b> tags of the requirements section.  The @b I of course,
   stands for "implements":

   - <b>I.DLD.Structured</b> The DLD specification provides a structural
   breakdown along the lines of the HLD specification.  This makes it
   easy to understand and analyze the various facets of the design.
   - <b>I.DLD.What</b> The DLD style guide requires that a
   DLD contain a Functional Specification section.
   - <b>I.DLD.How</b> The DLD style guide requires that a
   DLD contain a Logical Specification section.
   - <b>I.DLD.Maintainable</b> The DLD style guide requires that the
   DLD be written in the main header file of the component.
   It can be maintained along with the code, without
   requiring one to resort to other documents and tools.  The only
   exception to this would be for images referenced by the DLD specification,
   as Doxygen does not provide sufficient support for this purpose.

   This section is meant as a cross check for the DLD writer to ensure
   that all requirements have been addressed.  It is recommended that you
   fill it in as part of the DLD review.

   <hr>
   @section DLD-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   Unit tests should be planned for all interfaces exposed by the
   component.  Testing should not just include correctness tests, but
   should also test failure situations.  This includes testing of
   <i>expected</i> return error codes when presented with invalid
   input or when encountering unexpected data or state.  Note that
   assertions are not testable - the unit test program terminates!

   Another area of focus is boundary value tests, where variable
   values are equal to but do not exceed their maximum or minimum
   possible values.

   As a further refinement and a plug for Test Driven Development, it
   would be nice if the designer can plan the order of development of
   the interfaces and their corresponding unit tests.  Code inspection
   could overlap development in such a model.

   Testing should relate to specific use cases described in the HLD if
   possible.

   It is acceptable that this section be located in a separate @@subpage like
   along the lines of the Functional Specification.  This can be deferred
   to the UT phase where additional details on the unit tests are available.

   <hr>
   @section DLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   Testing should relate to specific use cases described in the HLD if
   possible.

   It is acceptable that this section be located in a separate @@subpage like
   along the lines of the Functional Specification.  This can be deferred
   to the ST phase where additional details on the system tests are available.


   <hr>
   @section DLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section DLD-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjQ3Z3NraDI4ZG0&hl=en_US">Detailed level design HOWTO</a>,
   an older document on which this style guide is partially based.
   - <a href="http://www.stack.nl/~dimitri/doxygen/manual.html">Doxygen
   Manual</a>
   - <a href="http://www.graphviz.org">Graphviz - Graph Visualization
   Software</a> for documentation on the @c dot command.
   - <a href="http://www.mcternan.me.uk/mscgen">Mscgen home page</a>

 */

#include "doc/dld_template.h"

/**
   @defgroup DLDDFSInternal Colibri Sample Module Internals
   @brief Detailed functional specification of the internals of the
   sample module.

   This example is part of the DLD Template and Style Guide. It illustrates
   how to keep internal documentation separate from external documentation
   by using multiple @@defgroup commands in different files.

   Please make sure that the module cross-reference the DLD, as shown below.

   @see @ref DLD and @ref DLD-lspec

   @{
 */

/** @} end-of-DLDFS */
struct c2_io_ioseg {
	/** IO segment for read or write request fop. */
	struct c2_fop_io_seg	*rw_seg;
        /** Linkage to the list of such structures. */
        struct c2_list_link	 io_linkage;
};

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

/**
   Allocates the array of IO segments from IO vector.
   @retval - 0 if succeeded, negative error code otherwise.
 */
static int iosegs_alloc(struct c2_fop_io_vec *iovec, const uint32_t count)
{
	C2_PRE(iovec != NULL);
	C2_PRE(count != 0);

	C2_ALLOC_ARR(iovec->iv_segs, count);
	return iovec->iv_segs == NULL ? 0 : -ENOMEM;
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

/**
   Returns IO vector from given IO fop.
 */
static struct c2_fop_io_vec *iovec_get(struct c2_fop *fop)
{
	return &io_rw_get(fop)->crw_iovec;
}

/**
   Deallocates and removes a generic IO segment from aggr_list.
 */
static void ioseg_unlink_free(struct c2_io_ioseg *ioseg)
{
	C2_PRE(ioseg != NULL);

	c2_list_del(&ioseg->io_linkage);
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

/**
   Returns if given 2 fops belong to same type.
 */
static bool io_fop_type_equal(const struct c2_fop *fop1,
		const struct c2_fop *fop2)
{
	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	return fop1->f_type == fop2->f_type;
}

/**
   Returns the number of IO fragements (discontiguous buffers)
   for a fop of type read or write.
 */
static uint64_t io_fop_fragments_nr_get(struct c2_fop *fop)
{
	uint32_t	      i;
	uint64_t	      frag_nr = 1;
	struct c2_fop_io_vec *iovec;

	C2_PRE(fop != NULL);
	C2_PRE(is_io(fop));

	iovec = iovec_get(fop);
	for (i = 0; i < iovec->iv_count - 1; ++i)
		if (iovec->iv_segs[i].is_offset +
		    iovec->iv_segs[i].is_buf.ib_count !=
		    iovec->iv_segs[i+1].is_offset)
			frag_nr++;
	return frag_nr;
}

/**
   Allocates a new generic IO segment
 */
static int io_fop_seg_init(struct c2_io_ioseg **ns, struct c2_io_ioseg *cseg)
{
	struct c2_io_ioseg *new_seg;

	C2_PRE(ns != NULL);

	C2_ALLOC_PTR(new_seg);
	if (new_seg == NULL)
		return -ENOMEM;
	C2_ALLOC_PTR(new_seg->rw_seg);
	if (new_seg->rw_seg == NULL) {
		c2_free(new_seg);
		return -ENOMEM;
	}
	*ns = new_seg;
	*new_seg = *cseg;
	return 0;
}

/**
   Adds a new IO segment to the aggr_list conditionally.
 */
static int io_fop_seg_add_cond(struct c2_io_ioseg *cseg,
		struct c2_io_ioseg *nseg)
{
	int			 rc;
	struct c2_io_ioseg	*new_seg;

	C2_PRE(cseg != NULL);
	C2_PRE(nseg != NULL);

	if (nseg->rw_seg->is_offset < cseg->rw_seg->is_offset) {
		rc = io_fop_seg_init(&new_seg, nseg);
		if (rc < 0)
			return rc;

		c2_list_add_before(&cseg->io_linkage, &new_seg->io_linkage);
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
static void io_fop_seg_coalesce(struct c2_io_ioseg *seg,
		struct c2_list *aggr_list)
{
	int			 rc;
	bool			 added = false;
	struct c2_io_ioseg	*new_seg;
	struct c2_io_ioseg	*ioseg;
	struct c2_io_ioseg	*ioseg_next;

	C2_PRE(seg != NULL);
	C2_PRE(aggr_list != NULL);

	c2_list_for_each_entry_safe(aggr_list, ioseg, ioseg_next,
			struct c2_io_ioseg, io_linkage) {
		/* If given segment fits before some other segment
		   in increasing order of offsets, add it before
		   current segments from aggr_list. */
		rc = io_fop_seg_add_cond(ioseg, seg);
		if (rc == -ENOMEM)
			return;
		if (rc == 0) {
			added = true;
			break;
		}
	}

	/* Add a new IO segment unconditionally in aggr_list. */
	if (!added) {
		rc = io_fop_seg_init(&new_seg, seg);
		if (rc < 0)
			return;
		c2_list_add_tail(aggr_list, &new_seg->io_linkage);
	}
}

/**
   Coalesces the IO segments from a number of IO fops to create a list
   of IO segments containing merged segments.
   @param aggr_list - list of IO segments which gets populated during
   this operation.
*/
static int io_fop_segments_coalesce(struct c2_fop_io_vec *iovec,
		struct c2_list *aggr_list)
{
	uint32_t		i;
	int			rc = 0;
	uint32_t		segs_nr;
	struct c2_io_ioseg	ioseg;

	C2_PRE(iovec != NULL);
	C2_PRE(aggr_list != NULL);

	/* For each segment from incoming IO vector, check if it can
	   be merged with any of the existing segments from aggr_list.
	   If yes, merge it else, add a new entry in aggr_list. */
	segs_nr = iovec->iv_count;
	for (i = 0; i < segs_nr; ++i) {
		ioseg.rw_seg = &iovec->iv_segs[i];
		io_fop_seg_coalesce(&ioseg, aggr_list);
	}

	return rc;
}

/**
   Coalesces the IO vectors of a list of read/write fops into IO vector
   of given resultant fop. At a time, all fops in the list are either
   read fops or write fops. Both fop types can not be present simultaneously.

   @param fop_list - list of fops. These structures contain either read or
   write fops. Both fop types can not be present in the fop_list simultaneously.
   @param res_fop - resultant fop with which the resulting IO vector is
   associated.
   @param bkpfop - A fop used to store the original IO vector of res_fop
   whose IO vector is replaced by the coalesced IO vector.
   from resultant fop and it is restored on receving the reply of this
   coalesced IO request. @see io_fop_iovec_restore.
 */
static int io_fop_coalesce(const struct c2_list *fop_list,
		struct c2_fop *res_fop, struct c2_fop *bkp_fop)
{
	int			 res;
	int			 i = 0;
	uint64_t		 curr_segs;
	struct c2_fop		*fop;
	struct c2_list		 aggr_list;
	struct c2_io_ioseg	*ioseg;
	struct c2_io_ioseg	*ioseg_next;
	struct c2_io_ioseg	 res_ioseg;
	struct c2_fop_type	*fopt;
	struct c2_fop_io_vec	*iovec;
	struct c2_fop_io_vec	*bkp_vec;
	struct c2_fop_io_vec	*res_iovec;

	C2_PRE(fop_list != NULL);
	C2_PRE(res_fop != NULL);

	fopt = res_fop->f_type;
	C2_PRE(is_io(res_fop));

        /* Make a copy of original IO vector belonging to res_fop and place
           it in input parameter vec which can be used while restoring the
           IO vector. */
	bkp_fop = c2_fop_alloc(fopt, NULL);
	if (bkp_fop == NULL)
		return -ENOMEM;

	c2_list_init(&aggr_list);

	/* Traverse the fop_list, get the IO vector from each fop,
	   pass it to a coalescing routine and get result back
	   in another list. */
	c2_list_for_each_entry(fop_list, fop, struct c2_fop, f_link) {
		iovec = iovec_get(fop);
		res = io_fop_segments_coalesce(iovec, &aggr_list);
	}

	/* Allocate a new generic IO vector and copy all (merged) IO segments
	   to the new vector and make changes to res_fop accordingly. */
	C2_ALLOC_PTR(res_iovec);
	if (res_iovec == NULL) {
		res = -ENOMEM;
		goto cleanup;
	}

	curr_segs = c2_list_length(&aggr_list);
	res = iosegs_alloc(res_iovec, curr_segs);
	if (res != 0)
		goto cleanup;
	res_iovec->iv_count = curr_segs;

	c2_list_for_each_entry_safe(&aggr_list, ioseg, ioseg_next,
			struct c2_io_ioseg, io_linkage) {
		res_ioseg.rw_seg = &res_iovec->iv_segs[i];
		*res_ioseg.rw_seg = *ioseg->rw_seg;
		ioseg_unlink_free(ioseg);
		i++;
	}
	c2_list_fini(&aggr_list);
	res_iovec->iv_count = i;

	iovec = iovec_get(res_fop);
	bkp_vec = iovec_get(bkp_fop);
	*bkp_vec = *iovec;
	*iovec = *res_iovec;
	return res;
cleanup:
	C2_ASSERT(res != 0);
	if (res_iovec != NULL)
		c2_free(res_iovec);
	c2_list_for_each_entry_safe(&aggr_list, ioseg, ioseg_next,
				    struct c2_io_ioseg, io_linkage)
		ioseg_unlink_free(ioseg);
	c2_list_fini(&aggr_list);
	return res;
}

/**
   Restores the original IO vector of parameter fop from the appropriate
   IO vector from parameter bkpfop.
   @param fop - Incoming fop. This fop is same as res_fop parameter from
   the subroutine io_fop_coalesce. @see io_fop_coalesce.
   @param bkpfop - Backup fop with which the original IO vector of
   coalesced fop was stored.
 */
static void io_fop_iovec_restore(struct c2_fop *fop, struct c2_fop *bkpfop)
{
	struct c2_fop_io_vec *vec;

	C2_PRE(fop != NULL);
	C2_PRE(bkpfop != NULL);

	vec = iovec_get(fop);
	c2_free(vec->iv_segs);
	*vec = *(iovec_get(bkpfop));
	c2_fop_free(bkpfop);
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

/**
 * readv FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_cob_readv_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = NULL,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_op_equal = io_fop_type_equal,
	.fto_fid_equal = io_fop_fid_equal,
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_iovec_restore = io_fop_iovec_restore,
};

/**
 * writev FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_cob_writev_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = NULL,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_op_equal = io_fop_type_equal,
	.fto_fid_equal = io_fop_fid_equal,
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_iovec_restore = io_fop_iovec_restore,
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

/**
 * FOP definitions for readv and writev operations.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv, "Read request",
		C2_IOSERVICE_READV_OPCODE, &c2_io_cob_readv_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev, "Write request",
		C2_IOSERVICE_WRITEV_OPCODE, &c2_io_cob_writev_ops);

/**
 * FOP definitions of readv and writev reply FOPs.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply",
		    C2_IOSERVICE_WRITEV_REP_OPCODE, &c2_io_rwv_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply",
		    C2_IOSERVICE_READV_REP_OPCODE, &c2_io_rwv_rep_ops);

static struct c2_fop_type_format *ioservice_fmts[] = {
	&c2_fop_file_fid_tfmt,
	&c2_fop_io_buf_tfmt,
	&c2_fop_io_seg_tfmt,
	&c2_fop_io_vec_tfmt,
	&c2_fop_cob_rw_tfmt,
	&c2_fop_cob_rw_reply_tfmt,
};

static struct c2_fop_type *ioservice_fops[] = {
	&c2_fop_cob_readv_fopt,
	&c2_fop_cob_writev_fopt,
	&c2_fop_cob_readv_rep_fopt,
	&c2_fop_cob_writev_rep_fopt,
};

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
