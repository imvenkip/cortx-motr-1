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


   <HR>
   @section Layout-DB-ovw Overview
   This document contains the detail level design for the Layout DB Module.

   Purpose of the Layout-DB DLD <BR>
   The purpose of the Layout-DB Detailed Level Design (DLD) specification
   is to:
   - Refine the higher level design
   - To be verified by inspectors and architects
   - To guide the coding phase

   <HR>
   @section Layout-DB-def Definitions
     - COB: COB is component object and is defined at
     <a href="https://docs.google.com/a/xyratex.com/spreadsheet/ccc?key=0Ajg1HFjUZcaZdEpJd0tmM3MzVy1lMG41WWxjb0t4QkE&hl=en_US#gid=0">C2 Glossary</a>

   <HR>
   @section Layout-DB-req Requirements
   The specified requirements are as follows:
   - R.LAYOUT.SCHEMA.Layid: Layout identifiers are unique globally in
     the system, and persistent in the life cycle.
   - R.LAYOUT.SCHEMA.Types There are multiple layout types for
     different purposes: SNS, block map, local raid, de-dup, encryption,
     compression, etc.
   - R.LAYOUT.SCHEMA.Formulae
      - Parameters: Layout may contain sub-map information. Layout may
        contain some formula, and its parameters and real mapping information
        should be calculated from the formula and its parameters.
      - Garbage Collection: If some objects are deleted from the system,
        their associated layout may still be left in the system, with zero
        reference count. This layout can be re-used, or be garbage
        collected in some time.
   - R.LAYOUT.SCHEMA.Sub-Layouts: Sub-layouts.

   <HR>
   @section Layout-DB-depends Dependencies
   - Layout is a managed resource and depends upon Resource Manager.
   - Layout DB module depends upon the Layout module since the Layout module
     creates the layouts and uses/manages them.
   - Layout DB module depends upon the DB5 interfaces exposed by
     Colibri since the layouts are stored using the DB5 data-base.

   <HR>
   @section Layout-DB-highlights Design Highlights
   - Layout in itself is a managed resource.
   - The Layout DB module provides support for storing layouts with multiple
     layout types.
   - It provides support to store composite layout maps.
   - It is required that for adding a layout type or layout enumeration type,
     central layout/layout.h should not require modifications.

   <HR>
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
   "Layout" components though not able to achieve as of now! Any inputs
   are welcome.

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
   The layout schema for the Layout DB module consists of the following tables.
   - @ref Layout-DB-lspec-schema-layouts
   - @ref Layout-DB-lspec-schema-cob_lists
   - @ref Layout-DB-lspec-schema-comp_layout_ext_map

   Key-Record structures for these tables are described below.

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

   e.g. A layout with PDCLUST layout type and with FORMULA enumeration type,
   uses the layout_rec_attrs to store attributes like N and K.

   It is possible that some layout types do not need to store any attributes.
   e.g. A layout with PDCLUST layout type with LIST enumeration type does
   not need to store any attributes. Similarlly, a layout with COMPOSITE
   layout type does not need to store any attributes.

   @subsection Layout-DB-lspec-schema-cob_lists Table cob_lists
   @verbatim
   Table Name: cob_lists
   Key:
      - layout_id
      - cob_index
   Record:
      - cob_id

   @endverbatim

   This table contains multiple COB identifier entries for every PDCLUST type
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
   of the key called as prefix (128 bit). A segment ([A, B), V) is stored as
   a record (A, V) with a key (prefix, B). The high extent end is used as
   a part of the key.

   Since prefix is required to be 128 bit in size, layout id (unit64_t) of
   the composite layout is used as a part of the prefix (struct layout_prefix)
   to identify an extent map belonging to one specific composite layout. The
   lower 64 bits are currently unused (fillers).

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

   <HR>
   @section Layout-DB-conformance Conformance
   - I.LAYOUT.SCHEMA.Layid: Layout identifiers are unique globally in
     the system, and persistent in the life cycle. It is assumed that the
     layout identifiers are assigned by the Layout module and Layout DB module
     helps to store those persistently.
   - I.LAYOUT.SCHEMA.Types: There are multiple layout types for different
     purposes: SNS, block map, local raid, de-dup, encryption, compression,
     etc.
     <BR>
     Layout DB module supports storing all kinds of layout types supported
     currently by the layout module viz. PDCLUST and COMPOSITE.
     The framework supports to add other layout types, as required in
     the future.
   - I.LAYOUT.SCHEMA.Formulae:
      - Parameters:
         - In case of PDCLUST layout type using FORMULA enumeration method,
           formula is stored by the Layout DB and substituting parameters
           in the stored formula derives the real mapping information that
           is the list of COB identifiers.
      - Garbage Collection:
         - A layout with PDCLUST layout type and with LIST enumeration method
           is deleted when its last reference is released. Similarlly, a
           layout with COMPOSITE layout is deleted when its last reference
           is released.
         - A layout with PDCLUST layout type and with FORMULA enumeration
           method is never deleted and thus can be reused.
   - I.LAYOUT.SCHEMA.Sub-Layouts: COMPOSITE type of layout is used to
     store sub-layouts.

   <HR>
   @section Layout-DB-ut Unit Tests

   Following cases will be tested by unit tests:

   @test Registering layout types including PDCLUST amd COMPOSITE types.

   @test Unregistering layout types including PDCLUST amd COMPOSITE types.

   @test Registering each of LIST and FORMULA enum types.

   @test Unregistering each of LIST and FORMULA enum types.

   @test Adding layouts with all the possible combinations of all the layout types and enumeration types.

   @test Deleting layouts with all the possible combinations of all the layout types and enumeration types.

   @test Updating layouts with all the possible combinations of all the layout types and enumeration types.

   @test Reading a layout with all the possible combinations of all the layout types and enumeration types.

   @test Checking DB persistence by comparing a layout with the layout read from the DB, for all the possible combinations of all the layout types and enumeration types.

   <HR>
   @section Layout-DB-st System Tests

   System testing will include tests where multiple processes are writing
   to and reading from the DB at the same time.

   <HR>
   @section Layout-DB-O Analysis

   <HR>
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

/** Invariant for c2_ldb_rec */
static bool __attribute__ ((unused)) layout_db_rec_invariant(const struct c2_ldb_rec *l)
{
	return true;
}

static int __attribute__ ((unused)) ldb_schema_internal_init(struct c2_ldb_schema *schema, struct c2_dbenv *db)
{
	return 0;
}

static void __attribute__ ((unused)) ldb_schema_internal_fini(struct c2_ldb_schema *schema)
{
}

/** @} end group LayoutDBDFSInternal */

/**
 * @addtogroup LayoutDBDFS
 * @{
 */

/**
   Initializes new layout schema - creates the DB tables.
*/
int c2_ldb_schema_init(struct c2_ldb_schema *schema,
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
void c2_ldb_schema_fini(struct c2_ldb_schema *schema)
{
   /*
	@code
	Use the DB interface c2_table_fini() to de-intialize the DB
	tables.
	@endcode
   */

	return;
}

/**
   Registers a new layout type with the layout types maintained by
   c2_ldb_schema::ls_type[].
*/
void c2_ldb_type_register(struct c2_ldb_schema *schema,
			  const struct c2_layout_type *lt)
{
   /**
	@code
	C2_PRE(IS_IN_ARRAY(lt->lt_id, schema->ls_type));
	C2_PRE(schema->ls_type[lt->lt_id] == NULL);

	schema->ls_type[lt->lt_id] = lt;

	Allocate type specific schema data using lto_register().
	lt->lto_register(schema, lt);

	@endcode
   */
	return;
}

/**
   Unregisters a layout type from the layout types maintained by
   c2_ldb_schema::ls_enum[].
*/
void c2_ldb_type_unregister(struct c2_ldb_schema *schema,
			    const struct c2_layout_type *lt)
{
	return;
}

/**
   Registers a new enumeration type with the enumeration types
   maintained by c2_ldb_schema::ls_type[].
*/
void c2_ldb_enum_register(struct c2_ldb_schema *schema,
			  const struct c2_layout_enum_type *let)
{
   /**
	@code
	C2_PRE(IS_IN_ARRAY(let->let_id, schema->ls_enum));
	C2_PRE(schema->ls_enum[let->let_id] == NULL);

	schema->ls_enum[let->let_id] = let;

	Allocates enum specific schema data using leto_register().
	let->leto_register(schema, let);

	@endcode
   */

	return;
}

/**
   Unregisters an enumeration type from the enumeration types
   maintained by c2_ldb_schema::ls_enum[].
*/
void c2_ldb_enum_unregister(struct c2_ldb_schema *schema,
			    const struct c2_layout_enum_type *et)
{
	return;
}

/**
   @todo Add description
*/
void **c2_ldb_type_data(struct c2_ldb_schema *schema,
			const struct c2_layout_type *lt)
{
   /**
	@code
	C2_PRE(IS_IN_ARRAY(lt->lt_id, schema->ls_type_data));
	return &schema->ls_type_data[lt->lt_id];
	@endcode
   */
	return NULL;
}

/**
   @todo Add description
*/
void **c2_ldb_enum_data(struct c2_ldb_schema *schema,
			const struct c2_layout_enum_type *et)
{
   /*
	@code
	C2_PRE(IS_IN_ARRAY(et->let_id, schema->ls_enum_data));
	return &schema->ls_enum_data[et->let_id];
	@endcode
   */
	return NULL;
}

/**
   Adds a new layout record entry into the layouts table.
   If applicable, adds information related to this layout, into the relevant
   tables.
*/
int c2_ldb_rec_add(const struct c2_layout *l,
		   struct c2_ldb_schema *schema,
		   struct c2_db_tx *tx)
{
   /**
	@code
	Store layout representation in a buffer using c2_lay_encode().
	Add record to the DB using l->l_type->lt_ops->lto_rec_add().
	@endcode
   */

	return 0;
}

/**
   Deletes a layout record and its related information from the
   relevant tables.

   Layouts with enumeration type other than 'formula' are destroyed if and
   only if their respective referecne count is 0.

   A layout with formula enumeration type is never destroyed.
*/
int c2_ldb_rec_delete(const struct c2_layout *layout,
		      struct c2_ldb_schema *schema,
		      struct c2_db_tx *tx)
{
   /**
	@code
	Store layout representation in a buffer using c2_lay_encode().
	Use the function l->l_type->lt_ops->lto_rec_delete.
	@endcode
   */
	return 0;
}

/**
   Updates a layout record and its related information from the
   relevant tables.
*/
int c2_ldb_rec_update(const struct c2_layout *layout,
		      struct c2_ldb_schema *schema,
		      struct c2_db_tx *tx)
{
   /**
	@code
	Store layout representation in a buffer using c2_lay_encode().
	Use the function l->l_type->lt_ops->lto_rec_update.
	@endcode
   */
	return 0;
}

/**
   Obtains a layout record with the specified layout_id, and its related
   information from the relevant tables.
*/
int c2_ldb_rec_lookup(const uint64_t *lid,
		      struct c2_ldb_schema *schema,
		      struct c2_db_tx *tx,
		      struct c2_layout *out)
{
   /**
	@code
	---
	Use the function l->l_type->lt_ops->lto_rec_lookup to obtain
	the buffer including the record.
	Convert the buffer into layout using using c2_lay_decode().
	---

	Invoke
	struct c2_db_pair	 pair;
	struct c2_layout_rec	*rec;
	struct c2_bufvec_cursor	 it;

	c2_db_pair_setup(&pair, &schema->ls_layout_entries,
			 lid, sizeof(uint64_t),
			 c2_lay_rec, sizeof c2_lay_rec);


	set_key_val_pair(lid);

	c2_table_lookup(&schema->ls_layout_entries, &pair);

	Setup cursor to point at the beginning of the key-val pair record.

	Parse generic part of the key-val pair record using decode.
	c2_layout_rec_decode(&it, &rec);

	parse the layout type specific part using decode
	schema->ls_types[rec->lr_lt_id]->lto_decode(&it, rec, out);

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
