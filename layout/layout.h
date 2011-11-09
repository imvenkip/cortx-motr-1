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

/* import */
#include "lib/cdefs.h"
#include "lib/vec.h"

/**
   @defgroup layout Layouts.

   @{
 */

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
	struct c2_uint128 layout_id;
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

struct c2_layout {
	const struct c2_layout_type      *l_type;
	const struct c2_layout_formula   *l_form;
	const struct c2_layout_parameter *l_actuals;
	struct c2_layout_id               l_id;
	const struct c2_layout_ops       *l_ops;

	union layout_type_specific_attrs {
		/** PDCLUST-LINEAR layout type specific attributes */
		struct pdclust_linear_attrs pdclust_linear_attrs;
		/** PDCLUST-LIST layout type specific attributes */
		struct l_cob_id_entry *pdclust_list_cob_list_head;
		/** COMPOSITE layout type specific attributes */
		struct composite_layout_extent *comp_ext_list_head;
	};
};

struct pdclust_linear_attrs {
	/** Number of data units in the parity group */
	uint32_t N; 
	/** Number of parity units in the parity group */
	uint32_t K;
};

/**
	A linked list of cob ids.
*/
struct l_cob_id_entry {
	struct c2_cob_id cob_id;
	struct l_cob_id_entry *cob_id_next;
};

/**
	Linked list of extents owned by a composite layout, along with cob ids
	for those extents.
*/
struct composite_layout_extent {
	struct c2_ext comp_ext;
	struct c2_cob_id comp_ext_cob_id;
	struct composite_layout_extent *comp_ext_next;
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
	/** Converts an in-memory layout record to DB record */
	int (*l_rec_encode)(const struct c2_layout l,
		struct c2_layout_rec l_rec_out);			
	/** Adds a new layout entry and its relevant information into the 
	relevant tables from the layout schema. */
	int (*l_rec_add)(const struct c2_layout_rec l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);			
	/** Deletes a layout entry and its relevant information from the
	relevant tables from the layout schema. */
	int (*l_rec_delete)(const struct c2_layout_rec l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);	
	/** Updates a layout entry and its relevant information in the
	relevant tables from the layout schema. */
	int (*l_rec_update)(const struct c2_layout_rec l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);
	/** Locates a layout entry and its relevant information from the
	relevant tables from the layout schema. */
	int (*l_rec_lookup)(struct c2_layout_rec l_rec_out,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);
	/** Releases reference on a layout. */
	int (*l_rec_put)(struct c2_layout_rec l_rec,
		const struct c2_layout_schema *l_schema,
		const struct c2_db_tx *tx);
};

/**
	Structure specific to per type of a layout
	There is an instance of c2_layout_type for each one of PDCLUST-LINEAR, 
	PDCLUST-LIST and COMPOSITE layout types.
*/
struct c2_layout_type {
	const char  *lt_name;
	bool       (*lt_equal)(const struct c2_layout *l0, 
			       const struct c2_layout *l1);
	/** Converts a layout record from DB to in-memory fomat */
	int        (*l_decode_rec)(const struct c2_layout_rec l_rec,
				struct c2_layout l_out);			
}

int  c2_layouts_init(void);
void c2_layouts_fini(void);

/**
	Implementation of l_rec_encode for PDCLUST-LINEAR type.
	Converts in memory layout structre to DB record format.
*/
int pdclust_linear_encode(const struct c2_layout *l, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_encode for PDCLUST-LIST type. 
	Converts in memory layout structre to DB record format.
*/
int pdclust_list_encode(const struct c2_layout *l, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_encode for COMPOSITE type. 
	Converts in memory layout structre to DB record format.
*/
int composite_linear_encode(const struct c2_layout *l, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_add for PDCLUST-LINEAR type. 
	Adds layout entry into the table layout_entries.
*/
int pdclust_linear_add(const struct c2_layout *l, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_add for PDCLUST-LIST type. 
	Adds a layout entry into the table layout_entries.
	Adds list of cob ids to the table pdclust_list_cob_lists.
*/
int pdclust_list_add(const struct c2_layout *l,
		const struct c2_layout_schema *l_schema, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_add for COMPOSITE type. 
	Adds the layout entry into the table layout_entries.
	Adds the relevant extent map into the composite_ext_map table.
*/

int composite_add(const struct c2_layout *l, 
		const struct c2_layout_schema *l_schema, 
		struct c2_layout_rec l_rec);


/**
	Implementation of l_rec_delete for PDCLUST-LINEAR type. 
	Deletes the layout entry from the table layout_entries.
	This function is un-used currently since PDCLUST-LINEAR type of 
	layout is not deleted anytime. 
*/
int pdclust_linear_delete(const struct c2_layout *l, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_delete for PDCLUST-LIST type. 
	Deletes the layout entry from the table layout_entries.
	Deletes the relevant list of cob ids from the table 
	pdclust_list_cob_lists.
*/
int pdclust_list_delete(const struct c2_layout *l,
		const struct c2_layout_schema *l_schema, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_delete for COMPOSITE type. 
	Deletes the layout entry from the table layout_entries.
	Deletes the relevant extent map from the 
	composite_ext_map table.
*/

int composite_delete(const struct c2_layout *l, 
		const struct c2_layout_schema *l_schema, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_update for PDCLUST-LINEAR type. 
	Updates the layout entry in the table layout_entries.
*/
int pdclust_linear_update(const struct c2_layout *l, 
		const struct c2_layout_schema *l_schema, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_update for PDCLUST-LINEAR type. 
	Updates the layout entry in the table layout_entries.
	Updates the relevant list of cob ids in the table 
	pdclust_list_cob_lists.
*/
int pdclust_list_update(const struct c2_layout *l,
		const struct c2_layout_schema *l_schema, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_update for COMPOSITE type. 
	Updates the layout entry in the table layout_entries.
	Updates the relevant extent map in the composite_ext_map table.
*/
int composite_update(const struct c2_layout *l, 
		const struct c2_layout_schema *l_schema, 
		struct c2_layout_rec l_rec);

/**
	Implementation of l_rec_lookup for PDCLUST-LINEAR type.
	Locates the layout entry in the table layout_entries.
*/
int pdclust_linear_lookup(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema, 
		struct c2_layout l_rec_out);

/**
	Implementation of l_rec_lookup for PDCLUST-LIST type.
	Locates the layout entry from the table layout_entries.
	Locates the relevant list of cob ids from the table 
	pdclust_list_cob_lists.
*/
int pdclust_list_lookup(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema, 
		struct c2_layout l_rec_out);

/**
	Implementation of l_rec_lookup for COMPOSITE type.
	Locates the layout entry from the table layout_entries.
	Locates the relevant extent map from the composite_ext_map table.
*/
int composite_locate(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema, 
		struct c2_layout l_rec_out);

/**
	Implementation of l_rec_put for PDCLUST-LINEAR type.
	Releases reference on the layout from the table layout_entries.
*/
int pdclust_linear_put(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema);

/**
	Implementation of l_rec_put for PDCLUST-LIST type.
	Releases reference on the layout from the table layout_entries.
*/
int pdclust_list_put(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema);

/**
	Implementation of l_rec_put for COMPOSITE type.
	Releases reference on the layout from the table layout_entries.
*/
int composite_put(const struct c2_layout_id l_id,
		const struct c2_layout_schema *l_schema);

/**
	Implementation of l_decode_rec for PDCLUST-LINEAR type.
	Converts a layout record from DB to in-memory format.
*/
int pdclust_linear_decode(const struct c2_layout_rec l_rec,
		struct c2_layout l_out);			

/**
	Implementation of l_decode_rec for PDCLUST-LIST type.
	Converts a layout record from DB to in-memory format.
*/
int pdclust_list_decode(const struct c2_layout_rec l_rec,
		struct c2_layout l_out);			

/**
	Implementation of l_decode_rec for COMPOSITE type.
	Converts a layout record from DB to in-memory format.
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
