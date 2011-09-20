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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>
 * Original creation date: 09/16/2011
 */

#ifndef __COLIBRI_DLD_STYLE_1_H__
#define __COLIBRI_DLD_STYLE_1_H__

/**
   @page DLD Colibri DLD Template

   - @ref DLD-ovw
   - @ref DLD-def
   - @ref DLD-req
   - @ref DLD-depends
   - @ref DLD-highlights
   - @ref DLD-fspec
      - @ref DLD-fspec-ds
      - @ref DLD-fspec-if
      - @ref DLD-fspec-cli
      - @ref DLD-fspec-usecases
   - @ref DLD-lspec
      - @ref DLD-lspec-comps
      - @ref DLD-lspec-state
      - @ref DLD-lspec-thread
      - @ref DLD-lspec-numa
   - @ref DLD-conformance
   - @ref DLD-ut
   - @ref DLD-st
   - @ref DLD-O
   - @ref DLD-ref

   @see Detailed functional specifications in @ref DLDDFS.


   <hr>
   @section DLD-ovw Overview
   <i>All specifications must start with an Overview section that briefly
   describes the document and provides any additional instructions or hints on
   how to best read the specification.</i>

   This document is intended to be a style guide for a detail level design
   specification, and is designed to be viewed both through a text editor and
   in a browser after Doxygen processing.

   You can use this document as a template by deleting all content but
   retaining the sections referenced above and the overall Doxygen structure of
   a page with one or more component modules.

   It is recommended that you retain the italicized instructions that follow
   the formally recognized section headers, until at least the DLD review
   phase.  You may leave the instructions in the final document if you wish.

   Please check your grammar and punctuation, and run the document through
   a spelling checker during your DLDR phase.

   <b>Purpose of a DLD</b><br>
   The purpose of the Detailed Level Design (DLD) specification of a
   component is to:
   - Refine higher level designs
   - To be verified by inspectors and architects
   - To guide the coding phase

   <b>Location of the DLD</b><br>
   The Colibri project requires Detailed Level Designs in the source
   code itself.  This greatly aids in keeping the design documentation
   up to date through the lifetime of the implementation code.

   The main DLD specification shall be in the master header file of
   the component being designed.  This is the header file that
   consumers of the component's API would include.  In case of stand
   alone components, an appropriate alternative header file should be chosen.
   Parts of the specification may involve other source files.

   The DLD is followed in the master header file by the detailed
   functional specification of the component interfaces.

   <b>Formatting language</b><br>
   Doxygen is the formatting tool of choice.  The Doxgyen @@page
   format is used to define a separate top-level browsable element
   that contains the body of the DLD. The @@section and @@subsection
   formatting commands are used to provide internal structure.
   The page title will be visible in the <b>Related Pages</b> tab in the
   main browser window, as well as displayed as a top-level element in the
   explorer side-bar.

   Detailed functional specifications follow the DLD using @@defgroup
   commands for each component module.

   <b>Layout of the DLD</b><br>
   The DLD specification is required to be sectioned in the specific manner
   illustrated by this document.  It is similar to the sectioning found
   in a High Level Design.

   Not all sections may be applicable to the design, but mandatory sections may
   not be omitted.  If a mandatory section does not apply it should clearly be
   marked as non-applicable, along with an explanation.  Additional sections or
   sub-sectioning may be added as required.

   <hr>
   @section DLD-def Definitions
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
   <i>Mandatory. This section briefly summarises the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - The DLD specification requires formal sectioning to address specific
   aspects of the design.
   - The DLD is with the code, and the specification is designed to be
   viewed either as text or through Doxygen.
   - This document can be used as a template.

   <hr>
   @section DLD-fspec Functional Specification
   <i>Mandatory. This section describes the external interfaces of the
   component, showing what the component does to address the requirements.
   It also briefly identifies the consumers of these interfaces.
   The section has mandatory subsections created using the Doxygen
   @@subsection command.</i>

   @subsection DLD-fspec-ds Data structures
   <i>Mandatory for programmatic interfaces.
   Components with programming interfaces should provide an
   enumeration and <i>brief</i> description of the major data structures
   defined by this component.  No details of the data structure are
   required here, just the salient points.</i>

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

   The section could also describe what use it makes of data structures
   described elsewhere.

   @subsection DLD-fspec-if Interfaces
   <i>Mandatory for programmatic interfaces.
   Components with programming interfaces should provide an
   enumeration and brief description of the programming interfaces.</i>

   The section may be further organized by function using additional
   subsectioning with the Doxygen @@subsubsection command.  It is
   useful to create a local table of contents to illustrate the structure
   of what follows.
   - @ref DLD-fspec-if-cons
   - @ref DLD-fspec-acc
   - @ref DLD-fspec-opi

   @subsubsection DLD-fspec-if-cons Constructors and Destructors
   This is an example of a sub-sub-section.

   @subsubsection DLD-fspec-acc Accessors and Invariants
   This is an example of a sub-sub-section.

   @subsubsection DLD-fspec-opi Operational Interfaces
   This is an example of a sub-sub-section.

   @subsection DLD-fspec-cli Command Usage
   <i>Mandatory for command line programs.
   Components that provide programs would provide a specification of the
   command line invocation arguments.
   In addition, the format of any any structured file consumed or produced
   by the interface must be described in this section.</i>

   @subsection DLD-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>


   <hr>
   @section DLD-lspec Logical Specification
   <i>Mandatory.
   This section describes the internal design of the component, explaining
   how the functional specification is met.  Sub-components and diagrams
   of their interaction should go into this section.
   The section has mandatory subsections created using the Doxygen
   @@subsection command.
   The designer should feel free to use additional sub-sectioning
   if needed.</i>

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
     node [stype=box];
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

   If an external tool, such as Visio, is used to create an
   image, the source of that image (e.g. the Visio @c .vsd file)
   should be checked into the source tree so that future maintainers can
   modify the figure.  This applies to all non-embedded image source files,
   not just Visio.


   @subsection DLD-lspec-sc Sub-component design
   <i>This section describes the design of sub-component. Feel free to
   add any additional sectioning to describe logically distinct
   subcomponents.</i>

   Sample non-standard sub-section to illustrate how to documentation the design
   of a sub-component.

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
   easy to understand and analyse the various facets of the design.
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
   <i>Mandatory.
   This section describes the unit tests that will be designed.</i>

   Unit tests should be planned for all interfaces exposed by the component.
   Testing should not just include correctness tests, but should also
   test failure situations.  This includes testing of <i>expected</i>
   return error codes when presented with invalid input or when encountering
   unexpected data or state.
   Note that assertions are not testable - the unit test program
   terminates!

   Another area of focus is boundary value tests,
   where variable values are equal to but do not exceed their maximum or
   minimum possible values.

   As a further refinement and a plug for Test Driven Development, it
   would be nice if the designer can plan the order of development of
   the interfaces and their corresponding unit tests.  Code inspection
   could overlap development in such a model.

   Testing should relate to specific use cases described in the HLD if
   possible.


   <hr>
   @section DLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   Testing should relate to specific use cases described in the HLD if
   possible.


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

/**
   @defgroup DLDDFS Colibri Sample Module
   @brief Detailed functional specifications of a hypothetical module.

   This page is part of the DLD style template.
   Detailed functional specifications go into a module described by
   the Doxygen @@defgroup command.

   Module documentation may spread across multiple source files.  Make
   sure that the @@addtogroup Doxygen command is used in the other
   files to merge their documentation into the main group.  When doing so,
   it is important to ensure that the material flows logically when read
   through Doxygen.

   You are not constrained to have only one module in the design.  If multiple
   modules are present you may use multiple @@defgroup commands to create
   individual documentation pages for each such module, though it is good idea
   to use separate header files for the additional modules.  Please make sure
   that the DLD and the modules cross-reference each other, as shown below.

   @see The @ref DLD "Colibri Sample DLD" its @ref DLD-fspec and
   its @ref DLD-lspec-thread

   @{
*/

/**
   Data structure to do something.

 */
struct dld_sample_ds1 {
	/** The z field */
	int dsd_z_field;
	/** Flux density */
	int dsd_flux_density;
};

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
   - The default return convention (0 for success and <tt>-errno</tt>
   on failure) should not be repeated.
   - The @@pre and @@post conditions are preferably expressed in code.

   @param param1 Parameter 1 must be locked before use.
   @param read_only This controls the modifiability of the foo object.
   Set to @c true to prevent modification.
   @retval return value
   @pre Precondition, preferably expressed in code.
   @post Postcondition, preferably expressed in code.
*/
int dld_sample_sub1(struct dld_sample_ds1 *param1, bool read_only);

/**
   @} DLDDFS end group
*/

#endif /*  __COLIBRI_DLD_STYLE_1_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
