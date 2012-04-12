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
 * @page Layout-DB Layout DB DLD
 *
 * - @ref Layout-DB-ovw
 * - @ref Layout-DB-def
 * - @ref Layout-DB-req
 * - @ref Layout-DB-depends
 * - @ref Layout-DB-highlights
 * - @subpage Layout-DB-fspec "Functional Specification"
 * - @ref Layout-DB-lspec
 *    - @ref Layout-DB-lspec-comps
 *    - @ref Layout-DB-lspec-schema
 *    - @ref Layout-DB-lspec-state
 *    - @ref Layout-DB-lspec-thread
 *    - @ref Layout-DB-lspec-numa
 * - @ref Layout-DB-conformance
 * - @ref Layout-DB-ut
 * - @ref Layout-DB-st
 * - @ref Layout-DB-O
 * - @ref Layout-DB-ref
 *
 * <HR>
 * @section Layout-DB-ovw Overview
 * This document contains the detail level design for the Layout DB Module.
 *
 * Purpose of the Layout-DB DLD <BR>
 * The purpose of the Layout-DB Detailed Level Design (DLD) specification is to:
 * - Refine the higher level design
 * - To be verified by inspectors and architects
 * - To guide the coding phase
 *
 * <HR>
 * @section Layout-DB-def Definitions
 *   - COB: COB is component object and is defined at
 *   <a href="https://docs.google.com/a/xyratex.com/spreadsheet/ccc?key=0Ajg1HFjUZcaZdEpJd0tmM3MzVy1lMG41WWxjb0t4QkE&hl=en_US#gid=0">C2 Glossary</a>
 *
 * <HR>
 * @section Layout-DB-req Requirements
 * The specified requirements are as follows:
 * - R.LAYOUT.SCHEMA.Layid: Layout identifiers are unique globally in
 *   the system, and persistent in the life cycle.
 * - R.LAYOUT.SCHEMA.Types There are multiple layout types for
 *   different purposes: SNS, block map, local raid, de-dup, encryption,
 *   compression, etc.
 * - R.LAYOUT.SCHEMA.Formulae
 *    - Parameters: Layout may contain sub-map information. Layout may
 *      contain some formula, and its parameters and real mapping information
 *      should be calculated from the formula and its parameters.
 *    - Garbage Collection: If some objects are deleted from the system,
 *      their associated layout may still be left in the system, with zero
 *      reference count. This layout can be re-used, or be garbage
 *      collected in some time.
 * - R.LAYOUT.SCHEMA.Sub-Layouts: Sub-layouts.
 *
 * <HR>
 * @section Layout-DB-depends Dependencies
 * - Layout is a managed resource and depends upon Resource Manager.
 * - Layout DB module depends upon the Layout module since the Layout module
 *   creates the layouts and uses/manages them.
 * - Layout DB module depends upon the DB5 interfaces exposed by
 *   Colibri since the layouts are stored using the DB5 data-base.
 *
 * <HR>
 * @section Layout-DB-highlights Design Highlights
 * - Layout and layout-id are managed resources. @see @ref layout
 * - The Layout DB module provides support for storing layouts with multiple
 *   layout types.
 * - It provides support for storing composite layout maps.
 * - It is required that for adding a layout type or layout enumeration type,
 *   central layout.h should not require modifications.
 * - It is assumed that the roblem of coruption is going to be attacked
 *   generically at the lower layers (db and fop) transparently, instead of
 *   adding magic numbers and check-sums in every module. Thus the input to
 *   Layou DB APIs which is either a layout or a FOP buffer in most of the
 *   cases is going to be tested for corruption by db or fop layer, as
 *   applicable.
 *
 * <HR>
 * @section Layout-DB-lspec Logical Specification
 * Layout DB makes use of the DB5 data-base to persistently store the layout
 * entries. This section describes how the Layout DB module works.
 *
 * - @ref Layout-DB-lspec-comps
 * - @ref Layout-DB-lspec-schema
 *    - @ref Layout-DB-lspec-ds1
 *    - @ref Layout-DB-lspec-sub1
 *    - @ref LayoutDBDFSInternal
 * - @ref Layout-DB-lspec-state
 * - @ref Layout-DB-lspec-thread
 * - @ref Layout-DB-lspec-numa
 *
 *
 * @subsection Layout-DB-lspec-comps Component Overview
 * The following diagram shows the internal components of the "Layout" module,
 * including the "Layout DB" component.
 *
 * @dot
 * digraph {
 *   node [style=box];
 *   label = "Layout Components and Interactions";
 *
 *   subgraph colibri_client {
 *       label = "Client";
 *       cClient [label="Client"];
 *   }
 *
 *   subgraph colibri_layout {
 *       label = "Layout";
 *
 *   cLDB [label="Layout DB", style="filled"];
 *   cFormula [label="Layout Formula", style="filled"];
 *   cLayout [label="Layout (Managed Resource)", style="filled"];
 *
 *       cLDB -> cFormula [label="build formula"];
 *       cFormula -> cLayout [label="build layout"];
 *    }
 *
 *   subgraph colibri_server {
 *       label = "Server";
 *       cServer [label="Server"];
 *    }
 *
 *   cClient -> cFormula [label="substitute"];
 *   cServer -> cFormula [label="substitute"];
 *
 *   { rank=same; cClient cFormula cServer }
 *  }
 *  @enddot
 *
 * @subsection Layout-DB-lspec-schema Layout Schema Design
 * The layout schema for the Layout DB module consists of the following tables.
 * - @ref Layout-DB-lspec-schema-layouts
 * - @ref Layout-DB-lspec-schema-cob_lists
 * - @ref Layout-DB-lspec-schema-comp_layout_ext_map
 *
 * Key-Record structures for these tables are described below.
 *
 * @subsection Layout-DB-lspec-schema-layouts Table layouts
 * @verbatim
 * Table Name: layouts
 * Key: layout_id
 * Record:
 *    - layout_type_id
 *    - layout_enumeration_type_id
 *    - reference_count
 *    - layout_type_specific_data (optional)
 *
 * @endverbatim
 *
 * layout_type_specific_data field is used to store layout type or layout enum
 * type specific data. Structure of this field varies accordingly. For example:
 * - In case of a layout with PDCLUST layout type, the structure
 *   c2_ldb_pdclust_rec is used to store attributes like enumeration type id,
 *   N, K, P.
 * - In case of a layout with LIST enum type, an array of ldb_list_cob_entry
 *   structure with size LDB_MAX_INLINE_COB_ENTRIES is used to store a few COB
 *   entries inline into the layouts table itself.
 * - It is possible that some layouts do not need to store any layout type or
 *   layout enum type specific data in this layouts table. For example, a
 *   layout with COMPOSITE layout type.
 *
 * @subsection Layout-DB-lspec-schema-cob_lists Table cob_lists
 * @verbatim
 * Table Name: cob_lists
 * Key:
 *    - layout_id
 *    - cob_index
 * Record:
 *    - cob_id
 *
 *  @endverbatim
 *
 * This table contains multiple COB identifier entries for every PDCLUST type
 * of layout with LIST enumeration type.
 *
 * layout_id is a foreign key referring record, in the layouts table.
 *
 * cob_index for the first entry in this table will be continuation of the
 * llce_cob_index from the array of ldb_list_cob_entry stored in layouts table.
 *
 * @subsection Layout-DB-lspec-schema-comp_layout_ext_map Table comp_layout_ext_map
 *
 * @verbatim
 * Table Name: comp_layout_ext_map
 * Key
 *    - composite_layout_id
 *    - last_offset_of_segment
 * Record
 *    - start_offset_of_segment
 *    - layout_id
 *
 * @endverbatim
 *
 * composite_layout_id is the layout_id for the COMPOSITE type of layout,
 * stored as key in the layouts table.
 *
 * layout_id is a foreign key referring record, in the layouts table.
 *
 * Layout DB uses a single c2_emap instance to implement the composite layout
 * extent map viz. comp_layout_ext_map. This table stores the "layout segment
 * to sub-layout id mappings" for each compsite layout.
 *
 * Since prefix (an element of the key for c2_emap) is required to be 128 bit
 * in size, layout id (unit64_t) of the composite layout is used as a part of
 * the prefix (struct layout_prefix) to identify an extent map belonging to one
 * specific composite layout. The lower 64 bits are currently unused (fillers).
 *
 * An example:
 *
 * Suppose a layout L1 is of the type composite and constitutes of 3
 * sub-layouts say S1, S2, S3. These sub-layouts S1, S2 and S3 use
 * the layouts with layout id L11, L12 and L13 respectively.
 *
 * In this example, for the composite layout L1, the comp_layout_ext_map
 * table stores 3 layout segments viz. S1, S2 and S3. All these 3 segments
 * are stored in the form of ([A, B), V) where:
 * - A is the start offset from the layout L1
 * - B is the end offset from the layout L1
 * - V is the layout id for the layout used by the respective segment and
 *   is either of L11, L12 or L13 as applicable.
 *
 * @subsubsection Layout-DB-lspec-ds1 Subcomponent Data Structures
 * See @ref LayoutDBDFSInternal for internal data structures.
 *
 * @subsubsection Layout-DB-lspec-sub1 Subcomponent Subroutines
 * See @ref LayoutDBDFSInternal for internal subroutines.
 *
 * @subsection Layout-DB-lspec-state State Specification
 *
 * @subsection Layout-DB-lspec-thread Threading and Concurrency Model
 * - DB5 internally provides synchronization against various table entries.
 *   Hence layout schema does not need to do much in that regard.
 * - Various arrays in struct c2_layout_domain (viz. ld_type[], ld_enum[]),
 *   holding registered layout types and enum types, are protected by using
 *   c2_layout_domain::ld_lock, specifically, during registration and
 *   unregistration routines for various layout types and enum types.
 * - Various tables those are part of layout DB, directly or indirectly
 *   pointed by struct c2_ldb_schema, are protected by using
 *   c2_ldb_schema::ls_lock.
 * - The in-memory c2_layout object is protected using c2_layout::l_lock.
 *
 * @subsection Layout-DB-lspec-numa NUMA optimizations
 *
 * <HR>
 * @section Layout-DB-conformance Conformance
 * - I.LAYOUT.SCHEMA.Layid: Layout identifiers are unique globally in
 *   the system, and persistent in the life cycle. It is assumed that the
 *   layout identifiers are assigned by the Layout module and Layout DB module
 *   helps to store those persistently.
 * - I.LAYOUT.SCHEMA.Types: There are multiple layout types for different
 *   purposes: SNS, block map, local raid, de-dup, encryption, compression, etc.
 *   <BR>
 *   Layout DB module supports storing all kinds of layout types supported
 *   currently by the layout module viz. PDCLUST and COMPOSITE.
 *   The framework supports to add other layout types, as required in
 * the future.
 * - I.LAYOUT.SCHEMA.Formulae:
 *    - Parameters:
 *       - In case of PDCLUST layout type using LINEAR enumeration,
 *         linear formula is stored by the Layout DB and substituting
 *         parameters in the stored formula derives the real mapping
 *         information that is the list of COB identifiers.
 *    - Garbage Collection:
 *       - A layout with PDCLUST layout type and with LIST enumeration
 *         is deleted when its last reference is released. Similarlly, a
 *         layout with COMPOSITE layout is deleted when its last reference
 *         is released.
 *       - A layout with PDCLUST layout type and with LINEAR enumeration
 *         method is never deleted and thus can be reused.
 * - I.LAYOUT.SCHEMA.Sub-Layouts: COMPOSITE type of layout is used to
 *     store sub-layouts.
 *
 * <HR>
 * @section Layout-DB-ut Unit Tests
 *
 * Following cases will be tested by unit tests:
 *
 * @test 1) Registering layout types including PDCLUST amd COMPOSITE types.
 *
 * @test 2) Unregistering layout types including PDCLUST amd COMPOSITE types.
 *
 * @test 3) Registering each of LIST and LINEAR enum types.
 *
 * @test 4) Unregistering each of LIST and LINEAR enum types.
 *
 * @test 5) Encode layout with each of layout type and enum types.
 *
 * @test 6) Decode layout with each of layout type and enum types.
 *
 * @test 7) Adding layouts with all the possible combinations of all the
 *          layout types and enumeration types.
 *
 * @test 8) Deleting layouts with all the possible combinations of all the
 *          layout types and enumeration types.
 *
 * @test 9) Updating layouts with all the possible combinations of all the
 *          layout types and enumeration types.
 *
 * @test 10) Reading a layout with all the possible combinations of all the
 *           layout types and enumeration types.
 *
 * @test 11) Checking DB persistence by comparing a layout with the layout read
 *           from the DB, for all the possible combinations of all the layout
 *           types and enumeration types.
 *
 * @test 12) Covering all the negative test cases.
 *
 * @test 13) Covering all the error cases. This will be done after error
 *           injection framework is ready that is being worked upon.
 *
 * <HR>
 * @section Layout-DB-st System Tests
 *
 * System testing will include tests where multiple processes are writing
 * to and reading from the DB at the same time.
 *
 * <HR>
 * @section Layout-DB-O Analysis
 *
 * <HR>
 * @section Layout-DB-ref References
 * - <a href="https://docs.google.com/a/xyratex.com/document/d/15-G-tUZfSuK6lpOuQ_Hpbw1jJ55MypVhzGN2r1yAZ4Y/edit?hl=en_US
">HLD of Layout Schema</a>
 * - <a href="https://docs.google.com/a/xyratex.com/document/d/12olF9CWN35HCkz-ZcEH_c0qoS8fzTxuoTqCCDN9EQR0/edit?hl=en_US#heading=h.gz7460ketfn1">Understanding LayoutSchema</a>
 *
 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"    /* memset() */
#include "lib/vec.h"     /* C2_BUFVEC_INIT_BUF() */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "db/db_common.h" /* c2_db_buf_init() */
#include "layout/layout_internal.h"
#include "layout/layout_db.h"

extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

extern const struct c2_addb_ev ldb_lookup_success;
extern const struct c2_addb_ev ldb_lookup_fail;
extern const struct c2_addb_ev ldb_add_success;
extern const struct c2_addb_ev ldb_add_fail;
extern const struct c2_addb_ev ldb_update_success;
extern const struct c2_addb_ev ldb_update_fail;
extern const struct c2_addb_ev ldb_delete_success;
extern const struct c2_addb_ev ldb_delete_fail;

extern const struct c2_layout_type c2_pdclust_layout_type;
extern const struct c2_layout_enum_type c2_list_enum_type;
extern const struct c2_layout_enum_type c2_linear_enum_type;

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
 * Compare layouts table keys.
 * This is a 3WAY comparison.
 */
static int l_key_cmp(struct c2_table *table,
		     const void *key0, const void *key1)
{
	const uint64_t *lid0 = key0;
	const uint64_t *lid1 = key1;

	return C2_3WAY(*lid0, *lid1);;
}

/**
 * table_ops for layouts table.
 */
static const struct c2_table_ops layouts_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_uint128)
		},
		[TO_REC] = {
			.max_size = ~0
		}
	},
	.key_cmp = l_key_cmp
};

static int schema_invariant(const struct c2_ldb_schema *schema)
{
	return schema != NULL && schema->ls_domain != NULL &&
		schema->ls_dbenv != NULL;
}

/**
 * Maximum possible size for a record in the layouts table (without
 * considering the data in the tables other than layouts) is maintained in
 * c2_ldb_schema::ls_max_recsize.
 * This function updates c2_ldb_schema::ls_max_recsize.
 */
static void max_recsize_update(struct c2_layout_domain *dom)
{
	uint32_t    i;
	c2_bcount_t recsize;
	c2_bcount_t max_recsize = 0;

	C2_PRE(domain_invariant(dom));

	/*
	 * Iterate over all the layout types to find maximum possible recsize.
	 */
	for (i = 0; i < ARRAY_SIZE(dom->ld_type); ++i) {
		if (dom->ld_type[i] == NULL)
			continue;

		recsize = dom->ld_type[i]->lt_ops->lto_max_recsize(dom);
		max_recsize = max64u(max_recsize, recsize);
	}

	dom->ld_schema->ls_max_recsize =  sizeof(struct c2_ldb_rec) +
					  max_recsize;
}

/**
 * Returns actual size for a record in the layouts table (without
 * considering the data in the tables other than layouts).
 */
static c2_bcount_t recsize_get(struct c2_layout_domain *dom,
			       struct c2_layout *l)
{
	c2_bcount_t            recsize;
	struct c2_layout_type *lt;

	C2_PRE(domain_invariant(dom));
	C2_PRE(layout_invariant(l));

	lt = dom->ld_type[l->l_type->lt_id];
	C2_ASSERT(is_layout_type_valid(lt->lt_id, dom));

	recsize = sizeof(struct c2_ldb_rec) +
		  lt->lt_ops->lto_recsize(dom, l);

	C2_POST(recsize <= c2_layout_max_recsize(dom));

	return recsize;
}

/**
 * Write layout record to the layouts table.
 *
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record which could be one of ADD/UPDATE/DELETE.
 */
int ldb_layout_write(enum c2_layout_xcode_op op, uint64_t lid,
		     struct c2_db_pair *pair, c2_bcount_t recsize,
		     struct c2_ldb_schema *schema, struct c2_db_tx *tx)
{
	int   rc;
	void *key_buf = pair->dp_key.db_buf.b_addr;
	void *rec_buf = pair->dp_rec.db_buf.b_addr;

	C2_PRE(schema != NULL);
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
	       op == C2_LXO_DB_DELETE);
	C2_PRE(pair != NULL);
	C2_PRE(key_buf != NULL);
	C2_PRE(rec_buf != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof lid);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= recsize);
	C2_PRE(recsize >= sizeof(struct c2_ldb_rec) &&
	       recsize <= c2_layout_max_recsize(schema->ls_domain));
	C2_PRE(tx != NULL);

	*(uint64_t *)key_buf = lid;

	c2_db_pair_setup(pair, &schema->ls_layouts,
			 key_buf, sizeof lid, rec_buf, recsize);

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

	c2_db_pair_fini(pair);

	return rc;
}

/**
 * Read existing record from the layouts table into the provided area.
 */
static int rec_get(struct c2_layout *l, void *area,
		   struct c2_ldb_schema *schema, struct c2_db_tx *tx)
{
	struct c2_db_pair  pair;
	c2_bcount_t        max_recsize;
	int                rc;

	C2_PRE(schema != NULL);
	C2_PRE(l != NULL);
	C2_PRE(area != NULL);

	max_recsize = c2_layout_max_recsize(schema->ls_domain);

	/*
	 * The max_recsize is never expected to be that large. But still,
	 * since it is being typcasted to uint32_t.
	 */
	C2_ASSERT(max_recsize <= UINT32_MAX);

	c2_db_pair_setup(&pair, &schema->ls_layouts,
			 &l->l_id, sizeof l->l_id,
			 area, (uint32_t)max_recsize);

	rc = c2_table_lookup(tx, &pair);
	C2_ASSERT(rc != -ENOENT);

	c2_db_pair_fini(&pair);

	return rc;
}

/** @} end group LayoutDBDFSInternal */

/**
 * @addtogroup LayoutDBDFS
 * @{
 */

/**
 * Initializes layout domain - Initializes arrays to hold the objects for
 * layout types and enum types.
 */
int c2_layout_domain_init(struct c2_layout_domain *dom)
{
	C2_PRE(dom != NULL);

	C2_ENTRY();

	C2_SET0(dom);

	c2_mutex_init(&dom->ld_lock);

	/*
	 * Can not invoke invariant here since the dom->ld_schema pointer is
	 * not yet set. It will be set once the c2_ldb_schema object associated
	 * with this domain object is initialized.
	 */
	C2_POST(dom->ld_schema == NULL);

	C2_LEAVE();
	return 0;
}

/**
 * Finalizes the layout domain.
 * @pre All the layout types and enum types should be unregistered.
 */
void c2_layout_domain_fini(struct c2_layout_domain *dom)
{
	uint32_t i;

	/*
	 * Can not invoke invariant here since the dom->ld_schema pointer is
	 * expected to be set to NULL during finalization of the c2_ldb_schema
	 * object associated with this domain.
	 */
	C2_PRE(dom != NULL);

	/*
	 * Verify that the schema object associated with this domain has been
	 * finalized prior to this routine being invoked.
	 */
	C2_PRE(dom->ld_schema == NULL);

	C2_ENTRY();

	/* Verify that all the layout types were unregistered. */
	for (i = 0; i < ARRAY_SIZE(dom->ld_type); ++i)
		C2_ASSERT(dom->ld_type[i] == NULL);

	/* Verify that all the enum types were unregistered. */
	for (i = 0; i < ARRAY_SIZE(dom->ld_enum); ++i)
		C2_ASSERT(dom->ld_enum[i] == NULL);

	c2_mutex_fini(&dom->ld_lock);

	C2_LEAVE();
}



/**
 * Initializes layout schema - creates the layouts table.
 * @pre dbenv Caller should have performed c2_dbenv_init() on dbenv.
 */
int c2_ldb_schema_init(struct c2_ldb_schema *schema,
		       struct c2_layout_domain *dom,
		       struct c2_dbenv *dbenv)
{
	int rc;

	C2_PRE(schema != NULL);
	C2_PRE(dom != NULL);
	C2_PRE(dbenv != NULL);

	C2_ENTRY();

	C2_SET0(schema);

	schema->ls_domain = dom;
	schema->ls_dbenv = dbenv;

	c2_mutex_init(&schema->ls_lock);

	rc = c2_table_init(&schema->ls_layouts, schema->ls_dbenv, "layouts",
			   DEFAULT_DB_FLAG, &layouts_table_ops);
	if (rc != 0) {
		layout_log("c2_ldb_schema_init", "c2_table_init() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_func_fail.ae_id,
			   &layout_global_ctx, !LID_APPLICABLE, LID_NONE, rc);

		schema->ls_dbenv = NULL;
		c2_mutex_fini(&schema->ls_lock);
	}

	/* Store pointer to schema in the domain object. */
	dom->ld_schema = schema;

	C2_POST(schema_invariant(schema));

	C2_LEAVE("rc %d", rc);
	return rc;
}

/**
 * Finalizes the layout schema.
 * @pre All the layout types and enum types should be unregistered.
 */
void c2_ldb_schema_fini(struct c2_ldb_schema *schema)
{
	C2_PRE(schema_invariant(schema));

	C2_ENTRY();

	schema->ls_domain->ld_schema = NULL;

	c2_table_fini(&schema->ls_layouts);
	c2_mutex_fini(&schema->ls_lock);
	schema->ls_dbenv = NULL;
	schema->ls_domain = NULL;

	C2_LEAVE();
}

/**
 * Registers all the available layout types and enum types.
 */
int c2_layout_register(struct c2_layout_domain *dom)
{
	int rc;

	C2_PRE(domain_invariant(dom));

	rc = c2_layout_type_register(dom, &c2_pdclust_layout_type);
	if (rc != 0)
		return rc;

	rc = c2_layout_enum_type_register(dom, &c2_list_enum_type);
	if (rc != 0)
		return rc;

	rc = c2_layout_enum_type_register(dom, &c2_linear_enum_type);
	return rc;
}

void c2_layout_unregister(struct c2_layout_domain *dom)
{
	C2_PRE(domain_invariant(dom));

	c2_layout_enum_type_unregister(dom, &c2_list_enum_type);
	c2_layout_enum_type_unregister(dom, &c2_linear_enum_type);

	c2_layout_type_unregister(dom, &c2_pdclust_layout_type);
}

/**
 * Registers a new layout type with the layout types maintained by
 * c2_layout_domain::ld_type[] and initializes type layout specific tables,
 * if applicable.
 */
int c2_layout_type_register(struct c2_layout_domain *dom,
			 const struct c2_layout_type *lt)
{
	int rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);
	C2_PRE(IS_IN_ARRAY(lt->lt_id, dom->ld_type));

	C2_ENTRY("Layout-type-id %lu", (unsigned long)lt->lt_id);

	c2_mutex_lock(&dom->ld_lock);

	C2_ASSERT(dom->ld_type[lt->lt_id] == NULL);
	C2_ASSERT(lt->lt_ops != NULL);

	dom->ld_type[lt->lt_id] = (struct c2_layout_type *)lt;

	/* Get the first reference on this layout type. */
	C2_ASSERT(dom->ld_type_ref_count[lt->lt_id] == 0);
	C2_CNT_INC(dom->ld_type_ref_count[lt->lt_id]);

	/* Allocate type specific schema data. */
	c2_mutex_lock(&dom->ld_schema->ls_lock);

	rc = lt->lt_ops->lto_register(dom->ld_schema, lt);
	if (rc != 0)
		layout_log("c2_layout_type_register", "lto_register() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_func_fail.ae_id,
			   &layout_global_ctx, !LID_APPLICABLE, LID_NONE, rc);

	max_recsize_update(dom);

	c2_mutex_unlock(&dom->ld_schema->ls_lock);

	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Layout-type-id %lu, rc %d", (unsigned long)lt->lt_id, rc);
	return rc;
}

/**
 * Unregisters a layout type from the layout types maintained by
 * c2_layout_domain::ld_type[] and finalizes type layout specific tables,
 * if applicable.
 */
void c2_layout_type_unregister(struct c2_layout_domain *dom,
			    const struct c2_layout_type *lt)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);
	C2_PRE(dom->ld_type[lt->lt_id] == lt);

	C2_ENTRY("Layout-type-id %lu", (unsigned long)lt->lt_id);

	c2_mutex_lock(&dom->ld_lock);

	c2_mutex_lock(&dom->ld_schema->ls_lock);

	lt->lt_ops->lto_unregister(dom->ld_schema, lt);
	max_recsize_update(dom);

	c2_mutex_unlock(&dom->ld_schema->ls_lock);

	/* Release the last reference on this layout type. */
	C2_ASSERT(dom->ld_type_ref_count[lt->lt_id] == 1);
	C2_CNT_DEC(dom->ld_type_ref_count[lt->lt_id]);

	dom->ld_type[lt->lt_id] = NULL;
	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Layout-type-id %lu", (unsigned long)lt->lt_id);
}

/**
 * Registers a new enumeration type with the enumeration types
 * maintained by c2_layout_domain::ld_enum[] and initializes enum type specific
 * tables, if applicable.
 */
int c2_layout_enum_type_register(struct c2_layout_domain *dom,
				 const struct c2_layout_enum_type *let)
{
	int rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);
	C2_PRE(IS_IN_ARRAY(let->let_id, dom->ld_enum));

	C2_ENTRY("Enum_type_id %lu", (unsigned long)let->let_id);

	c2_mutex_lock(&dom->ld_lock);

	C2_ASSERT(dom->ld_enum[let->let_id] == NULL);
	C2_ASSERT(let->let_ops != NULL);

	dom->ld_enum[let->let_id] = (struct c2_layout_enum_type *)let;

	/* Get the first reference on this enum type. */
	C2_CNT_INC(dom->ld_enum_ref_count[let->let_id]);
	C2_ASSERT(dom->ld_enum_ref_count[let->let_id] == DEFAULT_REF_COUNT);

	/* Allocate enum type specific schema data. */
	c2_mutex_lock(&dom->ld_schema->ls_lock);

	rc = let->let_ops->leto_register(dom->ld_schema, let);
	if (rc != 0)
		layout_log("c2_layout_enum_type_register",
			   "leto_register() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_func_fail.ae_id,
			   &layout_global_ctx, !LID_APPLICABLE, LID_NONE, rc);

	max_recsize_update(dom);

	c2_mutex_unlock(&dom->ld_schema->ls_lock);

	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Enum_type_id %lu, rc %d", (unsigned long)let->let_id, rc);
	return rc;
}

/**
 * Unregisters an enumeration type from the enumeration types
 * maintained by c2_layout_domain::ld_enum[] and finalizes enum type
 * specific tables, if applicable.
 */
void c2_layout_enum_type_unregister(struct c2_layout_domain *dom,
				    const struct c2_layout_enum_type *let)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);
	C2_PRE(dom->ld_enum[let->let_id] == let);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)let->let_id);

	c2_mutex_lock(&dom->ld_lock);

	c2_mutex_lock(&dom->ld_schema->ls_lock);

	let->let_ops->leto_unregister(dom->ld_schema, let);
	max_recsize_update(dom);

	c2_mutex_unlock(&dom->ld_schema->ls_lock);

	/* Release the last reference on this enum type. */
	C2_ASSERT(dom->ld_enum_ref_count[let->let_id] == DEFAULT_REF_COUNT);
	C2_CNT_DEC(dom->ld_enum_ref_count[let->let_id]);

	dom->ld_enum[let->let_id] = NULL;
	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Enum_type_id %lu", (unsigned long)let->let_id);
}

/**
 * Looks up a persistent layout record with the specified layout_id, and
 * its related information from the relevant tables.
 *
 * @param pair A c2_db_pair sent by the caller along with having set
 * pair->dp_key.db_buf and pair->dp_rec.db_buf. This is to leave the buffer
 * allocation with the caller.
 *
 * Regarding the size of the pair->dp_rec.db_buf:
 * The buffer size should be large enough to contain the data that is to be
 * read specifically from the layouts table. It means it needs to be at the
 * most the size returned by c2_layout_max_recsize().
 *
 * @post Layout object is built internally (along with enumeration object being
 * built if applicable). Hence, user needs to finalize the layout object when
 * done with the use. It can be accomplished by performing l->l_ops->lo_fini(l).
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
	c2_bcount_t              max_recsize;
	c2_bcount_t              recsize;
	void                    *key_buf = pair->dp_key.db_buf.b_addr;
	void                    *rec_buf = pair->dp_rec.db_buf.b_addr;

	C2_PRE(schema_invariant(schema));
	C2_PRE(lid != LID_NONE);
	C2_PRE(pair != NULL);
	C2_PRE(key_buf != NULL);
	C2_PRE(rec_buf != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof lid);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_ldb_rec));
	C2_PRE(tx != NULL);
	C2_PRE(out != NULL && *out == NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	c2_mutex_lock(&schema->ls_lock);

	max_recsize = c2_layout_max_recsize(schema->ls_domain);

	recsize = pair->dp_rec.db_buf.b_nob <= max_recsize ?
		  pair->dp_rec.db_buf.b_nob : max_recsize;

	*(uint64_t *)key_buf = lid;
	memset(rec_buf, 0, pair->dp_rec.db_buf.b_nob);

	c2_db_pair_setup(pair, &schema->ls_layouts,
			 key_buf, sizeof lid, rec_buf, recsize);

	rc = c2_table_lookup(tx, pair);
	if (rc != 0) {
		layout_log("c2_ldb_lookup", "c2_table_lookup() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   ldb_lookup_fail.ae_id,
			   &layout_global_ctx, LID_APPLICABLE, lid, rc);
		goto out;
	}

	bv =  (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&rec_buf, &recsize);

	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_decode(schema->ls_domain, lid, &cur, C2_LXO_DB_LOOKUP,
			      schema, tx, out);
	if (rc != 0) {
		layout_log("c2_ldb_lookup", "c2_layout_decode() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   ldb_lookup_fail.ae_id,
			   &layout_global_ctx, LID_APPLICABLE, lid, rc);
		goto out;
	}

	layout_log("c2_ldb_lookup", "",
		   PRINT_ADDB_MSG, !PRINT_TRACE_MSG,
		   ldb_lookup_success.ae_id,
		   &(*out)->l_addb, LID_APPLICABLE, lid, rc);

out:
	c2_db_pair_fini(pair);
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
 * allocation with the caller.
 *
 * Regarding the size of the pair->dp_rec.db_buf:
 * The buffer size should be large enough to contain the data that is to be
 * written specifically to the layouts table. It means it needs to be at the
 * most the size returned by c2_layout_max_recsize().
 */
int c2_ldb_add(struct c2_ldb_schema *schema,
	       struct c2_layout *l,
	       struct c2_db_pair *pair,
	       struct c2_db_tx *tx)
{
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	c2_bcount_t              recsize;
	int                      rc;

	C2_PRE(schema_invariant(schema));
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

	rc = c2_layout_encode(schema->ls_domain, l, C2_LXO_DB_ADD,
			      schema, tx, NULL, &cur);
	if (rc != 0) {
		layout_log("c2_ldb_add", "c2_layout_encode() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   ldb_add_fail.ae_id,
			   &l->l_addb, LID_APPLICABLE, l->l_id, rc);
		goto out;
	}

	recsize = recsize_get(schema->ls_domain, l);
	rc = ldb_layout_write(C2_LXO_DB_ADD, l->l_id, pair, recsize,
			      schema, tx);
	if (rc != 0) {
		layout_log("c2_ldb_add", "ldb_layout_write() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   ldb_add_fail.ae_id,
			   &l->l_addb, LID_APPLICABLE, l->l_id, rc);
		goto out;
	}

	layout_log("c2_ldb_add", "",
		   PRINT_ADDB_MSG, !PRINT_TRACE_MSG,
		   ldb_add_success.ae_id,
		   &l->l_addb, LID_APPLICABLE, l->l_id, rc);
out:
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
 * allocation with the caller.
 *
 * Regarding the size of the pair->dp_rec.db_buf:
 * The buffer size should be large enough to contain the data that is to be
 * written specifically to the layouts table. It means it needs to be at the
 * most the size returned by c2_layout_max_recsize().
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
	c2_bcount_t              recsize;
	int                      rc;

	C2_PRE(schema_invariant(schema));
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
	 * Get the existing record from the layouts table. It is used to
	 * ensure that nothing other than l_ref gets updated for an existing
	 * layout record.
	 */
	recsize = c2_layout_max_recsize(schema->ls_domain);
	oldrec_area = c2_alloc(recsize);
	if (oldrec_area == NULL) {
		rc = -ENOMEM;
		layout_log("c2_ldb_update", "c2_alloc() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_oom.ae_id,
			   &l->l_addb, LID_APPLICABLE, l->l_id, rc);
		goto out;
	}

	rc = rec_get(l, oldrec_area, schema, tx);
	if (rc != 0) {
		layout_log("c2_ldb_update", "c2_table_lookup() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   ldb_update_fail.ae_id,
			   &l->l_addb, LID_APPLICABLE, l->l_id, rc);
		goto out;
	}

	oldrec_bv = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&oldrec_area,
							 &recsize);
	c2_bufvec_cursor_init(&oldrec_cur, &oldrec_bv);

	/* Now, proceed to update the layout. */
	memset(pair->dp_rec.db_buf.b_addr, 0, pair->dp_rec.db_buf.b_nob);

	bv =  (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
						   &pair->dp_rec.db_buf.b_nob);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_encode(schema->ls_domain, l, C2_LXO_DB_UPDATE,
			      schema, tx,
			      &oldrec_cur, &cur);
	if (rc != 0) {
		layout_log("c2_ldb_update", "c2_layout_encode() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   ldb_update_fail.ae_id,
			   &l->l_addb, LID_APPLICABLE, l->l_id, rc);
		goto out;
	}

	recsize = recsize_get(schema->ls_domain, l);
	rc = ldb_layout_write(C2_LXO_DB_UPDATE, l->l_id, pair, recsize,
			      schema, tx);
	if (rc != 0) {
		layout_log("c2_ldb_update", "c2_table_update() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   ldb_update_fail.ae_id,
			   &l->l_addb, LID_APPLICABLE, l->l_id, rc);
		goto out;
	}

	layout_log("c2_ldb_update", "",
		   PRINT_ADDB_MSG, !PRINT_TRACE_MSG,
		   ldb_update_success.ae_id,
		   &l->l_addb, LID_APPLICABLE, l->l_id, rc);

out:
	c2_free(oldrec_area);
	c2_mutex_unlock(&schema->ls_lock);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

/**
 * Deletes a layout record with given layout id and its related information
 * from the relevant tables.
 *
 * @param pair A c2_db_pair sent by the caller along with having set
 * pair->dp_key.db_buf and pair->dp_rec.db_buf. This is to leave the buffer
 * allocation with the caller.
 *
 * Regarding the size of the pair->dp_rec.db_buf:
 * The buffer size should be large enough to contain the data that is to be
 * written specifically to the layouts table. It means it needs to be at the
 * most the size returned by c2_layout_max_recsize().
 */
int c2_ldb_delete(struct c2_ldb_schema *schema,
		  struct c2_layout *l,
		  struct c2_db_pair *pair,
		  struct c2_db_tx *tx)
{
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	c2_bcount_t              recsize;
	int                      rc;

	C2_PRE(schema_invariant(schema));
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

	rc = c2_layout_encode(schema->ls_domain, l, C2_LXO_DB_DELETE,
			      schema, tx, NULL, &cur);
	if (rc != 0) {
		layout_log("c2_ldb_delete", "c2_layout_encode() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   ldb_delete_fail.ae_id,
			   &l->l_addb, LID_APPLICABLE, l->l_id, rc);
		goto out;
	}

	recsize = recsize_get(schema->ls_domain, l);
	rc = ldb_layout_write(C2_LXO_DB_DELETE, l->l_id, pair, recsize,
			      schema, tx);
	if (rc != 0) {
		layout_log("c2_ldb_delete", "c2_table_delete() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   ldb_delete_fail.ae_id,
			   &l->l_addb, LID_APPLICABLE, l->l_id, rc);
		goto out;
	}

	layout_log("c2_ldb_delete", "",
		   PRINT_ADDB_MSG, !PRINT_TRACE_MSG,
		   ldb_delete_success.ae_id,
		   &l->l_addb, LID_APPLICABLE, l->l_id, rc);

out:
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
