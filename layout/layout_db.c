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
 * Original author: Trupti Patil <Trupti_Patil@xyratex.com>
 * Original creation date: 10/18/2011
 */

/**
   @page Layout-DB Layout DB DLD

   - @ref Layout-DB-ovw
   - @ref Layout-DB-def
   - @ref Layout-DB-req
   - @ref Layout-DB-depends
   - @ref Layout-DB-highlights
   - @subpage Layout-DB-fspec "Functional Specification" 
   - @ref Layout-DB-lspec
      - @ref Layout-DB-lspec-comps
      - @ref Layout-DB-lspec-schema
      - @ref Layout-DB-lspec-state
      - @ref Layout-DB-lspec-thread
      - @ref Layout-DB-lspec-numa
   - @ref Layout-DB-conformance
   - @ref Layout-DB-ut
   - @ref Layout-DB-st
   - @ref Layout-DB-O
   - @ref Layout-DB-ref


   <hr>
   @section Layout-DB-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   Note: The instructions in italics from the DLD template are ratained 
   currently for reference and will be removed after the first round of the 
   DLD inspection.

   This document contains the detail level design for the Layout DB Module.

   <b>Purpose of the Layout-DB DLD</b><br>
   The purpose of the Layout-DB Detailed Level Design (DLD) specification
   is to:
   - Refine the higher level design
   - To be verified by inspectors and architects
   - To guide the coding phase

   <hr>
   @section Layout-DB-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   - <b>COB</b> COB is component object and is defined at  
     <a href="https://docs.google.com/a/xyratex.com/spreadsheet/ccc?key=0Ajg1HFjUZcaZdEpJd0tmM3MzVy1lMG41WWxjb0t4QkE&hl=en_US#gid=0">C2 Glossary</a>

   <hr>
   @section Layout-DB-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   The specified requirements are as follows: 
   - <b>R.LAYOUT.SCHEMA.Layid</b> Layout identifiers are unique globally in
     the system, and persistent in the life cycle.
   - <b>R.LAYOUT.SCHEMA.Types</b> There are multiple layout types for 
     different purposes: SNS, block map, local raid, de-dup, encryption, 
     compression, etc.
   - <b>R.LAYOUT.SCHEMA.Formulae</b>
      - <b>Parameters</b> Layout may contain sub-map information. Layout may
        contain some formula, and its parameters and real mapping information
        should be calculated from the formula and its parameters.
      - <b>Garbage Collection</b> If some objects are deleted from the system,
        their associated layout may still be left in the system, with zero
        reference count. This layout can be re-used, or be garbage 
        collected in some time.
   - <b>R.LAYOUT.SCHEMA.Sub-Layouts</b> Sub-layouts.

   <hr>
   @section Layout-DB-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   - Layout is a managed resource and depends upon Resource Manager.
   - Layout DB module depends upon the Layout module since the Layout module
     creates the layouts and uses/manages them.
   - Layout DB module depends upon the DB5 interfaces exposed by
     Colibri since the layouts are stored using the DB5 data-base.

   <hr>
   @section Layout-DB-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - Layout in itself is a managed resource.
   - The Layout DB module provides support for storing layouts with multiple
     layout types.
   - It provides support to store composite layout maps.

   <hr>
   @section Layout-DB-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   Layout DB makes use of the DB5 data-base to persistently store the layout
   entries. This section describes how the Layout DB module works.

   - @ref Layout-DB-lspec-comps
   - @ref Layout-DB-lspec-schema
      - @ref Layout-DB-lspec-ds1
      - @ref Layout-DB-lspec-sub1
      - @ref LayoutDBDFSInternal
   - @ref Layout-DB-lspec-state
   - @ref Layout-DB-lspec-thread
   - @ref Layout-DB-lspec-numa


   @subsection Layout-DB-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   The following diagram shows the internal components of the Layout module.
   (TODO: Would like to reverse the positions of the "Layout DB" and the
   "Layout" components but not able to achieve it at least as of now!)

   @dot
   digraph {
     node [style=box];
     label = "Layout Components and Interactions";

     subgraph colibri_client {
         label = "Client";
         cClient [label="Client"];
     }

     subgraph colibri_layout {
         label = "Layout";

     cLDB [label="Layout DB", style="filled"];
     cFormula [label="Layout Formula", style="filled"];
     cLayout [label="Layout (Managed Resource)", style="filled"];

         cLDB -> cFormula [label="build formula"];
         cFormula -> cLayout [label="build layout"];
     }

     subgraph colibri_server {
         label = "Server";
         cServer [label="Server"];
     }

     cClient -> cFormula [label="substitute"];
     cServer -> cFormula [label="substitute"];

     { rank=same; cClient cFormula cServer } 
   }
   @enddot

   @subsection Layout-DB-lspec-schema Layout Schema Design
   <i>Such sections briefly describes the purpose and design of each
   sub-component. Feel free to add multiple such sections, and any additional
   sub-sectioning within.</i>

   The layout schema for the Layout DB module consists of the following three
   tables
   - @ref Layout-DB-lspec-schema-layout-entries
   - @ref Layout-DB-lspec-schema-pdclust_list_cob_lists 
   - @ref Layout-DB-lspec-schema-comp_layout_ext_map 

   <b>Layout types</b> supported currently by the Layout module are:
   - PDCLUST <BR>
     This layout type applies parity declustering feature to the striping
     process. Parity declustering is to keep rebuild overhead low by 
     striping a file over more servers or drives than there are units in
     the parity group. The PDCLUST type of layout either uses a formula
     or a list to enumerate the COB identifiers.
     The enumeration method types supported for PDCLUST type of layout are:
      - LINEAR <BR>
        PDCLUST layout type with LINEAR enumeration type uses a formula to 
        enumerate the COB identifiers.
      - LIST <BR>
        PDCLUST layout type with LIST enumeration type uses a list to 
        enumerate the COB identifiers.
   - COMPOSITE <BR>
     This layout type partitions a file or a part of the file into 
     various segments while each of those segment uses a different layout.

   <b>Layout record types</b> supported currently by the Layout DB module are:
   - PDCLUST_LINEAR
      - This is a layout record type with layout type as PDCLUST and its 
        enumeration type as LINEAR.
      - This requires storing some attributes like N, K.
      - The layout record entry is made into the layout_entries table while
        the layout attributes are stored in the layout record itself.
   - PDCLUST_LIST
      - This is a layout record type with layout type as PDCLUST and its
        enumeration type as LIST.
      - This requires storing list of cob identifiers belonging to this layout
        along with index in this layout.
      - The layout record entry is made into the layout_entries table while
        the cob identifiers along with cob index are stored in a separate 
        table viz. pdclust_list_cob_lists.
   - COMPOSITE
      - This is a layout record type with layout type as COMPOSITE.
      - It requires storing extent map for each layout record of this type.
      - The extent map is used to provide the file segment to sub layout 
        mappings for all the segments belonging to this layout.
      - The layout record entry is made into the layout_entries table while
        the extent maps are stored in a separate table viz. comp_layout_ext_map.

   Key-Record structure for the tables:

   @subsection Layout-DB-lspec-schema-layout-entries Table layout_entries
   @verbatim
   Table Name: layout_entries
   Key: layout_id
   Record:
      - layout_type (PDCLUST_LINEAR | PDCLUST_LIST | COMPOSITE)
      - reference_count
      - pdclust_linear_rec_attrs 

   @endverbatim
   
   For PDCLUST_LINEAR layout type, the pdclust_linear_rec_attrs
   contains N (number of data units in the parity group) and K (number of
   parity units in the parity groups).

   For PDCLUST_LIST and COMPOSITE layout types, the
   pdclust_linear_rec_attrs field is not used.
   
   @subsection Layout-DB-lspec-schema-pdclust_list_cob_lists Table pdclust_list_cob_lists
   @verbatim
   Table Name: pdclust_list_cob_lists
   Key:
      - layout_id
      - cob_index
   Record:
      - cob_id

   @endverbatim

   This table contains multiple cob identifier entries for every PDCLUST_LIST type of
   layout.

   layout_id is a foreign key referring record, in the layout_entries table.

   @subsection Layout-DB-lspec-schema-comp_layout_ext_map Table comp_layout_ext_map

   @verbatim
   Table Name: comp_layout_ext_map
   Key
      - composite_layout_id
      - last_offset_of_segment
   Record
      - start_offset_of_segment
      - layout_id

   @endverbatim

   layout_id is a foreign key referring record, in the layout_entries table.

   Layout DB uses a single c2_emap instance to implement the composite layout
   extent map viz. comp_layout_ext_map. This table stores the "layout segment
   to sub-layout id mappings" for each compsite layout. 

   c2_emap table is a framework to store a collection of related extent maps.
   Individual extent maps within a collection are identified by an element 
   of the key called as prefix (128 bit).
   
   For each composite layout, its layout id (c2_layout_id) is used as a 
   prefix to identify an extent map belonging to one composite layout.

   An example:

   Suppose a layout L1 is of the type composite and constitues of 3 
   sub-layouts say S1, S2, S3. These sub-layouts S1, S2 and S3 use 
   the layouts with layout id L11, L12 and L13 respectively.
  
   In this example, for the composite layout L1, the comp_layout_ext_map
   table stores 3 layout segments viz. S1, S2 and S3. All these 3 segments 
   are stored in the form of ([A, B), V) where:
   - A is the start offset from the layout L1
   - B is the end offset from the layout L1
   - V is the layout id for the layout used by the respective segment and
     is either of L11, L12 or L13 as applicable.
          
   @subsubsection Layout-DB-lspec-ds1 Subcomponent Data Structures
   <i>This section briefly describes the internal data structures that are
   significant to the design of the sub-component. These should not be a part
   of the Functional Specification.</i>

   See @ref LayoutDBDFSInternal for internal data structures.

   @subsubsection Layout-DB-lspec-sub1 Subcomponent Subroutines
   <i>This section briefly describes the interfaces of the sub-component that
   are of significance to the design.</i>

   See @ref LayoutDBDFSInternal for internal subroutines.

   @subsection Layout-DB-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   @subsection Layout-DB-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   @subsection Layout-DB-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   <hr>
   @section Layout-DB-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref Layout-DB-req section,
   and explains briefly how the DLD meets the requirement.</i>

   - <b>I.LAYOUT.SCHEMA.Layid</b> Layout identifiers are unique globally in
     the system, and persistent in the life cycle. It is assumed that the 
     layout identifiers are assigned by the Layout module and Layout DB module
     helps to store those persistently.
   - <b>I.LAYOUT.SCHEMA.Types</b> There are multiple layout types for different
     purposes: SNS, block map, local raid, de-dup, encryption, compression, 
     etc.
     <BR>
     Layout record types currently supported by the Layout DB module are:
        - PDCLUST_LINEAR and PDCLUST_LIST layout record types to represent
          PDCLUST layout type used for SNS.
        - COMPOSITE layout record type to represent Composite layout type.
     The framework supports to add other layout record types, as required in
     the future, though doing so will require some source changes to the 
     Layout DB module.
   - <b>I.LAYOUT.SCHEMA.Formulae</b>
      - <b>Parameters</b>
         - In case of PDCLUST_LINEAR layout record type, substituting parameters
           in the stored formula derives the real mapping information that is 
           the list of COB identifiers.
      - <b>Garbage Collection</b>
         - PDCLUST_LIST and COMPOSITE type of layout records are deleted when
           their last reference is released.
         - PDCLUST_LINEAR type of layout is never deleted and thus can be
           reused.
   - <b>I.LAYOUT.SCHEMA.Sub-Layouts</b> COMPOSITE type of layout is used to
     store sub-layouts.

   <hr>
   @section Layout-DB-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   TODO: Add test cases.

   <hr>
   @section Layout-DB-st System Tests
   <i>Mandatory.</i>
   
   TODO: Deferred until first round of inspection.

   <hr>
   @section Layout-DB-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section Layout-DB-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/document/d/15-G-tUZfSuK6lpOuQ_Hpbw1jJ55MypVhzGN2r1yAZ4Y/edit?hl=en_US
">HLD of Layout Schema</a>
   - <a href="https://docs.google.com/a/xyratex.com/document/d/12olF9CWN35HCkz-ZcEH_c0qoS8fzTxuoTqCCDN9EQR0/edit?hl=en_US#heading=h.gz7460ketfn1">Understanding LayoutSchema</a>

 */

#include "layout/layout_db.h"

/**
   @defgroup LayoutDBDFSInternal Layout DB Internals
   @brief Detailed functional specification of the internals of the
   Layout-DB module.

   This section covers the data structures and sub-routines used internally.

   @see @ref Layout-DB "Layout-DB DLD" and @ref Layout-DB-lspec "Layout-DB Logical Specification".

   @{
*/

/** Structure used internally */

/** Invariant for c2_layout_rec */
/*
static bool layout_db_rec_invariant(const struct c2_layout_rec *l)
{
	return true;
}
*/

/** @} end LayoutDBDFSInternal */

/**
 * @addtogroup LayoutDBDFS
 * @{
 */

/**
   Initializes new layout schema - initializes DB environment, creates the
   DB tables.
*/
int c2_layout_schema_init(struct c2_layout_schema *l_schema)
{
	/* Uses the DB interface c2_dbenv_init() and c2_table_init() to
	intialize the DB env and the tables respectively. */

	return 0;
}

/**
   De-initializes the layout schema - de-initializes the DB tables and the
   DB environment.
*/
void c2_layout_schema_fini(struct c2_layout_schema *l_schema)
{
	/* Uses the DB interface c2_table_fini() and c2_dbenv_fini() to
	de-intialize the DB tables and tables respectively. */

	return;	
}

/**
   Adds a new layout record and its related information into the relevant
   tables.

   This includes the following:
   - In case of PDCLUST_LIST type of a layout record, it adds list of cob
     ids to the pdclust_list_cob_lists table.
   - If case of COMPOSITE type of a layout record, it adds the an
     extent map into the comp_layout_ext_map table.
*/
int c2_layout_rec_add(const struct c2_layout *layout, 
		const struct c2_layout_schema *l_schema, 
		const struct c2_db_tx *tx)
{
	/* Encodes the layout DB record using the function pointer 
	   layout->l_ops->l_rec_encode and adds the record to the DB using 
	   the function pointer layout->l_ops->l_rec_add. */

	return 0;
}

/**
   Deletes a layout record and its related information from the
   relevant tables.
*/
int c2_layout_rec_delete(const struct c2_layout *layout, 
		const struct c2_layout_schema *l_schema, 
		const struct c2_db_tx *tx)
{
	/* Uses the function pointer layout->l_ops->l_rec_delete. */
	return 0;
}

/** 
   Updates a layout record and its related information from the
   relevant tables.
*/
int c2_layout_rec_update(const struct c2_layout *layout,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx)
{
	/* Uses the function pointer layout->l_ops->l_rec_update. */
	return 0;
}
 
/**
   Obtains a layout record with the specified layout_id, and its related
   information from the relevant tables.
*/
int c2_layout_rec_lookup(const struct c2_layout_id *l_id,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx,
		struct c2_layout_rec *l_rec_out)
{
	/* Uses the function pointer layout->l_ops->l_rec_lookup. */
	return 0;
}

/**
   Adds a reference on the specified layout record from the layout_entries
   table.
   Implementation is common to all the types of layout records.
*/
int c2_layout_rec_ref_get(const struct c2_layout_rec *l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx)
{
	return 0;
}

/**
   Releases a reference on the specified layout record from the 
   layout_entries table.

   Destroys layout record with either of the following record types when
   its last reference is released:
   - PDCLUST_LIST
   - COMPOSITE
   <BR>
   A PDCLUST_LINEAR type of layout record is never destroyed.
*/
int c2_layout_rec_ref_put(const struct c2_layout *layout,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx)
{
	/* Uses the function pointers layout->l_ops->l_rec_ref_put and 
	layout->l_ops->l_rec_delete. */
	return 0;
}

/** @} end of LayoutDBDFS */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
