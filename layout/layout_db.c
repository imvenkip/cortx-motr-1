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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

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
 * @subsection Layout-DB-lspec-schema-comp_layout_ext_map
 * Table comp_layout_ext_map
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
 * @test 13) Covering all the error cases.
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

extern const struct c2_addb_ev layout_lookup_fail;
extern const struct c2_addb_ev layout_add_fail;
extern const struct c2_addb_ev layout_update_fail;
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

static int pair_init(struct c2_db_pair *pair,
		     struct c2_layout *l,
		     struct c2_db_tx *tx,
		     enum c2_layout_xcode_op op,
		     c2_bcount_t recsize)
{
	void                    *key_buf = pair->dp_key.db_buf.b_addr;
	void                    *rec_buf = pair->dp_rec.db_buf.b_addr;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  rec_cur;
	int                      rc;

	C2_PRE(key_buf != NULL);
	C2_PRE(rec_buf != NULL);
	C2_PRE(pair->dp_key.db_buf.b_nob == sizeof l->l_id);
	C2_PRE(pair->dp_rec.db_buf.b_nob >= recsize);
	C2_PRE(C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_DB_ADD,
			  C2_LXO_DB_UPDATE, C2_LXO_DB_DELETE)));
	C2_PRE(recsize >= sizeof(struct c2_layout_rec));

	*(uint64_t *)key_buf = l->l_id;
	memset(pair->dp_rec.db_buf.b_addr, 0, pair->dp_rec.db_buf.b_nob);
	c2_db_pair_setup(pair, &l->l_dom->ld_layouts,
			 key_buf, sizeof l->l_id,
			 rec_buf, recsize);
	if (op == C2_LXO_DB_LOOKUP)
		rc = 0;
	else {
		bv = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&rec_buf,
						  &pair->dp_rec.db_buf.b_nob);
		c2_bufvec_cursor_init(&rec_cur, &bv);

		rc = c2_layout_encode(l, op, tx, &rec_cur);
		if (rc != 0) {
			c2_layout__log("pair_init", "c2_layout_encode() failed",
				       &c2_addb_func_fail, &l->l_addb,
				       l->l_id, rc);
			c2_db_pair_fini(pair);
		}
	}
	return rc;
}


/** @} end group LayoutDBDFSInternal */

/**
 * @addtogroup LayoutDBDFS
 * @{
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
	struct c2_layout        *l;
	struct c2_layout        *ghost;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(lid > 0);
	C2_PRE(lt != NULL);
	C2_PRE(tx != NULL);
	C2_PRE(pair != NULL);
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	if (dom->ld_type[lt->lt_id] != lt) {
		c2_layout__log("c2_layout_lookup", "Unregistered layout type",
			       &layout_lookup_fail, &layout_global_ctx,
			       lid, -EPROTO);
		return -EPROTO;
	}

	c2_mutex_lock(&dom->ld_lock);
	l = c2_layout__list_lookup(dom, lid, true);
	c2_mutex_unlock(&dom->ld_lock);
	if (l != NULL) {
		/*
		 * Layout object exists in memory and c2_layout__list_lookup()
		 * has now acquired a reference on it.
		 */
		*out = l;
		C2_POST(c2_layout__invariant(*out));
		C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, 0);
		return 0;
	}

	/* Allocate outside of the domain lock to improve concurrency. */
	rc = lt->lt_ops->lto_allocate(dom, lid, &l);
	if (rc != 0) {
		c2_layout__log("c2_layout_lookup", "lto_allocate() failed",
			       &layout_lookup_fail, &layout_global_ctx,
			       lid, rc);
		return rc;
	}
	/* Here, lto_allocate() has locked l->l_lock. */

	/* Re-check for possible concurrent layout creation. */
	c2_mutex_lock(&dom->ld_lock);
	ghost = c2_layout__list_lookup(dom, lid, true);
	if (ghost != NULL) {
		/*
		 * Another instance of the layout with the same layout id
		 * "ghost" was created while the domain lock was released.
		 * Use it. c2_layout__list_lookup() has now acquired a
		 * reference on "ghost".
		 */
		c2_mutex_unlock(&dom->ld_lock);
		l->l_ops->lo_delete(l);

		/* Wait for possible decoding completion. */
		c2_mutex_lock(&ghost->l_lock);
		c2_mutex_unlock(&ghost->l_lock);

		*out = ghost;
		C2_POST(c2_layout__invariant(*out));
		C2_POST(l->l_ref > 0);
		C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, 0);
		return 0;
	}
	c2_mutex_unlock(&dom->ld_lock);

	max_recsize = c2_layout_max_recsize(dom);
	recsize = pair->dp_rec.db_buf.b_nob <= max_recsize ?
		  pair->dp_rec.db_buf.b_nob : max_recsize;
	rc = pair_init(pair, l, tx, C2_LXO_DB_LOOKUP, recsize);
	C2_ASSERT(rc == 0);
	rc = c2_table_lookup(tx, pair);
	if (rc != 0) {
		/* Error covered in UT. */
		l->l_ops->lo_delete(l);
		c2_layout__log("c2_layout_lookup", "c2_table_lookup() failed",
			       &layout_lookup_fail, &layout_global_ctx,
			       lid, rc);
		goto out;
	}

	bv = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&pair->dp_rec.db_buf.b_addr,
						  &recsize);
	c2_bufvec_cursor_init(&cur, &bv);
	rc = c2_layout_decode(l, &cur, C2_LXO_DB_LOOKUP, tx);
	if (rc != 0) {
		/* Error covered in UT. */
		l->l_ops->lo_delete(l);
		c2_layout__log("c2_layout_lookup", "c2_layout_decode() failed",
			       &layout_lookup_fail, &layout_global_ctx,
			       lid, rc);
		goto out;
	}
	*out = l;
	C2_CNT_INC(l->l_ref);
	C2_POST(c2_layout__invariant(*out) && l->l_ref > 0);
	c2_mutex_unlock(&l->l_lock);
out:
	c2_db_pair_fini(pair);
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

int c2_layout_add(struct c2_layout *l,
		  struct c2_db_tx *tx,
		  struct c2_db_pair *pair)
{
	c2_bcount_t recsize;
	int         rc;

	C2_PRE(c2_layout__invariant(l));
	C2_PRE(tx != NULL);
	C2_PRE(pair != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	c2_mutex_lock(&l->l_lock);
	recsize = l->l_ops->lo_recsize(l);
	rc = pair_init(pair, l, tx, C2_LXO_DB_ADD, recsize);
	if (rc == 0) {
		rc = c2_table_insert(tx, pair);
		if (rc != 0)
			c2_layout__log("c2_layout_add",
				       "c2_table_insert() failed",
				       &layout_add_fail, &l->l_addb,
				       l->l_id, rc);
		c2_db_pair_fini(pair);
	} else
		c2_layout__log("c2_layout_add",
			       "pair_init() failed",
			       &layout_add_fail, &l->l_addb,
			       l->l_id, rc);
	c2_mutex_unlock(&l->l_lock);
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

int c2_layout_update(struct c2_layout *l,
		     struct c2_db_tx *tx,
		     struct c2_db_pair *pair)
{
	c2_bcount_t recsize;
	int         rc;

	C2_PRE(c2_layout__invariant(l));
	C2_PRE(tx != NULL);
	C2_PRE(pair != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	c2_mutex_lock(&l->l_lock);
	recsize = l->l_ops->lo_recsize(l);
	rc = pair_init(pair, l, tx, C2_LXO_DB_UPDATE, recsize);
	if (rc == 0) {
		rc = c2_table_update(tx, pair);
		if (rc != 0)
			c2_layout__log("c2_layout_update",
				       "c2_table_update() failed",
				       &layout_update_fail, &l->l_addb,
				       l->l_id, rc);
		c2_db_pair_fini(pair);
	} else
		c2_layout__log("c2_layout_update",
			       "pair_init() failed",
			       &layout_update_fail, &l->l_addb,
			       l->l_id, rc);
	c2_mutex_unlock(&l->l_lock);
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

int c2_layout_delete(struct c2_layout *l,
		     struct c2_db_tx *tx,
		     struct c2_db_pair *pair)
{
	c2_bcount_t recsize;
	int         rc;

	C2_PRE(c2_layout__invariant(l));
	C2_PRE(tx != NULL);
	C2_PRE(pair != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	c2_mutex_lock(&l->l_lock);
	recsize = l->l_ops->lo_recsize(l);
	rc = pair_init(pair, l, tx, C2_LXO_DB_DELETE, recsize);
	if (rc == 0) {
		rc = c2_table_delete(tx, pair);
		if (rc != 0)
			c2_layout__log("c2_layout_delete",
				       "c2_table_delete() failed",
				       &layout_delete_fail, &l->l_addb,
				       l->l_id, rc);
		c2_db_pair_fini(pair);
	} else
		c2_layout__log("c2_layout_delete",
			       "pair_init() failed",
			       &layout_delete_fail, &l->l_addb,
			       l->l_id, rc);
	c2_mutex_unlock(&l->l_lock);
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
