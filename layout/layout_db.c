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
   This document contains the detail level design for the Layout DB Module.

   <b>Purpose of the Layout-DB DLD</b><BR>
   The purpose of the Layout-DB Detailed Level Design (DLD) specification
   is to:
   - Refine the higher level design
   - To be verified by inspectors and architects
   - To guide the coding phase

   <hr>
   @section Layout-DB-def Definitions
     - <b>COB</b> COB is component object and is defined at
     <a href="https://docs.google.com/a/xyratex.com/spreadsheet/ccc?key=0Ajg1HFjUZcaZdEpJd0tmM3MzVy1lMG41WWxjb0t4QkE&hl=en_US#gid=0">C2 Glossary</a>

   <hr>
   @section Layout-DB-req Requirements
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
   - Layout is a managed resource and depends upon Resource Manager.
   - Layout DB module depends upon the Layout module since the Layout module
     creates the layouts and uses/manages them.
   - Layout DB module depends upon the DB5 interfaces exposed by
     Colibri since the layouts are stored using the DB5 data-base.

   <hr>
   @section Layout-DB-highlights Design Highlights
   - Layout in itself is a managed resource.
   - The Layout DB module provides support for storing layouts with multiple
     layout types.
   - It provides support to store composite layout maps.
   - It is required that for adding a layout type or layout enumeration type,
     central layout/layout.h should not require modifications.

   <hr>
   @section Layout-DB-lspec Logical Specification
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
   The following diagram shows the internal components of the Layout module.

   @todo Would like to reverse the positions of the "Layout DB" and the
   "Layout" components but not able to achieve it at least as of now! Any
   inputs are welcome.

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
   The layout schema for the Layout DB module consists of the following three
   tables
   - @ref Layout-DB-lspec-schema-layouts
   - @ref Layout-DB-lspec-schema-cob_lists
   - @ref Layout-DB-lspec-schema-comp_layout_ext_map

   @todo Add one table each to store 'layout type to layout description
   mappings' and 'enumeration type to enumeration description mappings'.

   Key-Record structure for the tables is described in the following
   subsections.

   @subsection Layout-DB-lspec-schema-layouts Table layouts
   @verbatim
   Table Name: layouts
   Key: layout_id
   Record:
      - layout_type_id
      - layout_enumeration_type_id
      - reference_count
      - layout_rec_attrs

   @endverbatim

   e.g. A layout with PDCLUST layout type and with LINEAR enumeration type,
   uses the layout_rec_attrs to store attributes like N and K.

   It is possible that some layout types do not need to store any attributes.
   e.g. A layout with PDCLUST layout type with LIST enumeration and a layout with
   with COMPOSITE layout type do not need to store these attributes.

   @todo A decision needs to be made if it is ok to store these attributes in
   this primary table (cons: wastes this space for the layout entries which do not
   need these attributes) or they should be stored outside this table that will
   store data only for the layout entries for which such attributes are
   applicable.

   @subsection Layout-DB-lspec-schema-cob_lists Table cob_lists
   @verbatim
   Table Name: cob_lists
   Key:
      - layout_id
      - cob_index
   Record:
      - cob_id

   @endverbatim

   This table contains multiple cob identifier entries for every PDCLUST type
   of layout with LIST enumeration type.

   layout_id is a foreign key referring record, in the layouts table.

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

   composite_layout_id is the layout_id for the COMPOSITE type of layout,
   stored as key in the layouts table.

   layout_id is a foreign key referring record, in the layouts table.

   Layout DB uses a single c2_emap instance to implement the composite layout
   extent map viz. comp_layout_ext_map. This table stores the "layout segment
   to sub-layout id mappings" for each compsite layout.

   c2_emap table is a framework to store a collection of related extent maps.
   Individual extent maps within a collection are identified by an element
   of the key called as prefix (128 bit).

   For each composite layout, its layout id (c2_layout_id) is used as a
   prefix to identify an extent map belonging to one composite layout.

   An example:

   Suppose a layout L1 is of the type composite and constitutes of 3
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
   See @ref LayoutDBDFSInternal for internal data structures.

   @subsubsection Layout-DB-lspec-sub1 Subcomponent Subroutines
   See @ref LayoutDBDFSInternal for internal subroutines.

   @subsection Layout-DB-lspec-state State Specification

   @subsection Layout-DB-lspec-thread Threading and Concurrency Model

   @subsection Layout-DB-lspec-numa NUMA optimizations

   <hr>
   @section Layout-DB-conformance Conformance
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
         - In case of PDCLUST_LINEAR layout record type, substituting
           parameters in the stored formula derives the real mapping
           information that is the list of COB identifiers.
      - <b>Garbage Collection</b>
         - PDCLUST_LIST and COMPOSITE type of layout records are deleted when
           their last reference is released.
         - PDCLUST_LINEAR type of layout is never deleted and thus can be
           reused.
   - <b>I.LAYOUT.SCHEMA.Sub-Layouts</b> COMPOSITE type of layout is used to
     store sub-layouts.

   <hr>
   @section Layout-DB-ut Unit Tests

   @todo Add test cases.

   <hr>
   @section Layout-DB-st System Tests

   @todo Add test cases.

   <hr>
   @section Layout-DB-O Analysis

   <hr>
   @section Layout-DB-ref References
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

/** Invariant for c2_layout_rec */
static bool __attribute__ ((unused)) layout_db_rec_invariant(const struct c2_layout_rec *l)
{
	return true;
}

/** @} end group LayoutDBDFSInternal */

/**
 * @addtogroup LayoutDBDFS
 * @{
 */

/**
   Initializes new layout schema - creates the DB tables.
*/
int c2_layout_schema_init(struct c2_layout_schema *l_schema,
			  struct c2_dbenv *db)
{
   /**
	@code
	Use the DB interface c2_table_init() to intialize the DB tables.
	@endcode
   */
	return 0;
}

/**
   De-initializes the layout schema - de-initializes the DB tables and the
   DB environment.
*/
void c2_layout_schema_fini(struct c2_layout_schema *l_schema)
{
   /*
	@code
	Uses the DB interface c2_table_fini() and c2_dbenv_fini() to
	de-intialize the DB tables and tables respectively.
	@endcode
   */

	return;
}

/**
   @todo
*/
void c2_layout_type_register(struct c2_layout_schema *l_schema,
			     const struct c2_layout_type *lt)
{
	return;
}

/**
   @todo
*/
void c2_layout_type_unregister(struct c2_layout_schema *l_schema,
			       const struct c2_layout_type *lt)
{
	return;
}

/**
   @todo
*/
void c2_layout_enum_register(struct c2_layout_schema *l_schema,
			     const struct c2_layout_enum_type *et)
{
	return;
}

/**
   @todo
*/
void c2_layout_enum_unregister(struct c2_layout_schema *l_schema,
			       const struct c2_layout_enum_type *et)
{
	return;
}

/**
   Adds a new layout record entry into the layouts table.
   If applicable, adds information related to this layout, into the relevant
   tables.
*/
int c2_layout_rec_add(const struct c2_layout *l,
		      struct c2_layout_schema *l_schema,
		      struct c2_db_tx *tx)
{
   /**
	@code
	Store layout representation in a buffer using c2_layout_encode().
	Add record to the DB using l->l_type->lt_ops->lto_rec_add().
	@endcode
   */

	return 0;
}

/**
   Deletes a layout record and its related information from the
   relevant tables.

   For example, following types of layout records can be destroyed if their
   respective reference count is 0:
   - A layout with PDCLUST layout type and with LIST enumeration type.
   - A layout with COMPOSITE layout type.
   <BR>
   A formula layout is never destroyed. e.g. A layout with PDCLUST layout type
   and with LINEAR enumeration type s never destroyed.

   If a layout can not be destroyed due to above conditions not being met,
   this API returns failure, though such specialization is taken care of by
   layout type specific implementation methods.
*/
int c2_layout_rec_delete(const struct c2_layout *layout,
			 struct c2_layout_schema *l_schema,
			 struct c2_db_tx *tx)
{
   /**
	@code
	Store layout representation in a buffer using c2_layout_encode().
	Use the function l->l_type->lt_ops->lto_rec_delete.
	@endcode
   */
	return 0;
}

/**
   Updates a layout record and its related information from the
   relevant tables.
*/
int c2_layout_rec_update(const struct c2_layout *layout,
			 struct c2_layout_schema *l_schema,
			 struct c2_db_tx *tx)
{
   /**
	@code
	Store layout representation in a buffer using c2_layout_encode().
	Use the function l->l_type->lt_ops->lto_rec_update.
	@endcode
   */
	return 0;
}

/**
   Obtains a layout record with the specified layout_id, and its related
   information from the relevant tables.
*/
int c2_layout_rec_lookup(const struct c2_layout_id *l_id,
			 struct c2_layout_schema *l_schema,
			 struct c2_db_tx *tx,
			 struct c2_layout *l_out)
{
   /**
	@code
	Use the function l->l_type->lt_ops->lto_rec_update to obtain
	the buffer including the record.

	Convert the buffer into layout using using c2_layout_decode().
	@endcode
   */
	return 0;
}

/** @} end group LayoutDBDFS */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
