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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 07/09/2010
 */

#ifndef __COLIBRI_LAYOUT_LAYOUT_H__
#define __COLIBRI_LAYOUT_LAYOUT_H__

/**
 * @defgroup layout Layouts.
 *
 * A 'layout' is an attribute of a file. It maps a file onto a set of network
 * resources. viz. component objects.
 *
 * A 'layout type' specifies how a file is stored in a collection of targets.
 * It provides the <offset-in-gob> to <traget-idx, offset-in-target> mapping.
 *
 * An 'enumeration' provides <gfid, target-idx> to <cob-fid> mapping. Not all
 * the layout types need an enumeration. e.g. Layouts with types composite,
 * de-dup do not need an enumeration.
 *
 * An 'enumeration type' determines how a collection of component object
 * identifiers (cob-fid) is specified. e.g. it may be specified as a list or
 * by means of some linear formula.
 *
 * Layout types supported currently are:
 * - PDCLUST <BR>
 *   This layout type applies parity declustering feature to the striping
 *   process. Parity declustering feature is to keep the rebuild overhead low
 *   by striping a file over more servers or drives than there are units in
 *   the parity group.
 * - COMPOSITE <BR>
 *   This layout type partitions a file or a part of the file into
 *   various segments while each of those segment uses a different layout.
 *
 * Enumeration types (also referred as 'enum types') supported currently are:
 * - LINEAR <BR>
 *   A layout with LINEAR enumeration type uses a formula to enumerate all
 *   its component object identifiers.
 * - LIST <BR>
 *   A layout with LIST enumeration type uses a list to enumerate all its
 *   component object identifiers.
 *
 * A layout as well as a layout-id are resources (managed by the 'Colibri
 * Resource Manager').
 *
 * Layout being a resource, it can be cached by clients and revoked when it is
 * changed.
 *
 * Layout Id being a resource, a client can cache a range of layout ids that
 * it uses to create new layouts without contacting the server.
 *
 * A layout can be assignd to a file both by server and the client.
 * @{
 */

/* import */
#include "lib/cdefs.h"
#include "lib/types.h"    /* uint64_t */
#include "lib/tlist.h"    /* struct c2_tl */
#include "lib/mutex.h"    /* struct c2_mutex */

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
 * In-memory representation of a layout.
 */
struct c2_layout {
	/** Layout id. */
	uint64_t                         l_id;

	/** Layout type. */
	const struct c2_layout_type     *l_type;

	/**
	 * Layout reference count.
	 * Indicating how many users this layout has.
	 */
	uint32_t                         l_ref;

	/** Pool identifier. */
	uint64_t                         l_pool_id;

	/**
	 * Lock to protect a c2_layout instance and all its direct/indirect
	 * members.
	 */
	struct c2_mutex                  l_lock;

	/** Layout operations vector. */
	const struct c2_layout_ops      *l_ops;

	struct c2_addb_ctx               l_addb;
};

struct c2_layout_ops {
	/** Cleans up while c2_layout object is about to be destoryed. */
	void    (*lo_fini)(struct c2_layout *lay);
};

/**
 * Layout DB operation on a layout record, performed through either
 * c2_layout_decode or c2_layout_encode routines.
 * C2_LXO_DB_NONE indicates that encode/decode has to operate on a buffer.
 */
enum c2_layout_xcode_op {
	C2_LXO_DB_NONE,
	C2_LXO_DB_LOOKUP,
	C2_LXO_DB_ADD,
	C2_LXO_DB_UPDATE,
	C2_LXO_DB_DELETE,
};

/**
 * Structure specific to per layout type.
 * There is an instance of c2_layout_type for each one of layout types. e.g.
 * for PDCLUST and COMPOSITE layout types.
 */
struct c2_layout_type {
	/** Layout type name. */
	const char                      *lt_name;

	/** Layout type id. */
	uint32_t                         lt_id;

	/** Layout type operations vector. */
	const struct c2_layout_type_ops *lt_ops;
};

struct c2_layout_type_ops {
	/**
	 * Allocates layout type specific schema data.
	 * e.g. comp_layout_ext_map table.
	 */
	int        (*lto_register)(struct c2_ldb_schema *schema,
				   const struct c2_layout_type *lt);

	/** Deallocates layout type specific schema data. */
	void       (*lto_unregister)(struct c2_ldb_schema *schema,
				     const struct c2_layout_type *lt);

	/** Returns applicable max record size for the layouts table. */
	uint32_t   (*lto_max_recsize)(struct c2_ldb_schema *schema);

	/**
	 * Returns applicable record size for the layouts table, for the
	 * specified layout.
	 */
	int        (*lto_recsize)(struct c2_ldb_schema *schema,
				  struct c2_layout *l, uint32_t *recsize);

	/**
	 * Continues building the in-memory layout object either from the
	 * buffer or from the DB.
	 * Allocates an instance of some layout-type specific data-type
	 * which embeds c2_layout. Sets c2_layout::l_ops.
	 */
	int    (*lto_decode)(struct c2_ldb_schema *schema, uint64_t lid,
			     uint64_t pool_id,
			     struct c2_bufvec_cursor *cur,
			     enum c2_layout_xcode_op op,
			     struct c2_db_tx *tx,
			     struct c2_layout **out);

	/**
	 * Continues storing the layout representation either in the buffer
	 * or in the DB.
	 */
	int    (*lto_encode)(struct c2_ldb_schema *schema,
			     struct c2_layout *l,
			     enum c2_layout_xcode_op op,
			     struct c2_db_tx *tx,
			     struct c2_bufvec_cursor *oldrec_cur,
			     struct c2_bufvec_cursor *out);
};

/**
 *  Layout enumeration.
 */
struct c2_layout_enum {
	/** Layout enumeration type. */
	const struct c2_layout_enum_type *le_type;

	/** Layout id for the layout this enum is associated with. */
	uint64_t                          le_lid;

	const struct c2_layout_enum_ops  *le_ops;
};

struct c2_layout_enum_ops {
	/** Returns number of objects in the enumeration. */
	uint32_t (*leo_nr)(const struct c2_layout_enum *e, uint64_t lid);

	/**
	 * Returns idx-th object in the enumeration.
	 * @pre idx < e->l_enum_ops->leo_nr(e)
	 */
	void     (*leo_get)(const struct c2_layout_enum *e, uint64_t lid,
			    uint32_t idx, const struct c2_fid *gfid,
			    struct c2_fid *out);

	void     (*leo_fini)(struct c2_layout_enum *e, uint64_t lid);
};

/**
 * Structure specific to per layout enumeration type.
 * There is an instance of c2_layout_enum_type for each one of enumeration
 * types. e.g. for LINEAR and LIST enumeration types.
 */
struct c2_layout_enum_type {
	/** Layout enumeration type name. */
	const char                           *let_name;

	/** Layout enumeration type id. */
	uint32_t                              let_id;

	/** Layout enumeration type operations vector. */
	const struct c2_layout_enum_type_ops *let_ops;
};

struct c2_layout_enum_type_ops {
	/**
	 * Allocates enumeration type specific schema data.
	 * e.g. cob_lists table.
	 */
	int        (*leto_register)(struct c2_ldb_schema *schema,
				    const struct c2_layout_enum_type *et);

	/** Deallocates enumeration type specific schema data. */
	void       (*leto_unregister)(struct c2_ldb_schema *schema,
				      const struct c2_layout_enum_type *et);

	/** Returns applicable max record size for the layouts table. */
	uint32_t   (*leto_max_recsize)(void);

	/**
	 * Returns applicable record size for the layouts table, for the
	 * specified layout.
	 */
	uint32_t   (*leto_recsize)(struct c2_layout_enum *e, uint64_t lid);

	/**
	 * Continues building the in-memory layout object, either from
	 * the buffer or from the DB.
	 */
	int        (*leto_decode)(struct c2_ldb_schema *schema,
				  uint64_t lid,
				  struct c2_bufvec_cursor *cur,
				  enum c2_layout_xcode_op op,
				  struct c2_db_tx *tx,
				  struct c2_layout_enum **out);

	/**
	 * Continues storing layout representation either in the buffer
	 * or in the DB.
	 */
	int        (*leto_encode)(struct c2_ldb_schema *schema,
				  struct c2_layout *l,
				  enum c2_layout_xcode_op op,
				  struct c2_db_tx *tx,
				  struct c2_bufvec_cursor *oldrec_cur,
				  struct c2_bufvec_cursor *out);
};

/**
    Layout using enumeration.
*/
struct c2_layout_striped {
	/** super class */
	struct c2_layout           ls_base;

	/** Layout enumeration. */
	struct c2_layout_enum      *ls_enum;
};


int c2_layouts_init(void);
void c2_layouts_fini(void);

int c2_layout_init(struct c2_layout *lay,
		   uint64_t lid,
		   uint64_t pool_id,
		   const struct c2_layout_type *type,
		   const struct c2_layout_ops *ops);
void c2_layout_fini(struct c2_layout *lay);

int c2_layout_striped_init(struct c2_layout_striped *str_lay,
			   struct c2_layout_enum *e,
			   uint64_t lid, uint64_t pool_id,
			   const struct c2_layout_type *type,
			   const struct c2_layout_ops *ops);
void c2_layout_striped_fini(struct c2_layout_striped *strl);

int c2_layout_enum_init(struct c2_layout_enum *le, uint64_t lid,
			const struct c2_layout_enum_type *lt,
			const struct c2_layout_enum_ops *ops);
void c2_layout_enum_fini(struct c2_layout_enum *le);


int c2_layout_decode(struct c2_ldb_schema *schema, uint64_t lid,
		     struct c2_bufvec_cursor *cur,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_layout **out);
int c2_layout_encode(struct c2_ldb_schema *schema,
		     struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *oldrec_cur,
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
