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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/bitstring.h"

#include "cob/cob.h"

#ifdef __KERNEL__
#define printf printk
#endif

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

/** Namespace table definition */
static int ns_cmp(struct c2_table *table, const void *key0, const void *key1)
{
	const struct c2_cob_nskey *cnk0 = key0;
	const struct c2_cob_nskey *cnk1 = key1;
        int rc;

        C2_PRE(c2_stob_id_is_set(&cnk0->cnk_pfid));
        C2_PRE(c2_stob_id_is_set(&cnk1->cnk_pfid));

        rc = c2_stob_id_cmp(&cnk0->cnk_pfid, &cnk1->cnk_pfid);
        return rc ?: c2_bitstring_cmp(&cnk0->cnk_name, &cnk1->cnk_name);
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

int c2_cob_nskey_size(const struct c2_cob_nskey *cnk)
{
        return (sizeof(*cnk) +
                c2_bitstring_len_get(&cnk->cnk_name));
}

/** Object index table definition */
static int oi_cmp(struct c2_table *table, const void *key0, const void *key1)
{
	const struct c2_cob_oikey *cok0 = key0;
	const struct c2_cob_oikey *cok1 = key1;
        int rc;

        C2_PRE(c2_stob_id_is_set(&cok0->cok_stobid));
        C2_PRE(c2_stob_id_is_set(&cok1->cok_stobid));

        rc = c2_stob_id_cmp(&cok0->cok_stobid, &cok1->cok_stobid);
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
	const struct c2_stob_id	*id0 = key0;
	const struct c2_stob_id *id1 = key1;

	C2_PRE(c2_stob_id_is_set(id0));
	C2_PRE(c2_stob_id_is_set(id1));

	return c2_stob_id_cmp(id0, id1);
}
static const struct c2_table_ops cob_fab_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_stob_id)
		},
		[TO_REC] = {
                        .max_size = sizeof(struct c2_cob_fabrec)
		}
	},
	.key_cmp = fb_cmp
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

        /* locate table based on domain id */
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

        c2_addb_ctx_init(&dom->cd_addb, &c2_cob_domain_addb, &env->d_addb);

        return 0;
}
C2_EXPORTED(c2_cob_domain_init);

void c2_cob_domain_fini(struct c2_cob_domain *dom)
{
        c2_table_fini(&dom->cd_namespace);
        c2_table_fini(&dom->cd_object_index);
        c2_table_fini(&dom->cd_fileattr_basic);
	c2_rwlock_fini(&dom->cd_guard);
	c2_addb_ctx_fini(&dom->cd_addb);
}
C2_EXPORTED(c2_cob_domain_fini);

static void cob_free_cb(struct c2_ref *ref);

static void cob_init(struct c2_cob_domain *dom, struct c2_cob *cob)
{
        cob->co_dom = dom;
	c2_ref_init(&cob->co_ref, 1, cob_free_cb);
	C2_SET0(&cob->co_stobid);
        c2_rwlock_init(&cob->co_guard);
        cob->co_valid = 0;
	c2_addb_ctx_init(&cob->co_addb, &c2_cob_addb, &dom->cd_addb);
}

static void cob_fini(struct c2_cob *cob)
{
        if (cob->co_valid & CA_NSKEY_FREE)
                c2_free(cob->co_nskey);
        else if (cob->co_valid & CA_NSKEY_DB)
                c2_db_pair_fini(&cob->co_oipair);

	c2_rwlock_fini(&cob->co_guard);
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

   @todo Optimization: we should have a pool of cobs to reuse.
   We need a general memory pool handler: slab_init(size, count).
 */
static int cob_alloc(struct c2_cob_domain *dom, struct c2_cob **out)
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

/** Search for a record in the namespace table

    If the lookup fails, we return error and co_valid accurately reflects
    the missing fields.

    @see cob_oi_lookup
 */
static int cob_ns_lookup(struct c2_cob *cob, struct c2_db_tx *tx)
{
        struct c2_db_pair pair;
        int rc;

        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 cob->co_nskey, c2_cob_nskey_size(cob->co_nskey),
			 &cob->co_nsrec, sizeof cob->co_nsrec);
        rc = c2_table_lookup(tx, &pair);

        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);

        if (rc == 0) {
                cob->co_valid |= CA_NSREC;
                C2_POST(c2_stob_id_is_set(&cob->co_stobid));
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
        struct c2_cob_oikey oikey;
        int rc;

        if (cob->co_valid & CA_NSKEY)
                /* Don't need to lookup anything if nskey is already here */
                return 0;

        oikey.cok_stobid = cob->co_stobid;
        oikey.cok_linkno = 0;

        /* Find the name from the object index table.
           Note the key buffer is out of scope outside of this function,
           but the record is good until c2_db_pair_fini. */
        c2_db_pair_setup(&cob->co_oipair, &cob->co_dom->cd_object_index,
			 &oikey, sizeof oikey,
                         NULL, 0);
        rc = c2_table_lookup(tx, &cob->co_oipair);
        c2_db_pair_release(&cob->co_oipair);
        if (rc) {
                c2_db_pair_fini(&cob->co_oipair);
                return rc;
        }

        cob->co_nskey =
                (struct c2_cob_nskey *)cob->co_oipair.dp_rec.db_buf.b_addr;
        cob->co_valid |= CA_NSKEY | CA_NSKEY_DB;

        return 0;
}

/** Search for a record in the fileattr_basic table
  @see cob_ns_lookup
  @see cob_oi_lookup
 */
static int cob_fab_lookup(struct c2_cob *cob, struct c2_db_tx *tx)
{
        struct c2_db_pair pair;
        int rc;

        if (cob->co_valid & CA_FABREC)
                return 0;

        c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
			 &cob->co_stobid, sizeof cob->co_stobid,
			 &cob->co_fabrec, sizeof cob->co_fabrec);
        rc = c2_table_lookup(tx, &pair);
        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);

        if (rc == 0)
                cob->co_valid |= CA_FABREC;

        return rc;
}

/**
   Check if a cob with this name is in the cache

   This takes a cob reference if the cob is found in the cache.
   Returns -ENOENT if not in cache.

   @see cob_cache_oicheck
 */
static int cob_cache_nscheck(struct c2_cob_domain *dom,
                             const struct c2_cob_nskey *nskey,
                             struct c2_cob **out)
{
        /* @todo implement a cache for cobs and check if the cob with this
         nskey is in the cache */
        return -ENOENT;
}

/**
   Lookup a filename in the namespace table

   Check if cached first; otherwise create a new cob and populate it with
   the contents of the namespace record; i.e. the stat data and fid.

   The stat data and the namespace key (filename) may be cached.

   This lookup adds a reference to the cob.

   On namespace table lookup failure, no cob is created. On failure to lookup
   other data, co_valid fields shall be correctly set.
 */
int c2_cob_lookup(struct c2_cob_domain *dom, struct c2_cob_nskey *nskey,
                  uint64_t need, struct c2_cob **out, struct c2_db_tx *tx)
{
        struct c2_cob *cob;
        int rc;

        rc = cob_cache_nscheck(dom, nskey, out);
        if (rc == 0) {
                /* Cached, we took ref above. But we do need to free nskey
                 if they asked */
                if (need & CA_NSKEY_FREE)
                        c2_free(nskey);
                return 0;
        }

        /* Get cob memory */
        rc = cob_alloc(dom, &cob);
        if (rc)
                return rc;

        cob->co_nskey = nskey;
        rc = cob_ns_lookup(cob, tx);
        if (rc) {
                c2_cob_put(cob);
                return rc;
        }

        if (need & CA_NSKEY_FREE)
        /* Otherwise we can't assume NSKEY will stick around */
                cob->co_valid |= CA_NSKEY | CA_NSKEY_FREE;

        /* Get the fabrec here too if needed.  co_valid will be set
         correctly inside the call so we can ignore the return code */
        if (need & CA_FABREC)
                cob_fab_lookup(cob, tx);

        *out = cob;
	return rc;
}

/**
   Check if a cob with this fid is in the cache

   This takes a cob reference if the cob is found in the cache.
   @see cob_cache_nscheck
 */
static int cob_cache_oicheck(struct c2_cob_domain *dom,
                             const struct c2_stob_id *id, struct c2_cob **out)
{
        /* @todo implement a cache for cobs and check if the cob with this
         oi is in the cache */
        return -ENOENT;
}

/**
   Locate by stob id

   Check if cached first; otherwise create a new cob and populate it with
   the contents of the oi record; i.e. the filename.

   This lookup adds a reference to the cob.
 */
int c2_cob_locate(struct c2_cob_domain *dom, const struct c2_stob_id *id,
                  struct c2_cob **out, struct c2_db_tx *tx)
{
        struct c2_cob *cob;
        int rc;

        C2_PRE(c2_stob_id_is_set(id));

        rc = cob_cache_oicheck(dom, id, out);
        if (rc == 0) /* cached, took ref */
                return 0;

        /* Get cob memory */
        rc = cob_alloc(dom, &cob);
        if (rc)
                return rc;

        cob->co_stobid = *id;
        rc = cob_oi_lookup(cob, tx);
        if (rc) {
                c2_cob_put(cob);
                return rc;
        }

        /* Let's assume we want to lookup these up as well */
        cob_ns_lookup(cob, tx);
        cob_fab_lookup(cob, tx);

        *out = cob;
	return rc;
}

C2_ADDB_EV_DEFINE(cob_eexist, "md_exists", C2_ADDB_EVENT_COB_MDEXISTS,
		  C2_ADDB_INVAL);

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
                  uint64_t              need,
                  struct c2_cob       **out,
                  struct c2_db_tx      *tx)
{
        struct c2_cob      *cob = NULL;
        struct c2_cob_oikey oikey;
        struct c2_db_pair   pair;
	int rc;

        C2_PRE(nskey != NULL);
        C2_PRE(nsrec != NULL);
        C2_PRE(fabrec != NULL);
	C2_PRE(c2_stob_id_is_set(&nsrec->cnr_stobid));
        C2_PRE(c2_stob_id_is_set(&nskey->cnk_pfid));
        C2_PRE(nsrec->cnr_nlink == 1);

        /* Get cob memory */
        rc = cob_alloc(dom, &cob);
        if (rc)
                return rc;

        /* Populate the cob */

        cob->co_nskey = nskey;
        /* Take over nskey memory management from caller */
        if (need & CA_NSKEY_FREE)
                cob->co_valid |= CA_NSKEY | CA_NSKEY_FREE;

        /* Add to object index table.  Table insert should fail if
           already exists. */
        /* Fid in object index key is the child fid of nsrec */
        oikey.cok_stobid = nsrec->cnr_stobid;
        oikey.cok_linkno = 0;
        c2_db_pair_setup(&pair, &dom->cd_object_index,
			 &oikey, sizeof oikey,
			 cob->co_nskey, c2_cob_nskey_size(cob->co_nskey));

        rc = c2_table_insert(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	if (rc)
                goto out_free;

        /* Cache the nsrec */
        cob->co_nsrec = *nsrec;
        cob->co_valid |= CA_NSREC;

        /* Add to namespace table */
        c2_db_pair_setup(&pair, &dom->cd_namespace,
			 cob->co_nskey, c2_cob_nskey_size(cob->co_nskey),
			 &cob->co_nsrec, sizeof cob->co_nsrec);

        rc = c2_table_insert(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	if (rc)
                goto out_free;

        /* Cache the fabrec */
        cob->co_fabrec = *fabrec;
        cob->co_valid |= CA_FABREC;

        /* Add to filattr-basic table */
        c2_db_pair_setup(&pair, &dom->cd_fileattr_basic,
			 &cob->co_stobid, sizeof cob->co_stobid,
			 &cob->co_fabrec, sizeof cob->co_fabrec);

        rc = c2_table_insert(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	if (rc)
                goto out_free;

        *out = cob;
	return 0;

out_free:
        c2_cob_put(cob);
        C2_ADDB_ADD(&dom->cd_addb, &cob_addb_loc, cob_eexist, rc);
        return rc;
}

/** For assertions only */
static bool c2_cob_is_valid(struct c2_cob *cob)
{
        return c2_stob_id_is_set(&cob->co_stobid);
}

C2_ADDB_EV_DEFINE(cob_delete, "md_delete", C2_ADDB_EVENT_COB_MDDELETE,
		  C2_ADDB_FLAG);

/**
   Delete the metadata for this cob.

   Caller must be holding a reference on this cob, which
   will be released here.

   @todo right now we don't handle hardlinks
 */
int c2_cob_delete(struct c2_cob *cob, struct c2_db_tx *tx)
{
        struct c2_cob_oikey oikey;
        struct c2_db_pair pair;
        int rc;

        C2_PRE(c2_cob_is_valid(cob));

        /* We need the name key */
        rc = cob_oi_lookup(cob, tx);
        if (rc)
                goto out;
        C2_POST(cob->co_valid & CA_NSKEY);

        /* Remove from the object index table */
        oikey.cok_stobid = cob->co_stobid;
        oikey.cok_linkno = 0;
        c2_db_pair_setup(&pair, &cob->co_dom->cd_object_index,
			 &oikey, sizeof oikey,
			 NULL, 0);
        rc = c2_table_delete(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
        if (rc)
                goto out;

        /* Remove from the namespace table */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 cob->co_nskey, c2_cob_nskey_size(cob->co_nskey),
			 NULL, 0);
        rc = c2_table_delete(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);

        /* Remove from the fileattr_basic table */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
			 &cob->co_stobid, sizeof cob->co_stobid,
			 NULL, 0);
        /* ignore errors; it's a dangling table entry but causes no harm */
        c2_table_delete(tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);

out:
        /* If the op failed, assume we're not going to do anything else about
           it, so log and drop in all cases. */
        C2_ADDB_ADD(&cob->co_dom->cd_addb, &cob_addb_loc, cob_delete, rc == 0);
        c2_cob_put(cob);
        return rc;
}

int c2_cob_update(struct c2_cob		*cob,
		  struct c2_cob_nsrec	*nsrec,
		  struct c2_cob_fabrec	*fabrec,
		  struct c2_db_tx	*tx)
{
	struct c2_db_pair	pair;
	int			rc;

	C2_PRE(c2_cob_is_valid(cob));
	C2_PRE(cob->co_valid & CA_NSKEY);

	if (nsrec != NULL) {
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
		cob->co_fabrec = *fabrec;
		cob->co_valid |= CA_FABREC;

		c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
			&cob->co_nsrec.cnr_stobid, sizeof cob->co_nsrec.cnr_stobid,
			&cob->co_fabrec, sizeof cob->co_fabrec);

		rc = c2_table_update(tx, &pair);

		c2_db_pair_release(&pair);
		c2_db_pair_fini(&pair);
	}

out:
	C2_ADDB_ADD(&cob->co_dom->cd_addb, &cob_addb_loc,
			c2_addb_func_fail, "cob_update", rc);
	return rc;
}

void c2_cob_nskey_make(struct c2_cob_nskey **keyh, uint64_t hi, uint64_t lo,
			const char *name)
{
        struct c2_cob_nskey *key;

        key = c2_alloc(sizeof(*key) + strlen(name));
	if (key == NULL)
		return;

        key->cnk_pfid.si_bits.u_hi = hi;
        key->cnk_pfid.si_bits.u_lo = lo;
        memcpy(c2_bitstring_buf_get(&key->cnk_name), name, strlen(name));
        c2_bitstring_len_set(&key->cnk_name, strlen(name));
        *keyh = key;
}

void c2_cob_namespace_traverse(struct c2_cob_domain	*dom)
{
	struct c2_db_cursor	cursor;
	struct c2_db_pair	pair;
	struct c2_cob_nskey	*nskey;
	struct c2_cob_nsrec	nsrec;
	struct c2_db_tx		tx;
	int			rc;

	nskey = c2_alloc(sizeof (*nskey) + 20);

	c2_db_tx_init(&tx, dom->cd_dbenv, 0);
	rc = c2_db_cursor_init(&cursor, &dom->cd_namespace, &tx, 0);
	if (rc != 0) {
		printf("ns_traverse: error during cursor init %d\n", rc);
		return;
	}

	printf("=============== Namespace Table ================\n");
	c2_db_pair_setup(&pair, &dom->cd_namespace, nskey, sizeof (*nskey) + 20,
				&nsrec, sizeof nsrec);
	while ((rc = c2_db_cursor_next(&cursor, &pair)) == 0) {
#ifndef __KERNEL__
		printf("[%lx:%lx:%s] -> [%lx:%lx]\n", nskey->cnk_pfid.si_bits.u_hi,
				nskey->cnk_pfid.si_bits.u_lo,
				nskey->cnk_name.b_data,
				nsrec.cnr_stobid.si_bits.u_hi,
				nsrec.cnr_stobid.si_bits.u_lo);
#endif
	}

	printf("=================================================\n");
	c2_db_cursor_fini(&cursor);
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	c2_db_tx_commit(&tx);

}

void c2_cob_fb_traverse(struct c2_cob_domain	*dom)
{
	struct c2_db_cursor	cursor;
	struct c2_db_pair	pair;
	struct c2_stob_id	key;
	struct c2_cob_fabrec	rec;
	struct c2_db_tx		tx;
	int			rc;

	c2_db_tx_init(&tx, dom->cd_dbenv, 0);
	rc = c2_db_cursor_init(&cursor, &dom->cd_fileattr_basic, &tx, 0);
	if (rc != 0) {
		printf("fb_traverse: error during cursor init %d\n", rc);
		return;
	}

	printf("=============== FB Table ================\n");
	c2_db_pair_setup(&pair, &dom->cd_fileattr_basic, &key, sizeof key,
				&rec, sizeof rec);
	while ((rc = c2_db_cursor_next(&cursor, &pair)) == 0) {
#ifndef __KERNEL__
		printf("[%lx:%lx] -> [%lu:%lu]\n", key.si_bits.u_hi,
				key.si_bits.u_lo,
				rec.cfb_version.vn_lsn,
				rec.cfb_version.vn_vc);
#endif
	}

	printf("=================================================\n");
	c2_db_cursor_fini(&cursor);
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	c2_db_tx_commit(&tx);

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
