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
 * For example, PDCLUST, RAID1, RAID5 are some types of layout, while
 * COMPOSITE being another special layout type.
 *
 * An 'enumeration' provides <gfid, target-idx> to <cob-fid> mapping. Not all
 * the layout types need an enumeration. For example, layouts with types
 * composite, de-dup do not need an enumeration.
 *
 * An 'enumeration type' determines how a collection of component object
 * identifiers (cob-fid) is specified. For example, it may be specified as a
 * list or by means of some linear formula.
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
 * A layout can be assigned to a file both by server and the client.
 *
 * The sequence of operation related to domain intialisation/finalisation,
 * layout type and enum type registration and unregistration is as follows:
 * - Initialise c2_layout_domain object.
 * - Register layout types and enum types using c2_layout_register().
 * - Perform various required operations including usage of c2_layout_encode(),
 *   c2_layout_decode(), c2_layout_lookup(), c2_layout_add(),
 *   c2_layout_update(), c2_layout_delete(), leo_nr(), leo_get().
 * - Unregister layout types and enum types using c2_layout_unregister.
 * - Finalise c2_layout_domain object.
 *
 * Regarding client/server access to various APIs from layout and layout-DB
 * modules:
 * - The APIs exported through layout.h are available both to the client and
 *   the server.
 * - the APIs exported through layout_db.h are available only to the server.
 *
 * @{
 */

/* import */
#include "lib/types.h"    /* uint64_t */
#include "lib/tlist.h"    /* struct c2_tl */
#include "lib/mutex.h"    /* struct c2_mutex */

#include "db/db.h"        /* struct c2_table */
#include "addb/addb.h"

struct c2_addb_ctx;
struct c2_bufvec_cursor;
struct c2_fid;

/* export */
struct c2_layout_schema;
struct c2_layout_domain;
struct c2_layout;
struct c2_layout_ops;
enum c2_layout_xcode_op;
struct c2_layout_type;
struct c2_layout_type_ops;
struct c2_layout_enum;
struct c2_layout_enum_ops;
struct c2_layout_enum_type;
struct c2_layout_enum_type_ops;

enum {
	C2_LAYOUT_TYPE_MAX      = 32,
	C2_LAYOUT_ENUM_TYPE_MAX = 32
};

/**
 * In-memory data structure for the layout schema.
 * It includes a pointer to the layouts table and some related
 * parameters. ls_type_data[] and ls_enum_data[] store pointers to tables
 * applicable, if any, for various layout types and enum types.
 * There is one instance of layout domain object per address space.
 */
struct c2_layout_schema {
	/** Pointer to dbenv; to keep things together. */
	struct c2_dbenv         *ls_dbenv;

	/** Table for layout record entries. */
	struct c2_table          ls_layouts;

	/** Layout type specific data. */
	void                    *ls_type_data[C2_LAYOUT_TYPE_MAX];

	/** Layout enum type specific data. */
	void                    *ls_enum_data[C2_LAYOUT_ENUM_TYPE_MAX];

	/** Maximum possible size for a record in the layouts table. */
	c2_bcount_t              ls_max_recsize;

	/**
	 * Lock to protect the instance of c2_layout_schema, including all
	 * its members.
	 */
	struct c2_mutex          ls_lock;
};

/**
 * Layout domain.
 * There is one instance of layout domain object per address space.
 */
struct c2_layout_domain {
	/** Layout types array. */
	struct c2_layout_type      *ld_type[C2_LAYOUT_TYPE_MAX];

	/** Enumeration types array. */
	struct c2_layout_enum_type *ld_enum[C2_LAYOUT_ENUM_TYPE_MAX];

	/** Reference count on layout types. */
	uint32_t                    ld_type_ref_count[C2_LAYOUT_TYPE_MAX];

	/** Reference count on enum types. */
	uint32_t                    ld_enum_ref_count[C2_LAYOUT_ENUM_TYPE_MAX];

	/** c2_layout_schema object. */
	struct c2_layout_schema     ld_schema;

	/**
	 * Lock to protect the instance of c2_layout_domain, including all
	 * its members.
	 */
	struct c2_mutex             ld_lock;
};

/**
 * In-memory representation of a layout.
 */
struct c2_layout {
	/** Layout id. */
	uint64_t                     l_id;

	/** Layout type. */
	const struct c2_layout_type *l_type;

	/* Layout reference count, indicating how many users this layout has. */
	uint32_t                     l_ref;

	/** Pool identifier. */
	uint64_t                     l_pool_id;

	/**
	 * Lock to protect a c2_layout instance and all its direct/indirect
	 * members.
	 */
	struct c2_mutex              l_lock;

	/** Layout operations vector. */
	const struct c2_layout_ops  *l_ops;

	struct c2_addb_ctx           l_addb;
};

struct c2_layout_ops {
	/** Cleans up while c2_layout object is about to be destoryed. */
	void    (*lo_fini)(struct c2_layout *l, struct c2_layout_domain *dom);
};

/**
 * Operation on a layout record, performed through either c2_layout_decode()
 * or c2_layout_encode() routines.
 * C2_LXO_BUFFER_OP indicates that c2_layout_decode()/c2_layout_encode() has
 * to operate upon a buffer.
 */
enum c2_layout_xcode_op {
	C2_LXO_BUFFER_OP, /* Operate on a buffer. */
	C2_LXO_DB_LOOKUP, /* Lookup for layout from the DB. */
	C2_LXO_DB_ADD,    /* Add layout to the DB. */
	C2_LXO_DB_UPDATE, /* Update layout in the DB. */
	C2_LXO_DB_DELETE  /* Delete layout from the DB. */
};

/**
 * Structure specific to a layout type.
 * There is an instance of c2_layout_type for each one of the layout types.
 * For example, for PDCLUST and COMPOSITE layout types.
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
	 * For example, comp_layout_ext_map table.
	 */
	int         (*lto_register)(struct c2_layout_domain *dom,
				    const struct c2_layout_type *lt);

	/** Deallocates layout type specific schema data. */
	void        (*lto_unregister)(struct c2_layout_domain *dom,
				      const struct c2_layout_type *lt);

	/** Returns applicable max record size for the layouts table. */
	c2_bcount_t (*lto_max_recsize)(struct c2_layout_domain *dom);

	/**
	 * Returns applicable record size for the layouts table, for the
	 * specified layout.
	 */
	c2_bcount_t (*lto_recsize)(struct c2_layout_domain *dom,
				   struct c2_layout *l);

	/**
	 * Continues building the in-memory layout object either from the
	 * buffer or from the DB.
	 * Allocates an instance of some layout-type specific data-type
	 * which embeds c2_layout and stores the resultant c2_layout object
	 * in the parameter out.
	 * Internally, sets c2_layout::l_ops.
	 */
	int         (*lto_decode)(struct c2_layout_domain *dom,
				  uint64_t lid, uint64_t pool_id,
				  struct c2_bufvec_cursor *cur,
				  enum c2_layout_xcode_op op,
				  struct c2_db_tx *tx,
				  struct c2_layout **out);

	/**
	 * Continues storing the layout representation either in the buffer
	 * provided by the caller or in the DB.
	 */
	int         (*lto_encode)(struct c2_layout_domain *dom,
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

	void     (*leo_fini)(struct c2_layout_domain *dom,
			     struct c2_layout_enum *e, uint64_t lid);
};

/**
 * Structure specific to a layout enumeration type.
 * There is an instance of c2_layout_enum_type for each one of enumeration
 * types. For example, for LINEAR and LIST enumeration types.
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
	 * For example, cob_lists table.
	 */
	int         (*leto_register)(struct c2_layout_domain *dom,
				     const struct c2_layout_enum_type *et);

	/** Deallocates enumeration type specific schema data. */
	void        (*leto_unregister)(struct c2_layout_domain *dom,
				       const struct c2_layout_enum_type *et);

	/** Returns applicable max record size for the layouts table. */
	c2_bcount_t (*leto_max_recsize)(void);

	/**
	 * Returns applicable record size for the layouts table, for the
	 * specified layout.
	 */
	c2_bcount_t (*leto_recsize)(struct c2_layout_enum *e, uint64_t lid);

	/**
	 * Continues building the in-memory layout object, either from
	 * the buffer or from the DB.
	 * Allocates an instance of some enum-type specific data-type
	 * which embeds c2_layout_enum and stores the resultant
	 * c2_layout_enum object in the parameter out.
	 * Internally, sets c2_layout_enum::le_ops.
	 */
	int         (*leto_decode)(struct c2_layout_domain *dom,
				   uint64_t lid,
				   struct c2_bufvec_cursor *cur,
				   enum c2_layout_xcode_op op,
				   struct c2_db_tx *tx,
				   struct c2_layout_enum **out);

	/**
	 * Continues storing layout representation either in the buffer
	 * provided by the caller or in the DB.
	 */
	int         (*leto_encode)(struct c2_layout_domain *dom,
				   const struct c2_layout_enum *le,
				   uint64_t lid,
				   enum c2_layout_xcode_op op,
				   struct c2_db_tx *tx,
				   struct c2_bufvec_cursor *oldrec_cur,
				   struct c2_bufvec_cursor *out);
};

/**
 *  Layout using enumeration.
 */
struct c2_layout_striped {
	/** Super class. */
	struct c2_layout       ls_base;

	/** Layout enumeration. */
	struct c2_layout_enum *ls_enum;
};

int c2_layouts_init(void);
void c2_layouts_fini(void);

int c2_layout_domain_init(struct c2_layout_domain *dom, struct c2_dbenv *db);
void c2_layout_domain_fini(struct c2_layout_domain *dom);

int c2_layout_register(struct c2_layout_domain *dom);
void c2_layout_unregister(struct c2_layout_domain *dom);

int c2_layout_type_register(struct c2_layout_domain *dom,
			    const struct c2_layout_type *lt);
void c2_layout_type_unregister(struct c2_layout_domain *dom,
			       const struct c2_layout_type *lt);

int c2_layout_enum_type_register(struct c2_layout_domain *dom,
				 const struct c2_layout_enum_type *et);
void c2_layout_enum_type_unregister(struct c2_layout_domain *dom,
				    const struct c2_layout_enum_type *et);

void c2_layout_get(struct c2_layout *l);
void c2_layout_put(struct c2_layout *l);

int c2_layout_decode(struct c2_layout_domain *dom,
		     uint64_t lid, struct c2_bufvec_cursor *cur,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_layout **out);
int c2_layout_encode(struct c2_layout_domain *dom,
		     struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *oldrec_cur,
		     struct c2_bufvec_cursor *out);

c2_bcount_t c2_layout_max_recsize(struct c2_layout_domain *dom);

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
