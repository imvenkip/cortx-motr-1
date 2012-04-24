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
 *                  Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 10/24/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <limits.h>

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/bitstring.h"

#include "cob.h"

/**
   @addtogroup cob
   @{
*/

static const struct c2_addb_ctx_type c2_cob_domain_addb = {
	.act_name = "cob-domain"
};

static const struct c2_addb_ctx_type c2_cob_addb = {
	.act_name = "cob"
};

static const struct c2_addb_loc cob_addb_loc = {
	.al_name = "cob"
};

void c2_cob_make_oikey(struct c2_cob_oikey *oikey, 
                       const struct c2_fid *fid,
                       int linkno)
{
        oikey->cok_fid = *fid;
        oikey->cok_linkno = linkno;
}

void c2_cob_make_nskey(struct c2_cob_nskey **keyh, 
                       const struct c2_fid *pfid,
                       const char *name, 
                       int namelen)
{
        struct c2_cob_nskey *key;

        key = c2_alloc(sizeof(*key) + namelen);
        C2_ASSERT(key != NULL);
        key->cnk_pfid = *pfid;
        c2_bitstring_copy(&key->cnk_name, name, namelen);
        *keyh = key;
}

int c2_cob_nskey_cmp(const struct c2_cob_nskey *k0, 
                     const struct c2_cob_nskey *k1)
{
        int rc;

        C2_PRE(c2_fid_is_set(&k0->cnk_pfid));
        C2_PRE(c2_fid_is_set(&k1->cnk_pfid));

        rc = c2_fid_cmp(&k0->cnk_pfid, &k1->cnk_pfid);
        return rc ?: c2_bitstring_cmp(&k0->cnk_name, &k1->cnk_name);
}

int c2_cob_nskey_size(const struct c2_cob_nskey *cnk)
{
        return sizeof(*cnk) +
                c2_bitstring_len_get(&cnk->cnk_name);
}

int c2_cob_fabrec_size(const struct c2_cob_fabrec *rec)
{
        return sizeof(*rec) + rec->cfb_linklen;
}

void c2_cob_make_fabrec(struct c2_cob_fabrec **rech, 
                        const char *link, int linklen)
{
        struct c2_cob_fabrec *rec;
        
        rec = c2_alloc(sizeof(struct c2_cob_fabrec) + linklen);
        C2_ASSERT(rec != NULL);
        rec->cfb_linklen = linklen;
        if (linklen > 0)
                memcpy(rec->cfb_link, link, linklen);
        *rech = rec;
}

int c2_cob_fabrec_size_max(void)
{
        return sizeof(struct c2_cob_fabrec) + NAME_MAX;
}

void c2_cob_make_fabrec_max(struct c2_cob_fabrec **rech)
{
        struct c2_cob_fabrec *rec;
        
        rec = c2_alloc(sizeof(struct c2_cob_fabrec) + NAME_MAX);
        C2_ASSERT(rec != NULL);
        rec->cfb_linklen = NAME_MAX;
        *rech = rec;
}

/**
   Make nskey for iterator. Allocate space for max possible name
   but put real string len into the struct.
*/
void c2_cob_make_nskey_max(struct c2_cob_nskey **keyh, 
                           const struct c2_fid *pfid,
                           const char *name, 
                           int namelen)
{
        struct c2_cob_nskey *key;

        key = c2_alloc(sizeof(*key) + NAME_MAX);
        key->cnk_pfid = *pfid;
        memcpy(c2_bitstring_buf_get(&key->cnk_name), name, namelen);
        c2_bitstring_len_set(&key->cnk_name, namelen);
        *keyh = key;
}

/**
   Key size for iterator in which case we don't know exact length of key
   and want to allocate it for worst case scenario, that is, for max 
   possible name len.
 */
int c2_cob_nskey_size_max(const struct c2_cob_nskey *cnk)
{
        return sizeof(*cnk) + NAME_MAX;
}

/** 
   Namespace table definition
*/
static int ns_cmp(struct c2_table *table, const void *key0, const void *key1)
{
        return c2_cob_nskey_cmp((const struct c2_cob_nskey *)key0, 
                                (const struct c2_cob_nskey *)key1);
}

static const struct c2_table_ops cob_ns_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = ~0
		},
		[TO_REC] = {
			.max_size = sizeof(struct c2_cob_nsrec)
		}
	},
	.key_cmp = ns_cmp
};

/** 
   Object index table definition. 
*/
static int oi_cmp(struct c2_table *table, const void *key0, const void *key1)
{
	const struct c2_cob_oikey *cok0 = key0;
	const struct c2_cob_oikey *cok1 = key1;
        int                        rc;

        C2_PRE(c2_fid_is_set(&cok0->cok_fid));
        C2_PRE(c2_fid_is_set(&cok1->cok_fid));

        rc = c2_fid_cmp(&cok0->cok_fid, &cok1->cok_fid);
        return rc ?: C2_3WAY(cok0->cok_linkno, cok1->cok_linkno);
}

static const struct c2_table_ops cob_oi_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_cob_oikey)
		},
		[TO_REC] = {
			.max_size = ~0
		}
	},
	.key_cmp = oi_cmp
};

static int fb_cmp(struct c2_table *table, const void *key0, const void *key1)
{
	const struct c2_cob_fabkey *cok0 = key0;
	const struct c2_cob_fabkey *cok1 = key1;

        C2_PRE(c2_fid_is_set(&cok0->cfb_fid));
        C2_PRE(c2_fid_is_set(&cok1->cfb_fid));

        return c2_fid_cmp(&cok0->cfb_fid, &cok1->cfb_fid);
}

static const struct c2_table_ops cob_fab_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_cob_fabkey)
		},
		[TO_REC] = {
                        .max_size = ~0
		}
	},
	.key_cmp = fb_cmp
};

/**
   Omg table definition.
*/
static int omg_cmp(struct c2_table *table, const void *key0, const void *key1)
{
	const struct c2_cob_omgkey *cok0 = key0;
	const struct c2_cob_omgkey *cok1 = key1;
        return C2_3WAY(cok0->cok_omgid, cok1->cok_omgid);
}

static const struct c2_table_ops cob_omg_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_cob_omgkey)
		},
		[TO_REC] = {
                        .max_size = sizeof(struct c2_cob_omgrec)
		}
	},
	.key_cmp = omg_cmp
};

static char *cob_dom_id_make(char *buf, const struct c2_cob_domain_id *id,
                             const char *prefix)
{
        sprintf(buf, "%s%u", prefix ? prefix : "", id->id);
        return buf;
}

/**
   Set up a new cob domain

   Tables are identified by the domain id, which must be set before calling
   this function.
  */
int c2_cob_domain_init(struct c2_cob_domain *dom, struct c2_dbenv *env,
                       struct c2_cob_domain_id *id)
{
        char table[16];
        int rc;

        dom->cd_id = *id;
        C2_PRE(dom->cd_id.id != 0);

	c2_rwlock_init(&dom->cd_guard);
        dom->cd_dbenv = env;

        /* Locate table based on domain id */
        rc = c2_table_init(&dom->cd_namespace, dom->cd_dbenv,
                           cob_dom_id_make(table, &dom->cd_id, "ns"),
                           0, &cob_ns_ops);
        if (rc != 0)
                return rc;
        rc = c2_table_init(&dom->cd_object_index, dom->cd_dbenv,
                           cob_dom_id_make(table, &dom->cd_id, "oi"),
                           0, &cob_oi_ops);
        if (rc != 0) {
                c2_table_fini(&dom->cd_namespace);
                return rc;
        }
        rc = c2_table_init(&dom->cd_fileattr_basic, dom->cd_dbenv,
                           cob_dom_id_make(table, &dom->cd_id, "fb"),
                           0, &cob_fab_ops);
	if (rc != 0) {
                c2_table_fini(&dom->cd_object_index);
                c2_table_fini(&dom->cd_namespace);
		return rc;
        }

        rc = c2_table_init(&dom->cd_fileattr_omg, dom->cd_dbenv,
                           cob_dom_id_make(table, &dom->cd_id, "fo"),
                           0, &cob_omg_ops);
	if (rc != 0) {
                c2_table_fini(&dom->cd_fileattr_basic);
                c2_table_fini(&dom->cd_object_index);
                c2_table_fini(&dom->cd_namespace);
		return rc;
        }

        c2_addb_ctx_init(&dom->cd_addb, &c2_cob_domain_addb, &env->d_addb);

        return 0;
}

void c2_cob_domain_fini(struct c2_cob_domain *dom)
{
        c2_table_fini(&dom->cd_fileattr_omg);
        c2_table_fini(&dom->cd_fileattr_basic);
        c2_table_fini(&dom->cd_object_index);
        c2_table_fini(&dom->cd_namespace);
	c2_rwlock_fini(&dom->cd_guard);
	c2_addb_ctx_fini(&dom->cd_addb);
}

static void cob_free_cb(struct c2_ref *ref);

static void cob_init(struct c2_cob_domain *dom, struct c2_cob *cob)
{
	c2_addb_ctx_init(&cob->co_addb, &c2_cob_addb, &dom->cd_addb);
	c2_ref_init(&cob->co_ref, 1, cob_free_cb);
        cob->co_fid = &cob->co_nsrec.cnr_fid;
        cob->co_dom = dom;
        cob->co_valid = 0;
}

static void cob_fini(struct c2_cob *cob)
{
        if (cob->co_valid & CA_NSKEY_FREE)
                c2_free(cob->co_nskey);
        else if (cob->co_valid & CA_NSKEY_DB)
                c2_db_pair_fini(&cob->co_oipair);
        if (cob->co_valid & CA_FABREC)
                c2_free(cob->co_fabrec);
	c2_addb_ctx_fini(&cob->co_addb);
}

/**
   Return cob memory to the pool
 */
static void cob_free_cb(struct c2_ref *ref)
{
	struct c2_cob *cob;

	cob = container_of(ref, struct c2_cob, co_ref);
        cob_fini(cob);
        c2_free(cob);
}

void c2_cob_get(struct c2_cob *cob)
{
	c2_ref_get(&cob->co_ref);
}

void c2_cob_put(struct c2_cob *cob)
{
        c2_ref_put(&cob->co_ref);
}

/**
   Allocate a new cob
 */
int c2_cob_alloc(struct c2_cob_domain *dom, struct c2_cob **out)
{
        struct c2_cob *cob;

        C2_ALLOC_PTR_ADDB(cob, &dom->cd_addb, &cob_addb_loc);
        if (cob == NULL)
                return -ENOMEM;

        cob_init(dom, cob);
        *out = cob;

        return 0;
}

static int cob_ns_lookup(struct c2_cob *cob, struct c2_db_tx *tx);
static int cob_oi_lookup(struct c2_cob *cob, struct c2_db_tx *tx);
static int cob_fab_lookup(struct c2_cob *cob, struct c2_db_tx *tx);

/** 
   Search for a record in the namespace table

   If the lookup fails, we return error and co_valid accurately reflects
   the missing fields.

   @see cob_oi_lookup
 */
static int cob_ns_lookup(struct c2_cob *cob, struct c2_db_tx *tx)
{
        struct c2_db_pair     pair;
        int                   rc;

        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 cob->co_nskey, c2_cob_nskey_size(cob->co_nskey),
			 &cob->co_nsrec, sizeof cob->co_nsrec);
        rc = c2_table_lookup(tx, &pair);
        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);
        
        if (rc == 0) {
                cob->co_valid |= CA_NSREC;
                C2_ASSERT(cob->co_nsrec.cnr_linkno > 0 ||
                          cob->co_nsrec.cnr_nlink > 0);
                C2_POST(c2_fid_is_set(cob->co_fid));
        }
        return rc;
}

/**
   Search for a record in the object index table.
   Most likely we want stat data for a given fid, so let's do that as well.

   @see cob_ns_lookup
 */
static int cob_oi_lookup(struct c2_cob *cob, struct c2_db_tx *tx)
{
        struct c2_db_cursor cursor;
        struct c2_cob_oikey oldkey;
        int                 rc;

        if (cob->co_valid & CA_NSKEY)
                return 0;

        if (cob->co_valid & CA_NSKEY_DB) {
                c2_db_pair_fini(&cob->co_oipair);
                cob->co_valid &= ~CA_NSKEY_DB;
        }

        /* 
         * Find the name from the object index table. Note the key buffer
         * is out of scope outside of this function, but the record is good
         * until c2_db_pair_fini. 
         */
        c2_db_pair_setup(&cob->co_oipair, &cob->co_dom->cd_object_index,
			 &cob->co_oikey, sizeof cob->co_oikey, NULL, 0);

        if (cob->co_oikey.cok_linkno != 0) {
                /*
                 * We use cursor here because in some situations we need
                 * to find most suitable position instead of exact location.
                 */
                rc = c2_db_cursor_init(&cursor, 
                                       &cob->co_dom->cd_object_index, tx, 0);
                if (rc) {
                        c2_db_pair_fini(&cob->co_oipair);
                        return rc;
                }

                oldkey = cob->co_oikey;
                rc = c2_db_cursor_get(&cursor, &cob->co_oipair);
                c2_db_cursor_fini(&cursor);
                if (rc) {
                        c2_db_pair_fini(&cob->co_oipair);
                        return rc;
                }

                /* 
                 * Found position should have same fid and linkno not less
                 * then requested. Otherwise we failed with lookup.
                 */
                if (!c2_fid_eq(&oldkey.cok_fid, &cob->co_oikey.cok_fid) ||
                    oldkey.cok_linkno > cob->co_oikey.cok_linkno) {
                        c2_db_pair_fini(&cob->co_oipair);
                        return -ENOENT;
                }
        } else {
                /*
                 * Let's use lookup that can return meaningful error code
                 * for case that we need exact position.
                 */
                rc = c2_table_lookup(tx, &cob->co_oipair);
                if (rc) {
                        c2_db_pair_fini(&cob->co_oipair);
                        return rc;
                }
        }

        cob->co_nskey =
                (struct c2_cob_nskey *)cob->co_oipair.dp_rec.db_buf.b_addr;
        cob->co_valid |= CA_NSKEY | CA_NSKEY_DB;

        return 0;
}

/** 
   Search for a record in the fileattr_basic table.

   @see cob_ns_lookup
   @see cob_oi_lookup
 */
static int cob_fab_lookup(struct c2_cob *cob, struct c2_db_tx *tx)
{
        struct c2_cob_fabkey fabkey;
        struct c2_db_pair    pair;
        int                  rc;

        if (cob->co_valid & CA_FABREC)
                return 0;

        fabkey.cfb_fid = *cob->co_fid;
        c2_cob_make_fabrec_max(&cob->co_fabrec);
        c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
			 &fabkey, sizeof fabkey, cob->co_fabrec,
			 c2_cob_fabrec_size_max());
        rc = c2_table_lookup(tx, &pair);
        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);

        if (rc == 0)
                cob->co_valid |= CA_FABREC;
        else
                cob->co_valid &= ~CA_FABREC;

        return rc;
}

/** 
   Search for a record in the fileattr_omg table.
   @see cob_fab_lookup
 */
static int cob_omg_lookup(struct c2_cob *cob, struct c2_db_tx *tx)
{
        struct c2_cob_omgkey omgkey;
        struct c2_db_pair    pair;
        int                  rc;

        if (cob->co_valid & CA_OMGREC)
                return 0;

        omgkey.cok_omgid = cob->co_nsrec.cnr_omgid;
        c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_omg,
			 &omgkey, sizeof omgkey,
			 &cob->co_omgrec, sizeof cob->co_omgrec);
        rc = c2_table_lookup(tx, &pair);
        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);

        if (rc == 0)
                cob->co_valid |= CA_OMGREC;
        else
                cob->co_valid &= ~CA_OMGREC;

        return rc;
}

/**
   Lookup a filename in the namespace table.

   Create a new cob and populate it with the contents of the namespace
   record; i.e. the stat data and fid.

   This lookup adds a reference to the cob.

   On namespace table lookup failure, no cob is created. On failure to lookup
   other data, co_valid fields shall be correctly set.
 */
int c2_cob_lookup(struct c2_cob_domain *dom, struct c2_cob_nskey *nskey,
                  uint64_t need, struct c2_cob **out, struct c2_db_tx *tx)
{
        struct c2_cob *cob;
        int            rc;

        /*
         * Zero out "out" just in case that if we fail here, it is
         * easier to find abnormal using of NULL cob.
         */
        C2_ASSERT(out != NULL);
        *out = NULL;

        rc = c2_cob_alloc(dom, &cob);
        if (rc)
                return rc;

        cob->co_nskey = nskey;
        cob->co_valid |= CA_NSKEY;
        
        if (need & CA_NSKEY_FREE)
                cob->co_valid |= CA_NSKEY_FREE;

        rc = cob_ns_lookup(cob, tx);
        if (rc) {
                c2_cob_put(cob);
                return rc;
        }

        /*
         * Get the fabrec here too if needed.  co_valid will be set
         * correctly inside the call so we can ignore the return code. 
         */
        if (need & CA_FABREC) {
                rc = cob_fab_lookup(cob, tx);
                if (rc) {
                        c2_cob_put(cob);
                        return rc;
                }
        }

        /*
         * Get omg attributes as well if we need it.
         */
        if (need & CA_OMGREC) {
                rc = cob_omg_lookup(cob, tx);
                if (rc) {
                        c2_cob_put(cob);
                        return rc;
                }
        }

        *out = cob;
	return rc;
}

/**
   Locate by object index key

   Otherwise create a new cob and populate it with the contents of the oi
   record; i.e. the filename.

   This lookup adds a reference to the cob.
 */
int c2_cob_locate(struct c2_cob_domain *dom, struct c2_cob_oikey *oikey,
                  struct c2_cob **out, struct c2_db_tx *tx)
{
        struct c2_cob *cob;
        int rc;

        C2_PRE(c2_fid_is_set(&oikey->cok_fid));

        /*
         * Zero out "out" just in case that if we fail here, it is
         * easier to find abnormal using of NULL cob.
         */
        C2_ASSERT(out != NULL);
        *out = NULL;

        /* Get cob memory. */
        rc = c2_cob_alloc(dom, &cob);
        if (rc)
                return rc;

        cob->co_oikey = *oikey;
        rc = cob_oi_lookup(cob, tx);
        if (rc) {
                c2_cob_put(cob);
                return rc;
        }

        rc = cob_ns_lookup(cob, tx);
        if (rc) {
                c2_cob_put(cob);
                return rc;
        }
        rc = cob_fab_lookup(cob, tx);
        if (rc) {
                c2_cob_put(cob);
                return rc;
        }
        rc = cob_omg_lookup(cob, tx);
        if (rc) {
                c2_cob_put(cob);
                return rc;
        }

        *out = cob;
	return rc;
}

int c2_cob_iterator_init(struct c2_cob *cob, 
                         struct c2_cob_iterator *it,
                         struct c2_bitstring *name,
                         struct c2_db_tx *tx)
{
        int rc;

        /*
         * Prepare entry key using passed started pos.
         */
        c2_cob_make_nskey_max(&it->ci_key, cob->co_fid, 
                              c2_bitstring_buf_get(name),
                              c2_bitstring_len_get(name));

        /*
         * Init iterator cursor with max possible key size.
         */
        c2_db_pair_setup(&it->ci_pair, &cob->co_dom->cd_namespace,
			 it->ci_key, c2_cob_nskey_size_max(it->ci_key), 
			 &it->ci_rec, sizeof it->ci_rec);

        rc = c2_db_cursor_init(&it->ci_cursor, 
                               &cob->co_dom->cd_namespace, tx, 0);
        if (rc) {
                c2_db_pair_release(&it->ci_pair);
                c2_db_pair_fini(&it->ci_pair);
                c2_free(it->ci_key);
                return rc;
        }
        it->ci_cob = cob;
        return rc;
}

/**
   Position in table according to @it properties.
*/
int c2_cob_iterator_get(struct c2_cob_iterator *it)
{
        int rc;

        rc = c2_db_cursor_get(&it->ci_cursor, &it->ci_pair);

        /*
         * Exact position found.
         */
        if (rc == 0)
                return 1;

        /*
         * Nothing found, cursor is on another object key. 
         */
        if (!c2_fid_eq(&it->ci_key->cnk_pfid, it->ci_cob->co_fid))
                rc = -ENOENT;

        /*
         * Not exact position found.
         */
        return 0;
}

int c2_cob_iterator_next(struct c2_cob_iterator *it)
{
        int rc;

        rc = c2_db_cursor_next(&it->ci_cursor, &it->ci_pair);
        if (rc == -ENOENT)
                return 1;
        else if (rc)
                return rc;

        if (!c2_fid_eq(&it->ci_key->cnk_pfid, it->ci_cob->co_fid))
                return 1;

        return 0;
}

void c2_cob_iterator_fini(struct c2_cob_iterator *it)
{
        c2_db_pair_release(&it->ci_pair);
	c2_db_pair_fini(&it->ci_pair);
        c2_db_cursor_fini(&it->ci_cursor);
        c2_free(it->ci_key);
}

/** 
   For assertions only.
*/
static bool c2_cob_is_valid(struct c2_cob *cob)
{
        return c2_fid_is_set(cob->co_fid);
}

/**
   Add a new cob to the namespace.

   This doesn't create a new stob; just creates metadata table entries
   for it.

   This takes a reference on the cob in-memory struct.
 */
int c2_cob_create(struct c2_cob_domain *dom, 
                  struct c2_cob_nskey  *nskey,
                  struct c2_cob_nsrec  *nsrec,
                  struct c2_cob_fabrec *fabrec,
                  struct c2_cob_omgrec *omgrec,
                  struct c2_cob       **out,
                  struct c2_db_tx      *tx)
{
        struct c2_db_pair     pair;
        struct c2_cob_omgkey  omgkey;
        struct c2_cob_fabkey  fabkey;
        struct c2_db_cursor   cursor;
        struct c2_cob        *cob;
        int                   rc;

        C2_PRE(out != NULL);
        *out = NULL;

        C2_PRE(nskey != NULL);
        C2_PRE(nsrec != NULL);
        C2_PRE(fabrec != NULL);
        C2_PRE(omgrec != NULL);
	C2_PRE(c2_fid_is_set(&nsrec->cnr_fid));
        C2_PRE(c2_fid_is_set(&nskey->cnk_pfid));

        rc = c2_cob_alloc(dom, &cob);
        if (rc)
                return rc;
        /*
         * Allocate omgid using last allocated number + 1.
         * Find terminator record and do prev() out of it
         * to find last allocated.
         */
        omgkey.cok_omgid = ~0ULL;
        
        rc = c2_db_cursor_init(&cursor, 
                               &cob->co_dom->cd_fileattr_omg, tx, 0);
        if (rc)
                goto out;

        c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_omg,
			 &omgkey, sizeof omgkey, &cob->co_omgrec,
			 sizeof cob->co_omgrec);

        rc = c2_db_cursor_get(&cursor, &pair);
        if (rc == 0)
                rc = c2_db_cursor_prev(&cursor, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
        c2_db_cursor_fini(&cursor);

        if (rc == 0) {
                /*
                 * Bump last allocated omgid.
                 */
                nsrec->cnr_omgid = ++omgkey.cok_omgid;
        } else {
                /*
                 * No terminator record found, this
                 * must be root creating.
                 */
                nsrec->cnr_omgid = 0;
        }

        cob->co_nskey = nskey;
        cob->co_valid |= CA_NSKEY;

        /*
         * This is what add_name will use to create new name.
         */
        cob->co_nsrec = *nsrec;
        cob->co_valid |= CA_NSREC;
        cob->co_nsrec.cnr_cntr = 0;

        /*
         * Intialize counter with 1 which what will be used
         * for adding second name. We do it this way to avoid
         * doing special c2_cob_update() solely for having
         * this field stored in db.
         */
        nsrec->cnr_cntr = 1;

        /*
         * Let's create name, statdata and object index.
         */
        rc = c2_cob_add_name(cob, nskey, nsrec, tx);
        if (rc)
                goto out;

        /*
         * Prepare key for attribute tables.
         */
        fabkey.cfb_fid = *cob->co_fid;

        /* 
         * Now let's update file attributes. Cache the fabrec. 
         */
        cob->co_fabrec = fabrec;

        /* 
         * Add to fileattr-basic table. 
         */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
			 &fabkey, sizeof fabkey, cob->co_fabrec, 
			 c2_cob_fabrec_size(cob->co_fabrec));

        rc = c2_table_insert(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	if (rc)
                goto out;

        /*
         * Prepare omg key.
         */
        omgkey.cok_omgid = nsrec->cnr_omgid;

        /* 
         * Now let's update omg attributes. Cache the omgrec. 
         */
        cob->co_omgrec = *omgrec;
        cob->co_valid |= CA_OMGREC;
        
        /* 
         * Add to fileattr-omg table. 
         */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_omg,
			 &omgkey, sizeof omgkey,
			 &cob->co_omgrec, sizeof cob->co_omgrec);

        rc = c2_table_insert(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	if (rc)
	        goto out;

        cob->co_valid |= CA_NSKEY_FREE | CA_FABREC;
        *out = cob;
out:
        C2_ADDB_ADD(&cob->co_dom->cd_addb, &cob_addb_loc, 
                    c2_addb_func_fail, "cob_create", rc);
        if (rc)
                c2_cob_put(cob);
        return rc;
}

/**
   Kill name+statdata, object index entry, file attrs and omg.
 */
int c2_cob_delete(struct c2_cob *cob, struct c2_db_tx *tx)
{
        struct c2_cob_fabkey fabkey;
        struct c2_cob_omgkey omgkey;
        struct c2_db_pair    pair;
        int                  rc;

        C2_PRE(c2_cob_is_valid(cob));
        C2_PRE(cob->co_valid & CA_NSKEY);

        /*
         * Delete last name from namespace and object index.
         */
        rc = c2_cob_del_name(cob, cob->co_nskey, tx);
        if (rc)
                goto out;

        if (cob->co_nsrec.cnr_linkno == 0) {
                /* 
                 * Remove from the fileattr_basic table. 
                 */
                fabkey.cfb_fid = *cob->co_fid;
                c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
			         &fabkey, sizeof fabkey, NULL, 0);
        
                /* 
                 * Ignore errors; it's a dangling table entry but causes 
                 * no harm. 
                 */
                c2_table_delete(tx, &pair);
	        c2_db_pair_fini(&pair);

                /*
                 * @todo: Omgrec may be shared between multiple objects.
                 * Delete should take this into account as well as update.
                 */
                omgkey.cok_omgid = cob->co_nsrec.cnr_omgid;

                /* 
                 * Remove from the fileattr_omg table. 
                 */
                c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_omg,
			         &omgkey, sizeof omgkey, NULL, 0);
        
                /* 
                 * Ignore errors; it's a dangling table entry but causes 
                 * no harm. 
                 */
                c2_table_delete(tx, &pair);
	        c2_db_pair_fini(&pair);
	}
out:
        C2_ADDB_ADD(&cob->co_dom->cd_addb, &cob_addb_loc, 
                    c2_addb_func_fail, "cob_delete", rc);
        return rc;
}

/**
   Update file attributes of passed cob with @nsrec and fabrec fields.
*/
int c2_cob_update(struct c2_cob         *cob,
                   struct c2_cob_nsrec  *nsrec,
                   struct c2_cob_fabrec *fabrec,
                   struct c2_cob_omgrec *omgrec,
                   struct c2_db_tx      *tx)
{
        struct c2_cob_omgkey  omgkey;
        struct c2_cob_fabkey  fabkey;
        struct c2_db_pair     pair;
        int                   rc;
        
        C2_PRE(c2_cob_is_valid(cob));
        C2_PRE(cob->co_valid & CA_NSKEY);

        if (nsrec != NULL) {
                C2_ASSERT(nsrec->cnr_nlink > 0);

                cob->co_nsrec = *nsrec;
                cob->co_valid |= CA_NSREC;
        
                c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
                                 cob->co_nskey, c2_cob_nskey_size(cob->co_nskey),
                                 &cob->co_nsrec, sizeof cob->co_nsrec);
                rc = c2_table_update(tx, &pair);
                c2_db_pair_release(&pair);
                c2_db_pair_fini(&pair);
                if (rc)
                        goto out;
        }
        
        if (fabrec != NULL) {
                fabkey.cfb_fid = *cob->co_fid;
                if (fabrec != cob->co_fabrec) {
                        if (cob->co_valid & CA_FABREC)
                                c2_free(cob->co_fabrec);
                        cob->co_fabrec = fabrec;
                }
                cob->co_valid |= CA_FABREC;
        
                c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
                                 &fabkey, sizeof fabkey, cob->co_fabrec, 
                                 c2_cob_fabrec_size(cob->co_fabrec));
                rc = c2_table_update(tx, &pair);
                c2_db_pair_release(&pair);
                c2_db_pair_fini(&pair);
        }

        if (omgrec != NULL) {
                /*
                 * @todo: Omgrec may be shared between multiple objects.
                 * We need to take this into account.
                 */
                omgkey.cok_omgid = cob->co_nsrec.cnr_omgid;
                
                cob->co_omgrec = *omgrec;
                cob->co_valid |= CA_OMGREC;
        
                c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_omg,
                                 &omgkey, sizeof omgkey,
                                 &cob->co_omgrec, sizeof cob->co_omgrec);
                rc = c2_table_update(tx, &pair);
                c2_db_pair_release(&pair);
                c2_db_pair_fini(&pair);
        }
out:
        C2_ADDB_ADD(&cob->co_dom->cd_addb, &cob_addb_loc, 
                    c2_addb_func_fail, "cob_update", rc);
        return rc;
}

/**
   Add name to namespace and object index.
      
   cob   - stat data (zero name) cob;
   nskey - new name to add to the file;
   tx    - transaction handle.
*/
int c2_cob_add_name(struct c2_cob        *cob,
                    struct c2_cob_nskey  *nskey,
                    struct c2_cob_nsrec  *nsrec,
                    struct c2_db_tx      *tx)
{
        struct c2_cob_oikey  oikey;
        struct c2_db_pair    pair;
	int                  rc;

        C2_PRE(cob != NULL);
        C2_PRE(nskey != NULL);
        C2_PRE(c2_fid_is_set(&nskey->cnk_pfid));
        C2_PRE(c2_cob_is_valid(cob));

        /*
         * Add new name to object index table. Table insert should fail
         * if name already exists. 
         */
        c2_cob_make_oikey(&oikey, &nsrec->cnr_fid, 
                          cob->co_nsrec.cnr_cntr);

        c2_db_pair_setup(&pair, &cob->co_dom->cd_object_index,
			 &oikey, sizeof oikey, nskey, 
			 c2_cob_nskey_size(nskey));

        rc = c2_table_insert(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	if (rc)
                goto out;

        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 nskey, c2_cob_nskey_size(nskey),
			 nsrec, sizeof *nsrec);

        rc = c2_table_insert(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
out:
        C2_ADDB_ADD(&cob->co_dom->cd_addb, &cob_addb_loc, 
                    c2_addb_func_fail, "cob_add_name", rc);
        return rc;
}

/**
   Delete name from namespace and object index.
   
   cob   - stat data (zero name) cob;
   nskey - name to kill (may be the name of statdata);
   tx    - transcation handle.
*/
int c2_cob_del_name(struct c2_cob        *cob, 
                    struct c2_cob_nskey  *nskey,
                    struct c2_db_tx      *tx)
{
        struct c2_cob_oikey oikey;
        struct c2_cob_nsrec nsrec;
        struct c2_db_pair   pair;
        int                 rc;

        C2_PRE(c2_cob_is_valid(cob));
        C2_PRE(cob->co_valid & CA_NSKEY);

        /*
         * Kill name from namespace.
         */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
                         nskey, c2_cob_nskey_size(nskey),
		         &nsrec, sizeof nsrec);
        rc = c2_table_lookup(tx, &pair);
        if (rc) {
                c2_db_pair_fini(&pair);
                goto out;
        }
        
        rc = c2_table_delete(tx, &pair);
        c2_db_pair_fini(&pair);
        if (rc)
                goto out;

        /*
         * Let's also kill object index entry.
         */
        c2_cob_make_oikey(&oikey, cob->co_fid, nsrec.cnr_linkno);
        c2_db_pair_setup(&pair, &cob->co_dom->cd_object_index,
                         &oikey, sizeof oikey, NULL, 0);
        rc = c2_table_delete(tx, &pair);
        c2_db_pair_fini(&pair);

out:
        C2_ADDB_ADD(&cob->co_dom->cd_addb, &cob_addb_loc, 
                    c2_addb_func_fail, "cob_del_name", rc);
        return rc;
}

/**
   Rename oldkey with passed newkey.
   
   cob    - stat data (zero name) cob;
   srckey - source name;
   tgtkey - target name;
   tx     - transcation handle
*/
int c2_cob_update_name(struct c2_cob        *cob, 
                       struct c2_cob_nskey  *srckey,
                       struct c2_cob_nskey  *tgtkey,
                       struct c2_db_tx      *tx)
{
        struct c2_cob_nsrec  nsrec;
        struct c2_db_pair    pair;
        struct c2_cob_oikey  oikey;
        int                  rc;

        C2_PRE(c2_cob_is_valid(cob));
        C2_PRE(srckey != NULL && tgtkey != NULL);
        
        /*
         * Insert new record with nsrec found with srckey.
         */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
	                 srckey, c2_cob_nskey_size(srckey),
		         &nsrec, sizeof nsrec);
        rc = c2_table_lookup(tx, &pair);
        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);
        if (rc)
                goto out;

        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
                         tgtkey, c2_cob_nskey_size(tgtkey),
                         &nsrec, sizeof nsrec);
        rc = c2_table_insert(tx, &pair);
        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);
        if (rc)
                goto out;
        
        /*
         * Kill old record. Error will be returned if
         * nothing found.
         */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
                         srckey, c2_cob_nskey_size(srckey),
                         NULL, 0);
        rc = c2_table_delete(tx, &pair);
        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);
        if (rc)
                goto out;

        /* Update object index */
        c2_cob_make_oikey(&oikey, cob->co_fid, nsrec.cnr_linkno);
        c2_db_pair_setup(&pair, &cob->co_dom->cd_object_index,
                         &oikey, sizeof oikey, tgtkey,
                         c2_cob_nskey_size(tgtkey));
        rc = c2_table_update(tx, &pair);
        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);
        if (rc)
                goto out;

        /*
         * Update key to new one.
         */        
        if (cob->co_valid & CA_NSKEY_FREE)
                c2_free(cob->co_nskey);
        else if (cob->co_valid & CA_NSKEY_DB)
                c2_db_pair_fini(&cob->co_oipair);
        cob->co_valid &= ~(CA_NSKEY_FREE | CA_NSKEY_DB);
        c2_cob_make_nskey(&cob->co_nskey, &tgtkey->cnk_pfid,
                          c2_bitstring_buf_get(&tgtkey->cnk_name),
                          c2_bitstring_len_get(&tgtkey->cnk_name));
        cob->co_valid |= CA_NSKEY_FREE;
out:
        C2_ADDB_ADD(&cob->co_dom->cd_addb, &cob_addb_loc, 
                    c2_addb_func_fail, "cob_update_name", rc);
        return rc;
}

/** @} end group cob */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
