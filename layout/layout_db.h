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

#ifndef __COLIBRI_LAYOUT_LAYOUT_DB_H__
#define __COLIBRI_LAYOUT_LAYOUT_DB_H__

/**
 * @page Layout-DB-fspec Layout DB Functional Specification
 * Layout DB Module is used by the Layout module to make persistent records for
 * the layout entries created and used.
 *
 * This section describes the data structures exposed and the external
 * interfaces of the Layout DB module and it briefly identifies the users of
 * these interfaces so as to explain how to use this module.
 *
 * - @ref Layout-DB-fspec-ds
 * - @ref Layout-DB-fspec-sub
 * - @ref Layout-DB-fspec-usecases
 * - @ref LayoutDBDFS "Detailed Functional Specification"
 *
 * @section Layout-DB-fspec-ds Data Structures
 * - struct c2_ldb_schema
 * - struct c2_ldb_rec
 *
 * @section Layout-DB-fspec-sub Subroutines
 * - int c2_ldb_schema_init(struct c2_ldb_schema *schema, struct c2_dbenv *db)
 * - void c2_ldb_schema_fini(struct c2_ldb_schema *schema)
 * - void c2_ldb_type_register(struct c2_ldb_schema *schema, const struct c2_layout_type *lt)
 * - void c2_ldb_type_unregister(struct c2_ldb_schema *schema, const struct c2_layout_type *lt)
 * - void c2_ldb_enum_register(struct c2_ldb_schema *schema, const struct c2_layout_enum_type *et)
 * - void c2_ldb_enum_unregister(struct c2_ldb_schema *schema, const struct c2_layout_enum_type *et)
 * - int c2_ldb_lookup(struct c2_ldb_schema *schema, uint64_t lid, struct c2_db_pair *pair, struct c2_db_tx *tx, struct c2_layout **out)
 * - int c2_ldb_add(struct c2_ldb_schema *schema, struct c2_layout *l, struct c2_db_pair *pair, struct c2_db_tx *tx)
 * - int c2_ldb_update(struct c2_ldb_schema *schema, struct c2_layout *l, struct c2_db_pair *pair, struct c2_db_tx *tx)
 * - int c2_ldb_delete(struct c2_ldb_schema *schema, struct c2_layout *l, struct c2_db_pair *pair, struct c2_db_tx *tx)
 *
 * @subsection Layout-DB-fspec-sub-acc Accessors and Invariants
 *
 * @section Layout-DB-fspec-usecases Recipes
 * A file layout is used by the client to perform IO against that file. A
 * Layout for a file contains COB identifiers for all the COBs associated with
 * that file. These COB identifiers are stored by the layout either in the
 * form of a list or as a linear formula.
 *
 * Example use case of reading a file:
 * - Reading a file involves reading basic file attributes from the basic file
 *   attributes table).
 * - The layout id is obtained from the basic file attributes.
 * - A query is sent to the Layout module to obtain layout for this layout id.
 * - Layout module checks if the layout record is cached and if not, it reads
 *   the layout record from the layout DB. Examples are:
 *    - If the layout record is with the LINEAR enumeration, then the
 *      linear formula is obtained from the DB, required parameters are
 *      substituted into the formula and thus the list of COB identifiers is
 *      obtained to operate upon.
 *    - If the layout record is with the LIST enumeration, then the
 *      the list of COB identifiers is obtained from the layout DB itself.
 *    - If the layout record is of the COMPOSITE layout type, it means it
 *      constitutes of multiple sub-layouts. In this case, the sub-layouts are
 *      read from the layout DB. Those sub-layout records in turn could be of
 *      other layout types and with LINEAR or LIST enumeration for example.
 *      The sub-layout records are then read accordingly until the time the
 *      final list of all the COB identifiers is obtained.
 *
 *  @see @ref LayoutDBDFS "Layout DB Detailed Functional Specification"
 */

/* import */
#include "lib/arith.h"	/* struct C2_3WAY() */
#include "db/db.h"	/* struct c2_table */

#include "layout/layout.h"

/* export */
struct c2_ldb_schema;
struct c2_ldb_rec;


/**
 * @defgroup LayoutDBDFS Layout DB
 * @brief Detailed functional specification for Layout DB.
 *
 * Detailed functional specification provides documentation of all the data
 * structures and interfaces (internal and external).
 *
 * @see @ref Layout-DB "Layout DB DLD" and its @ref Layout-DB-fspec
 * "Layout DB Functional Specification".
 *
 * @{
 */

/**
 * In-memory data structure for the layout schema.
 * It includes a pointer to the layouts table and various related
 * parameters.
 * There is one instance of layout domain object per Colibri.
 */
struct c2_ldb_schema {
	/** Pointer to domain; to keep things together. */
	struct c2_layout_domain *ls_domain;

	/** Pointer to dbenv; to keep things together. */
	struct c2_dbenv         *ls_dbenv;

	/** Table for layout record entries. */
	struct c2_table          ls_layouts;

	/** Layout type specific data. */
	void                    *ls_type_data[C2_LAYOUT_TYPE_MAX];

	/** Layout enum type specific data. */
	void                    *ls_enum_data[C2_LAYOUT_ENUM_TYPE_MAX];

	/**
	 * Lock to protect the instance of c2_ldb_schema, including all
	 * its members.
	 */
	struct c2_mutex          ls_lock;
};

/**
 * layouts table.
 * Key is uint64_t, value obtained from c2_layout::l_id.
 */
struct c2_ldb_rec {
	/**
	 * Layout type id.
	 * Value obtained from c2_layout_type::lt_id.
	 */
	uint32_t                       lr_lt_id;

	/**
	 * Layout reference count, indicating number of users for this layout.
	 * Value obtained from c2_layout::l_ref.
	 */
	uint32_t                       lr_ref_count;

	/**
	 * Pool identifier.
	 * Value obtained from c2_layout::l_pid.
	 */
	uint64_t                       lr_pool_id;

	/**
	 * Layout type specific payload.
	 * Contains attributes specific to the applicable layout type and/or
	 * applicable to the enumeration type, if applicable.
	 */
	char                           lr_data[0];
};

int c2_ldb_schema_init(struct c2_ldb_schema *schema,
		       struct c2_layout_domain *domain,
		       struct c2_dbenv *db);
void c2_ldb_schema_fini(struct c2_ldb_schema *schema);

int c2_ldb_register(struct c2_layout_domain *dom);
void c2_ldb_unregister(struct c2_layout_domain *dom);

int c2_ldb_type_register(struct c2_layout_domain *dom,
			 const struct c2_layout_type *lt);
void c2_ldb_type_unregister(struct c2_layout_domain *dom,
			    const struct c2_layout_type *lt);

int c2_ldb_enum_register(struct c2_layout_domain *dom,
			 const struct c2_layout_enum_type *et);
void c2_ldb_enum_unregister(struct c2_layout_domain *dom,
			    const struct c2_layout_enum_type *et);

c2_bcount_t c2_ldb_max_recsize(struct c2_layout_domain *dom);
c2_bcount_t c2_ldb_recsize(struct c2_layout_domain *dom, struct c2_layout *l);

int c2_ldb_lookup(struct c2_ldb_schema *schema,
		  uint64_t id,
		  struct c2_db_pair *pair,
		  struct c2_db_tx *tx,
		  struct c2_layout **out);
int c2_ldb_add(struct c2_ldb_schema *schema,
	       struct c2_layout *l,
	       struct c2_db_pair *pair,
	       struct c2_db_tx *tx);
int c2_ldb_update(struct c2_ldb_schema *schema,
		  struct c2_layout *l,
		  struct c2_db_pair *pair,
		  struct c2_db_tx *tx);
int c2_ldb_delete(struct c2_ldb_schema *schema,
		  struct c2_layout *l,
		  struct c2_db_pair *pair,
		  struct c2_db_tx *tx);

/** @} end group LayoutDBDFS */

#endif /*  __COLIBRI_LAYOUT_LAYOUT_DB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
