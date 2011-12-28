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

#ifndef __COLIBRI_LAYOUT_LAYOUT_DB_H__
#define __COLIBRI_LAYOUT_LAYOUT_DB_H__

/* import */
#include "fid/fid.h"	/* struct c2_fid */
#include "db/db.h"	/* struct c2_table */

#include "layout/layout.h"

/* export */
struct c2_ldb_schema;
struct c2_ldb_rec;

/**
   @page Layout-DB-fspec Layout DB Functional Specification
   Layout DB Module is used by the Layout module to make persistent records for
   the layout entries created and used.

   This section describes the data structures exposed and the external
   interfaces of the Layout DB module and it briefly identifies the users of
   these interfaces so as to explain how to use this module.

   - @ref Layout-DB-fspec-ds
   - @ref Layout-DB-fspec-sub
   - @ref Layout-DB-fspec-usecases
   - @ref LayoutDBDFS "Detailed Functional Specification"

   @section Layout-DB-fspec-ds Data Structures
   - struct c2_ldb_schema
   - struct c2_ldb_rec

   @section Layout-DB-fspec-sub Subroutines
   - int c2_ldb_schema_init(struct c2_ldb_schema *schema, struct c2_dbenv *db)
   - void c2_ldb_schema_fini(struct c2_ldb_schema *schema)
   - void c2_ldb_type_register(struct c2_ldb_schema *schema, const struct c2_layout_type *lt)
   - void c2_ldb_type_unregister(struct c2_ldb_schema *schema, const struct c2_layout_type *lt)
   - void c2_ldb_enum_register(struct c2_ldb_schema *schema, const struct c2_layout_enum_type *et)
   - void c2_ldb_enum_unregister(struct c2_ldb_schema *schema, const struct c2_layout_enum_type *et)
   - void **c2_ldb_type_data(struct c2_ldb_schema *schema, const struct c2_layout_type *lt)
   - void **c2_ldb_enum_data(struct c2_ldb_schema *schema, const struct c2_layout_enum_type *et)
   - int c2_ldb_rec_add(const struct c2_layout *l, struct c2_ldb_schema *schema, struct c2_db_tx *tx)
   - int c2_ldb_rec_delete(const struct c2_layout *l, struct c2_ldb_schema *schema, struct c2_db_tx *tx)
   - int c2_ldb_rec_update(const struct c2_layout *l, struct c2_ldb_schema *schema, struct c2_db_tx *tx)
   - int c2_ldb_rec_lookup(const uint64_t *l_id, struct c2_ldb_schema *schema, struct c2_db_tx *tx, c2_layout *out);

   @subsection Layout-DB-fspec-sub-acc Accessors and Invariants

   @section Layout-DB-fspec-usecases Recipes
   A file layout is used by the client to perform IO against that file. A
   Layout for a file contains COB identifiers for all the COBs associated with
   that file. These COB identifiers are stored by the layout either in the
   form of a list or as a linear formula.

   Example use case of reading a file:
   - Reading a file involves reading basic file attributes from the basic file
     attributes table).
   - The layout id is obtained from the basic file attributes.
   - A query is sent to the Layout module to obtain layout for this layout id.
   - Layout module checks if the layout record is cached and if not, it reads
     the layout record from the layout DB. Examples are:
      - If the layout record is with the LINEAR enumeration method, then the
        linear formula is obtained from the DB, required parameters are
	substituted into the formula and thus the list of COB identifiers is
	obtained to operate upon.
      - If the layout record is with the LIST enumeration method, then the
        the list of COB identifiers is obtained from the layout DB itself.
      - If the layout record is of the COMPOSITE layout type, it means it
        constitutes of multiple sub-layouts. In this case, the sub-layouts are
        read from the layout DB. Those sub-layout records in turn could be of
        other layout types and with either LINEAR or LIST enumeration methods.
        The sub-layout records are then read accordingly until the time the
        final list of all the COB identifiers is obtained.

   @see @ref LayoutDBDFS "Layout DB Detailed Functional Specification"
*/

/**
 * @addtogroup LayoutDBDFSInternal
 * @{
 */

enum {
	C2_LAY_TYPE_MAX        = 32,
	C2_LAY_ENUM_MAX        = 32,
};

/** @} end group LayoutDBDFS */

/**
   @defgroup LayoutDBDFS Layout DB
   @brief Detailed functional specification for Layout DB.

   Detailed functional specification provides documentation of all the data
   structures and interfaces (internal and external).

   @see @ref Layout-DB "Layout DB DLD" and its @ref Layout-DB-fspec
   "Layout DB Functional Specification".

   @{
*/

/**
   In-memory data structure for the layout schema.
   It includes pointer to the layouts table and various related
   parameters.
*/
struct c2_ldb_schema {
	/** Table for layout record entries. */
	struct c2_table                ls_layouts;

	/** Layout types array. */
	struct c2_layout_type         *ls_type[C2_LAY_TYPE_MAX];

	/** Enumeration types array. */
	struct c2_layout_enum_type    *ls_enum[C2_LAY_ENUM_MAX];

	/** Layout type specific data. */
	void                          *ls_type_data[C2_LAY_TYPE_MAX];

	/** Layout enum type specific data. */
	void                          *ls_enum_data[C2_LAY_ENUM_MAX];

	/** Lock to protect the instance of c2_ldb_schema, including all
	    its members.
	*/
	struct c2_mutex                ls_lock;
};

/**
   layouts table
   Key is uint64_t, value obtained from c2_layout::l_id.
*/
struct c2_ldb_rec {
	/** Layout type id.
	    Value obtained from  c2_layout_type::lt_id.
	*/
	uint64_t                       lr_lt_id;

	/** Layout reference count.
	    Indicating number of files using this layout.
	*/
	uint64_t                       lr_ref_count;

	/** Layout type specific payload.
	    Contains attributes specific to per layout type. */
	char                           lr_data[0];

};

int c2_ldb_schema_init(struct c2_ldb_schema *schema,
		       struct c2_dbenv *db);
void c2_ldb_schema_fini(struct c2_ldb_schema *schema);

void c2_ldb_type_register(struct c2_ldb_schema *schema,
			  const struct c2_layout_type *lt);
void c2_ldb_type_unregister(struct c2_ldb_schema *schema,
			    const struct c2_layout_type *lt);

void c2_ldb_enum_register(struct c2_ldb_schema *schema,
			  const struct c2_layout_enum_type *et);
void c2_ldb_enum_unregister(struct c2_ldb_schema *schema,
			    const struct c2_layout_enum_type *et);

void **c2_ldb_type_data(struct c2_ldb_schema *schema,
			const struct c2_layout_type *lt);
void **c2_ldb_enum_data(struct c2_ldb_schema *schema,
			const struct c2_layout_enum_type *et);

int c2_ldb_rec_add(const struct c2_layout *layout,
		   struct c2_ldb_schema *schema,
		   struct c2_db_tx *tx);
int c2_ldb_rec_delete(const uint64_t lid,
		      struct c2_ldb_schema *schema,
		      struct c2_db_tx *tx);
int c2_ldb_rec_update(const struct c2_layout *layout,
		      struct c2_ldb_schema *schema,
		      struct c2_db_tx *tx);
int c2_ldb_rec_lookup(const uint64_t *id,
		      struct c2_ldb_schema *schema,
		      struct c2_db_tx *tx,
		      struct c2_layout *out);

/** @} end group LayoutDBDFS */

/**
 * @addtogroup LayoutDBDFSInternal
 * @{
 */

/**
   Compare layouts table keys.
   This is a 3WAY comparison.
*/
static int l_key_cmp(struct c2_table *table,
		     const void *key0,
		     const void *key1)
{
	return 0;
}

/**
   table_ops for layouts table.
*/
static const struct c2_table_ops layouts_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_uint128)
		},
		[TO_REC] = {
			.max_size = sizeof(struct c2_ldb_rec)
		}
	},
	.key_cmp = l_key_cmp
};

/**
   cob_lists table.
*/
struct ldb_cob_lists_key {
	/** Layout id, value obtained from c2_layout::l_id. */
	uint64_t                  lclk_id;

	/** Index for the COB from the layout it is part of. */
	uint32_t                  lclk_cob_index;
};

struct ldb_cob_lists_rec {
	/** COB identifier. */
	struct c2_fid             lclr_cob_id;
};

/**
   Compare cob_lists table keys.
   This is a 3WAY comparison.
*/
static int lcl_key_cmp(struct c2_table *table,
		       const void *key0,
		       const void *key1)
{
	return 0;
}

/**
   table_ops for cob_lists table.
*/
static const struct c2_table_ops cob_lists_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct ldb_cob_lists_key)
		},
		[TO_REC] = {
			.max_size = sizeof(struct ldb_cob_lists_rec)
		}
	},
	.key_cmp = lcl_key_cmp
};

/**
   Prefix for comp_layout_ext_map table.
*/
struct layout_prefix {
	/** Layout id for the composite layout.
	    Value is same as c2_layout::l_id.
	*/
	uint64_t                  lp_l_id;

	/** Filler since prefix is a 128 bit field.
	    Currently un-used.
	*/
	uint64_t                  lp_filler;
};


/** @} end group LayoutDBDFSInternal */

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
