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
   - @ref DLD-fspec
      - @ref DLD-fspec-ds
      - @ref DLD-fspec-if
           - @ref DLD-fspec-if-cons "Constructors and Destructors"
      - @ref DLD-fspec-cli
      - @ref DLD-fspec-usecases
   - @ref DLD-lspec
      - @ref DLD-lspec-ovw
      - @ref DLD-lspec-sc1
      - @ref DLD-lspec-state
      - @ref DLD-lspec-thread
      - @ref DLD-lspec-numa
      - @ref DLD-conformance
   - @ref DLD-ut
   - @ref DLD-st
   - @ref DLD-O
   - @ref DLD-ref

   @see Detailed functional specifications in @ref DLDDFS.

   @note This page is designed to be viewed both through a text editor
   and in a browser after Doxygen processing.


   <hr>
   @section DLD-ovw Overview
   This document is intended to be a style guide for a detail level
   design specification.  All such specifications must start with an
   Overview section that briefly describes the document and provides any
   additional instructions or hints on how to read it.

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
   Doxygen is the formatting tool of choice.  The doxgyen @@page
   format is used to define a separate top-level browsable element
   that contains the body of the DLD. The @@section and @@subsection
   formatting commands are used to provide internal structure.
   The page title will be visible in the <b>Related Pages</b> tab in the
   main browser window, as well as displayed as a top-level element in the
   explorer side-bar.

   <b>Layout of the DLD</b><br>
   The DLD specification is required to be sectioned in a stylized manner
   as demonstrated by this guide. It is similar to the sectioning found
   in a High Level Design.

   Not all sections may be applicable to the component in question,
   but mandatory section may not be omitted.  Instead, it should be
   provided with a disclaimer body indicating that the section does
   not apply to the component along with an explanation.  Additional
   sections or sub-sectioning may be added as required.

   @todo Improve the Doxygen style sheet

   @see <a href="https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjQ3Z3NraDI4ZG0&hl=en_US">Detailed level design HOWTO</a>,
   an older document on which this style guide is partially based.

   <hr>
   @section DLD-def Definitions
   Mandatory.
   The DLD shall provide definitions of the terms introduced by the
   specification, as well as the relevant terms used by the
   specification but described elsewhere.

   Previously defined terms:
   - <b>Logical Specification</b> This explains how the component works.
   - <b>Functional Specification</b> This is explains how to use the component.

   New terms:
   - <b>Detailed Functional Specification</b> This provides
     documentation of al the data structures and interfaces (internal
     and external).
   - <b>State model</b> This explains the lifecycle of component data
     structures.
   - <b>Concurrency and threading model</b> This explains how the the
     component works in a multi-threaded environment.

   <hr>
   @section DLD-req Requirements
   Mandatory.
   The DLD shall state the requirements that it attempts to meet.
   They should be expressed in a list, thusly:
   - <b>R.DLD.Structured</b> The DLD shall be decomposed into a standard
   set of section.  Sub-sections may be used to further decompose the
   material of a section into logically disjoint units.
   - <b>R.DLD.What</b> The DLD shall describe the externally visible
   data structures and interfaces of the component through a
   functional specification section.
   - <b>R.DLD.How</b> The DLD shall explain its inner algorithms through
   a logical specification section.

   <hr>
   @section DLD-fspec Functional Specification
   This section describes the external interfaces of the component and
   briefly identifies the consumers of these interfaces.

   @subsection DLD-fspec-ds Data structures
   Mandatory for programatic interfaces.
   Components with programming interfaces should provide an
   enumeration and <i>brief</i> description of the major data structures
   defined by this component.  No details of the data structure are
   required here, just the salient points. For example:

<table border="0">
<tr><td>&nbrsp; &nbrsp; &nbrsp; &nbrsp; &nbrsp; &nbrsp;</td><td>
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

   The section could also describe what use it makes of data structures
   described elsewhere.

   @subsection DLD-fspec-if Interfaces
   Mandatory for programatic interfaces.
   Components with programming interfaces should provide an
   enumeration and <i>brief</i> description
   of the programming interfaces.  The section may be further organized
   by function. For example
        - @ref DLD-fspec-if-cons "Constructors and Destructors"
	- <b>Accessors and invariants</b>
	- <b>Operational interfaces</b>

   There is no @@subsubsection tag in Doxygen, so use bold-faced caption
   paragraphs to accomplish this further sub-sectioning. Another alternative
   is to use the @<h4> HTML tag and an explicit @@anchor reference to create
   a linkable Doxygen heading.  The corresponding @@ref tag format is shown
   here as well as at the top of the page.

   <h4>@anchor DLD-fspec-if-cons Constructors and destructors</h4>
   This is an example of a sub-sub-section.
   The doxygen style-sheet does not seem to have definitions for
   the @<h4> HTML tag.

   @subsection DLD-fspec-cli Command Usage
   Mandatory for command line programs.
   Components that provide programs would provide a specification of the
   command line invocation arguments.

   In addition, the format of any any structured file consumed or produced
   by the interface must be described in this section.

   @subsection DLD-fspec-usecases Recipies
   This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.

   There is no @@subsubsection tag in Doxygen, so use bold-faced caption
   paragraphs to further sub-section each recipie.


   <hr>
   @section DLD-lspec Logical Specification
   Mandatory.
   This section describes the internal design of the component. It has
   many subsections, some of which are mandatory.
   The designer should feel free to use sub-sectioning to decompose the
   design into logical disjoint pieces.

   Any of these sub-sections can utilize diagrams if needed.
   UML and sequence diagrams often illustrate points better than
   any written explanation.  For example:
   <img src="../../doc/dld-sample.gif" alt="doc/dld-sample.gif">
   An image is relatively easy to load, provided you remember that the
   doxygen output is viewed from the doc/html directory, so all paths
   should be relative to that frame of reference.

   @subsection DLD-lspec-ovw Design overview
   Mandatory.
   This section provides a brief overview of the design, its
   internal logical decomposition if any, etc.

   @subsection DLD-lspec-sc1 Sub-component1 design
   This section describes the design of <i>Sub-component1</i>. \&c

   @subsection DLD-lspec-state State Specification
   Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.

   Diagrams are almost essential here. Use the dot support built
   into Doxygen.  Here, for example, is a figure from the "rpc/session.h"
   file:
   @dot
   digraph example {
       node [shape=record, fontname=Helvetica, fontsize=10]
       S0 [label="", shape="plaintext", layer=""]
       S1 [label="Uninitialized"]
       S2 [label="Initialized"]
       S3 [label="Connecting"]
       S4 [label="Active"]
       S5 [label="Terminating"]
       S6 [label="Terminated"]
       S7 [label="Uninitialzed"]
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

   @subsection DLD-lspec-thread Threading and Concurrency Model
   Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks mutexes and condition variables).

   It clearly explains all aspects of synchronization, including locking
   order protocols, existential protection of objects by their state, etc.

   Diagrams are very useful here.

   @subsection DLD-lspec-numa NUMA optimizations
   Mandatory for components with programatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.

   Conversely, it can describe if sub-optimal behavior arises due
   to contention for shared component resources by multiple processors.

   The section is marked mandatory because if forces the designer to
   consider these aspects of concurrency.

   @subsection DLD-conformance Conformance
   Mandatory.
   This section cites each requirement in the @ref DLD-req section,
   and explains briefly how the DLD meets the requirement.


   <hr>
   @section DLD-ut Unit Tests
   Mandatory.
   This section describes the unit tests that will be designed.

   Unit tests should be planned for all interfaces exposed by the component.
   Testing should not just include correctness tests, but should also
   test failure situations.  This includes testing of <i>expected</i>
   returnerror codes when presented with invalid input or when encountering
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
   Mandatory.
   This section describes the system testing done, if applicable.

   Testing should relate to specific use cases described in the HLD if
   possible.


   <hr>
   @section DLD-O Analysis
   This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.

   <hr>
   @section DLD-ref References
   References to other documents are essential.  In particular a link
   to the HLD for the DLD should be provided.
   - <a href="https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjQ3Z3NraDI4ZG0&hl=en_US">Detailed level design HOWTO</a>
   - <a href="http://www.stack.nl/~dimitri/doxygen/maual.html">Doxygen
   Manual</a>
   - <a href="http://www.graphviz.org">Graphviz - Graph Visualization
   Software</b> for documentation on the @c dot command.

 */

/**
   @defgroup DLDDFS Colibri Sample Component Module
   @brief Detailed functional specifications of a hypothetical component.

   This page is part of the DLD style template.

   Detailed functional specifications go into a component specific
   module described by a Doxygen group.

   Module documentation may spread across multiple source files.  Make
   sure that the @@addtogroup Doxygen command is used in the other
   files.

   A component is not constrainted to have only one module.  If multiple
   modules are present, make sure that the DLD and the modules
   cross-reference each other, as shown below.

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
   prototype.  For example it would be wrong to say, <tt>"@@param read_only
   A boolean parameter."</tt>.
   - The default return convention (0 for success and <tt>-errno</tt>
   on failure) should not be repeated.
   - The @@pre and @@post conditions are preferably expressed in code.

   @param param1 Parameter 1 must be locked before use.
   @param read_only This controls the modifiability of the foo object.
   Set to @c true to prevent modification.
   @retval return value
   @pre Precondition, preferably in code.
   @post Postcondition, preferably in code.
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
