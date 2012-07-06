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
 * Purpose of the Layout-DB DLD @n
 * The purpose of the Layout-DB Detailed Level Design (DLD) specification is to:
 * - Refine the higher level design
 * - To be verified by inspectors and architects
 * - To guide the coding phase
 *
 * <HR>
 * @section Layout-DB-def Definitions
 *   - COB: COB is component object and is defined at
 *   <a href="https://docs.google.com/a/xyratex.com/spreadsheet/ccc?
 *    key=0Ajg1HFjUZcaZdEpJd0tmM3MzVy1lMG41WWxjb0t4QkE&hl=en_US#gid=0">
 *    C2 Glossary</a>
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
 * - It is assumed that the problem of coruption is going to be attacked
 *   generically at the lower layers (db and fop) transparently, instead of
 *   adding magic numbers and check-sums in every module. Thus the input to
 *   Layout DB APIs which is either a layout or a FOP buffer in most of the
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
 *    - reference_count
 *    - pool_id
 *    - layout_type_specific_data (optional)
 *
 * @endverbatim
 *
 * layout_type_specific_data field is used to store layout type or layout enum
 * type specific data. Structure of this field varies accordingly. For example:
 * - In case of a layout with PDCLUST layout type, the structure
 *   c2_layout_pdclust_rec is used to store attributes like enumeration type
 *   id, N, K, P.
 * - In case of a layout with LIST enum type, an array of c2_fid
 *   structure with size LDB_MAX_INLINE_COB_ENTRIES is used to store a few COB
 *   entries inline into the layouts table itself.
 * - It is possible that some layouts do not need to store any layout type or
 *   layout enum type specific data in the layouts table. For example, a
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
 * cob_index for the first entry in this table will be the continuation of the
 * index from the array of c2_fid structures stored inline in the layouts
 * table.
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
 * This module does follow state machine ind of a design. Hence, this section
 * is not applicable.
 *
 * @subsection Layout-DB-lspec-thread Threading and Concurrency Model
 * See @ref layout-thread.
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
 *   @n
 *   Layout DB module supports storing all kinds of layout types supported
 *   currently by the layout module viz. PDCLUST and COMPOSITE.
 *   The framework supports to add other layout types, as required in
 *   the future.
 * - I.LAYOUT.SCHEMA.Formulae:
 *    - Parameters:
 *       - In case of PDCLUST layout type using LINEAR enumeration,
 *         linear formula is stored by the Layout DB and substituting
 *         parameters in the stored formula derives the real mapping
 *         information that is the list of COB identifiers.
 *    - Garbage Collection:
 *       - A layout is deleted when its last reference is released.
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
 * - <a href="https://docs.google.com/a/xyratex.com/document/d/
 *    15-G-tUZfSuK6lpOuQ_Hpbw1jJ55MypVhzGN2r1yAZ4Y/edit?hl=en_US">
 *    HLD of Layout Schema</a>
 * - <a href="https://docs.google.com/a/xyratex.com/document/d/
 *    12olF9CWN35HCkz-ZcEH_c0qoS8fzTxuoTqCCDN9EQR0/edit?hl=en_US#
 *    heading=h.gz7460ketfn1">Understanding LayoutSchema</a>
 *
 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"  /* memset() */
#include "lib/vec.h"   /* C2_BUFVEC_INIT_BUF() */
#include "lib/finject.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "layout/layout_internal.h"
#include "layout/layout_db.h"

extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

extern const struct c2_addb_ev layout_lookup_success;
extern const struct c2_addb_ev layout_lookup_fail;
extern const struct c2_addb_ev layout_add_success;
extern const struct c2_addb_ev layout_add_fail;
extern const struct c2_addb_ev layout_update_success;
extern const struct c2_addb_ev layout_update_fail;
extern const struct c2_addb_ev layout_delete_success;
extern const struct c2_addb_ev layout_delete_fail;

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
 * Returns actual size for a record in the layouts table (without
 * considering the data in the tables other than layouts).
 */
static c2_bcount_t recsize_get(const struct c2_layout *l)
{
	c2_bcount_t            recsize;

	C2_PRE(c2_layout__invariant(l));
	recsize = sizeof(struct c2_layout_rec) + l->l_ops->lo_recsize(l);
	C2_POST(recsize <= c2_layout_max_recsize(l->l_dom));
	return recsize;
}

/**
 * Write layout record to the layouts table.
 *
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record which could be one of ADD/UPDATE/DELETE.
 */
int layout_write(const struct c2_layout *l,
		 struct c2_db_tx *tx,
		 enum c2_layout_xcode_op op,
		 struct c2_db_pair *pair,
		 c2_bcount_t recsize)
{
	int   rc;
	void *key_buf = pair->dp_key.db_buf.b_addr;
	void *rec_buf = pair->dp_rec.db_buf.b_addr;

	C2_PRE(l != NULL);
	C2_PRE(tx != NULL);
	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE, C2_LXO_DB_DELETE)));
	C2_PRE(pair != NULL);
	C2_PRE(key_buf != NULL);
	C2_PRE(rec_buf != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof l->l_id);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= recsize);
	C2_PRE(recsize >= sizeof(struct c2_layout_rec) &&
	       recsize <= c2_layout_max_recsize(l->l_dom));

	*(uint64_t *)key_buf = l->l_id;
	c2_db_pair_setup(pair, &l->l_dom->ld_schema.ls_layouts,
			 key_buf, sizeof l->l_id, rec_buf, recsize);

	/*
	 * ADDB records covering the failure of c2_table_insert(),
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
static int rec_get(struct c2_db_tx *tx, struct c2_layout *l,
		   void *area, size_t max_recsize)
{
	struct c2_db_pair  pair;
	int                rc;

	C2_PRE(tx != NULL);
	C2_PRE(l != NULL);
	C2_PRE(area != NULL);
	/*
	 * The max_recsize is never expected to be that large. But still,
	 * since it is being type-casted here to uint32_t.
	 */
	C2_PRE(max_recsize <= UINT32_MAX);

	c2_db_pair_setup(&pair, &l->l_dom->ld_schema.ls_layouts,
			 &l->l_id, sizeof l->l_id,
			 area, (uint32_t)max_recsize);

	/*
	 * ADDB records covering the failure of c2_table_lookup() is added
	 * into the caller of this routine.
	 */
	rc = c2_table_lookup(tx, &pair);

	c2_db_pair_fini(&pair);
	return rc;
}

/** @} end group LayoutDBDFSInternal */

/**
 * @addtogroup LayoutDBDFS
 * @{
 */

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
 * built if applicable). User is expected to add rererence/s to this layout
 * object while using it. Releasing the last reference will finalise the layout
 * object by freeing it.
 */
int c2_layout_lookup(struct c2_layout_domain *dom,
		     uint64_t lid,
		     struct c2_layout_type *lt,
		     struct c2_db_tx *tx,
		     struct c2_db_pair *pair,
		     struct c2_layout **out)
{
	int                      rc;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	c2_bcount_t              max_recsize;
	c2_bcount_t              recsize;
	void                    *key_buf = pair->dp_key.db_buf.b_addr;
	void                    *rec_buf = pair->dp_rec.db_buf.b_addr;
	struct c2_layout        *l;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(lid != LID_NONE);
	C2_PRE(c2_layout_find(dom, lid) == NULL);
	C2_PRE(lt != NULL);
	C2_PRE(dom->ld_type[lt->lt_id] == lt);
	C2_PRE(tx != NULL);
	C2_PRE(pair != NULL);
	C2_PRE(key_buf != NULL);
	C2_PRE(rec_buf != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof lid);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_layout_rec));
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	c2_mutex_lock(&dom->ld_schema.ls_lock);

	rc = lt->lt_ops->lto_allocate(dom, lid, &l);
	if (rc != 0) {
		c2_layout__log("c2_layout_lookup", "lto_allocate() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_lookup_fail, &layout_global_ctx,
			       lid, rc);
		goto out;
	}
	/* todo Check carefully if the layout should be added to list
	 * as a part of lto_allocate() or as a part of c2_layout__populate. */

	C2_ASSERT(c2_layout__allocated_invariant(l)); //todo remove

	max_recsize = c2_layout_max_recsize(dom);
	recsize = pair->dp_rec.db_buf.b_nob <= max_recsize ?
		  pair->dp_rec.db_buf.b_nob : max_recsize;

	*(uint64_t *)key_buf = lid;
	memset(rec_buf, 0, pair->dp_rec.db_buf.b_nob);
	c2_db_pair_setup(pair, &dom->ld_schema.ls_layouts,
			 key_buf, sizeof lid, rec_buf, recsize);

	rc = c2_table_lookup(tx, pair);
	if (rc != 0) {
		l->l_ops->lo_delete(l); //todo
		c2_layout__log("c2_layout_lookup", "c2_table_lookup() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_lookup_fail, &layout_global_ctx,
			       lid, rc);
		goto out;
	}

	bv = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&rec_buf, &recsize);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_decode(l, C2_LXO_DB_LOOKUP, tx, &cur);
	if (rc != 0) {
		l->l_ops->lo_delete(l);
		c2_layout__log("c2_layout_lookup", "c2_layout_decode() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_lookup_fail, &layout_global_ctx,
			       lid, rc);
		goto out;
	}
	*out = l;
out:
	c2_db_pair_fini(pair);
	c2_mutex_unlock(&dom->ld_schema.ls_lock);
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
int c2_layout_add(struct c2_layout *l,
		  struct c2_db_tx *tx,
		  struct c2_db_pair *pair)
{
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	c2_bcount_t              recsize;
	int                      rc;

	C2_PRE(c2_layout__invariant(l));
	C2_PRE(tx != NULL);
	C2_PRE(pair != NULL);
	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof l->l_id);
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_layout_rec));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&l->l_dom->ld_schema.ls_lock);

	memset(pair->dp_rec.db_buf.b_addr, 0, pair->dp_rec.db_buf.b_nob);
	bv = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
						  &pair->dp_rec.db_buf.b_nob);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_encode(l, C2_LXO_DB_ADD, tx, NULL, &cur);
	if (rc != 0) {
		c2_layout__log("c2_layout_add", "c2_layout_encode() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_add_fail, &l->l_addb, l->l_id, rc);
		goto out;
	}

	recsize = recsize_get(l);
	rc = layout_write(l, tx, C2_LXO_DB_ADD, pair, recsize);
	if (rc != 0)
		c2_layout__log("c2_layout_add", "layout_write() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_add_fail, &l->l_addb, l->l_id, rc);

out:
	c2_mutex_unlock(&l->l_dom->ld_schema.ls_lock);
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
int c2_layout_update(struct c2_layout *l,
		     struct c2_db_tx *tx,
		     struct c2_db_pair *pair)
{
	void                    *oldrec_area;
	struct c2_bufvec         oldrec_bv;
	struct c2_bufvec_cursor  oldrec_cur;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	c2_bcount_t              recsize;
	int                      rc;

	C2_PRE(c2_layout__invariant(l));
	C2_PRE(tx != NULL);
	C2_PRE(pair != NULL);
	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof l->l_id);
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_layout_rec));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&l->l_dom->ld_schema.ls_lock);

	/*
	 * Get the existing record from the layouts table. It is used to
	 * ensure that nothing other than l_ref gets updated for an existing
	 * layout record.
	 */
	recsize = c2_layout_max_recsize(l->l_dom);
	oldrec_area = c2_alloc(recsize);
	if (oldrec_area == NULL) {
		rc = -ENOMEM;
		c2_layout__log("c2_layout_update", "c2_alloc() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_oom, &l->l_addb, l->l_id, rc);
		goto out;
	}

	rc = rec_get(tx, l, oldrec_area, recsize);
	if (rc != 0) {
		c2_layout__log("c2_layout_update", "c2_table_lookup() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_update_fail, &l->l_addb, l->l_id, rc);
		goto out;
	}

	oldrec_bv = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&oldrec_area,
							 &recsize);
	c2_bufvec_cursor_init(&oldrec_cur, &oldrec_bv);

	/* Now, proceed to update the layout. */
	memset(pair->dp_rec.db_buf.b_addr, 0, pair->dp_rec.db_buf.b_nob);

	bv = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
						  &pair->dp_rec.db_buf.b_nob);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_encode(l, C2_LXO_DB_UPDATE, tx, &oldrec_cur, &cur);
	if (rc != 0) {
		c2_layout__log("c2_layout_update", "c2_layout_encode() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_update_fail, &l->l_addb, l->l_id, rc);
		goto out;
	}

	recsize = recsize_get(l);
	rc = layout_write(l, tx, C2_LXO_DB_UPDATE, pair, recsize);
	if (rc != 0)
		c2_layout__log("c2_layout_update", "c2_table_update() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_update_fail, &l->l_addb, l->l_id, rc);
out:
	c2_free(oldrec_area);
	c2_mutex_unlock(&l->l_dom->ld_schema.ls_lock);
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
int c2_layout_delete(struct c2_layout *l,
		     struct c2_db_tx *tx,
		     struct c2_db_pair *pair)
{
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	c2_bcount_t              recsize;
	int                      rc;

	C2_PRE(tx != NULL);
	C2_PRE(pair != NULL);
	C2_PRE(pair->dp_key.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof l->l_id);
	C2_PRE(pair->dp_rec.db_buf.b_addr != NULL);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= sizeof(struct c2_layout_rec));
	C2_PRE(c2_layout__invariant(l));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&l->l_dom->ld_schema.ls_lock);

	memset(pair->dp_rec.db_buf.b_addr, 0, pair->dp_rec.db_buf.b_nob);

	bv = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
						  &pair->dp_rec.db_buf.b_nob);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = c2_layout_encode(l, C2_LXO_DB_DELETE, tx, NULL, &cur);
	if (rc != 0) {
		c2_layout__log("c2_layout_delete", "c2_layout_encode() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_delete_fail, &l->l_addb, l->l_id, rc);
		goto out;
	}

	recsize = recsize_get(l);
	rc = layout_write(l, tx, C2_LXO_DB_DELETE, pair, recsize);
	if (rc != 0)
		c2_layout__log("c2_layout_delete", "c2_table_delete() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &layout_delete_fail, &l->l_addb, l->l_id, rc);
out:
	c2_mutex_unlock(&l->l_dom->ld_schema.ls_lock);
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
