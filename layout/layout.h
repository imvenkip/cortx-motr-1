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
#include "lib/vec.h"	/* struct c2_bufvec_cursor */
#include "lib/types.h"	/* uint64_t */
#include "fid/fid.h"	/* struct c2_fid */

struct c2_layout_rec;
struct c2_layout_schema;
struct c2_db_tx;

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
	uint64_t        l_id;
};

/**
   Structure specific to per layout type.
   There is an instance of c2_layout_type for each one of layout types. e.g.
   for PDCLUST and COMPOSITE layout types.
*/
struct c2_layout_type {
	const char                       *lt_name;

	/** Layout type id.
 	    This is stored on the persistent store and is passed across
	    the network to identify layout types.
 	*/
	uint32_t                          lt_id;

	const struct c2_layout_type_ops  *lt_ops;
};

struct c2_layout_type_ops {
	bool	(*lto_equal)(const struct c2_layout *l0,
			     const struct c2_layout *l1);

	/** Continue to decode layout representation stored in the buffer and
	    to build the layout.
	    The newly created layout is allocated as an instance of some 
	    layout-type specific data-type which embeds c2_layout.

	    This method sets c2_layout::l_ops. 
	*/	    	 
	int	(*lto_decode)(const struct c2_bufvec_cursor *cur,
			      struct c2_layout **l_out);
	
	/** Continue to store layout representation in the buffer */
	int	(*lto_encode)(const struct c2_layout *l,
			      struct c2_bufvec_cursor *cur_out);

	/** Adds a new layout record and its related information into the 
	    the relevant tables. */
	int (*lto_rec_add)(const struct c2_bufvec_cursor *cur,
			   struct c2_layout_schema *l_schema,
			   struct c2_db_tx *tx);
	
	/** Deletes a layout record and its related information from the
	    relevant tables. 
	*/
	int (*lto_rec_delete)(const struct c2_bufvec_cursor *cur,
			      struct c2_layout_schema *l_schema,
			      struct c2_db_tx *tx);
	
	/** Updates a layout entry and its related information in the
	    relevant tables from the layout schema. 
	*/
	int (*lto_rec_update)(const struct c2_bufvec_cursor *cur,
			      struct c2_layout_schema *l_schema,
			      struct c2_db_tx *tx);
	
	/** Locates a layout entry and its related information from the
	    relevant tables from the layout schema. 
	*/
	int (*lto_rec_lookup)(const struct c2_layout_id *l_id,
			      struct c2_layout_schema *l_schema,
			      struct c2_db_tx *tx,
			      struct c2_bufvec_cursor *cur_out);
};


/** @todo Change type of l_id to c2_layout_id, requires changes in pdclust */
struct c2_layout {
	struct c2_uint128		  l_id;
	const struct c2_layout_type	 *l_type;
	const struct c2_layout_enum	 *l_enum;
	const struct c2_layout_ops       *l_ops;
};

struct c2_layout_ops {
};


/**
   Structure specific to per layout enumeration type.
   There is an instance of c2_layout_enum_type for each one of enumeration
   types. e.g. for LINEAR and LIST enumeration types.
*/
struct c2_layout_enum_type {
	const char			     *let_name;
	/** Layout enumeration type id
	    This is stored on the persistent store and is pased across
	    the network to identify the layout enumeration type.
	*/
	uint32_t			      let_id;
	const struct c2_layout_enum_type_ops *let_ops;
};

struct c2_layout_enum_type_ops {
	/** Continue to encode layout representation stored in the buffer and
	    to build the layout.
	*/
	int (*leto_decode)(const struct c2_bufvec_cursor *cur,
			   struct c2_layout **l_out);

	/** Continue to store layout representation in the buffer */
	int (*leto_encode)(const struct c2_layout *l,
			   struct c2_bufvec_cursor *cur_out);

	/** Continue to add the new layout record by adding list of cob ids */
	int (*leto_rec_add)(const struct c2_bufvec_cursor *cur,
			    struct c2_layout_schema *l_schema,
			    struct c2_db_tx *tx);
	
	/** Continue to delete the layout record */
	int (*leto_rec_delete)(const struct c2_bufvec_cursor *cur,
			       struct c2_layout_schema *l_schema,
			       struct c2_db_tx *tx);
	
	/** Continue to updates the layout record */
	int (*leto_rec_update)(const struct c2_bufvec_cursor *cur,
			       struct c2_layout_schema *l_schema,
			       struct c2_db_tx *tx);
	
	/** Continue to locates layout record information */
	int (*leto_rec_lookup)(const struct c2_layout_id *l_id,
			       struct c2_layout_schema *l_schema,
			       struct c2_db_tx *tx,
			       struct c2_bufvec_cursor *cur_out);
};

/** 
   Layout enumeration.
*/
struct c2_layout_enum {
	const struct c2_layout_enum_ops *l_enum_ops;
};

struct c2_layout_enum_ops {
	/** Returns number of objects in the enumeration. */
	uint32_t (*leo_nr)(const struct c2_layout_enum *e);
	
	/** Returns nr-th object in the enumeration.
	    @pre nr < e->l_enum_ops->leo_ne(e)
	*/
	void (*leo_get)(const struct c2_layout_enum *e, 
			uint32_t nr,
			struct c2_fid *fid_out);
};


void c2_layout_init(struct c2_layout *lay);
void c2_layout_fini(struct c2_layout *lay);

int  c2_layouts_init(void);
void c2_layouts_fini(void);


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
