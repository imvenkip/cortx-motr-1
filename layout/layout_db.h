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
#include "lib/refs.h"	/* struct c2_ref */
#include "lib/types.h"	/* struct c2_uint128 */
#include "db/db.h"	/* struct c2_table */
#include "db/extmap.h"	/* struct c2_emap */

#include "layout/layout.h"

/* export */
struct c2_layout_schema;
struct c2_layout_rec;
struct c2_layout_rec_attrs;

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
   - struct c2_layout_schema
   - struct c2_layout_rec
   - struct c2_layout_rec_attrs

   @todo Not sure why this enum does not link back to where it is defined!
   Need to figure out. Any inputs are welcome.

   @section Layout-DB-fspec-sub Subroutines
   - int c2_layout_schema_init(struct c2_layout_schema *l_schema)
   - void c2_layout_schema_fini(struct c2_layout_schema *l_schema)
   - int c2_layout_rec_add(const struct c2_layout *l, const struct c2_layout_schema *l_schema, const struct c2_db_tx *tx)
   - int c2_layout_rec_delete(const struct c2_layout *l, const struct c2_layout_schema *l_schema, const struct c2_db_tx *tx)
   - int c2_layout_rec_update(const struct c2_layout *l, const struct c2_layout_schema *l_schema, const struct c2_db_tx *tx)
   - int c2_layout_rec_lookup(const struct c2_layout_id *l_id, const struct c2_layout_schema *l_schema, const struct c2_db_tx *tx, struct c2_layout_rec *l_rec_out);

   @subsection Layout-DB-fspec-sub-acc Accessors and Invariants

   @section Layout-DB-fspec-usecases Recipes
   A file layout is used by the client to perform IO against the file. Layout
   for a file contains COB identifiers for all the COBs associated with that
   file. These COB identifiers are stored by the layout either in the form of
   a list (PDCLUST_LIST layout type) or as a formula (PDCLUST_LINEAR layout
   type).

   Example use case of reading a file:
   - Reading a file involves reading basic file attributes from the basic file
     attributes table).
   - The layout id is obtained from the basic file attributes.
   - A query is sent to the Layout module to obtain layout for this layout id.
   - Layout module checks if the layout record is cached and if not, it reads
     the layout record from the layout DB.
      - If the layout record is of the type PDCLUST_LINEAR which means it is a
        formula, then the required parameters are substituted into the formula
        and thus the list of cob identifiers is obtained to operate upon.
      - If the layout record is of the type PDCLUST_LIST which means it stores
        the list of cob identifiers in itself, then that list is obtained
        from the layout DB itself.
      - If the layout record is of the type COMPOSITE, it means it constitutes
        of multiple sub-layouts. In this case, the sub-layouts are read from
        the layout DB. Those sub-layouts records in turn could be of the type
        PDCLUST_LIST or PDCLUST_LINEAR or COMPOSITE. The sub-layout records
        are then read accordingly until the time the final list of all the cob
        identifiers is obtained.

   @see @ref LayoutDBDFS "Layout DB Detailed Functional Specification"
 */

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
   Attributes for PDCLUST_LINEAR type of layout record.
*/
struct c2_layout_rec_attrs {
	/** Number of data units in the parity group (N) */
	uint32_t plra_num_of_data_units;
	/** Number of parity units in the parity group (K) */
	uint32_t plra_num_of_parity_units;
};


/**
   In-memory data structure for the layout schema.
   It includes pointers to all the DB tables and various related
   parameters.
*/
struct c2_layout_schema {
	/** Layout DB environment */
	struct c2_dbenv *ls_dbenv;

	/** Table for layout record entries */
	struct c2_table ls_layout_entries;

	/** Table for cob lists for all PDCLUST_LIST type of layout records */
	struct c2_table ls_cob_lists;

	/* Table for extent maps for all the COMPOSITE type of layout records */
	struct c2_emap ls_comp_layout_ext_map;
};

/**
   layout_entries table
   Key is c2_layout_id.
*/
struct c2_layout_rec {
	/** Layout type id */
	uint32_t lr_lt_id;

	/** Layout enumeration type id */
	uint32_t lr_let_id;

	/** Layout record reference count indicating number of files using
	this layout */
	struct uint32_t lr_ref_count;

	/** Struct to store PDCLUST_LINEAR record type specific data */
	struct c2_layout_rec_attrs lr_linear_attrs;
};


int c2_layout_schema_init(struct c2_layout_schema *l_schema);
void c2_layout_schema_fini(struct c2_layout_schema *l_schema);
int c2_layout_rec_add(const struct c2_layout *layout,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx);
int c2_layout_rec_delete(const struct c2_layout *layout,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx);
int c2_layout_rec_update(const struct c2_layout *layout,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx);
int c2_layout_rec_lookup(const struct c2_layout_id *l_id,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx,
		struct c2_layout_rec *l_recrec_out);
/**
   @} LayoutDBDFS end group
*/

/**
 * @addtogroup LayoutDBDFSInternal
 * @{
 */

/**
   Compare layout_entries table keys
*/
static int le_key_cmp(struct c2_table *table,
		const void *key0,
		const void *key1)
{
	return 0;
}

/**
   table_ops for layout_entries table
*/
static const struct c2_table_ops layout_entries_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_uint128)
		},
		[TO_REC] = {
			.max_size = sizeof(struct c2_layout_rec)
		}
	},
	.key_cmp = le_key_cmp
};

/**
   cob_lists table
*/
struct layout_cob_lists_key {
	struct c2_layout_id lclk_l_id;
	uint32_t lclk_cob_index;
};

struct layout_cob_lists_rec {
	struct c2_fid lclr_cob_id;
};

/**
   Compare cob_lists table keys
*/
static int lcl_key_cmp(struct c2_table *table,
		const void *key0,
		const void *key1)
{
	return 0;
}

/**
   table_ops for cob_lists table
*/
static const struct c2_table_ops cob_lists_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct layout_cob_lists_key)
		},
		[TO_REC] = {
			.max_size = sizeof(struct layout_cob_lists_rec)
		}
	},
	.key_cmp = lcl_key_cmp
};

/** @} end of LayoutDBDFSInternal */

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
