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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 08/23/2011
 */

#pragma once

#ifndef __MERO_COBFID_MAP_H__
#define __MERO_COBFID_MAP_H__

/**
   @defgroup cobfidmap Cobfid Map
   @brief A cobfid map is a persistent data structure that tracks the id of
   cobs and their associated file fid contained within other containers. SNS
   repair requires the ability to iterate over the cob_fids on a given device,
   ordered by their associated file_fid.

   The map is built over a m0_database file stored on disk.
   All serialization around the use of these interfaces must be performed
   by the invoker:
   - Only one instance of a given cobfid map should be opened in the same
   process.
   - Any given cobfid map must be protected from concurrent use within the
   process.
   - Access to the same cobfid map from multiple processes is not supported.
   - Multiple maps may be used concurrently within the same process.

   @see <a href="https://docs.google.com/a/xyratex.com/document/d/1T6sWG32Fj1DOJgC5UcJDdfghse8r7loX-kdgx8a4fXM/edit?hl=en_US">HLD of Auxiliary Database for SNS Repair</a>
   for background information.

   Sample usage is shown below. Error handling is intentionally not shown
   for brevity.  C++ style comments are only for illustration - they are not
   valid in Mero:
@code
struct m0_dbenv mydbenv;
struct m0_addb_ctx myaddb_ctx;
struct m0_cobfid_map mymap;
struct m0_cobfid_map_iter myiter;

uint64_t container_id;
struct m0_fid file_fid;
struct m0_uint128 cob_fid;

// initialize mydbenv

// create or open the map
rc = m0_cobfid_map_init(&mymap, &mydbenv, &myaddb_ctx, "mycobfidmapname");

// insert records
rc = m0_cobfid_map_add(&mymap, container_id, file_fid, cob_fid, &mydbtx);
...

// enumerate
rc = m0_cobfid_map_container_enum(&mymap, container_id, &myiter, &mydbtx);
while ((rc = m0_cobfid_map_iter_next(&myiter,&container_id,&file_fid,&cob_fid,
		&mydbtx)) == 0) {
	// process record
}
// cleanup
m0_cobfid_map_fini(&mymap);
@endcode

   @{
 */

#include "addb/addb.h" /* struct m0_addb_ctx */
#include "db/db.h"     /* struct m0_dbenv */
#include "fid/fid.h"   /* struct m0_fid */
#include "lib/time.h"  /* m0_time_t */
#include "lib/types.h" /* struct m0_uint128 */
#include "lib/refs.h"  /* struct m0_ref */

struct m0_cobfid_map_iter_ops; /* forward reference */
struct m0_reqh;

/**
   This data structure tracks the persistent (on-disk) Cobfid map in-memory.
 */
struct m0_cobfid_map {
	uint64_t            cfm_magic;
	struct m0_dbenv    *cfm_dbenv;     /**< Database environment pointer */
	struct m0_addb_ctx *cfm_addb;      /**< ADDB context */
	char		   *cfm_map_name;  /**< Name of the map */
	m0_time_t           cfm_last_mod;  /**< Time last modified */
	struct m0_table     cfm_table;     /**< Table corresponding to cfm */
	struct m0_mutex     cfm_mutex;
	bool                cfm_is_initialised;
	uint64_t            cfm_ref_cnt;
};

/** enum indicating the query type */
enum m0_cobfid_map_query_type {
	/* zero not valid */
	/** Enumerate all associations in the map */
	M0_COBFID_MAP_QT_ENUM_MAP = 1,
	/** Enumerate associations in a container */
	M0_COBFID_MAP_QT_ENUM_CONTAINER = 2,
	/* last */
	M0_COBFID_MAP_QT_NR
};

/**
   This data structure keeps state during enumeration of the associations
   within the map. The same data structure is used to return the results
   of different types of enumerations.

   The data structure should be treated as opaque by the invoking application.
 */
struct m0_cobfid_map_iter {
	uint64_t                             cfmi_magic;
	/**< The map */
	struct m0_cobfid_map                *cfmi_cfm;
	/**< End or error indicator */
	int                                  cfmi_error;
	/**< Time last loaded */
	m0_time_t                            cfmi_last_load;
	/**< Next container id */
	uint64_t                             cfmi_next_ci;
	/**< Next fid value */
	struct m0_fid                        cfmi_next_fid;
	 /**< Last container id returned */
	uint64_t                             cfmi_last_ci;
	/**< Last fid value returned */
	struct m0_fid                        cfmi_last_fid;
	/**< Private read-ahead buffer */
	void                                *cfmi_buffer;
	/**< # recs in the buffer */
	unsigned int                         cfmi_num_recs;
	/**< index of last valid record */
	unsigned int	                     cfmi_last_rec;
	/**< The next record to return */
	unsigned int                         cfmi_rec_idx;
	/**< Indicates end of table */
	bool		                     cfmi_end_of_table;
	/**< Indicates iterator reload */
	bool		                     cfmi_reload;
	/**< The type of query */
	enum m0_cobfid_map_query_type        cfmi_qt;
	/**< Operations */
	const struct m0_cobfid_map_iter_ops *cfmi_ops;
};

/** Iterator operations */
struct m0_cobfid_map_iter_ops {
	/**
	   Loads the next batch of records into the iterator and updates the
	   iterator state to correctly position for the next call.
	 */
	int (*cfmio_fetch)(struct m0_cobfid_map_iter *);
	/**
	   Determines if the record in the specified position will
	   exhaust the iterator.
	   @param idx Index of a buffered record in the iterator.
	*/
	bool (*cfmio_is_at_end)(struct m0_cobfid_map_iter *, unsigned int idx);
	/**
	   Reload the records from the current position, because the map
	   may have been altered by an interveaning call to add or
	   delete a record, as indicated by comparing cfm_last_mod with
	   cfmi_last_load.
	*/
	int (*cfmio_reload)(struct m0_cobfid_map_iter *);
};

/**
   Prepare to use a cobfid map, creating it if necessary. The database file
   will be created in the standard location used for Mero databases.
   @param cfm      Pointer to the struct m0_cobfid_map to initialize.
   @param db_env   M0 database environment pointer. The pointer must remain
   valid until the map is finalized.
   @param addb_ctx Pointer to the ADDB context to use. The context must not
   be finalized until after the map is finalized.
   @param map_name Name of the map. The string is not referenced after this
   subroutine returns.
   @retval 0 on success
   @retval -errno on failure
   @see m0_cobfid_map_add()
   @see m0_cobfid_map_fini()
 */
M0_INTERNAL int m0_cobfid_map_init(struct m0_cobfid_map *cfm,
				   struct m0_dbenv *db_env,
				   struct m0_addb_ctx *addb_ctx,
				   const char *map_name);

/**
   Finalize use of a cobfid map.
   @param cfm Pointer to the struct m0_cobfid_map to finalize
 */
M0_INTERNAL void m0_cobfid_map_fini(struct m0_cobfid_map *cfm);

/**
   Finalize use of a cobfid map iterator.
   @param iter Pointer to the struct m0_cobfid_map_iter to finalize
 */
M0_INTERNAL void m0_cobfid_map_iter_fini(struct m0_cobfid_map_iter *iter);

/**
   Creates an association between the tuple of (container_id, file_fid)
   and a cob_fid. Any previous cob_fid association is replaced.
   @param cfm          Pointer to the struct m0_cobfid_map.
   @param container_id Identifier of the container or device.
   @param file_fid     Global file identifier.
   @param cob_fid      COB identifier.
   @retval 0 on success
   @retval -errno on failure
   @see m0_cobfid_map_del()
   @see m0_cobfid_map_enum()
   @see m0_cobfid_map_container_enum()
 */
M0_INTERNAL int m0_cobfid_map_add(struct m0_cobfid_map *cfm,
				  const uint64_t container_id,
				  const struct m0_fid file_fid,
				  struct m0_uint128 cob_fid);

/**
   Delete the association of the tuple (container_id, file_fid) with a cob_fid.
   @param cfm          Pointer to the struct m0_cobfid_map.
   @param container_id Identifier of the container or device.
   @param file_fid     Global file identifier.
   @retval 0 on success
   @retval -errno on failure
 */
M0_INTERNAL int m0_cobfid_map_del(struct m0_cobfid_map *cfm,
				  const uint64_t container_id,
				  const struct m0_fid file_fid);

/**
   Initializes an iterator to enumerate the associations within a container
   in the cobfid map. The associations are traversed in global file fid
   order.
   @param cfm          Pointer to the struct m0_cobfid_map.
   @param container_id Identifier of the container or device.
   @param iter         Pointer to iterator data structure to be initialized.
   The iterator is used to retrieve the results.
   @see m0_cobfid_map_iter_next()
   @retval 0  on success.
   @retval -errno on error.
 */
M0_INTERNAL int m0_cobfid_map_container_enum(struct m0_cobfid_map *cfm,
					     uint64_t container_id,
					     struct m0_cobfid_map_iter *iter);

/**
   Initializes an iterator to enumerate all of the associations
   in the cobfid map. The associations are traversed by container id
   and global file fid order.
   @param cfm  Pointer to the struct m0_cobfid_map.
   @param iter Pointer to iterator data structure to be initialized.
   The iterator is used to retrieve the results.
   @see m0_cobfid_map_iter_next()
   @retval 0  on success.
   @retval -errno on error.
 */
M0_INTERNAL int m0_cobfid_map_enum(struct m0_cobfid_map *cfm,
				   struct m0_cobfid_map_iter *iter);

/**
   Returns the next association in traversal order, pointed to by the
   iterator. The iterator gets advanced.

   Internally, the iterator will open a database transaction to read records
   from the file. The iterator may optimize by reading multiple records in
   a batch.

   @note Insertions and deletions prior to the iterator position may not
   be noticed, but any such changes after the current position will be
   noticed.

   @param iter           Iterator tracking the position in the enumeration.
   @param container_id_p Returns the identifier of the container or device.
   @param file_fid_p     Returns the global file identifier.
   @param cob_fid_p      Returns the COB identifier.
   @retval 0       on success
   @retval -ENOENT when the iterator is exhausted
   @retval -errno  on other errors
 */
M0_INTERNAL int m0_cobfid_map_iter_next(struct m0_cobfid_map_iter *iter,
					uint64_t * container_id_p,
					struct m0_fid *file_fid_p,
					struct m0_uint128 *cob_fid_p);


/**
 * Finds the struct m0_cobfid_map instance in the request handler using
 * cobfid_map_key and initialises the same if not already initialised.
 * Note that, this also takes a reference on the struct m0_cobfid_map instance,
 * thus the reference should be released by invoking corresponding
 * m0_cobfid_map_put().
 *
 * @see m0_cobfid_map_put()
 */
M0_INTERNAL int m0_cobfid_map_get(struct m0_reqh *reqh,
				  struct m0_cobfid_map **out);

/**
 * Releases a reference on struct m0_cobfid_map instance.
 *
 * @see m0_cobfid_map_setup_get()
 */
M0_INTERNAL void m0_cobfid_map_put(struct m0_reqh *reqh);

/** @} */

#endif /* __MERO_COBFID_MAP_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
