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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 02/08/2012
 */

/**
   @page LNetDRVDLD LNet Transport Device DLD

   - @ref LNetDRVDLD-ovw
   - @ref LNetDRVDLD-def
   - @ref LNetDRVDLD-req
   - @ref LNetDRVDLD-depends
   - @ref LNetDRVDLD-highlights
   - @subpage LNetDRVDLD-fspec "Functional Specification" <!-- Note @subpage -->
   - @ref LNetDRVDLD-lspec
      - @ref LNetDRVDLD-lspec-comps
      - @ref LNetDRVDLD-lspec-sc1
      - @ref LNetDRVDLD-lspec-state
      - @ref LNetDRVDLD-lspec-thread
      - @ref LNetDRVDLD-lspec-numa
   - @ref LNetDRVDLD-conformance
   - @ref LNetDRVDLD-ut
   - @ref LNetDRVDLD-st
   - @ref LNetDRVDLD-O
   - @ref LNetDRVDLD-ref


   <hr>
   @section LNetDRVDLD-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   This document is intended to be a style guide for a detail level
   design specification, and is designed to be viewed both through a
   text editor and in a browser after Doxygen processing.

   You can use this document as a template by deleting all content,
   while retaining the sections referenced above and the overall
   Doxygen structure of a page with one or more component modules.
   You <b><i>must</i></b> change the Doxygen reference tags used in
   @@page, @@section, @@subsection and @@defgroup examples when
   copying this template, and adjust @@ref and @@subpage references in
   the table of contents of your own document accordingly.

   Please provide a table of contents for the major sections, as shown above.
   Please use horizontal ruling exactly as shown in this template, and do not
   introduce additional lines.

   It is recommended that you retain the italicized instructions that
   follow the formally recognized section headers, until at least the
   DLD review phase.  You may leave the instructions in the final
   document if you wish.

   It is imperative that the document be neat when viewed textually
   through an editor and when browsing the Doxygen output - it is
   intended to be relevant for the entire code life-cycle.  Please
   check your grammar and punctuation, and run the document through a
   spelling checker.  It is also recommended that you run the source
   document through a paragraph formatting tool to produce neater
   text, though be careful while doing so as text formatters do not
   understand Doxygen syntax and significant line breaks.


   <b>Purpose of a DLD</b><br>
   The purpose of the Detailed Level Design (DLD) specification of a
   component is to:
   - Refine higher level designs
   - To be verified by inspectors and architects
   - To guide the coding phase

   <b>Location and layout of the DLD Specification</b><br>
   The Colibri project requires Detailed Level Designs in the source
   code itself.  This greatly aids in keeping the design documentation
   up to date through the lifetime of the implementation code.

   The main DLD specification shall primarily be located in a C file
   in the component being designed.  The main DLD specification can be
   quite large and is probably not of interest to a consumer of the
   component.

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

   Not all sections may be applicable to every design, but sections
   declared to be mandatory may not be omitted.  If a mandatory
   section does not apply, it should clearly be marked as
   non-applicable, along with an explanation.  Additional sections or
   sub-sectioning may be added as required.

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
   @section LNetDRVDLD-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   Previously defined terms:
   - <b>Logical Specification</b> This explains how the component works.
   - <b>Functional Specification</b> This is explains how to use the component.

   New terms:
   - <b>Detailed Functional Specification</b> This provides
     documentation of ll the data structures and interfaces (internal
     and external).
   - <b>State model</b> This explains the life cycle of component data
     structures.
   - <b>Concurrency and threading model</b> This explains how the the
     component works in a multi-threaded environment.

   <hr>
   @section LNetDRVDLD-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   They should be expressed in a list, thusly:
   - @b R.DLD.Structured The DLD shall be decomposed into a standard
   set of section.  Sub-sections may be used to further decompose the
   material of a section into logically disjoint units.
   - @b R.DLD.What The DLD shall describe the externally visible
   data structures and interfaces of the component through a
   functional specification section.
   - @b R.DLD.How The DLD shall explain its inner algorithms through
   a logical specification section.
   - @b R.DLD.Maintainable The DLD shall be easily maintainable during
   the lifetime of the code.

   <hr>
   @section LNetDRVDLD-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   The DLD specification style guide depends on the HLD and AR
   specifications as they identify requirements, use cases, <i>\&c.</i>.

   <hr>
   @section LNetDRVDLD-highlights Design Highlights
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
   @section LNetDRVDLD-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref LNetDRVDLD-lspec-comps
   - @ref LNetDRVDLD-lspec-sc1
      - @ref LNetDRVDLD-lspec-ds1
      - @ref LNetDRVDLD-lspec-sub1
      - @ref LNetDRVDLD  <!-- Note link -->
   - @ref LNetDRVDLD-lspec-state
   - @ref LNetDRVDLD-lspec-thread
   - @ref LNetDRVDLD-lspec-numa


   @subsection LNetDRVDLD-lspec-comps Component Overview
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

   @subsection LNetDRVDLD-lspec-sc1 Subcomponent design
   <i>Such sections briefly describes the purpose and design of each
   sub-component. Feel free to add multiple such sections, and any additional
   sub-sectioning within.</i>

   Sample non-standard sub-section to illustrate that it is possible to
   document the design of a sub-component.  This contrived example demonstrates
   @@subsubsections for the sub-component's data structures and subroutines.

   @subsubsection LNetDRVDLD-lspec-ds1 Subcomponent Data Structures
   <i>This section briefly describes the internal data structures that are
   significant to the design of the sub-component. These should not be a part
   of the Functional Specification.</i>

   Describe <i>briefly</i> the internal data structures that are significant to
   the design.  These should not be described in the Functional Specification
   as they are not part of the external interface.  It is <b>not necessary</b>
   to describe all the internal data structures here.  They should, however, be
   documented in Detailed Functional Specifications, though separate from the
   external interfaces.  See @ref LNetDRVDLDDFSInternal for example.

   - nlx_sample_internal

   @subsubsection LNetDRVDLD-lspec-sub1 Subcomponent Subroutines
   <i>This section briefly describes the interfaces of the sub-component that
   are of significance to the design.</i>

   Describe <i>briefly</i> the internal subroutines that are significant to the
   design.  These should not be described in the Functional Specification as
   they are not part of the external interface.  It is <b>not necessary</b> to
   describe all the internal subroutines here.  They should, however, be
   documented in Detailed Functional Specifications, though separate from the
   external interfaces.  See @ref LNetDRVDLDDFSInternal for example.

   - nlx_sample_internal_invariant()

   @subsection LNetDRVDLD-lspec-state State Specification
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

   @subsection LNetDRVDLD-lspec-thread Threading and Concurrency Model
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

   @subsection LNetDRVDLD-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   Conversely, it can describe if sub-optimal behavior arises due
   to contention for shared component resources by multiple processors.

   The section is marked mandatory because it forces the designer to
   consider these aspects of concurrency.

   <hr>
   @section LNetDRVDLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref LNetDRVDLD-req section,
   and explains briefly how the DLD meets the requirement.</i>

   Note the subtle difference in that @b I tags are used instead of
   the @b R tags of the requirements section.  The @b I of course,
   stands for "implements":

   - @b I.DLD.Structured The DLD specification provides a structural
   breakdown along the lines of the HLD specification.  This makes it
   easy to understand and analyze the various facets of the design.
   - @b I.DLD.What The DLD style guide requires that a
   DLD contain a Functional Specification section.
   - @b I.DLD.How The DLD style guide requires that a
   DLD contain a Logical Specification section.
   - @b I.DLD.Maintainable The DLD style guide requires that the
   DLD be written in the main header file of the component.
   It can be maintained along with the code, without
   requiring one to resort to other documents and tools.  The only
   exception to this would be for images referenced by the DLD specification,
   as Doxygen does not provide sufficient support for this purpose.

   This section is meant as a cross check for the DLD writer to ensure
   that all requirements have been addressed.  It is recommended that you
   fill it in as part of the DLD review.

   <hr>
   @section LNetDRVDLD-ut Unit Tests
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

   Use the Doxygen @@test tag to identify each test.  Doxygen collects these
   and displays them on a "Test List" page.

   <hr>
   @section LNetDRVDLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   Testing should relate to specific use cases described in the HLD if
   possible.

   It is acceptable that this section be located in a separate @@subpage like
   along the lines of the Functional Specification.  This can be deferred
   to the ST phase where additional details on the system tests are available.


   <hr>
   @section LNetDRVDLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section LNetDRVDLD-ref References
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

#include "net/lnet/lnet_ioctl.h"
#include "klnet_drv.h"

/**
   @defgroup LNetDRVDLDDFSInternal LNet Transport Device Internals
   @ingroup LNetDRVDLDDFS
   @brief Detailed functional specification of the internals of the
   sample module.

   This example is part of the DLD Template and Style Guide. It illustrates
   how to keep internal documentation separate from external documentation
   by using multiple @@defgroup commands in different files.

   Please make sure that the module cross-reference the DLD, as shown below.

   @see @ref LNetDRVDLD "LNet Transport Device DLD" and @ref LNetDRVDLD-lspec

   @{
 */

enum {
	DD_MAGIC = 0x64645f6d61676963ULL, /* dd_magic */
	DD_INITIAL_VALUE = 41,
};

/**
   Track each mapped memory region
 */
struct nlx_mem_area {
	/** Opaque user space address corresponding to this memory area. */
	unsigned long ma_user_addr;
	/** pages for each mapped object, assume each object < PAGE_SIZE
	    so max of 2 structs are required.
	 */
	struct page *ma_page[2];
	/** kernel addresses corresponding to each page */
	void *ma_addr[2];
	/** link in the nlx_dev_data::dd_mem_area @todo should be a tlink */
	struct c2_list_link ma_link;
};

/** Private data for each nlx file */
struct nlx_dev_data {
	uint64_t dd_magic;
	struct c2_mutex dd_mutex;
	/** proof-of-concept value to exchange via ioctl */
	unsigned int dd_value;
	/** list of struct nlx_mem_area, @todo should be a tlist */
	struct c2_list dd_mem_areas;
};

/**
   Release (unmap, unpin, unlink and free) a memory area.
   @param ma the memory area to release
 */
static void nlx_mem_area_put(struct nlx_mem_area *ma)
{
	kunmap(ma->ma_addr[0]);
	put_page(ma->ma_page[0]);
	if (ma->ma_addr[1] != NULL) {
		C2_ASSERT(ma->ma_page[1] != NULL);
		kunmap(ma->ma_addr[1]);
		put_page(ma->ma_page[1]);
	}
	c2_list_del(&ma->ma_link);
	c2_free(ma);
}

/**
   Record a memory area in the list of mapped user memory areas.
   On success, a new struct nlx_mem_area object is added to the
   list of memory areas tracked in the struct nlx_dev_data.
   @param dd device data structure tracking all resources for this instance
   @param uma memory descriptor, copied in from user space
 */
static int nlx_mem_area_map(struct nlx_dev_data *dd,
			    struct c2_net_lnet_mem_area *uma)
{
	int rc;
	int count;
	struct nlx_mem_area *ma;

	C2_PRE(c2_mutex_is_locked(&dd->dd_mutex));
	C2_PRE(current->mm != NULL);

	if (uma->nm_size > PAGE_SIZE) {
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, -EINVAL);
		return -EINVAL;
	}
	C2_ALLOC_PTR_ADDB(ma, &c2_net_addb, &nlx_addb_loc);
	if (ma == NULL)
		return -ENOMEM;

	if ((uma->nm_user_addr & PAGE_MASK) + uma->nm_size < PAGE_SIZE)
		count = 1;
	else
		count = 2;
	c2_mutex_unlock(&dd->dd_mutex);
	down_read(&current->mm->mmap_sem);
	rc = get_user_pages(current, current->mm,
			    uma->nm_user_addr, count, 1, 0,
			    ma->ma_page, NULL);
	up_read(&current->mm->mmap_sem);

	if (rc != count) {
		if (rc == 1)
			put_page(ma->ma_page[0]);
		c2_free(ma);
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, -ENOSPC);
		c2_mutex_lock(&dd->dd_mutex);
		return -ENOSPC;
	}

	/* note: kmap can page/sleep */
	ma->ma_user_addr = uma->nm_user_addr;
	ma->ma_addr[0] = kmap(ma->ma_page[0]);
	if (ma->ma_page[1] != NULL)
		ma->ma_addr[1] = kmap(ma->ma_page[1]);

	c2_mutex_lock(&dd->dd_mutex);
	c2_list_add(&dd->dd_mem_areas, &ma->ma_link);
	return 0;
}

/**
   Erase a previously recorded memory area from the list
   in the struct nlx_dev_data.
   @param dd device data structure tracking all resources for this instance
   @param uma memory descriptor, copied in from user space
 */
static int nlx_mem_area_unmap(struct nlx_dev_data *dd,
			      struct c2_net_lnet_mem_area *uma)
{
	struct nlx_mem_area *pos;

	C2_PRE(c2_mutex_is_locked(&dd->dd_mutex));
	C2_PRE(current->mm != NULL);

	c2_list_for_each_entry(&dd->dd_mem_areas,
			       pos, struct nlx_mem_area, ma_link) {
		if (pos->ma_user_addr == uma->nm_user_addr) {
			nlx_mem_area_put(pos);
			return 0;
		}
	}
	return -EINVAL;
}

static int nlx_dev_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct nlx_dev_data *dd = (struct nlx_dev_data *) file->private_data;
	struct c2_net_lnet_mem_area uma;
        int rc = -ENOTTY;

	C2_PRE(dd != NULL && dd->dd_magic == DD_MAGIC);

        if (_IOC_TYPE(cmd) != C2_LNET_IOC_MAGIC ||
            _IOC_NR(cmd) < C2_LNET_IOC_MIN_NR  ||
            _IOC_NR(cmd) > C2_LNET_IOC_MAX_NR) {
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, rc);
		return rc;
	}

	/** @todo check capable(CAP_SYS_ADMIN)? */

	rc = 0;
	c2_mutex_lock(&dd->dd_mutex);
	switch (cmd) {
	case C2_LNET_PROTOREAD:
		if (put_user(dd->dd_value, (unsigned int __user *) arg))
			rc = -EFAULT;
		break;
	case C2_LNET_PROTOWRITE:
		if (get_user(dd->dd_value, (unsigned int __user *) arg))
			rc = -EFAULT;
		break;
	case C2_LNET_PROTOMAP:
		if (copy_from_user(&uma, (void __user *) arg, sizeof uma))
			rc = -EFAULT;
		else
			rc = nlx_mem_area_map(dd, &uma);
		break;
	case C2_LNET_PROTOUNMAP:
		if (copy_from_user(&uma, (void __user *) arg, sizeof uma))
			rc = -EFAULT;
		else
			rc = nlx_mem_area_unmap(dd, &uma);
		break;
	default:
		rc = -ENOTTY;
		break;
	}
	c2_mutex_unlock(&dd->dd_mutex);
	return rc;
}

static int nlx_dev_open(struct inode *inode, struct file *file)
{
	int cnt = try_module_get(THIS_MODULE);
	struct nlx_dev_data *dd;

	if (cnt == 0) {
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, -ENODEV);
		return -ENODEV;
	}

	C2_ALLOC_PTR_ADDB(dd, &c2_net_addb, &nlx_addb_loc);
	if (dd == NULL)
		return -ENOMEM;
	dd->dd_magic = DD_MAGIC;
	dd->dd_value = DD_INITIAL_VALUE;
	c2_mutex_init(&dd->dd_mutex);
	c2_list_init(&dd->dd_mem_areas);
	file->private_data = dd;
	printk("c2_net: opened\n");
        return 0;
}

int nlx_dev_close(struct inode *inode, struct file *file)
{
	struct nlx_dev_data *dd = file->private_data;
	struct nlx_mem_area *pos;
	struct nlx_mem_area *next;

	C2_PRE(dd != NULL && dd->dd_magic == DD_MAGIC);

	file->private_data = NULL;
	/* user program may not unmap all areas, eg if it was killed */
	c2_list_for_each_entry_safe(&dd->dd_mem_areas,
				    pos, next, struct nlx_mem_area, ma_link)
		nlx_mem_area_put(pos);
	c2_list_fini(&dd->dd_mem_areas);
	c2_mutex_fini(&dd->dd_mutex);
	c2_free(dd);

	module_put(THIS_MODULE);
	printk("c2_net: opened\n");
	return 0;
}

static struct file_operations nlx_dev_file_ops = {
        .ioctl   = nlx_dev_ioctl,
        .open    = nlx_dev_open,
        .release = nlx_dev_close
};

static struct miscdevice nlx_dev = {
        .minor   = MISC_DYNAMIC_MINOR,
        .name    = "c2_lnet",
        .fops    = &nlx_dev_file_ops
};
static bool nlx_dev_registered = false;

int nlx_dev_init(void)
{
	int rc;

	rc = misc_register(&nlx_dev);
	if (rc != 0) {
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, rc);
		return rc;
	}
	nlx_dev_registered = true;
	printk("%s registered with minor %d\n", nlx_dev.name, nlx_dev.minor);
	return rc;
}

void nlx_dev_fini(void)
{
	int rc;

	if (nlx_dev_registered) {
		rc = misc_deregister(&nlx_dev);
		if (rc != 0)
			LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, rc);
		nlx_dev_registered = false;
		printk("%s deregistered\n", nlx_dev.name);
	}
}

/** @} */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
