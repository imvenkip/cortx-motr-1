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

   @see Detailed functional specifications in @ref DLDDFS.

   @note This page is designed to be viewed both through a text editor
   and in a browser after Doxygen processing.


   <hr>
   @section DLD-ovw Overview
   The purpose of the Detailed Level Design (DLD) specification of a
   component is to:
   - Refine higher level designs
   - To be verified by inspectors and architects
   - To guide the coding phase

   @subsection DLD-ovw-loc Location of the DLD
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

   @subsection DLD-ovw-fmt Formatting language
   Doxygen is the formatting tool of choice.  The doxgyen \@page
   format is used to define a separate top-level browsable element
   that contains the body of the DLD. The \@section and \@subsection
   formatting commands are used to provide internal structure.
   The page title will be visible in the <b>Related Pages</b> tab in the
   main browser window, as well as displayed as a top-level element in the
   explorer side-bar.

   @subsection DLD-ovw-layout Layout of the DLD
   The DLD specification is required to be sectioned in a stylized manner
   as demonstrated by this guide. It is similar to the sectioning
   found in a High Level Design.

   Not all sections may be applicable to the component in question,
   but mandatory section may not be omitted.  Instead, it should be
   provided with a disclaimer body indicating that the section does
   not apply to the component along with an explanation.  Additional
   sections or sub-sectioning may be added as required.

   @todo Improve the Doxygen style sheet

   @see <a href="https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjQ3Z3NraDI4ZG0&hl=en_US#Invariants">Detailed level design HOWTO</a>,
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
     strucutres.
   - <b>Concurrency and threading model</b> This explains how the the
     component works in a multi-threaded environment.

   <hr>
   @section DLD-req Requirements
   Mandatory.
   The DLD shall state the requirments that it attempts to meet.
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
   required here, just the salient points.

   The section could also describe what use it makes of data structures
   described elsewhere.

   @subsection DLD-fspec-if Interfaces
   Mandatory for programatic interfaces.
   Components with programming interfaces should provide an
   enumeration and <i>brief</i> description
   of the programming interfaces.  The section may be further organized
   by function. For example
        - <b>Constructors and destructors</b>
	- <b>Accessors and invariants</b>
	- <b>Operational interfaces</b>

   There is no \@subsubsection tag in Doxygen, so use bold-faced caption
   paragraphs to accomplish this further sub-sectioning.

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

   There is no \@subsubsection tag in Doxygen, so use bold-faced caption
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
   any written explanation.

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
   into Doxygen.

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
   This section cites each requiremnt in the @ref DLD-req section,
   and explains briefly how the DLD meets the requirement.


   <hr>
   @section DLD-ut Unit Tests
   Mandatory.
   This section describes the unit tests that will be designed.

   Unit tests should be planned for all interfaces exposed by the component.
   Testing should not just include correctness tests, but should also
   test failure situations.  Another area of focus is boundary value tests,
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

 */

/**
   @defgroup DLDDFS Colibri Sample Component Module
   @brief Detailed functional specifications of a hypothetical component.

   This page is part of the DLD style template.

   Detailed functional specifications go into a component specific
   module described by a Doxygen group.

   Module documentation may spread across multiple source files.  Make
   sure that the \@addtogroup Doxygen command is used in the other
   files.

   A component is not constrainted to have only one module.  If multiple
   modules are present, make sure that the DLD and the modules
   cross-reference each other, as shown below.

   @see The @ref DLD "Colibri Sample DLD" its @ref DLD-fspec and
   its @ref DLD-lspec-thread

   @{
*/

/**
   Data structure
 */
struct dld_sample_ds1 {
	/** The z field */
	int z_field;
};

/**
   Subroutine1
   @param param1 Parameter 1
   @retval return value
   @pre Precondition
   @post Postcondition
*/
int dld_sample_sub1(struct dld_sample_ds1 *param1);

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
