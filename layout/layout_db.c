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
   - It is assumed that the roblem of coruption is going to be attacked
     generically at the lower layers (db and fop) transparently, instead of
     adding magic numbers and check-sums in every module. Thus the input to
     Layou DB APIs which is either a layout or a FOP buffer in most of the
     cases is going to be tested for corruption by db or fop layer, as
     applicable.

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
#include "lib/misc.h"          /* C2_SET0() */
#include "lib/vec.h"
#include "lib/arith.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"         /* C2_LOG() */

#include "db/db_common.h"      /* c2_db_buf_init() */
#include "layout/layout_db.h"

extern int LID_NONE;
extern bool layout_invariant(const struct c2_layout *l);
extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

enum {
	DEF_DB_FLAGS = 0
};
int DEFAULT_DB_FLAG = DEF_DB_FLAGS;

C2_ADDB_EV_DEFINE(ldb_lookup_success, "layout_lookup_success",
		  C2_ADDB_EVENT_LAYOUT_LOOKUP_SUCCESS, C2_ADDB_FLAG);
C2_ADDB_EV_DEFINE(ldb_lookup_fail, "layout_lookup_fail",
		  C2_ADDB_EVENT_LAYOUT_LOOKUP_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(ldb_add_success, "layout_add_success",
		  C2_ADDB_EVENT_LAYOUT_ADD_SUCCESS, C2_ADDB_FLAG);
C2_ADDB_EV_DEFINE(ldb_add_fail, "layout_add_fail",
		  C2_ADDB_EVENT_LAYOUT_ADD_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(ldb_update_success, "layout_update_success",
		  C2_ADDB_EVENT_LAYOUT_UPDATE_SUCCESS, C2_ADDB_FLAG);
C2_ADDB_EV_DEFINE(ldb_update_fail, "layout_update_fail",
		  C2_ADDB_EVENT_LAYOUT_UPDATE_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(ldb_delete_success, "layout_delete_success",
		  C2_ADDB_EVENT_LAYOUT_DELETE_SUCCESS, C2_ADDB_FLAG);
C2_ADDB_EV_DEFINE(ldb_delete_fail, "layout_delete_fail",
		  C2_ADDB_EVENT_LAYOUT_DELETE_FAIL, C2_ADDB_FUNC_CALL);

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

/**
 * Write layout record to the layouts table.
 *
 * @param op - This enum parameter indicates what is the DB operation to be
 * performed on the layout record which could be one of ADD/UPDATE/DELETE.
 */
int ldb_layout_write(struct c2_ldb_schema *schema, enum c2_layout_xcode_op op,
		     uint64_t lid, struct c2_db_pair *pair, uint32_t recsize,
		     struct c2_db_tx *tx)
{
	int rc;

	C2_PRE(schema != NULL);
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
	       op == C2_LXO_DB_DELETE);
	C2_PRE(pair != NULL);
	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof lid);
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_ldb_rec));
	C2_PRE(recsize >= sizeof(struct c2_ldb_rec));
	C2_PRE(tx != NULL);

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

	/*
	 * ADDB messages covering the failure of c2_table_insert(),
	 * c2_table_update() and c2_table_delete(), are added into the
	 * the respective callers of this routine.
	 */
	if (op == C2_LXO_DB_ADD) {
		rc = c2_table_insert(tx, pair);
	} else if (op == C2_LXO_DB_UPDATE) {
		rc = c2_table_update(tx, pair);
	} else if (op == C2_LXO_DB_DELETE) {
		rc = c2_table_delete(tx, pair);
	}

	return rc;
}

static int get_oldrec(struct c2_ldb_schema *schema,
		      struct c2_layout *l, void *area, uint32_t nbytes)
{
	struct c2_db_pair  pair;
	struct c2_db_tx    tx;
	int                rc;

	C2_PRE(schema != NULL);
	C2_PRE(l != NULL);
	C2_PRE(area != NULL);
	C2_PRE(nbytes >= sizeof(struct c2_ldb_rec));

	/*
	 * The only caller of this routine is c2_ldb_update(). Hence, the
	 * ADDB messages are added using ldb_update_fail event.
	 */
	rc = c2_db_tx_init(&tx, schema->ls_dbenv, DEFAULT_DB_FLAG);
	if(rc != 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_update_fail, "c2_db_tx_init()", rc);
		C2_LOG("get_oldrec(): lid %llu, c2_table_lookup() rc %d",
		       (unsigned long long)l->l_id, rc);
		goto out;
	}

	c2_db_pair_setup(&pair, &schema->ls_layouts,
			 &l->l_id, sizeof l->l_id,
			 area, nbytes);

	rc = c2_table_lookup(&tx, &pair);

	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);

	if (rc != 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_update_fail, "c2_table_lookup()", rc);
		C2_LOG("get_oldrec(): lid %llu, c2_table_lookup() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
	}

	/* Ignoring the return status, this being a lookup operation. */
	c2_db_tx_commit(&tx);

out:
	return rc;
}

/**
 * @note These checks should not be moved to invariant which is checked
 * before locking schema object. The risk is that a layout type may be
 * unregistered by the time schema->ls_type[lt_id] is accessed.
 */
int layout_type_verify(const struct c2_ldb_schema *schema, uint32_t lt_id)
{
	int rc = 0;

	C2_PRE(schema != 0);

	if (!IS_IN_ARRAY(lt_id, schema->ls_type)) {
		rc = -EPROTO;
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    c2_addb_func_fail, "Invalid layout type id",
			    -EPROTO);
		C2_LOG("layout_type_verify(): Invalid Layout_type_id "
		       "%lu", (unsigned long)lt_id);
		goto out;
	}

	if (schema->ls_type[lt_id] == NULL) {
		rc = -ENOENT;
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    c2_addb_func_fail, "Unregistered Layout type",
			    -EPROTO);
		C2_LOG("layout_type_verify(): Unregistered Layout type,"
	               " Layout_type_id %lu", (unsigned long)lt_id);
	}
out:
	return rc;
}

/** @} end group LayoutDBDFSInternal */

/**
 * @addtogroup LayoutDBDFS
 * @{
 */

/**
 * Initializes layout schema - creates the layouts table.
 * @pre dbenv Caller should have performed c2_dbenv_init() on dbenv.
 */
int c2_ldb_schema_init(struct c2_ldb_schema *schema,
		       struct c2_dbenv *dbenv)
{
	int rc;
	uint32_t i;

	C2_PRE(schema != NULL);
	C2_PRE(dbenv != NULL);

	C2_ENTRY();

	schema->ls_dbenv = dbenv;

	for (i = 0; i < ARRAY_SIZE(schema->ls_type); ++i) {
		schema->ls_type[i]      = NULL;
		schema->ls_type_data[i] = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(schema->ls_enum); ++i) {
		schema->ls_enum[i]      = NULL;
		schema->ls_enum_data[i] = NULL;
	}

	c2_mutex_init(&schema->ls_lock);

	rc = c2_table_init(&schema->ls_layouts, schema->ls_dbenv, "layouts",
			   DEFAULT_DB_FLAG, &layouts_table_ops);
	if (rc != 0) {
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    c2_addb_func_fail, "c2_table_init", rc);
		C2_LOG("c2_ldb_schema_init(): c2_table_init() failed, rc %d",
		       rc);
		schema->ls_dbenv = NULL;
		c2_mutex_fini(&schema->ls_lock);
	}

	C2_LEAVE("rc %d", rc);
	return rc;
}

/**
 * De-initializes the layout schema.
 * @pre All the layout types and enum types should be deregistered.
 */
int c2_ldb_schema_fini(struct c2_ldb_schema *schema)
{
	uint32_t i;

	C2_PRE(schema != NULL);

	C2_ENTRY();

	/* Verify that all the layout types were deregistered. */
	for (i = 0; i < ARRAY_SIZE(schema->ls_type); ++i) {
		if (schema->ls_type[i] != NULL) {
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    c2_addb_func_fail,
				    "Layout type still registered", -EPROTO);
			C2_LOG("c2_ldb_schema_fini(): Layout type with "
			       "Layout_type_id %lu is still registered",
			       (unsigned long)schema->ls_type[i]->lt_id);
			return -EPROTO;
		}
	}

	/* Verify that all the enum types were deregistered. */
	for (i = 0; i < ARRAY_SIZE(schema->ls_enum); ++i) {
		if (schema->ls_enum[i] != NULL) {
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    c2_addb_func_fail,
				    "Enum type still registered", -EPROTO);
			C2_LOG("c2_ldb_schema_fini(): Enum type with "
			       "Enum_type_id %lu is still registered",
			       (unsigned long)schema->ls_enum[i]->let_id);
			return -EPROTO;
		}
	}

	c2_table_fini(&schema->ls_layouts);
	c2_mutex_fini(&schema->ls_lock);
	schema->ls_dbenv = NULL;

	C2_LEAVE();
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

	C2_ENTRY("Layout_type_id %lu", (unsigned long)lt->lt_id);

	if (schema->ls_type[lt->lt_id] == lt) {
		C2_LOG("c2_ldb_type_register(): Layout type is already"
		       "registered, Layout_type_id %lu",
			(unsigned long)lt->lt_id);
		return -EEXIST;
	}

	C2_ASSERT(schema->ls_type[lt->lt_id] == NULL);
	C2_ASSERT(lt->lt_ops != NULL);

	c2_mutex_lock(&schema->ls_lock);
	schema->ls_type[lt->lt_id] = (struct c2_layout_type *)lt;

	/* Allocate type specific schema data. */
	rc = lt->lt_ops->lto_register(schema, lt);
	if (rc != 0) {
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    c2_addb_func_fail, "lto_register", rc);
		C2_LOG("c2_ldb_type_register(): Layout_type_id %lu, "
		       "lto_register() failed, rc %d",
		       (unsigned long)lt->lt_id, rc);
	}

	c2_mutex_unlock(&schema->ls_lock);

	C2_LEAVE("Layout_type_id %lu, rc %d", (unsigned long)lt->lt_id, rc);

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
	C2_PRE(schema->ls_type[lt->lt_id] == lt);

	C2_ENTRY("Layout_type_id %lu", (unsigned long)lt->lt_id);

	c2_mutex_lock(&schema->ls_lock);

	schema->ls_type[lt->lt_id] = NULL;

	lt->lt_ops->lto_unregister(schema, lt);

	c2_mutex_unlock(&schema->ls_lock);

	C2_LEAVE("Layout_type_id %lu", (unsigned long)lt->lt_id);
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

	C2_ENTRY("Enum_type_id %lu", (unsigned long)let->let_id);

	if (schema->ls_enum[let->let_id] == let) {
		C2_LOG("c2_ldb_enum_register(): Enum type is already"
		       "registered, Enum_type_id %lu",
			(unsigned long)let->let_id);
		return -EEXIST;
	}

	C2_ASSERT(schema->ls_enum[let->let_id] == NULL);
	C2_ASSERT(let->let_ops != NULL);

	c2_mutex_lock(&schema->ls_lock);
	schema->ls_enum[let->let_id] = (struct c2_layout_enum_type *)let;

	/* Allocate enum type specific schema data. */
	rc = let->let_ops->leto_register(schema, let);
	if (rc != 0) {
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    c2_addb_func_fail, "leto_register", rc);
		C2_LOG("c2_ldb_enum_register(): Enum_type_id %lu, "
		       "leto_register() failed, rc %d",
		       (unsigned long)let->let_id, rc);
	}

	c2_mutex_unlock(&schema->ls_lock);

	C2_LEAVE("Enum_type_id %lu, rc %d", (unsigned long)let->let_id, rc);
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
	C2_PRE(schema->ls_enum[let->let_id] == let);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)let->let_id);

	c2_mutex_lock(&schema->ls_lock);

	schema->ls_enum[let->let_id] = NULL;

	if (let->let_ops != NULL && let->let_ops->leto_unregister != NULL)
		let->let_ops->leto_unregister(schema, let);

	c2_mutex_unlock(&schema->ls_lock);

	C2_LEAVE("Enum_type_id %lu", (unsigned long)let->let_id);
}

/**
 * Returns max possible size for a record in the layouts table (without
 * considering the data in the tables other than layouts).
 */
uint32_t c2_ldb_max_recsize(struct c2_ldb_schema *schema)
{
	uint32_t   i;
	uint32_t   recsize;
	uint32_t   max_recsize = 0;

	C2_PRE(schema != NULL);

	/*
	 * Iterate over all the layout types to find maximum possible recsize.
	 */
	for (i = 0; i < ARRAY_SIZE(schema->ls_type); ++i) {
		if (schema->ls_type[i] == NULL)
			continue;

		recsize = schema->ls_type[i]->lt_ops->lto_max_recsize(schema);
		max_recsize = max32u(max_recsize, recsize);
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
	C2_PRE(out != NULL && *out == NULL);

	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof lid);
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_ldb_rec));

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	c2_mutex_lock(&schema->ls_lock);

	pair->dp_table = &schema->ls_layouts;

	c2_db_buf_init(&pair->dp_key, DBT_COPYOUT,
		       pair->dp_key.db_buf.b_addr,
		       pair->dp_key.db_buf.b_nob);
	pair->dp_key.db_static = true;

	c2_db_buf_init(&pair->dp_rec, DBT_COPYOUT,
		       pair->dp_rec.db_buf.b_addr,
		       pair->dp_rec.db_buf.b_nob);
	pair->dp_rec.db_static = true;

	*(uint64_t *)pair->dp_key.db_buf.b_addr = lid;
	memset(pair->dp_rec.db_buf.b_addr, 0, pair->dp_rec.db_buf.b_nob);

	rc = c2_table_lookup(tx, pair);
	if (rc != 0) {
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    ldb_lookup_fail, "c2_table_lookup", rc);
		C2_LOG("c2_ldb_lookup(): lid %llu, c2_table_lookup() failed, "
		       "rc %d", (unsigned long long)lid, rc);
		goto out;
	}

	bv =  (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
						   &pair->dp_rec.db_buf.b_nob);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_decode(schema, lid, &cur, C2_LXO_DB_LOOKUP, tx, out);
	if (rc != 0) {
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    ldb_lookup_fail, "c2_layout_decode", rc);
		C2_LOG("c2_ldb_lookup(): lid %llu, c2_layout_decode() failed, "
		       "rc %d", (unsigned long long)lid, rc);
	}

out:
	c2_db_buf_fini(&pair->dp_key);
	c2_db_buf_fini(&pair->dp_rec);

	if (rc == 0) {
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    ldb_lookup_success, true);
	}

	c2_mutex_unlock(&schema->ls_lock);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
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
	int                      rc;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(pair != NULL);
	C2_PRE(tx != NULL);

	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof l->l_id);
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_ldb_rec));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&schema->ls_lock);

	memset(pair->dp_rec.db_buf.b_addr, 0, pair->dp_rec.db_buf.b_nob);

	bv =  (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
						   &pair->dp_rec.db_buf.b_nob);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_encode(schema, l, C2_LXO_DB_ADD, tx, NULL, &cur);
	if (rc != 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_add_fail, "c2_layout_encode()", rc);
		C2_LOG("c2_ldb_add(): lid %llu, c2_layout_encode() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
		goto out;
	}

	rc = layout_type_verify(schema, l->l_type->lt_id);
	if (rc != 0) {
		C2_LOG("c2_ldb_add(): lid %llu, Unqualified Layout_type_id "
		       "%lu, rc %d", (unsigned long long)l->l_id,
		       (unsigned long)l->l_type->lt_id, rc);
		goto out;
	}

	lt = schema->ls_type[l->l_type->lt_id];
	recsize = lt->lt_ops->lto_recsize(schema, l);

	rc = ldb_layout_write(schema, C2_LXO_DB_ADD, l->l_id, pair,
			      recsize, tx);
	if (rc != 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_add_fail, "c2_table_insert()", rc);
		C2_LOG("c2_ldb_add(): lid %llu, c2_table_insert() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
		goto out;
	}

out:
	if (rc == 0) {
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    ldb_add_success, true);
	}

	c2_mutex_unlock(&schema->ls_lock);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

/**
 * Updates a layout record. Only l_ref can be updated for an existing layout
 * record.
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
	void                    *oldrec_area;
	struct c2_bufvec         oldrec_bv;
	struct c2_bufvec_cursor  oldrec_cur;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	uint32_t                 recsize;
	struct c2_layout_type   *lt;
	int                      rc;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(pair != NULL);
	C2_PRE(tx != NULL);

	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof l->l_id);
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_ldb_rec));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&schema->ls_lock);

	/*
	 * Get the old record from the layouts table. It is used to ensure that
	 * nothing other than l_ref gets updated for an existing layout record.
	 */
	recsize = c2_ldb_max_recsize(schema);
	oldrec_area = c2_alloc(recsize);
	if (oldrec_area == NULL) {
		rc = -ENOMEM;
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc, c2_addb_oom);
		C2_LOG("c2_ldb_update(): lid %llu, c2_alloc() failed, rc %d",
		       (unsigned long long)l->l_id, rc);
		goto out;
	}

	memset(oldrec_area, 0, recsize);

	rc = get_oldrec(schema, l, oldrec_area, recsize);
	if (rc != 0) {
		C2_LOG("c2_ldb_update(): lid %llu, get_oldrec() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
		goto out;
	}

	oldrec_bv = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&oldrec_area,
						(c2_bcount_t *)&recsize);
	c2_bufvec_cursor_init(&oldrec_cur, &oldrec_bv);

	/* Now, proceed to update the layout. */
	memset(pair->dp_rec.db_buf.b_addr, 0, pair->dp_rec.db_buf.b_nob);

	bv =  (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
						   &pair->dp_rec.db_buf.b_nob);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_encode(schema, l, C2_LXO_DB_UPDATE, tx,
			      &oldrec_cur, &cur);
	if (rc != 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_update_fail, "c2_layout_encode()", rc);
		C2_LOG("c2_ldb_update(): lid %llu, c2_layout_encode() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
		goto out;
	}

	/* todo lt is not verified here. Should be verified thr l invariant. */
	lt = schema->ls_type[l->l_type->lt_id];

	recsize = lt->lt_ops->lto_recsize(schema, l);

	rc = ldb_layout_write(schema, C2_LXO_DB_UPDATE, l->l_id,
			      pair, recsize, tx);
	if (rc != 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_update_fail, "c2_table_update()", rc);
		C2_LOG("c2_ldb_update(): lid %llu, c2_table_update() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
		goto out;
	}

out:
	if (oldrec_area != NULL)
		c2_free(oldrec_area);

	if (rc == 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_update_success, true);
	}

	c2_mutex_unlock(&schema->ls_lock);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
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
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	uint32_t                 recsize;
	struct c2_layout_type   *lt;
	int                      rc;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(pair != NULL);
	C2_PRE(tx != NULL);

	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof l->l_id);
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_ldb_rec));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&schema->ls_lock);

	memset(pair->dp_rec.db_buf.b_addr, 0, pair->dp_rec.db_buf.b_nob);

	bv =  (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
						   &pair->dp_rec.db_buf.b_nob);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_encode(schema, l, C2_LXO_DB_DELETE, tx, NULL, &cur);
	if (rc != 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_delete_fail, "c2_layout_encode()", rc);
		C2_LOG("c2_ldb_delete(): lid %llu, c2_layout_encode() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
		goto out;
	}

	lt = schema->ls_type[l->l_type->lt_id];

	recsize = lt->lt_ops->lto_recsize(schema, l);

	rc = ldb_layout_write(schema, C2_LXO_DB_DELETE, l->l_id,
			      pair, recsize, tx);
	if (rc != 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_delete_fail, "c2_table_delete()", rc);
		C2_LOG("c2_ldb_delete(): lid %llu, c2_table_delete() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
		goto out;
	}

out:
	if (rc == 0) {
		C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
			    ldb_delete_success, true);
	}

	c2_mutex_unlock(&schema->ls_lock);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
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
