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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 07/09/2010
 */

#ifndef __COLIBRI_LAYOUT_LAYOUT_H__
#define __COLIBRI_LAYOUT_LAYOUT_H__

#include "lib/cdefs.h"
#include "lib/vec.h"
#include "cob/cob.h"	/* struct c2_cob_id */
#include "lib/ext.h"	/* struct c2_ext */

/* import */
struct c2_layout_rec;
struct c2_layout_schema;

/**
   @defgroup layout Layouts.

   @{
 */

/* export */
struct c2_layout_id;
struct c2_layout_formula;
struct c2_layout_parameter;
struct c2_layout_parameter_type;
struct c2_layout;
struct c2_layout_ops;
struct c2_layout_type;

struct c2_layout_ops;
struct c2_layout_formula_ops;


/** Unique layout id */
struct c2_layout_id {
	struct c2_uint128 l_id;
};

/** Classification of enumeration method types */
enum layout_enumeration_type_code {
	NONE,
	LINEAR,
	LIST
};

struct c2_layout_formula {
	const struct c2_layout_type        *lf_type;
	const struct c2_uint128             lf_id;
	const struct c2_layout_formula_ops *lf_ops;
};

struct c2_layout_formula_ops {
	int (*lfo_subst)(const struct c2_layout_formula *form, uint16_t nr,
			 const struct c2_layout_parameter *actuals, 
			 struct c2_layout **out);
};

struct pdclust_linear_attrs {
	/** Number of data units in the parity group (N) */
	uint32_t pla_num_of_data_units; 
	/** Number of parity units in the parity group (K) */
	uint32_t pla_num_of_parity_units;
};

/**
   TODO: Changing type of l_id to c2_layout_id requires changing the 
   prototype of c2_pdclust_build to accept "c2_layout_id *id" instead of
   "c2_uint128 *id". Hence, will change it once defining c2_layout_id
   is approved upon. 
*/
struct c2_layout {
	const struct c2_layout_type      *l_type;
	const struct c2_layout_formula   *l_form;
	const struct c2_layout_parameter *l_actuals;
	enum layout_enumeration_type_code l_enumeration_type;
	struct c2_uint128                 l_id;
	const struct c2_layout_ops       *l_ops;

	union {
		/** Attributes specific to the layout type PDCLUST with enumeration 
		type as LINEAR */
		struct pdclust_linear_attrs l_pdclust_linear_attrs;
		/** Pointer to the list of cob identifiers, specific to the layout 
		type PDCLUST with enumeration type as LIST */
		struct layout_cob_id_entry *l_pdclust_list_cob_list_head;
		/** Pointer to the list of extents, specific to the layout type 
		COMPOSITE */
		struct layout_composite_ext *l_comp_ext_list_head;
	} l_type_specific_attrs;
};

/**
   A linked list of cob ids.
*/
struct layout_cob_id_entry {
	struct c2_cob_id lci_cob_id;
	/** COB index from global file so as to identify sequence of COBs */
	struct c2_uint128 lci_cob_index;
	struct layout_cob_id_entry *lci_cob_id_next;
};

/**
   Linked list of extents owned by a composite layout, along with cob ids
   for those extents.
*/
struct layout_composite_ext {
	struct c2_ext lce_extent;
	struct c2_cob_id lce_cob_id;
	struct layout_composite_ext *lce_next;
};


void c2_layout_init(struct c2_layout *lay);
void c2_layout_fini(struct c2_layout *lay);

struct c2_layout_parameter {
	const struct c2_layout_parameter_type *lp_type;
	const void                            *lp_value;
};

struct c2_layout_parameter_type {
	const char *lpt_name;
	int       (*lpt_convert)(const struct c2_layout_parameter *other,
				 struct c2_layout_parameter *out);
};

struct c2_layout_ops {
	/** Converts a layout (in-memory) to layout record (DB format) */	
	int (*l_rec_encode)(const struct c2_layout *l,
		struct c2_layout_rec *l_rec_out);			
	/** Adds a new layout record and its related information into the 
	relevant tables. */
	int (*l_rec_add)(const struct c2_layout_rec *l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);			
	/** Deletes a layout record and its related information from the
	relevant tables. */
	int (*l_rec_delete)(const struct c2_layout_rec *l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);
	/** Updates a layout entry and its relevant information in the
	relevant tables from the layout schema. */
	int (*l_rec_update)(const struct c2_layout_rec *l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);
	/** Locates a layout entry and its relevant information from the
	relevant tables from the layout schema. */
	int (*l_rec_lookup)(const struct c2_layout_id *l_id,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx,
		struct c2_layout_rec *l_rec_out);
	/** Releases reference on a layout. */
	int (*l_rec_ref_put)(struct c2_layout_rec *l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);
};

/**
   Structure specific to per type of a layout
   There is an instance of c2_layout_type for each one of PDCLUST
   and COMPOSITE layout types.
*/
struct c2_layout_type {
	const char  *lt_name;
	bool       (*lt_equal)(const struct c2_layout *l0, 
			       const struct c2_layout *l1);
	/** Converts a layout record (DB format) to layout (in-memory format) */
	int        (*lt_rec_decode)(const struct c2_layout_rec l_rec,
				struct c2_layout l_out);			
};

int  c2_layouts_init(void);
void c2_layouts_fini(void);

/**
   Implementation of l_rec_encode for PDCLUST layout type.
   Converts a layout (in-memory fomrat) to layout record (DB format).
*/
int pdclust_encode(const struct c2_layout *l, 
		struct c2_layout_rec l_rec_out);

/**
   Implementation of l_rec_encode for COMPOSITE layout type. 
   Converts a layout (in-memory format) to layout record (DB format).
*/
int composite_encode(const struct c2_layout *l, 
		struct c2_layout_rec l_rec_out);

/**
   Implementation of l_rec_add for PDCLUST layout type. 
   Adds a layout entry into the layout_entries table.
   For LIST enumeration type, adds list of cob ids to the 
   pdclust_list_cob_lists table.
*/
int pdclust_add(const struct c2_layout_rec *l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx); 

/**
   Implementation of l_rec_add for COMPOSITE layout type. 
   Adds the layout entry into the layout_entries table .
   Adds the relevant extent map into the composite_ext_map table.
*/

int composite_add(const struct c2_layout_rec *l_rec, 
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);

/**
   Implementation of l_rec_delete for PDCLUST layout type.
   For LINEAR enumeration type, this function is un-used currently since 
   it never gets deleted.
   For LIST enumeration type, deletes the layout entry from the
   layout_entries table and deletes the relevant list of cob ids from
   the pdclust_list_cob_lists table.
*/
int pdclust_delete(const struct c2_layout_rec *l_rec, 
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);

/**
   Implementation of l_rec_delete for COMPOSITE layout type.
   Deletes the layout entry from the table layout_entries.
   Deletes the relevant extent map from the 
   composite_ext_map table.
*/
int composite_delete(const struct c2_layout_rec *l_rec, 
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);

/**
   Implementation of l_rec_update for PDCLUST layout type.
   Updates the layout entry in the layout_entries table.
   For LIST enumeration type, updates the relevant list of cob ids in
   the pdclust_list_cob_lists table.
*/
int pdclust_update(const struct c2_layout_rec *l_rec, 
		const struct c2_layout_schema *l_schema, 
		const struct c2_db_tx *tx);

/**
   Implementation of l_rec_update for COMPOSITE layout type.
   Updates the layout entry in the layout_entries table.
   Updates the relevant extent map in the composite_ext_map table.
*/
int composite_update(const struct c2_layout_rec *l_rec, 
		const struct c2_layout_schema *l_schema, 
		const struct c2_db_tx *tx);

/**
   Implementation of l_rec_lookup for PDCLUST layout type.
   Obtains the layout entry with the specified layout id, from the
   layout_entries table.
   For LIST enumeration type, obtains the relevant list of cob ids from
   the pdclust_list_cob_lists table.
*/
int pdclust_lookup(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx,
		struct c2_layout_rec *l_rec_out);

/**
   Implementation of l_rec_lookup for COMPOSITE layout type.
   Obtains the layout entry from the layout_entries table.
   Obtains the relevant extent map from the composite_ext_map table.
*/
int composite_lookup(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx,
		struct c2_layout_rec *l_rec_out);

/**
   Implementation of l_rec_ref_put for PDCLUST layout type.
   Releases reference on the layout record from the layout_entries table.
*/
int pdclust_rec_put(const struct c2_layout_rec *l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);

/**
   Implementation of l_rec_ref_put for COMPOSITE layout type.
   Releases reference on the layout from the layout_entries table.
*/
int composite_rec_put(const struct c2_layout_rec *l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);

/**
   Implementation of lt_rec_decode for PDCLUST layout type.
   Converts a layout record (DB format) to layout (in-memory format).
*/
int pdclust_decode(const struct c2_layout_rec *l_rec,
		struct c2_layout *l_out);			

/**
   Implementation of lt_rec_decode for COMPOSITE layout type.
   Converts a layout record (DB format) to layout (in-memory format).
*/
int composite_decode(const struct c2_layout_rec l_rec,
		struct c2_layout l_out);			


/** @} end group layout */

/* __COLIBRI_LAYOUT_LAYOUT_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
