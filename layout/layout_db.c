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
   - Layout and layout-id are managed resources. @see @ref layout
   - The Layout DB module provides support for storing layouts with multiple
     layout types.
   - It provides support for storing composite layout maps.
   - It is required that for adding a layout type or layout enumeration type,
     central layout.h should not require modifications.

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
   The following diagram shows the internal components of the "Layout" module,
   including the "Layout DB" component.

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
      - layout_type_specific_data (optional)

   @endverbatim

   layout_type_specific_data field is used to store layout type or layout enum
   type specific data. Structure of this field varies accordingly. e.g.
   - In case of a layout with PDCLUST layout type, the structure
     c2_ldb_pdclust_rec is used to store attributes like enumeration type id,
     N, K, P.
   - In case of a layout with LIST enum type, an array of ldb_list_cob_entry
     structure with size LDB_MAX_INLINE_COB_ENTRIES is used to store a few COB
     entries inline into the layouts table itself.
   - It is possible that some layouts do not need to store any layout type or
     layout enum type specific data in this layouts table. e.g. A layout with
     COMPOSITE layout type.

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

   cob_index for the first entry in this table will be continuation of the
   llce_cob_index from the array of ldb_list_cob_entry stored in layouts table.

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

   Since prefix (an element of the key for c2_emap) is required to be 128 bit
   in size, layout id (unit64_t) of the composite layout is used as a part of
   the prefix (struct layout_prefix) to identify an extent map belonging to one
   specific composite layout. The lower 64 bits are currently unused (fillers).

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
   DB5 internally provides synchrnization against various table entries.
   Hence layout schema need not do much in that regard.

   Various arrays in struct c2_ldb_schema are protected by using
   c2_ldb_schema::ls_lock.

   The in-memory c2_layout object is protected using c2_layout::l_lock.

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
         - In case of PDCLUST layout type using LINEAR enumeration,
           linear formula is stored by the Layout DB and substituting
	   parameters in the stored formula derives the real mapping
	   information that is the list of COB identifiers.
      - Garbage Collection:
         - A layout with PDCLUST layout type and with LIST enumeration
           is deleted when its last reference is released. Similarlly, a
           layout with COMPOSITE layout is deleted when its last reference
           is released.
         - A layout with PDCLUST layout type and with LINEAR enumeration
           method is never deleted and thus can be reused.
   - I.LAYOUT.SCHEMA.Sub-Layouts: COMPOSITE type of layout is used to
     store sub-layouts.

   <HR>
   @section Layout-DB-ut Unit Tests

   Following cases will be tested by unit tests:

   @test Registering layout types including PDCLUST amd COMPOSITE types.

   @test Unregistering layout types including PDCLUST amd COMPOSITE types.

   @test Registering each of LIST and LINEAR enum types.

   @test Unregistering each of LIST and LINEAR enum types.

   @test Encode layout with each of layout type and enum types.

   @test Decode layout with each of layout type and enum types.

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

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"        /* C2_SET0() */
#include "lib/vec.h"
#include "lib/arith.h"
#include "lib/trace.h"       /* C2_LOG() */
#include "db/db_common.h"    /* c2_db_buf_init() */

#include "layout/layout_internal.h"
#include "layout/layout_db.h"

/**
 * @addtogroup LayoutDBDFS
 * @{
 */

/**
 * Initializes layout schema - creates the layouts table.
 * @pre db Caller should have performed c2_dbenv_init() on db.
 */
int c2_ldb_schema_init(struct c2_ldb_schema *schema,
		       struct c2_dbenv *dbenv)
{
	int rc;
	int i;

	C2_PRE(schema != NULL);
	C2_PRE(dbenv != NULL);

	C2_LOG("Inside c2_ldb_schema_init()\n");

	schema->dbenv = dbenv;

	for (i = 0; i < ARRAY_SIZE(schema->ls_type); ++i) {
		schema->ls_type[i]      = NULL;
		schema->ls_type_data[i] = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(schema->ls_enum); ++i) {
		schema->ls_enum[i]      = NULL;
		schema->ls_enum_data[i] = NULL;
	}

	c2_mutex_init(&schema->ls_lock);

	rc = c2_table_init(&schema->ls_layouts, schema->dbenv, "layouts", 0,
			   &layouts_table_ops);

	return rc;
}

/**
 * De-initializes the layout schema.
 */
int c2_ldb_schema_fini(struct c2_ldb_schema *schema)
{
	int i;

	C2_PRE(schema != NULL);

	C2_LOG("Inside c2_ldb_schema_fini()\n");

	/* Verify that all the layout types were deregistered. */
	for (i = 0; i < ARRAY_SIZE(schema->ls_type); ++i) {
		if (schema->ls_type[i] != NULL)
			return -EPROTO;
	}

	/* Verify that all the enum types were deregistered. */
	for (i = 0; i < ARRAY_SIZE(schema->ls_enum); ++i) {
		if (schema->ls_enum[i] != NULL)
			return -EPROTO;
	}

	c2_table_fini(&schema->ls_layouts);
	c2_mutex_fini(&schema->ls_lock);
	schema->dbenv = NULL;

	return 0;
}

/**
 * Registers a new layout type with the layout types maintained by
 * c2_ldb_schema::ls_type[].
 */
int c2_ldb_type_register(struct c2_ldb_schema *schema,
			 const struct c2_layout_type *lt)
{
	int rc = 0;

	C2_PRE(schema != NULL);
	C2_PRE(lt != NULL);
	C2_PRE(IS_IN_ARRAY(lt->lt_id, schema->ls_type));

	if (schema->ls_type[lt->lt_id] == lt)
		return -EEXIST;

	C2_ASSERT(schema->ls_type[lt->lt_id] == NULL);

	c2_mutex_lock(&schema->ls_lock);
	schema->ls_type[lt->lt_id] = (struct c2_layout_type *)lt;

	/* Allocate type specific schema data using lto_register(). */
	if (lt->lt_ops != NULL && lt->lt_ops->lto_register != NULL)
		rc = lt->lt_ops->lto_register(schema, lt);

	c2_mutex_unlock(&schema->ls_lock);

	return rc;
}

/**
 * Unregisters a layout type from the layout types maintained by
 * c2_ldb_schema::ls_type[].
 */
void c2_ldb_type_unregister(struct c2_ldb_schema *schema,
			    const struct c2_layout_type *lt)
{
	C2_PRE(schema != NULL);
	C2_PRE(lt != NULL);

	c2_mutex_lock(&schema->ls_lock);

	schema->ls_type[lt->lt_id] = NULL;

	if (lt->lt_ops != NULL && lt->lt_ops->lto_unregister != NULL)
		lt->lt_ops->lto_unregister(schema, lt);

	c2_mutex_unlock(&schema->ls_lock);
}

/**
 * Registers a new enumeration type with the enumeration types
 * maintained by c2_ldb_schema::ls_enum[].
 */
int c2_ldb_enum_register(struct c2_ldb_schema *schema,
			 const struct c2_layout_enum_type *let)
{
	int rc = 0;

	C2_PRE(schema != NULL);
	C2_PRE(let != NULL);
	C2_PRE(IS_IN_ARRAY(let->let_id, schema->ls_enum));

	if (schema->ls_enum[let->let_id] == let)
		return -EEXIST;

	C2_ASSERT(schema->ls_enum[let->let_id] == NULL);

	c2_mutex_lock(&schema->ls_lock);
	schema->ls_enum[let->let_id] = (struct c2_layout_enum_type *)let;

	/* Allocate enum specific schema data using leto_register(). */
	if (let->let_ops != NULL && let->let_ops->leto_register != NULL)
		rc = let->let_ops->leto_register(schema, let);

	c2_mutex_unlock(&schema->ls_lock);

	return rc;
}

/**
 * Unregisters an enumeration type from the enumeration types
 * maintained by c2_ldb_schema::ls_enum[].
 */
void c2_ldb_enum_unregister(struct c2_ldb_schema *schema,
			    const struct c2_layout_enum_type *let)
{
	C2_PRE(schema != NULL);
	C2_PRE(let != NULL);

	c2_mutex_lock(&schema->ls_lock);

	schema->ls_enum[let->let_id] = NULL;

	if ((let->let_ops != NULL) && let->let_ops->leto_unregister != NULL)
		let->let_ops->leto_unregister(schema, let);

	c2_mutex_unlock(&schema->ls_lock);
}

/**
 * Returns pointer to the type specific data from the schema.
 */
void **c2_ldb_type_data(struct c2_ldb_schema *schema,
			const struct c2_layout_type *lt)
{
   /**
	@code
	C2_PRE(IS_IN_ARRAY(lt->lt_id, schema->ls_type_data));
	C2_PRE(schema->ls_type[lt->lt_id] == lt);

	return &schema->ls_type_data[lt->lt_id];
	@endcode
   */
	return NULL;
}

/**
 * Returns pointer to the enum specific data from the schema.
 */
void **c2_ldb_enum_data(struct c2_ldb_schema *schema,
			const struct c2_layout_enum_type *et)
{
   /**
	@code
	C2_PRE(IS_IN_ARRAY(et->let_id, schema->ls_enum_data));
	C2_PRE(schema->ls_enum[et->let_id] == et);

	return &schema->ls_enum_data[et->let_id];
	@endcode
   */
	return NULL;
}

/**
 * Returns max possible size for a record in the layouts table (without
 * considering the data in the tables other than layouts).
 */
uint32_t c2_ldb_max_recsize(struct c2_ldb_schema *schema)
{
	int        i;
	uint32_t   recsize;
	uint32_t   max_recsize = 0;

	/*
	 * Iterate over all the layout types to find maximum possible recsize.
	 */
	/* @todo Check for (schema->ls_type[i] != NULL) */
	for (i = 0; i < ARRAY_SIZE(schema->ls_type); ++i) {
		if (schema->ls_type[i] == NULL)
			continue;

		recsize = schema->ls_type[i]->lt_ops->lto_max_recsize(schema);
		max_recsize = max32(max_recsize, recsize);
	}

	return sizeof(struct c2_ldb_rec) + max_recsize;
}

/**
 * Looks up a persistent layout record with the specified layout_id, and
 * its related information from the relevant tables.
 *
 * @param pair A c2_db_pair sent by the caller along with having set
 * pair->dp_key.db_buf and pair->dp_rec.db_buf. This is to leave the buffer
 * allocation with the caller. The caller may take help of c2_ldb_max_recsize()
 * while deciding the size of the buffer.
 */
int c2_ldb_lookup(struct c2_ldb_schema *schema,
		  uint64_t lid,
		  struct c2_db_pair *pair,
		  struct c2_db_tx *tx,
		  struct c2_layout **out)
{
	int                      rc;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;

	C2_PRE(schema != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(pair != NULL);
	C2_PRE(tx != NULL);
	C2_PRE(out != NULL);

	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof lid);
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_ldb_rec));

	pair->dp_table = &schema->ls_layouts;
	*(uint64_t *)pair->dp_key.db_buf.b_addr = lid;

	c2_db_buf_init(&pair->dp_key, DBT_COPYOUT,
		       pair->dp_key.db_buf.b_addr,
		       pair->dp_key.db_buf.b_nob);
	pair->dp_key.db_static = false;

	c2_db_buf_init(&pair->dp_rec, DBT_COPYOUT,
		       pair->dp_rec.db_buf.b_addr,
		       pair->dp_rec.db_buf.b_nob);
	pair->dp_rec.db_static = false;

	C2_SET0(pair->dp_key.db_buf.b_addr);
	C2_SET0(pair->dp_rec.db_buf.b_addr);

	rc = c2_table_lookup(tx, pair);
	printf("c2_ldb_lookup(): Result of c2_table_lookup() %d, "
	       "lid %" PRId64 "\n", rc, lid);
	/* todo Handle the case - record not found. */
	if (rc != 0)
		return rc;

	bv =  (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
				&pair->dp_rec.db_buf.b_nob);

	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_decode(schema, lid, &cur, C2_LXO_DB_LOOKUP, tx, out);

	return rc;
}

/**
 * Adds a new layout record entry into the layouts table.
 * If applicable, adds layout type and enum type specific entries into the
 * relevant tables.
 *
 * @param pair A c2_db_pair sent by the caller along with having set
 * pair->dp_key.db_buf and pair->dp_rec.db_buf. This is to leave the buffer
 * allocation with the caller. The caller may take help of c2_ldb_max_recsize()
 * while deciding the size of the buffer.
 */
int c2_ldb_add(struct c2_ldb_schema *schema,
	       struct c2_layout *l,
	       struct c2_db_pair *pair,
	       struct c2_db_tx *tx)
{
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	uint32_t                 recsize;
	struct c2_layout_type   *lt;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(pair != NULL);
	C2_PRE(tx != NULL);

	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof(l->l_id));
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_ldb_rec));

	bv =  (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
				&pair->dp_rec.db_buf.b_nob);

	c2_bufvec_cursor_init(&cur, &bv);

	c2_layout_encode(schema, l, C2_LXO_DB_ADD, tx, &cur);

	lt = schema->ls_type[l->l_type->lt_id];

	recsize = lt->lt_ops->lto_recsize(schema, l);

	ldb_layout_write(schema, C2_LXO_DB_ADD, l->l_id, pair, recsize, tx);

	return 0;
}

/**
 * Updates a layout record and its related information from the
 * relevant tables.
 *
 * @param pair A c2_db_pair sent by the caller along with having set
 * pair->dp_key.db_buf and pair->dp_rec.db_buf. This is to leave the buffer
 * allocation with the caller. The caller may take help of c2_ldb_max_recsize()
 * while deciding the size of the buffer.
 */
int c2_ldb_update(struct c2_ldb_schema *schema,
		  struct c2_layout *l,
		  struct c2_db_pair *pair,
		  struct c2_db_tx *tx)
{
	struct c2_bufvec_cursor  cur;
	struct c2_bufvec         bv = C2_BUFVEC_INIT_BUF(
					&pair->dp_rec.db_buf.b_addr,
					&pair->dp_rec.db_buf.b_nob);
	uint32_t                 recsize;
	struct c2_layout_type   *lt;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(pair != NULL);
	C2_PRE(tx != NULL);

	c2_bufvec_cursor_init(&cur, &bv);

	c2_layout_encode(schema, l, C2_LXO_DB_UPDATE, tx, &cur);

	lt = schema->ls_type[l->l_type->lt_id];

	recsize = lt->lt_ops->lto_recsize(schema, l);

	ldb_layout_write(schema, C2_LXO_DB_UPDATE, l->l_id, pair, recsize, tx);

	return 0;
}

/**
 * Deletes a layout record with given layout id and its related information from
 * the relevant tables.
 *
 * @param pair A c2_db_pair sent by the caller along with having set
 * pair->dp_key.db_buf and pair->dp_rec.db_buf. This is to leave the buffer
 * allocation with the caller. The caller may take help of c2_ldb_max_recsize()
 * while deciding the size of the buffer.
 */
int c2_ldb_delete(struct c2_ldb_schema *schema,
		  struct c2_layout *l,
		  struct c2_db_pair *pair,
		  struct c2_db_tx *tx)
{
	struct c2_bufvec_cursor  cur;
	struct c2_bufvec         bv = C2_BUFVEC_INIT_BUF(
					&pair->dp_rec.db_buf.b_addr,
					&pair->dp_rec.db_buf.b_nob);
	uint32_t                 recsize;
	struct c2_layout_type   *lt;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(pair != NULL);
	C2_PRE(tx != NULL);

	c2_bufvec_cursor_init(&cur, &bv);

	c2_layout_encode(schema, l, C2_LXO_DB_DELETE, tx, &cur);

	lt = schema->ls_type[l->l_type->lt_id];

	recsize = lt->lt_ops->lto_recsize(schema, l);

	ldb_layout_write(schema, C2_LXO_DB_DELETE, l->l_id, pair, recsize, tx);

	return 0;
}


/** @} end group LayoutDBDFS */

/**
 * @defgroup LayoutDBDFSInternal Layout DB Internals
 * @brief Detailed functional specification of the internals of the
 * Layout-DB module.
 *
 * This section covers the data structures and sub-routines used internally.
 *
 * @see @ref Layout-DB "Layout-DB DLD"
 * and @ref Layout-DB-lspec "Layout-DB Logical Specification".
 *
 * @{
 */

int ldb_layout_write(struct c2_ldb_schema *schema,
		     enum c2_layout_xcode_op op,
		     uint64_t lid,
		     struct c2_db_pair *pair,
		     uint32_t recsize,
		     struct c2_db_tx *tx)
{
	pair->dp_table = &schema->ls_layouts;
	*(uint64_t *)pair->dp_key.db_buf.b_addr = lid;

	c2_db_buf_init(&pair->dp_key, DBT_COPYOUT,
		       pair->dp_key.db_buf.b_addr,
		       pair->dp_key.db_buf.b_nob);
	pair->dp_key.db_static = false;

	c2_db_buf_init(&pair->dp_rec, DBT_COPYOUT,
		       pair->dp_rec.db_buf.b_addr,
		       pair->dp_rec.db_buf.b_nob);
	pair->dp_rec.db_static = false;

	if (op == C2_LXO_DB_ADD) {
		c2_table_insert(tx, pair);
	} else if (op == C2_LXO_DB_UPDATE) {
		c2_table_update(tx, pair);
	} else if (op == C2_LXO_DB_DELETE) {
		c2_table_delete(tx, pair);
	}
	return 0;
}


/** @} end group LayoutDBDFSInternal */


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
