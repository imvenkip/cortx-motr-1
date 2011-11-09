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

#ifndef __COLIBRI_LAYOUT_DB_H__
#define __COLIBRI_LAYOUT_DB_H__

/* import */
#include "db/db.h"	/** c2_table */

/**
   @page Layout-DB-fspec Layout DB Functional Specification 
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   Note: The instructions in italics from the DLD template are ratained
   currently for reference and will be removed after the first round of the DLD
   inspection.

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
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   - struct c2_layout_schema
   - struct c2_layout_rec

   @section Layout-DB-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   - int c2_layout_schema_init(struct c2_layout_schema *l_schema)
   - void c2_layout_schema_fini(struct c2_layout_schema *l_schema)
   - int c2_layout_entry_add(const struct c2_layout layout, 
		const struct c2_layout_schema *l_schema, 
		const struct c2_db_tx *tx)
   - int c2_layout_entry_delete(const struct c2_layout layout, 
  		const struct c2_layout_schema *l_schema, 
		const struct c2_db_tx *tx)
   - int c2_layout_entry_update(const struct c2_layout layout,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx)
   - int c2_layout_entry_lookup(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx,
		struct c2_layout *l_out)
   - int c2_layout_entry_get(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx)
   - int c2_layout_entry_put(const struct c2_layout layout,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx)

   @subsection Layout-DB-fspec-sub-cons Constructors and Destructors

   @subsection Layout-DB-fspec-sub-acc Accessors and Invariants

   @section Layout-DB-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   A file layout is used by the client to perform IO against that file. Layout 
   for a file contains COB identifiers for all the COBs associated with that
   file. These COB identifiers are stored either in the form of a list 
   (COMPOSITE layout type) or as a formula (PDCLUST-LIST and PDCLUST-LINEAR 
   layout types). 
 
   Example use case of reading a file:
   - Reading a file involves reading basic file attributes from the basic file
   attributes table (FAB).
   - The layout id is obtained from the basic file attributes.
   - A query is sent to the Layout module to obtain layout for this layout id.
   - Layout module checks if the layout is cached and if not, it reads the 
   layout from the layout DB.
      - If the layout is of the type PDCLUST-LIST which means it stores the 
      list of cob identifiers in itself, then that list is obtained simply by
      obtaining the layout from the layout DB.  
      - If the layout is of the type PDCLUST-LINEAR which means it is a 
      formula, then the required parameters are substituted into the formula 
      and thus list of cob identifiers is obtained to operate upon. 
      - If the layout is of the type COMPOSITE, it means it constitutes of 
      multiple sub-layouts. In this case, the sub-layouts are read from the
      layout DB and those sub layouts in turn could be of the type PDCLUST-LIST
      or PDCLUST-LINEAR or COMPOSITE. The sub-layouts are read accordingly 
      until the time the final list of all the cob identifiers is obtained. 

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
	In-memory data structure for the layout schema.
	It includes pointers to all the DB tables and various related 
	parameters.
*/
struct c2_layout_schema {
	/** Layout DB environment */
	struct c2_dbenv *cl_dbenv;
	
	/** Table for layout entries */
	struct c2_table cls_layout_entries;

	/** Table for cob lists for all PDCLUST-LIST type of layout entries */
	struct c2_table cls_pdclust_list_cob_lists;

	/* Table for extent maps for all the COMPOSITE type of layout entries */
	struct c2_emap cls_composite_ext_map;
};

/** Classification of layout types */
enum layout_type_code {
	PDCLUST-LINEAR,
	PDCLUST-LIST,
	COMPOSITE
};

/**
	layout_entries table
	Key is c2_uint128 OR c2_layout_id. 
*/
struct c2_layout_rec {
	/** Type of layout */ 
	enum layout_type_code le_type_code;
	/** Layout reference count indicating number of files using this layout.
	struct c2_ref le_ref_count;
	
	/** Struct to store PDCLUST-LINEAR type specific data */
	struct pdclust_linear_formula_attrs le_linear_attrs;
};

/**
	Attributes for linear type of formula layout.
*/	
struct pdclust_linear_formula_attrs {
	/** Number of data units in parity group */
	unint32_t N;
	/** Number of parity units in parity group */
	unint32_t K;	
};

static const struct c2_table_ops layout_entries_table_ops = {
	.to = {
		[TO_KEY] = { 
			.max_size = sizeof(c2_uint128)
		},
		[TO_REC] = {
			.max_size = sizeof(struct c2_layout_rec)
		}
	},
	.key_comp = c2_uint12t_cmp
};


int c2_layout_schema_init(struct c2_layout_schema *l_schema);
void c2_layout_schema_fini(struct c2_layout_schema *l_schema);
int c2_layout_entry_add(const struct c2_layout layout, 
		const struct c2_layout_schema *l_schema, 
		const struct c2_db_tx *tx);
int c2_layout_entry_delete(const struct c2_layout layout, 
		const struct c2_layout_schema *l_schema, 
		const struct c2_db_tx *tx);
int c2_layout_entry_update(const struct c2_layout layout,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);
int c2_layout_entry_lookup(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx,
		struct c2_layout *l_out);
int c2_layout_entry_get(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);

/**
   @} LayoutDBDFS end group
*/


#endif /*  __COLIBRI_LAYOUT_DB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
