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
 * Original author: Nathan Rutman <nathan_rutman@xyratex.com>
 * Original creation date: 10/24/2010
 */

#ifndef __COLIBRI_COB_COB_H__
#define __COLIBRI_COB_COB_H__

#include "lib/atomic.h"
#include "lib/types.h"         /* c2_uint128 */
#include "lib/cdefs.h"
#include "lib/rwlock.h"
#include "lib/refs.h"
#include "lib/adt.h"
#include "lib/bitstring.h"
#include "addb/addb.h"
#include "db/db.h"
#include "stob/stob.h"
#include "dtm/verno.h"


/* import */
struct c2_db_tx;

/**
   @defgroup cob Component objects

   A Component Object is a metadata layer that sits over a storage object.
   It references a single storage object and contains metadata describing
   the object. The metadata is stored in database tables. A C2 Global
   Object (i.e. file) is made up of a collection of Component Objects
   (stripes).

   Component object metadata includes:
   - namespace information: parent object id, name, links
   - file attributes: owner/mode/group, size, m/a/ctime, acls
   - fol reference information:  log sequence number (lsn), version counter

   @see stob
   @{
 */

struct c2_cob;
struct c2_cob_id;
struct c2_cob_domain;
struct c2_cob_domain_id;

/**
  Unique cob domain identifier.

  A cob_domain identifier distinguishes domains within a single
  database environment.
 */
struct c2_cob_domain_id {
	uint32_t id;
};

/**
   cob domain

   Component object metadata is stored in database tables.  The database
   in turn is stored in a metadata container.  A cob_domain is a grouping
   of cob's described by a namespace or object index.  The objects referenced
   by the tables will reside in other, filedata containers.

   A c2_cob_domain is an in-memory structure that identifies these tables.
   The list of domains will be created at metadata container ingest (when
   a container is first "started/read/initialized/opened".)

   A c2_cob_domain cannot span multiple containers.  Eventually, there should
   be methods for combining/splitting containers and therefore cob
   domains and databases.

   A domain might cache component objects to speed up object lookup.
   This caching is not visible to upper layers.
*/
struct c2_cob_domain {
        struct c2_cob_domain_id cd_id;
        struct c2_rwlock        cd_guard;
        struct c2_dbenv        *cd_dbenv;
        struct c2_table         cd_object_index;
        struct c2_table         cd_namespace;
        struct c2_table         cd_fileattr_basic;
        /** an ADDB context for events related to this domain. */
        struct c2_addb_ctx      cd_addb;
};

int c2_cob_domain_init(struct c2_cob_domain *dom, struct c2_dbenv *env,
                       struct c2_cob_domain_id *id);
void c2_cob_domain_fini(struct c2_cob_domain *dom);


/** Namespace table
   For data objects, pfid = cfid and name = ""
 */
struct c2_cob_nskey {
        struct c2_stob_id   cnk_pfid;
        struct c2_bitstring cnk_name;
};

int c2_cob_nskey_size(const struct c2_cob_nskey *);

struct c2_cob_nsrec {
        struct c2_stob_id cnr_stobid;
        uint32_t          cnr_nlink;
        /** add other stat(2) data here: omg_ref, size, mactime */
};


/** Object index table */
struct c2_cob_oikey {
        struct c2_stob_id cok_stobid;
        uint32_t          cok_linkno;  /**< hardlink ordinal index */
};

/** The oi table record is a struct c2_cob_nskey */


/** Fileattr_basic table
    key is stobid

    @note version change at every ns manipulation and data write.
    If version and mtime/ctime both change frequently, at the same time,
    it is arguable better to put version info in the namespace table instead
    of fileattr_basic table so that there is only 1 table write.

    The reasoning behind the current design is that name-space table should be
    as compact as possible to reduce lookup footprint. Also, readdir benefits
    from smaller name-space entries.
 */
struct c2_cob_fabrec {
        struct c2_verno cfb_version; /**< version from last fop */
        /* add ACL, layout_ref, any other md not needed for stat(2) */
};


/**
   In-memory representation of a component object.

   A c2_cob is an in-memory structure, populated as needed from database
   table lookups.  The c2_cob may be cached and should be protected by a lock.

   The exposed methods to get a new cob are:
   - c2_cob_lookup   (lookup by filename)
   - c2_cob_locate   (lookup by stob_id)
   - c2_cob_create(stob_id, filename, attrs)
   The cobs returned by these methods are always populated.
   An "empty" cob is never exposed.

   Users use c2_cob_get/put to hold references; the cob may be destroyed on
   the last put, or it may be cached for faster future lookup.
   @todo at some point, we me replace the co_ref by taking a reference on the
   underlying co_stob.  At that point, we will need a callback at last put.
   We wait to see how cob users will use these references, whether they need
   callbacks in turn, etc.

   A c2_cob serves multiple purposes:

   - it acts as a handle for the underlying metadata storage. Meta-data
   operations can be executed on persistent storage by calling functions on
   c2_cob;

   - it caches certain metadata attributes in memory.

   <b>Concurrency control</b>
   co_guard is used to protect agaist manipulation of data inside a
   c2_cob (currently usused as there are no manipulation methods yet).

   <b>Liveness</b>
   A c2-cob may be freed when the reference count drops to 0

   @note: The c2_nskey is allocated separately because it is variable length.
   Once allocated, the cob can free the memory by using CA_NSKEY_FREE.
 */
struct c2_cob {
        struct c2_cob_domain  *co_dom;
        struct c2_stob        *co_stob;    /**< underlying storage object */
        struct c2_ref          co_ref;     /**< refcounter for caching cobs */
        struct c2_rwlock       co_guard;   /**< lock on cob manipulation */
        uint64_t               co_valid;   /**< @see enum ca_valid */
        struct c2_verno        co_version; /**< current object version */
        struct c2_cob_nskey   *co_nskey;   /**< pfid, filename */
        struct c2_cob_nsrec    co_nsrec;   /**< fid, stat data */
        struct c2_cob_fabrec   co_fabrec;  /**< fileattr_basic data */
        struct c2_db_pair      co_oipair;
	struct c2_addb_ctx     co_addb;
};

#define co_stobid co_nsrec.cnr_stobid

/** cob flags and valid attributes */
enum ca_valid {
        CA_NSKEY      = (1 << 0),
        CA_NSKEY_FREE = (1 << 1),  /**< cob responsible for dealloc of nskey */
        CA_NSKEY_DB   = (1 << 2),  /**< db responsible for dealloc of nskey */
        CA_NSREC      = (1 << 3),
        CA_FABREC     = (1 << 4),
        CA_OMG        = (1 << 5),
        CA_LAYOUT     = (1 << 6),
};

/**
   Lookup a filename in the namespace table

   Check if cached first; otherwise create a new cob and populate it with
   the contents of the namespace record; i.e. the stat data and fid.

   @see c2_cob_locate
 */
int c2_cob_lookup(struct c2_cob_domain *dom, struct c2_cob_nskey *nskey,
                  uint64_t need, struct c2_cob **out, struct c2_db_tx *tx);

/**
   Locate by stob id

   Check if cached first; otherwise create a new cob and populate it with
   the contents of the oi record; i.e. the filename.

   This does not lookup file attributes.

    @see c2_cob_lookup
 */
int c2_cob_locate(struct c2_cob_domain *dom, const struct c2_stob_id *id,
                  struct c2_cob **out, struct c2_db_tx *tx);

/**
   Create a new cob and add it to the namespace.

   This doesn't create a new storage object; just creates metadata table
   entries for it to enable namespace and oi lookup.
 */
int c2_cob_create(struct c2_cob_domain *dom,
                  struct c2_cob_nskey  *nskey,
                  struct c2_cob_nsrec  *nsrec,
                  struct c2_cob_fabrec *fabrec,
                  uint64_t              need,
                  struct c2_cob       **out,
                  struct c2_db_tx      *tx);

/**
   Delete the metadata for this cob.

   Caller must be holding a reference on this cob, which
   will be released here.

   This does not affect the underlying stob.
 */
int c2_cob_delete(struct c2_cob *cob, struct c2_db_tx *tx);

/**
   Update file attributes of passed cob with @nsrec, @fabrec
   and @omgrec fields.
*/
int c2_cob_update(struct c2_cob *cob,
                  struct c2_cob_nsrec  *nsrec,
                  struct c2_cob_fabrec *fabrec,
                  struct c2_db_tx      *tx);
/**
   Acquires an additional reference on the object.

   @see c2_cob_put()
 */
void c2_cob_get(struct c2_cob *obj);

/**
   Releases a reference on the object.

   When the last reference is released, the object can either return to the
   cache or can be immediately destroyed.

   @see c2_cob_get()
 */
void c2_cob_put(struct c2_cob *obj);


/**
   Helper routine to allocate and initialize c2_cob_nskey

   If memory allocation fails then *keyh is set to NULL
 */
void c2_cob_nskey_make(struct c2_cob_nskey **keyh, uint64_t hi, uint64_t lo,
                        const char *name);

void c2_cob_namespace_traverse(struct c2_cob_domain *dom);
void c2_cob_fb_traverse(struct c2_cob_domain *dom);
/** @} end group cob */

/* __COLIBRI_COB_COB_H__ */
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
