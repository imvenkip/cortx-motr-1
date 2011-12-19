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
 *                  Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 07/09/2010
 */

#ifndef __COLIBRI_LAYOUT_LAYOUT_H__
#define __COLIBRI_LAYOUT_LAYOUT_H__

/**
   @defgroup layout Layouts.

   A 'layout' is an attribute of a file. It maps a file onto a set of network
   resources viz. component objects. Thus, it enlists identifiers for all the
   component objects one file maps to.

   A layout is a resource (managed by the 'Colibri Resource Manager'). A layout
   can be assignd to a file both by server and the client.

   A 'layout type' specifies how a file is stored in a collection of component
   objects.

   An 'enumeration method' determines how a collection of component objects is
   specified. e.g. it may be specified as a list or by means of some linear
   formula.

   Layout types supported currently are:
   - PDCLUST <BR>
     This layout type applies parity declustering feature to the striping
     process. Parity declustering feature is to keep the rebuild overhead low
     by striping a file over more servers or drives than there are units in
     the parity group.
   - COMPOSITE <BR>
     This layout type partitions a file or a part of the file into
     various segments while each of those segment uses a different layout.

   Enumeration method types (also referred as 'enumeration types' or 'enum
   types') supported currently are:
   - LINEAR <BR>
     A layout with LINEAR enumeration method uses a formula to enumerate all
     its component object identifiers.
   - LIST <BR>
     A layout with LIST enumeration method uses a list to enumerate all its
     component object identifiers.

   @{
*/

/* import */
#include "lib/cdefs.h"
#include "lib/types.h"    /* uint64_t */
#include "lib/tlist.h"    /* struct c2_tl */

struct c2_bufvec_cursor;
struct c2_fid;
struct c2_db_tx;
struct c2_db_pair;
struct c2_ldb_rec;
struct c2_ldb_schema;

/* export */
struct c2_layout;
struct c2_layout_ops;
enum c2_layout_xcode_op;
struct c2_layout_type;
struct c2_layout_type_ops;
struct c2_layout_enum;
struct c2_layout_enum_ops;
struct c2_layout_enum_type;
struct c2_layout_enum_type_ops;

/**
    In-memory representation of a layout.
*/
struct c2_layout {
	/** Layout id. */
	uint64_t				 l_id;

	/** Layout type. */
	const struct c2_layout_type		*l_type;

	/** Layout enumeration. */
	const struct c2_layout_enum		*l_enum;

	/** Layout reference count.
	    Indicating how many files are using this layout.
	*/
	uint64_t				 l_ref;

	/** Layout operations vector. */
	const struct c2_layout_ops		*l_ops;
};

struct c2_layout_ops {
	/** Cleans up while c2_layout object is about to be destoryed. */
	void	(*lo_fini)(struct c2_layout *lay);
};

/**
   Layout DB operation on a layout record.
   C2_LXO_NONE indicates that encode/decode has to operate on a buffer.
*/
enum c2_layout_xcode_op {
	C2_LXO_NONE,
	C2_LXO_ADD,
	C2_LXO_UPDATE,
	C2_LXO_DELETE,
	C2_LXO_LOOKUP,
};

/**
   Structure specific to per layout type.
   There is an instance of c2_layout_type for each one of layout types. e.g.
   for PDCLUST and COMPOSITE layout types.
*/
struct c2_layout_type {
	/** Layout type name. */
	const char				*lt_name;

	/** Layout type id. */
	uint64_t				 lt_id;

	/** Layout type operations vector. */
	const struct c2_layout_type_ops		*lt_ops;
};

struct c2_layout_type_ops {
	/** Allocates layout type specific schema data.
	    e.g. comp_layout_ext_map table.
	*/
	int	(*lto_register)(struct c2_ldb_schema *schema,
				const struct c2_layout_type *lt);

	/** Deallocates layout type specific schema data. */
	int	(*lto_unregister)(struct c2_ldb_schema *schema,
				  const struct c2_layout_type *lt);

	/** Compares two layouts. */
	bool	(*lto_equal)(const struct c2_layout *l0,
			     const struct c2_layout *l1);

	/** Continues building the in-memory layout object either from the
	    buffer or from the DB.
	    Allocates an instance of some layout-type specific data-type
	    which embeds c2_layout.
	    Sets c2_layout::l_ops.
	*/
	int	(*lto_decode)(struct c2_ldb_schema *schema, uint64_t lid,
			      const struct c2_bufvec_cursor *cur,
			      enum c2_layout_xcode_op op,
			      struct c2_db_tx *tx,
			      struct c2_layout **out);

	/** Continues storing the layout representation either in the buffer
	    or in the DB. */
	int	(*lto_encode)(struct c2_ldb_schema *schema,
			      const struct c2_layout *l,
			      enum c2_layout_xcode_op op,
			      struct c2_db_tx *tx,
			      struct c2_bufvec_cursor *out);

	/** In case of a layouts using linear enumeration type, substitutes
	    attributes and parameters into the formula and obtains list of COB
	    identifiers.
	    Defining this function is applicable for the layout types which may
	    use linear enumeration type e.g. PDCLUST layout type.
	*/
	int	(*lto_subst)(const struct c2_layout *l,
			     struct c2_tl *outlist,
			     struct c2_fid gfid);
};

/**
   Layout enumeration method.
*/
struct c2_layout_enum {
	/** Pointer back to c2_layout object this c2_layout_enum is part of. */
	const struct c2_layout		*le_lptr;

	const struct c2_layout_enum_ops	*le_ops;
};

struct c2_layout_enum_ops {
	/** Returns number of objects in the enumeration. */
	uint32_t	(*leo_nr)(const struct c2_layout_enum *e,
				  struct c2_fid *gfid);

	/** Returns idx-th object in the enumeration.
	    @pre idx < e->l_enum_ops->leo_nr(e)
	*/
	void		(*leo_get)(const struct c2_layout_enum *e,
				   uint32_t idx,
				   struct c2_fid *gfid,
				   struct c2_fid *out);
};

/**
   Structure specific to per layout enumeration type.
   There is an instance of c2_layout_enum_type for each one of enumeration
   types. e.g. for LINEAR and LIST enumeration method types.
*/
struct c2_layout_enum_type {
	/** Layout enumeration type name. */
	const char				*let_name;

	/** Layout enumeration type id. */
	uint64_t				 let_id;

	/** Layout enumeration type operations vector. */
	const struct c2_layout_enum_type_ops	*let_ops;
};

struct c2_layout_enum_type_ops {
	/** Allocates enumeration type specific schema data.
	    e.g. cob_lists table.
	*/
	int	(*leto_register)(struct c2_ldb_schema *schema,
				 const struct c2_layout_enum_type *et);

	/** Deallocates enumeration type specific schema data. */
	int	(*leto_unregister)(struct c2_ldb_schema *schema,
				   const struct c2_layout_enum_type *et);

	/** Continues building the in-memory layout object, either from
	    the buffer or from the DB. */
	int	(*leto_decode)(struct c2_ldb_schema *schema,
			       uint64_t lid,
			       const struct c2_bufvec_cursor *cur,
			       enum c2_layout_xcode_op op,
			       struct c2_db_tx *tx,
			       struct c2_layout **out);

	/** Continues storing layout representation either in the buffer
	    or in the DB. */
	int	(*leto_encode)(struct c2_ldb_schema *schema,
			       const struct c2_layout *l,
			       enum c2_layout_xcode_op op,
			       struct c2_db_tx *tx,
			       struct c2_bufvec_cursor *out);
};

int	c2_layouts_init(void);
void	c2_layouts_fini(void);

void	c2_layout_init(struct c2_layout *lay,
		       const uint64_t lid,
		       const struct c2_layout_type *type,
		       const struct c2_layout_enum *e,
		       const struct c2_layout_ops *ops);
void	c2_layout_fini(struct c2_layout *lay);

int	c2_layout_decode(struct c2_ldb_schema *schema, uint64_t lid,
			 const struct c2_bufvec_cursor *cur,
			 enum c2_layout_xcode_op op,
			 struct c2_db_tx *tx,
			 struct c2_layout **out);
int	c2_layout_encode(struct c2_ldb_schema *schema,
			 const struct c2_layout *l,
			 enum c2_layout_xcode_op op,
			 struct c2_db_tx *tx,
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
