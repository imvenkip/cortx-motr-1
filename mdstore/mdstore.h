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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 01/17/2011
 */

#pragma once

#ifndef __COLIBRI_MDSTORE_MDSTORE_H__
#define __COLIBRI_MDSTORE_MDSTORE_H__

#include "cob/cob.h"

struct c2_cob_domain_id;
struct c2_stob_id;
struct c2_db_tx;
struct c2_dbenv;
struct c2_fid;
struct c2_fop;
struct c2_cob;

struct c2_mdstore {
        struct c2_cob_domain  md_dom;
        struct c2_cob        *md_root;

        /**
         * An ADDB context for events related to this store.
         */
        struct c2_addb_ctx    md_addb;
};

/**
 * Flags supplied to c2_mdstore_locate() to point out where a cob
 * should be found: on store, in opened files table or orhans table.
*/
enum c2_mdstore_locate_flags {
        /** Find cob on store. */
        C2_MD_LOCATE_STORED  = 1 << 0,
        /** Find cob in opened cobs table. */
        C2_MD_LOCATE_OPENED  = 1 << 1,
        /** Find cob in orphans table. */
        C2_MD_LOCATE_ORPHAN  = 1 << 2
};

typedef enum c2_mdstore_locate_flags c2_mdstore_locate_flags_t;

/**
 * Init mdstore and get it ready to work. If init_root == !0
 * then root cob is initialized.
*/
C2_INTERNAL int c2_mdstore_init(struct c2_mdstore *md,
				struct c2_cob_domain_id *id,
				struct c2_dbenv *db, bool init_root);

/**
 * Finalize mdstore instance.
 */
C2_INTERNAL void c2_mdstore_fini(struct c2_mdstore *md);

/**
 * Handle link operation described by @pfid and @name. Input
 * cob is so called statdata cob and returned by c2_cob_locate().
 * Error code is returned in error case or zero otherwise.
 */
C2_INTERNAL int c2_mdstore_link(struct c2_mdstore *md,
				struct c2_fid *pfid,
				struct c2_cob *cob,
				const char *name,
				int namelen, struct c2_db_tx *tx);

/**
 * Handle unlink operation described by @pfid and @name. Input
 * cob is so called statdata cob and returned by c2_cob_locate().
 * Error code is returned in error case or zero otherwise.
 */
C2_INTERNAL int c2_mdstore_unlink(struct c2_mdstore *md,
				  struct c2_fid *pfid,
				  struct c2_cob *cob,
				  const char *name,
				  int namelen, struct c2_db_tx *tx);

/**
 * Handle rename operation described by params. Input cobs are
 * statdata cobs and returned by c2_cob_locate(). Rest of the
 * arguments are self explanatory.
 *
 * Error code is returned in error case or zero otherwise.
 */
C2_INTERNAL int c2_mdstore_rename(struct c2_mdstore *md,
				  struct c2_fid *pfid_tgt,
				  struct c2_fid *pfid_src,
				  struct c2_cob *cob_tgt,
				  struct c2_cob *cob_src,
				  const char *tname,
				  int tnamelen,
				  const char *sname,
				  int snamelen, struct c2_db_tx *tx);

/**
 * Handle create operation described by @attr on @cob. Input @cob
 * is returned by c2_cob_alloc().
 *
 * Error code is returned in error case or zero otherwise.
*/
C2_INTERNAL int c2_mdstore_create(struct c2_mdstore *md,
				  struct c2_fid *pfid,
				  struct c2_cob_attr *attr,
				  struct c2_cob **out, struct c2_db_tx *tx);

/**
 * Handle open operation described by @flags on @cob. Input @cob
 * is so called statdata cob and returned by c2_cob_locate().
 * Error code is returned in error case or zero otherwise.
*/
C2_INTERNAL int c2_mdstore_open(struct c2_mdstore *md,
				struct c2_cob *cob,
				c2_mdstore_locate_flags_t flags,
				struct c2_db_tx *tx);

/**
 * Handle close operation on @cob. Input @cob is so called statdata
 * cob and returned by c2_cob_locate().
 *
 * Error code is returned in error case or zero otherwise.
*/
C2_INTERNAL int c2_mdstore_close(struct c2_mdstore *md,
				 struct c2_cob *cob, struct c2_db_tx *tx);

/**
 * Handle setattr operation described by @attr on @cob. Input @cob
 * is so called statdata cob and returned by c2_cob_locate().
 *
 * Error code is returned in error case or zero otherwise.
*/
C2_INTERNAL int c2_mdstore_setattr(struct c2_mdstore *md,
				   struct c2_cob *cob,
				   struct c2_cob_attr *attr,
				   struct c2_db_tx *tx);

/**
 * Get attributes of @cob into passed @attr. Input @cob
 * is so called statdata cob and returned by c2_cob_locate().
 *
 * Error code is returned in error case or zero otherwise.
*/
C2_INTERNAL int c2_mdstore_getattr(struct c2_mdstore *md,
				   struct c2_cob *cob,
				   struct c2_cob_attr *attr,
				   struct c2_db_tx *tx);

/**
 * Handle readdir operation described by @rdpg on @cob. Input @cob
 * is so called statdata cob and returned by c2_cob_locate().
 *
 * Error code is returned in error case or something >= 0 otherwise.
*/
C2_INTERNAL int c2_mdstore_readdir(struct c2_mdstore *md,
				   struct c2_cob *cob,
				   struct c2_rdpg *rdpg, struct c2_db_tx *tx);

/**
 * Find cob by fid.
 */
C2_INTERNAL int c2_mdstore_locate(struct c2_mdstore *md,
				  const struct c2_fid *fid,
				  struct c2_cob **cob,
				  int flags, struct c2_db_tx *tx);

/**
 * Find cob by parent fid and name.
 */
C2_INTERNAL int c2_mdstore_lookup(struct c2_mdstore *md,
				  struct c2_fid *pfid,
				  const char *name,
				  int namelen,
				  struct c2_cob **cob, struct c2_db_tx *tx);

/**
 * Get path by @fid. Path @path is allocated by
 * c2_alloc() on success and path is saved there.
 * When it is not longer needed it may be freed
 * with c2_free().
 */
C2_INTERNAL int c2_mdstore_path(struct c2_mdstore *md,
				struct c2_fid *fid, char **path);

/* __COLIBRI_MDSTORE_MDSTORE_H__ */
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
