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
 * Original author: Nathan Rutman <nathan_rutman@xyratex.com>
 * Original creation date: 10/24/2010
 */

#pragma once

#ifndef __COLIBRI_COB_COB_H__
#define __COLIBRI_COB_COB_H__

#include "lib/atomic.h"
#include "lib/cdefs.h"
#include "lib/rwlock.h"
#include "lib/refs.h"
#include "lib/adt.h"
#include "lib/bitstring.h"
#include "addb/addb.h"
#include "db/db.h"
#include "fid/fid.h"
#include "stob/stob.h"
#include "dtm/verno.h"

/* import */
struct c2_db_tx;

/**
   @defgroup cob Component objects

   A Component object is a metadata layer that sits over a storage object.
   It references a single storage object and contains metadata describing
   the object. The metadata is stored in database tables. A C2 Global
   Object (i.e. file) is made up of a collection of Component Objects
   (stripes).

   Component object metadata includes:
   - namespace information: parent object id, name, links
   - file attributes: owner/mode/group, size, m/a/ctime, acls
   - fol reference information:  log sequence number (lsn), version counter

   Metadata organization:

   COB uses four db tables for storing the following pieces of information:
   - Namespace - stores file names and file attributes for readdir speedup;

   - Object Index - stores links of file (all names of the file);

   - File attributes - stores file version, some replicator fields, basically
   anything that is not needed during stat and readdir operations;

   - One more table is needed for so called "omg" records (owner/mode/group).
   They store mode/uid/gid file attributes.

   For traditional file systems namespace we need two tables: name space and
   object index. Namespace and object index tables are used as following:

   Suppose that there is a file F that has got three names:

   "a/f0", "b/f1", "c/f3"

   Then namespace will have the following records:

   (a.fid, "f0")->(F stat data, 0)
   (b.fid, "f1")->(F 1)
   (c.fid, "f2")->(F 2)

   where, in first record, we have key constructed of f0's parent fid (the
   directory fid) and "f0", the filename itself. The namespace record is the
   file "f0" fid with full stat data, plus the link number for this name.
   
   Here "stat data" means that this record contains file attributes that usually
   extracted by stat utility or ls -la.

   First name stores the stat data (basic file system attributes) and has link
   number zero.
   All the other records have keys constructed from their parent fid and child
   name ("f1", "f2"), and the record only contains the file fid and link number
   that this record describes (1, 2).

   As the stat data is stored in the first record it eventually needs to be
   migrated when this name is killed. This will be shown below.

   The object index will contain records:

   (F, 0)->(a.fid, "f0")
   (F, 1)->(b.fid, "f1")
   (F, 2)->(c.fid, "f2")

   where the key is constructed of file fid and its link number. The record
   contains the parent fid plus the file name. That is, the object index
   enumerates all the names that file has. As we have already noted, the object
   index records have the same format as name space keys and may be used
   appropriately.

   When doing rm b/f0 we need to kill all its records in object index and namespace.
   That is, we need a key containing "a" fid and "f0" name. Using this key we can
   find its position in namespace table and kill the record. Before actually killing
   it, we need to check if this record stores file attributes. This is easy to do
   as ->linkno field is zero for stat data records. Stat data should be moved to
   next name and we need to find it somehow. In order to do this quickly we need
   to do lookup in object index for second file name with key containing linkno == 1,
   that is, next after 0, and file fid F.

   Doing so allows to find the record:

   (F, 0)->(a.fid, "f0")

   It describes second name of the file. We now can use its record as a key for name
   space and find second name record in name space table. Having the name, we can move
   stat data of F to it.

   Now kill the record in the object index. We have already found that we need key
   constructed of F and link number, that is, zero. Having this key we can kill object
   index entry describing "f0" name.

   We are done with unlink operation. Of course for cases when killing name that
   does not hold stat data, algorithm is much simpler. We just need to kill
   one record in name, update stat data record in namespace (decremented nlink
   should be updated in the store) and kill one record in object index.

   File attributes that are stored in separate tables may also be easily accessed
   using key constructed of (F), where F is file fid.

   Rationale:

   Hard-links are rare, make common case fast by placing attributes in the file's
   directory entry, make readdir fast by moving other attributes out.

   Corner cases:

   Rename and unlink. They need to move file attributes from zero name when moving
   it.

   Special case:

   Using cob api for ioservice to create data objects is special case. The main
   difference is that, parent fid (in nskey) and child fid (in nsrec) are the same.

   Cob iterator.

   In order to iterate over all names that "dir" cob contains, cob iterator
   is used. It is simple, cursor based API, that contains four methods:
   - c2_cob_iterator_init() - init iterator with fid, name position;
   - c2_cob_iterator_get()  - position iterator according with its properies;
   - c2_cob_iterator_next() - move to next position;
   - c2_cob_iterator_fini() - fini iterator.

   At the beginning cursor gets positioned onto position described with key that
   is constructed using passed cob's fid and name. Then iterator moves forward
   with next() method over namespace table until end of table is reached or a rec
   with another parent fid is found.

   Once iterator is not longer needed, it is fininalized by c2_cob_iterator_fini().
   @see c2_md_store_readdir()

   Mkfs.

   In order to use cob storage one needs to prepare it by mkfs. @see c2_cob_domain_mkfs()
   In this time the following structures are created in cob tables:

   - the main root cob with fid C2_COB_ROOT_FID

   - metadata hierarachy root cob (what potencially metadata client can see) with fid
   C2_COB_ROOT_FID and name C2_COB_ROOT_NAME

   - sessions root cob (all sessions are created below it) with fid C2_COB_SESSIONS_FID
   and name C2_COB_SESSIONS_NAME

   - omgid terminator record with id = ~0ULL. This is used for omgid allocation during
   c2_cob_create()

   Cob cannot be used properly without mkfs done. All unit tests that accessing cob
   and also all modules using cobs do c2_cob_domain_mkfs() on startup.

   @{
 */

struct c2_cob;
struct c2_fid;
struct c2_cob_id;
struct c2_cob_domain;
struct c2_cob_domain_id;

extern const char C2_COB_ROOT_NAME[];
extern const char C2_COB_SESSIONS_NAME[];

extern struct c2_fid C2_COB_ROOT_FID;
extern struct c2_fid C2_COB_SLASH_FID;
extern struct c2_fid C2_COB_SESSIONS_FID;

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
*/
struct c2_cob_domain {
        struct c2_cob_domain_id cd_id;
        struct c2_rwlock        cd_guard;
        struct c2_dbenv        *cd_dbenv;
        struct c2_table         cd_object_index;
        struct c2_table         cd_namespace;
        struct c2_table         cd_fileattr_basic;
        struct c2_table         cd_fileattr_omg;

        /**
           An ADDB context for events related to this domain.
         */
        struct c2_addb_ctx      cd_addb;
};

int c2_cob_domain_init(struct c2_cob_domain *dom, struct c2_dbenv *env,
                       struct c2_cob_domain_id *id);
void c2_cob_domain_fini(struct c2_cob_domain *dom);

int c2_cob_domain_mkfs(struct c2_cob_domain *dom, struct c2_fid *rootfid,
                       struct c2_fid *sessfid, struct c2_db_tx *tx);

/**
   Valid flags for cob attributes.
*/
enum c2_cob_valid_flags {
        C2_COB_ATIME   = 1 << 0,
        C2_COB_MTIME   = 1 << 1,
        C2_COB_CTIME   = 1 << 2,
        C2_COB_SIZE    = 1 << 3,
        C2_COB_MODE    = 1 << 4,
        C2_COB_UID     = 1 << 5,
        C2_COB_GID     = 1 << 6,
        C2_COB_BLOCKS  = 1 << 7,
        C2_COB_TYPE    = 1 << 8,
        C2_COB_FLAGS   = 1 << 9,
        C2_COB_NLINK   = 1 << 10,
        C2_COB_RDEV    = 1 << 11,
        C2_COB_BLKSIZE = 1 << 12
};

/**
   Attributes describing object that needs to be created or modified.
   This structure is filled by mdservice and used in mdstore and/or
   in cob modules for carrying in-request information to layers that
   should not be dealing with fop req or rep.
*/
struct c2_cob_attr {
        struct c2_fid     ca_pfid;    /**< parent fid */
        struct c2_fid     ca_tfid;    /**< object fid */
        uint16_t          ca_flags;   /**< valid fields (enum c2_cob_valid_flags) */
        uint32_t          ca_mode;    /**< protection. */
        uint32_t          ca_uid;     /**< user ID of owner. */
        uint32_t          ca_gid;     /**< group ID of owner. */
        uint64_t          ca_atime;   /**< time of last access. */
        uint64_t          ca_mtime;   /**< time of last modification. */
        uint64_t          ca_ctime;   /**< time of last status change. */
        uint64_t          ca_rdev;    /**< devid for special devices, used for replicator */
        uint32_t          ca_nlink;   /**< number of hard links. */
        uint64_t          ca_size;    /**< total size, in bytes. */
        uint64_t          ca_blksize; /**< blocksize for filesystem I/O. */
        uint64_t          ca_blocks;  /**< number of blocks allocated. */
        uint64_t          ca_version; /**< object version */
        char             *ca_name;    /**< object name */
        int32_t           ca_namelen; /**< name length */
        char             *ca_link;    /**< symlink */
};

/** 
   Namespace table key. For data objects, pfid = cfid and name = ""
 */
struct c2_cob_nskey {
        struct c2_fid       cnk_pfid;
        struct c2_bitstring cnk_name;
};

int c2_cob_nskey_size(const struct c2_cob_nskey *nskey);

int c2_cob_nskey_cmp(const struct c2_cob_nskey *k0, 
                     const struct c2_cob_nskey *k1);

/**
   Namespace table record. For each file many such nsrec recods may exist
   in case that multiple hardlinks exist. First of the, that is, zero record
   contains file attributes and may be called statdata.
*/
struct c2_cob_nsrec {
        struct c2_fid     cnr_fid;     /**< object fid */
        uint32_t          cnr_linkno;  /**< number of link for the name */

        /**
           The following fields are only important for 0-nsrec, that is,
           stat data. For other records, only two fields above are valid.
         */
        uint32_t          cnr_nlink;   /**< number of hard links. */
        uint32_t          cnr_cntr;    /**< linkno allocation counter. */
        uint64_t          cnr_omgid;   /**< uid/gid/mode slot reference */
        uint64_t          cnr_size;    /**< total size, in bytes. */
        uint64_t          cnr_blksize; /**< blocksize for filesystem I/O. */
        uint64_t          cnr_blocks;  /**< number of blocks allocated. */
        uint64_t          cnr_atime;   /**< time of last access. */
        uint64_t          cnr_mtime;   /**< time of last modification. */
        uint64_t          cnr_ctime;   /**< time of last status change. */
};

/** Object index table key. */
struct c2_cob_oikey {
        struct c2_fid     cok_fid;
        uint32_t          cok_linkno;  /**< hardlink ordinal index */
};

/** The oi table record is a struct c2_cob_nskey. */

/** 
   Fileattr_basic table key is c2_cob_fabkey

   @note version change at every ns manipulation and data write.
   If version and mtime/ctime both change frequently, at the same time,
   it is arguable better to put version info in the namespace table instead
   of fileattr_basic table so that there is only 1 table write.

   The reasoning behind the current design is that name-space table should be
   as compact as possible to reduce lookup footprint. Also, readdir benefits
   from smaller name-space entries.
 */
struct c2_cob_fabkey {
        struct c2_fid     cfb_fid;
};

struct c2_cob_fabrec {
        struct c2_verno   cfb_version;  /**< version from last fop */
        uint64_t          cfb_layoutid; /**< reference to layout. */
        uint16_t          cfb_linklen;  /**< symlink len if any */
        char              cfb_link[0];  /**< symlink body */
        /* add ACL, any other md not needed for stat(2) */
};

/**
   Key for omg table
*/
struct c2_cob_omgkey {
        uint64_t          cok_omgid;   /**< omg id ref */
};

/**
   Protection and access flags stored in this table.
*/
struct c2_cob_omgrec {
        uint32_t          cor_mode;    /**< protection. */
        uint32_t          cor_uid;     /**< user ID of owner. */
        uint32_t          cor_gid;     /**< group ID of owner. */
};

/**
   In-memory representation of a component object.

   A c2_cob is an in-memory structure, populated as needed from database
   table lookups.  The c2_cob may be cached and should be protected by a lock.

   The exposed methods to get a new cob are:
   - c2_cob_lookup() - lookup by filename
   - c2_cob_locate() - lookup by fid
   - c2_cob_create() - create new cob using passed nskey, nsrec and attrbutes
   The cobs returned by these methods are always populated.

   An empty cob is only exposed with c2_cob_alloc() method, which is used
   by request handler and (possibly) others. Such a cob is used as an input
   parameter to c2_cob_create() method.

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

   - it caches certain metadata attributes in memory (controlled by ->co_valid);

   <b>Liveness</b>
   A c2_cob may be freed when the reference count drops to 0

   @note: The c2_cob_nskey is allocated separately because it is variable
   length. Once allocated, the cob can free the memory by using CA_NSKEY_FREE.

   <b>Caching and concurrency</b>
   Cobs are not cached by cob domain or even cob API users. Rationale is the
   following:

   - we use db[45] for storing metadata and it already has cache that may work
   in a way that satisfies our needs;

   - using cache means substantial complications with locking and concurrent
   access. Currently these issues are completely covered by db[45].
 */
struct c2_cob {
        struct c2_cob_domain  *co_dom;
        struct c2_stob        *co_stob;     /**< underlying storage object */
        struct c2_ref          co_ref;      /**< refcounter for caching cobs */
        uint64_t               co_valid;    /**< @see enum c2_cob_valid_flags */
        struct c2_fid         *co_fid;      /**< object fid, refers to nsrec fid */
        struct c2_cob_nskey   *co_nskey;    /**< cob statdata nskey */
        struct c2_cob_oikey    co_oikey;    /**< object fid, linkno */
        struct c2_cob_nsrec    co_nsrec;    /**< object fid, basic stat data */
        struct c2_cob_fabrec  *co_fabrec;   /**< fileattr_basic data (acl, etc) */
        struct c2_cob_omgrec   co_omgrec;   /**< permission data */
        struct c2_db_pair      co_oipair;   /**< used for oi accesss */
        struct c2_addb_ctx     co_addb;     /**< cob private addb ctx. */
};

/**
   Cob iterator. Holds current position inside a cob (used readdir).
*/
struct c2_cob_iterator {
        struct c2_cob         *ci_cob;      /**< the cob we iterate */
        struct c2_db_cursor    ci_cursor;   /**< cob iterator cursor */
        struct c2_cob_nskey   *ci_key;      /**< current iterator pos */
        struct c2_cob_nsrec    ci_rec;      /**< current iterator rec */
        struct c2_db_pair      ci_pair;     /**< used for iterator cursor */
};

/**
   Cob flags and valid attributes.
*/
enum c2_cob_ca_valid {
        CA_NSKEY      = (1 << 0),  /**< nskey in cob is up-to-date */
        CA_NSKEY_FREE = (1 << 1),  /**< cob responsible for dealloc of nskey */
        CA_NSKEY_DB   = (1 << 2),  /**< db responsible for dealloc of nskey */
        CA_NSREC      = (1 << 3),  /**< nsrec in cob is up-to-date */
        CA_FABREC     = (1 << 4),  /**< fabrec in cob is up-to-date */
        CA_OMGREC     = (1 << 5),  /**< omgrec in cob is up-to-date */
        CA_LAYOUT     = (1 << 6),  /**< layout in cob is up-to-date */
};

/**
   Lookup a filename in the namespace table.

   Allocate a new cob and populate it with the contents of the
   namespace record; i.e. the stat data and fid. This function
   also looks up fab and omg tables, depending on "need" flag.

   @see c2_cob_locate
 */
int c2_cob_lookup(struct c2_cob_domain *dom,
                  struct c2_cob_nskey  *nskey,
                  uint64_t              need,
                  struct c2_cob       **out,
                  struct c2_db_tx      *tx);

/**
   Locate by object index key.

   Create a new cob and populate it with the contents of the
   a record; i.e. the filename. This also lookups for all attributes,
   that is, fab, omg, etc., according to @need flags.

   @see c2_cob_lookup
 */
int c2_cob_locate(struct c2_cob_domain    *dom,
                  struct c2_cob_oikey     *oikey,
                  uint64_t                 need,
                  struct c2_cob          **out,
                  struct c2_db_tx         *tx);

/**
   Add a cob to the namespace.

   This doesn't create a new storage object; just creates
   metadata table entries for it to enable namespace and oi lookup.

   cob    - cob instance allocted with c2_cob_alloc()
   nskey  - namespace key made with c2_cob_nskey_make()
   nsrec  - namespace record with all attrbiutes set
   fabrec - symlink record
   omgrec - owner/mode/group record
   tx     - transaction handle
 */
int c2_cob_create(struct c2_cob         *cob,
                  struct c2_cob_nskey   *nskey,
                  struct c2_cob_nsrec   *nsrec,
                  struct c2_cob_fabrec  *fabrec,
                  struct c2_cob_omgrec  *omgrec,
                  struct c2_db_tx       *tx);

/**
   Delete name with statdata, entry in object index and all file
   attributes from fab, omg, etc., tables.
*/
int c2_cob_delete(struct c2_cob *cob, 
                  struct c2_db_tx *tx);

/**
   Update file attributes of passed cob with @nsrec, @fabrec 
   and @omgrec fields.
*/
int c2_cob_update(struct c2_cob        *cob,
                  struct c2_cob_nsrec  *nsrec,
                  struct c2_cob_fabrec *fabrec,
                  struct c2_cob_omgrec *omgrec,
                  struct c2_db_tx      *tx);

/**
   Add name to namespace and object index.
   cob   - stat data (zero name) cob;
   nskey - new name to add to the file;
   tx    - transaction handle.
*/
int c2_cob_add_name(struct c2_cob        *cob,
                    struct c2_cob_nskey  *nskey,
                    struct c2_cob_nsrec  *nsrec,
                    struct c2_db_tx      *tx);

/**
   Delete name from namespace and object index.

   cob   - stat data (zero name) cob;
   nskey - name to kill (may be the name of statdata);
   tx    - transaction handle.
*/
int c2_cob_del_name(struct c2_cob        *cob,
                    struct c2_cob_nskey  *nskey,
                    struct c2_db_tx      *tx);

/**
   Rename oldkey with passed newkey.

   cob    - stat data (zero name) cob;
   srckey - source name;
   tgtkey - target name;
   tx     - transaction handle
*/
int c2_cob_update_name(struct c2_cob        *cob,
                       struct c2_cob_nskey  *srckey,
                       struct c2_cob_nskey  *tgtkey,
                       struct c2_db_tx      *tx);

/**
   Init cob iterator on passed @cob and @name as a start position.
*/
int c2_cob_iterator_init(struct c2_cob          *cob,
                         struct c2_cob_iterator *it,
                         struct c2_bitstring    *name,
                         struct c2_db_tx        *tx);

/**
   Position to next name in a dir cob.
*/
int c2_cob_iterator_next(struct c2_cob_iterator *it);

/**
   Position in table according with @it properties.
*/
int c2_cob_iterator_get(struct c2_cob_iterator *it);

/**
   Finish cob iterator.
*/
void c2_cob_iterator_fini(struct c2_cob_iterator *it);

/**
   Allocate a new cob on passed @dom.
 */
int c2_cob_alloc(struct c2_cob_domain *dom,
                 struct c2_cob       **out);

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
   Create object index key that is used for operations on object index table.
   It consists of object fid an linkno depending on what record we want to 
   find.
*/
void c2_cob_oikey_make(struct c2_cob_oikey *oikey,
                       const struct c2_fid *fid,
                       int linkno);

/**
   Create namespace table key for ns table manipulation. It contains parent fid
   and child name.
*/
void c2_cob_nskey_make(struct c2_cob_nskey **keyh,
                       const struct c2_fid *pfid,
                       const char *name,
                       int namelen);

/**
   Allocate fabrec record according with @link and @linklen and setup record
   fields.
*/
void c2_cob_fabrec_make(struct c2_cob_fabrec **rech,
                        const char *link, int linklen);

/**
   Try to allocate new omgid using omg table and terminator record. Save
   allocated id in @omgid if not NULL.
*/
int c2_cob_alloc_omgid(struct c2_cob_domain *dom,
                       struct c2_db_tx *tx,
                       uint64_t *omgid);

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
