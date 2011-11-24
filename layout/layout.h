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

/**
   @defgroup layout Layouts.

   A layout is an attribute of a file and it provides information on component
   objects this file maps to.

   A layout type specifies how a file is stored in a collection of component
   objects.

   An enumeration method determines how a collection of component object is
   specified.

   Layout types supported currently are:
   - PDCLUST <BR>
     This layout type applies parity declustering feature to the striping
     process. Parity declustering feature is to keep the rebuild overhead low
     by striping a file over more servers or drives than there are units in
     the parity group.
   - COMPOSITE <BR>
     This layout type partitions a file or a part of the file into
     various segments while each of those segment uses a different layout.

   Enumeration methods supported currently are:
   - FORMULA <BR>
     A layout with FORMULA enumeration method uses a formula to enumerate all
     its component object identifiers.
   - LIST <BR>
     A layout with LIST enumeration method uses a list to enumerate all its
     component object identifiers.

   @{
*/

/* import */
#include "lib/cdefs.h"
#include "lib/types.h"	/* uint64_t */

struct c2_bufvec_cursor;
struct c2_fid;
struct c2_layout_rec;
struct c2_layout_schema;
struct c2_db_tx;

/* export */
struct c2_layout_id;
struct c2_layout_type;
struct c2_layout_ops;
struct c2_layout;
struct c2_layout_ops;
struct c2_layout_enum_type;
struct c2_layout_enum_ops;
struct c2_layout_enum;
struct c2_layout_enum_ops;

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

	/** Layout type id. */
	uint64_t                          lt_id;

	const struct c2_layout_type_ops  *lt_ops;
};

struct c2_layout_type_ops {
	/** Compares two layouts */
	bool	(*lto_equal)(const struct c2_layout *l0,
			     const struct c2_layout *l1);

	/** Continues decoding layout representation stored in the buffer and
	    building the layout.
	    The newly created layout is allocated as an instance of some
	    layout-type specific data-type which embeds c2_layout.

	    Sets c2_layout::l_ops.
	*/
	int	(*lto_decode)(const struct c2_bufvec_cursor *cur,
			      struct c2_layout **l_out);

	/** Continue storing layout representation in the buffer */
	int	(*lto_encode)(const struct c2_layout *l,
			      struct c2_bufvec_cursor *cur_out);

	/** Adds a new layout record and its related information into the
	    the relevant tables.
	*/
	int (*lto_rec_add)(const struct c2_bufvec_cursor *cur,
			   struct c2_layout_schema *l_schema,
			   struct c2_db_tx *tx);

	/** Deletes a layout record and its related information from the
	    relevant tables.
	*/
	int	(*lto_rec_delete)(const struct c2_bufvec_cursor *cur,
				  struct c2_layout_schema *l_schema,
				  struct c2_db_tx *tx);

	/** Updates a layout record and its related information in the
	    relevant tables.
	*/
	int	(*lto_rec_update)(const struct c2_bufvec_cursor *cur,
				  struct c2_layout_schema *l_schema,
				  struct c2_db_tx *tx);

	/** Locates a layout record and its related information from the
	    relevant tables.
	*/
	int	(*lto_rec_lookup)(const struct c2_layout_id *l_id,
				  struct c2_layout_schema *l_schema,
				  struct c2_db_tx *tx,
				  struct c2_bufvec_cursor *cur_out);
};


/** @todo Change type of l_id to uint64_t (not c2_layout_id).
    Requires changes in pdclust
    In-memory representation of a layout.
*/

struct c2_layout {
	struct c2_uint128		  l_id;
	const struct c2_layout_type	 *l_type;
	const struct c2_layout_enum      *l_enum;
	const struct c2_layout_ops       *l_ops;
};

struct c2_layout_ops {
	void	(*lo_init)(struct c2_layout *lay);
	void	(*lo_fini)(struct c2_layout *lay);
};

/**
   Structure specific to per layout enumeration type.
   There is an instance of c2_layout_enum_type for each one of enumeration
   types. e.g. for FORMULA and LIST enumeration method types.
*/
struct c2_layout_enum_type {
	/** Layout enumeration type name. */
	const char			     *let_name;

	/** Layout enumeration type id. */
	uint64_t			      let_id;

	const struct c2_layout_enum_type_ops *let_ops;
};

struct c2_layout_enum_type_ops {
	/** Continues encoding layout representation stored in the buffer and
	    building the layout.
	*/
	int	(*leto_decode)(const struct c2_bufvec_cursor *cur,
			       struct c2_layout **out);

	/** Continues storing layout representation in the buffer */
	int	(*leto_encode)(const struct c2_layout *l,
			       struct c2_bufvec_cursor *out);

	/** Continues adding the new layout record by adding list of cob ids */
	int	(*leto_rec_add)(const struct c2_bufvec_cursor *cur,
				struct c2_layout_schema *schema,
				struct c2_db_tx *tx);

	/** Continues deleting the layout record */
	int	(*leto_rec_delete)(const struct c2_bufvec_cursor *cur,
				   struct c2_layout_schema *l_schema,
				   struct c2_db_tx *tx);

	/** Continues updating the layout record */
	int	(*leto_rec_update)(const struct c2_bufvec_cursor *cur,
				   struct c2_layout_schema *schema,
				   struct c2_db_tx *tx);

	/** Continue to locates layout record information */
	int	(*leto_rec_lookup)(const struct c2_layout_id *id,
				   struct c2_layout_schema *schema,
				   struct c2_db_tx *tx,
				   struct c2_bufvec_cursor *out);
};

/**
   Layout enumeration method.
*/
struct c2_layout_enum {
	const struct c2_layout_enum_ops *le_ops;
};

struct c2_layout_enum_ops {
	/** Returns number of objects in the enumeration. */
	uint32_t (*leo_nr)(const struct c2_layout_enum *e);

	/** Returns idx-th object in the enumeration.
	    @pre idx < e->l_enum_ops->leo_nr(e)
	*/
	void (*leo_get)(const struct c2_layout_enum *e,
			uint32_t idx,
			struct c2_fid *fid_out);
};


void	c2_layout_init(struct c2_layout *lay);
void	c2_layout_fini(struct c2_layout *lay);

int	c2_layouts_init(void);
void	c2_layouts_fini(void);

int	c2_layout_decode(const struct c2_bufvec_cursor *cur,
			 struct c2_layout **out);
int	c2_layout_encode(const struct c2_layout *l,
			 struct c2_bufvec_cursor *out);

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
