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
 * @section layout-terminology Terminology
 * - Layout @n
 *   A 'layout' is an attribute of a file. It maps a file onto a set of network
 *   resources. viz. component objects.
 *
 * - Layout type @n
 *   A 'layout type' specifies how a file is stored in a collection of targets.
 *   It provides the <offset-in-gob> to <traget-idx, offset-in-target> mapping.
 *   For example, PDCLUST, RAID1, RAID5 are some types of layout, while
 *   COMPOSITE being another special layout type.
 *
 * - Enumeration @n
 *   An 'enumeration' provides <gfid, target-idx> to <cob-fid> mapping. Not all
 *   the layout types need an enumeration. For example, layouts with types
 *   composite, de-dup do not need an enumeration.
 *
 * - Enumeration type @n
 *   An 'enumeration type' determines how a collection of component object
 *   identifiers (cob-fid) is specified. For example, it may be specified as a
 *   list or by means of some linear formula.
 *
 * @section layout-types-supported Supported layout and enum types
 * - Layout types supported currently are:
 *   - PDCLUST @n
 *     This layout type applies parity declustering feature to the striping
 *     process. Parity declustering feature is to keep the rebuild overhead low
 *     by striping a file over more servers or drives than there are units in
 *     the parity group.
 *   - COMPOSITE @n
 *     This layout type partitions a file or a part of the file into
 *     various segments while each of those segment uses a different layout.
 *
 * - Enumeration types (also referred as 'enum types') supported currently are:
 *   - LINEAR @n
 *     A layout with LINEAR enumeration type uses a formula to enumerate all
 *     its component object identifiers.
 *   - LIST @n
 *     A layout with LIST enumeration type uses a list to enumerate all its
 *     component object identifiers.
 *
 * @section layout-managed-resources Layout Managed Resources
 * - A layout as well as a layout-id are resources (managed by the 'Colibri
 *   Resource Manager').
 * - Layout being a resource, it can be cached by the clients and can be
 *   revoked when it is changed.
 * - Layout Id being a resource, a client can cache a range of layout ids that
 *   it uses to create new layouts without contacting the server.
 * - A layout can be assigned to a file both by the server and the client.
 *
 * @section layout-operations-sequence Sequence Of Layout Operation
 * The sequence of operation related to domain intialisation/finalisation,
 * layout type and enum type registration and unregistration is as follows:
 * - Initialise c2_layout_domain object.
 * - Register layout types and enum types using c2_layout_all_types_register().
 * - Perform various required operations including usage of c2_pdclust_build(),
 *   c2_layout_encode(), c2_layout_decode(), c2_layout_lookup(),
 *   c2_layout_add(), c2_layout_update(), c2_layout_delete(), leo_nr(),
 *   leo_get().
 * - Unregister layout types and enum types using
 *   c2_layout_iall_types_unregister().
 * - Finalise c2_layout_domain object.
 *
 * @section layout-client-server-access Client Server Access to APIs
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
struct c2_striped_layout;

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

	/** List of pointers for layout objects associated with this domain. */
	struct c2_tl                ld_layout_list;

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
	struct c2_layout_type       *l_type;

	/** Layout domain this layout object is part of. */
	struct c2_layout_domain     *l_dom;

	/* Layout reference count, indicating how many users this layout has. */
	uint32_t                     l_ref;

	/**
	 * Lock to protect a c2_layout instance and all its direct/indirect
	 * members.
	 */
	struct c2_mutex              l_lock;

	/** Layout operations vector. */
	const struct c2_layout_ops  *l_ops;

	struct c2_addb_ctx           l_addb;

	/** Magic number set while c2_layout object is initialised. */
	uint64_t                     l_magic;

	/**
	 * Linkage used for maintaining list of the layout objects stored in
	 * the c2_layout_domain object.
	 */
	struct c2_tlink              l_list_linkage;
};

struct c2_layout_ops {
	/**
	 * Finalises the layout object. It involves finalising its enumeration
	 * object, if applicable.
	 */
	void        (*lo_fini)(struct c2_layout *l);

	/**
	 * Finalises the layout object that is only allocated and not
	 * populated. Since it is not populated, it does not contain
	 * enumeration object.
	 */
	void        (*lo_delete)(struct c2_layout *l);

	/**
	 * Returns size of the layout type specific and enum type specific data
	 * stored in the "layouts" (primary) table, for the specified layout.
	 * @invariant l->l_ops->lo_recsize(l)
	 *            <= l->l_type->lt_ops->lto_max_recsize(l->l_dom);
	 */
	c2_bcount_t (*lo_recsize)(const struct c2_layout *l);
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
 * Any layout type can be registered with only one domain, at a time.
 */
struct c2_layout_type {
	/** Layout type name. */
	const char                      *lt_name;

	/** Layout type id. */
	uint32_t                         lt_id;

	/** Layout domain with which the layout type is registered. */
	struct c2_layout_domain         *lt_domain;

	/**
	 * Layout type reference count, indicating 'how many layout objects
	 * using this layout type' exist in the domain the layout type is
	 * registered with.
	 */
	uint32_t                         lt_ref_count;

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
	 * Allocates an instance of some layout-type specific data-type
	 * which embeds c2_layout and stores the resultant c2_layout object
	 * in the parameter out.
	 * @post ergo(result == 0, *out != NULL && (*out)->l_ops != NULL)
	 */
	int         (*lto_allocate)(struct c2_layout_domain *dom,
				    uint64_t lid,
				    struct c2_layout **out);

	/**
	 * Continues building the in-memory layout object either from the
	 * buffer or from the DB.
	 * @pre C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_BUFFER_OP))
	 * @pre ergo(op == C2_LXO_DB_LOOKUP, tx != NULL)
	 */
	// todo Make lto_decode operation of a layout, not layout type
	int         (*lto_decode)(struct c2_layout *l,
				  enum c2_layout_xcode_op op,
				  struct c2_db_tx *tx,
				  struct c2_bufvec_cursor *cur,
				  uint32_t ref_count);

	/**
	 * Continues storing the layout representation either in the buffer
	 * provided by the caller or in the DB.
	 * @pre C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE,
	 *                 C2_LXO_DB_DELETE, C2_LXO_BUFFER_OP))
	 * @pre ergo(op != C2_LXO_BUFFER_OP, tx != NULL)
	 */
	int         (*lto_encode)(struct c2_layout *l,
				  enum c2_layout_xcode_op op,
				  struct c2_db_tx *tx,
				  struct c2_bufvec_cursor *oldrec_cur,
				  struct c2_bufvec_cursor *out);
};

/**
 * Layout enumeration.
 */
struct c2_layout_enum {
	/** Layout enumeration type. */
	struct c2_layout_enum_type      *le_type;

	/** Striped layout object this enum is associated with. */
	const struct c2_striped_layout  *le_sl;

	/** Enum operations vector. */
	const struct c2_layout_enum_ops *le_ops;

	/** Magic number set while c2_layout_enum object is initialised. */
	uint64_t                         le_magic;
};

struct c2_layout_enum_ops {
	/** Returns number of objects in the enumeration. */
	uint32_t    (*leo_nr)(const struct c2_layout_enum *e);

	/**
	 * Returns idx-th object in the enumeration.
	 * @pre idx < e->l_enum_ops->leo_nr(e)
	 */
	void        (*leo_get)(const struct c2_layout_enum *e, uint32_t idx,
			       const struct c2_fid *gfid, struct c2_fid *out);

	/**
	 * Returns size of the enum type specific data stored in the "layouts"
	 * (primary) table, for the specified enumeration.
	 * @invariant e->le_ops->leo_recsize(e)
	 *            <= e->le_type->let_ops->leto_max_recsize();
	 */
	c2_bcount_t (*leo_recsize)(struct c2_layout_enum *e);

	void        (*leo_fini)(struct c2_layout_enum *e);

	/**
	 * Finalises the enum object that is only allocated and not
	 * populated.
	 */
	void        (*leo_delete)(struct c2_layout_enum *e);
};

/**
 * Structure specific to a layout enumeration type.
 * There is an instance of c2_layout_enum_type for each one of enumeration
 * types. For example, for LINEAR and LIST enumeration types.
 * Any enumeration type can be registered with only one domain, at a time.
 */
struct c2_layout_enum_type {
	/** Layout enumeration type name. */
	const char                           *let_name;

	/** Layout enumeration type id. */
	uint32_t                              let_id;

	/** Layout domain with which the enum type is registered. */
	struct c2_layout_domain              *let_domain;

	/**
	 * Enum type reference count, indicating 'how many enum objects
	 * using this enum type' exist in the domain the enum type is
	 * registered with.
	 */
	uint32_t                              let_ref_count;

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
	 * Allocates an instance of some enum-type specific data-type
	 * which embeds c2_layout_enum and stores the resultant
	 * c2_layout_enum object in the parameter out.
	 * @post ergo(result == 0, *out != NULL && (*out)->le_ops != NULL)
	 */
	int         (*leto_allocate)(struct c2_layout_domain *dom,
				     struct c2_layout_enum **out);

	/**
	 * Continues building the in-memory layout object, either from
	 * the buffer or from the DB.
	 * @pre C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_BUFFER_OP))
	 * @pre ergo(op == C2_LXO_DB_LOOKUP, tx != NULL)
	 */
	// todo Make leto_decode operation of enumeration, not enum type
	int         (*leto_decode)(struct c2_layout_enum *e,
				   struct c2_striped_layout *stl,
				   enum c2_layout_xcode_op op,
				   struct c2_db_tx *tx,
				   struct c2_bufvec_cursor *cur);

	/**
	 * Continues storing layout representation either in the buffer
	 * provided by the caller or in the DB.
	 * @pre C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE,
	 *                 C2_LXO_DB_DELETE, C2_LXO_BUFFER_OP))
	 * @pre ergo(op != C2_LXO_BUFFER_OP, tx != NULL)
	 */
	int         (*leto_encode)(const struct c2_layout_enum *le,
				   enum c2_layout_xcode_op op,
				   struct c2_db_tx *tx,
				   struct c2_bufvec_cursor *oldrec_cur,
				   struct c2_bufvec_cursor *out);
};

/** Layout using enumeration. */
struct c2_striped_layout {
	/** Super class. */
	struct c2_layout       sl_base;

	/** Layout enumeration. */
	struct c2_layout_enum *sl_enum;
};

int c2_layouts_init(void);
void c2_layouts_fini(void);

/**
 * Initialises layout domain - Initialises arrays to hold the objects for
 * layout types and enum types and initialises the schema object.
 * @pre Caller should have performed c2_dbenv_init() on dbenv.
 */
int c2_layout_domain_init(struct c2_layout_domain *dom, struct c2_dbenv *db);

/**
 * Finalises the layout domain.
 * @pre All the layout types and enum types should be unregistered.
 */
void c2_layout_domain_fini(struct c2_layout_domain *dom);

/** Registers all the standard layout types and enum types. */
int c2_layout_all_types_register(struct c2_layout_domain *dom);

/** Unrgisters all the standard layout types and enum types. */
void c2_layout_all_types_unregister(struct c2_layout_domain *dom);

/**
 * Registers a new layout type with the layout types maintained by
 * c2_layout_domain::ld_type[] and initialises layout type specific tables,
 * if applicable.
 */
int c2_layout_type_register(struct c2_layout_domain *dom,
			    struct c2_layout_type *lt);

/**
 * Unregisters a layout type from the layout types maintained by
 * c2_layout_domain::ld_type[] and finalises layout type specific tables,
 * if applicable.
 */
void c2_layout_type_unregister(struct c2_layout_domain *dom,
			       struct c2_layout_type *lt);

/**
 * Registers a new enumeration type with the enumeration types
 * maintained by c2_layout_domain::ld_enum[] and initialises enum type
 * specific tables, if applicable.
 */
int c2_layout_enum_type_register(struct c2_layout_domain *dom,
				 struct c2_layout_enum_type *et);

/**
 * Unregisters an enumeration type from the enumeration types
 * maintained by c2_layout_domain::ld_enum[] and finalises enum type
 * specific tables, if applicable.
 */
void c2_layout_enum_type_unregister(struct c2_layout_domain *dom,
				    struct c2_layout_enum_type *et);

/**
 * Finds a layout with the given identifier, from the list of layout objects
 * maintained in the layout domain.
 */
struct c2_layout *c2_layout_find(struct c2_layout_domain *dom, uint64_t lid);

/** Adds a reference to the layout. */
void c2_layout_get(struct c2_layout *l);

/**
 * Releases a reference on the layout. If it is the last reference being
 * released, then it removes the layout entry from the layout list
 * maintained in the layout domain and then finalises the layout along
 * with finalising its enumeration object, if applicable.
 */
void c2_layout_put(struct c2_layout *l);

/**
 * This method
 * - Either continues to build an in-memory layout object from its
 *   representation 'stored in the Layout DB'
 * - Or builds an in-memory layout object from its representation 'received
 *   through a buffer'.
 *
 * Two use cases of c2_layout_decode()
 * - Server decodes an on-disk layout record by reading it from the Layout
 *   DB, into an in-memory layout structure, using c2_layout_lookup() which
 *   internally calls c2_layout_decode().
 * - Client decodes a buffer received over the network, into an in-memory
 *   layout structure, using c2_layout_decode().
 *
 * @param cur Cursor pointing to a buffer containing serialised representation
 * of the layout. Regarding the size of the buffer:
 * - In case c2_layout_decode() is called through c2_layout_add(), then the
 *   buffer should be containing all the data that is read specifically from
 *   the layouts table. It means its size needs to be at the most the size
 *   returned by c2_layout_max_recsize().
 * - In case c2_layout_decode() is called by some other caller, then the
 *   buffer should be containing all the data belonging to the specific layout.
 *   It may include data that spans over tables other than layouts as well. It
 *   means its size may need to be even more than the one returned by
 *   c2_layout_max_recsize(). For example, in case of LIST enumeration type,
 *   the buffer needs to contain the data that is stored in the cob_lists table.
 *
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record. It could be LOOKUP if at all a DB operation.
 * If it is BUFFER_OP, then the layout is decoded from its representation
 * received through the buffer.
 *
 * @post Layout object is built internally (along with enumeration object being
 * built if applicable). User is expected to add rererence/s to this layout
 * object while using it. Releasing the last reference will finalise the layout
 * object by freeing it.
 */
int c2_layout_decode(struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *cur);

/**
 * This method uses an in-memory layout object and
 * - Either adds/updates/deletes it to/from the Layout DB
 * - Or converts it to a buffer.
 *
 * Two use cases of c2_layout_encode()
 * - Server encodes an in-memory layout object into a buffer using
 *   c2_layout_encode(), so as to send it to the client.
 * - Server encodes an in-memory layout object using one of c2_layout_add(),
 *   c2_layout_update() or c2_layout_delete() and adds/updates/deletes
 *   it to or from the Layout DB.
 *
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all a DB operation which could be
 * one of ADD/UPDATE/DELETE. If it is BUFFER_OP, then the layout is stored
 * in the buffer provided by the caller.
 *
 * @param oldrec_cur Cursor pointing to a buffer to be used to read the
 * exisiting layout record from the layouts table. Applicable only in case of
 * layou update operation. In other cases, it is expected to be NULL.
 *
 * @param out Cursor pointing to a buffer. Regarding the size of the buffer:
 * - In case c2_layout_encode() is called through c2_layout_add()|
 *   c2_layout_update()|c2_layout_delete(), then the buffer size should be
 *   large enough to contain the data that is to be written specifically to
 *   the layouts table. It means it needs to be at the most the size returned
 *   by c2_layout_max_recsize().
 * - In case c2_layout_encode() is called by some other caller, then the
 *   buffer size should be large enough to contain all the data belonging to
 *   the specific layout. It means the size required may even be more than
 *   the one returned by c2_layout_max_recsize(). For example, in case of LIST
 *   enumeration type, some data goes into table other than layouts, viz.
 *   cob_lists table.
 *
 * @post
 * - If op is is either for ADD|UPDATE|DELETE, respective DB operation is
 *   continued.
 * - If op is BUFFER_OP, the buffer contains the serialised representation
 *   of the whole layout.
 */
int c2_layout_encode(struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *oldrec_cur,
		     struct c2_bufvec_cursor *out);

/**
 * Returns maximum possible size for a record in the layouts table (without
 * considering the data in the tables other than layouts), from what is
 * maintained in the c2_layout_schema object.
 */
c2_bcount_t c2_layout_max_recsize(const struct c2_layout_domain *dom);

/** Returns c2_striped_layout object for the specified c2_layout object. */
struct c2_striped_layout *c2_layout_to_striped(const struct c2_layout *l);

/**
 * Returns c2_layout_enum object for the specified c2_striped_layout
 * object.
 */
struct c2_layout_enum
*c2_striped_layout_to_enum(const struct c2_striped_layout *stl);

/** Returns number of objects in the enumeration. */
uint32_t c2_layout_enum_nr(const struct c2_layout_enum *e);

/* Returns idx-th object in the enumeration. */
void c2_layout_enum_get(const struct c2_layout_enum *e,
			uint32_t idx,
			const struct c2_fid *gfid,
			struct c2_fid *out);

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
