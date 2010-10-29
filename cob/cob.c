#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

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

/** Namespace table definition */
static int ns_cmp(struct c2_table *table, const void *key0, const void *key1)
{
	const struct c2_cob_nskey *cnk0 = key0;
	const struct c2_cob_nskey *cnk1 = key1;
        int rc;

        C2_PRE(c2_stob_id_is_set(&cnk0->cnk_pfid));
        C2_PRE(c2_stob_id_is_set(&cnk1->cnk_pfid));

        rc = c2_stob_id_cmp(&cnk0->cnk_pfid, &cnk1->cnk_pfid);
        if (rc == 0)
                return c2_bitstring_cmp(&cnk0->cnk_name, &cnk1->cnk_name);

	return rc;
}

static const struct c2_table_ops cob_ns_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = C2_COB_NSKEY_MAX
		},
		[TO_REC] = {
			.max_size = sizeof(struct c2_cob_nsrec)
		}
	},
	.key_cmp = ns_cmp
};

int c2_cob_nskey_size(struct c2_cob_nskey *cnk)
{
        return sizeof(*cnk) + c2_bitstring_len_get(&cnk->cnk_name);
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
        if (rc == 0)
                return C2_3WAY(cok0->cok_linkno, cok1->cok_linkno);
	return rc;
}

static const struct c2_table_ops cob_oi_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_cob_oikey)
		},
		[TO_REC] = {
			.max_size = C2_COB_NSKEY_MAX
		}
	},
	.key_cmp = oi_cmp
};

static const struct c2_table_ops cob_fab_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_stob_id)
		},
		[TO_REC] = {
                        .max_size = sizeof(struct c2_cob_fabrec)
		}
	},
	.key_cmp = oi_cmp
};

static char *cob_dom_id_make(char *buf, struct c2_cob_domain_id *id,
                             char *prefix)
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
        if (rc != 0)
                return rc;
        rc = c2_table_init(&dom->cd_fileattr_basic, dom->cd_dbenv,
                               cob_dom_id_make(table, &dom->cd_id, "fb"),
                               0, &cob_fab_ops);
	if (rc != 0)
		return rc;

        c2_addb_ctx_init(&dom->cd_addb, &c2_cob_domain_addb, &env->d_addb);

        return 0;
}

void c2_cob_domain_fini(struct c2_cob_domain *dom)
{
        c2_table_fini(&dom->cd_namespace);
        c2_table_fini(&dom->cd_object_index);
        c2_table_fini(&dom->cd_fileattr_basic);
	c2_rwlock_fini(&dom->cd_guard);
	c2_addb_ctx_fini(&dom->cd_addb);
}

static void cob_free_cb(struct c2_ref *ref);

static void cob_init(struct c2_cob_domain *dom, struct c2_cob *obj)
{
        obj->co_dom = dom;
	c2_ref_init(&obj->co_ref, 1, cob_free_cb);
	memset(&obj->co_id, 0, sizeof(obj->co_id));
	c2_rwlock_init(&obj->co_guard);
        obj->co_nsrec = NULL;
        obj->co_nskey = NULL;
        obj->co_fabrec = NULL;
	c2_addb_ctx_init(&obj->co_addb, &c2_cob_addb, &dom->cd_addb);
}

static void cob_fini(struct c2_cob *obj)
{
	c2_rwlock_fini(&obj->co_guard);
	c2_addb_ctx_fini(&obj->co_addb);
}

/**
   Return cob memory to the pool
 */
static int cob_free(struct c2_cob *cob)
{
        cob_fini(cob);

        if (cob->co_nskey != NULL)
                c2_free(cob->co_nskey);
        if (cob->co_nsrec != NULL)
                c2_free(cob->co_nsrec);
        if (cob->co_fabrec != NULL)
                c2_free(cob->co_fabrec);
        c2_free(cob);

        return 0;
}

static void cob_free_cb(struct c2_ref *ref)
{
	struct c2_cob *cob;

	cob = container_of(ref, struct c2_cob, co_ref);
	cob_free(cob);
}

void c2_cob_get(struct c2_cob *obj)
{
	c2_ref_get(&obj->co_ref);
}

void c2_cob_put(struct c2_cob *obj)
{
        c2_ref_put(&obj->co_ref);
}

/**
   Allocate a new cob

   Optimization: we should have a pool of cobs to reuse.
   We need a general memory pool handler: slab_init(size, count).
 */
static int cob_new(struct c2_cob_domain *dom, struct c2_cob **out)
{
        struct c2_cob *cob;

        C2_ALLOC_PTR(cob);
        if (cob == NULL) {
                C2_ADDB_ADD(&dom->cd_addb,
                            &cob_addb_loc, c2_addb_oom);
                return -ENOMEM;
        }

        cob_init(dom, cob);
        *out = cob;

        return 0;
}

/** Copy the ns key into a cob */
static int cob_nskey_cache(struct c2_cob *cob, struct c2_cob_nskey *nskey)
{
        if (cob->co_nskey != NULL)
                c2_free(cob->co_nskey);

        cob->co_nskey = c2_alloc(c2_cob_nskey_size(nskey)) ;
        if (cob->co_nskey == NULL) {
                C2_ADDB_ADD(&cob->co_addb,
                            &cob_addb_loc, c2_addb_oom);
                return -ENOMEM;
        }

        memcpy(cob->co_nskey, nskey, c2_cob_nskey_size(nskey));
        cob->co_valid |= CA_NSKEY;

        return 0;
}

/**
 Allocate a new namespace record

 Optimization: we should have a pool of nsrecs to reuse.
 */
static int cob_nsrec_new(struct c2_cob *cob)
{
        if (cob->co_nsrec != NULL)
                return 0;

        C2_ALLOC_PTR(cob->co_nsrec);
        if (cob->co_nsrec == NULL) {
                C2_ADDB_ADD(&cob->co_addb,
                            &cob_addb_loc, c2_addb_oom);
                return -ENOMEM;
        }

        return 0;
}

static int cob_ns_lookup(struct c2_cob *cob, struct c2_cob_nskey *key,
                         struct c2_db_tx *tx);

static int cob_oi_lookup(struct c2_cob *cob, struct c2_cob_oikey *key,
                         struct c2_db_tx *tx);

static int cob_fab_lookup(struct c2_cob *cob, struct c2_stob_id *key,
                          struct c2_db_tx *tx);

/** Search for a record in the namespace table
    @see cob_oi_lookup
 */
static int cob_ns_lookup(struct c2_cob *cob, struct c2_cob_nskey *nskey,
                         struct c2_db_tx *tx)
{
        struct c2_db_pair pair;
        int rc;

        /* allocate space if needed */
        rc = cob_nsrec_new(cob);
        if (rc)
                return rc;

        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 nskey, c2_cob_nskey_size(nskey),
			 cob->co_nsrec, sizeof *cob->co_nsrec);
        rc = c2_table_lookup(tx, &pair);

        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);

        if (rc == 0) {
                cob->co_id = cob->co_nsrec->cnr_stobid;
                cob->co_valid |= CA_NSREC;
        }

        /* And get the fabrec here too if needed  */
        if (cob->co_need & CA_FABREC)
                cob_fab_lookup(cob, &cob->co_id, tx);

        return rc;
}

/* TODO replace this with a per-thread session buffer */
char lookupbuf[C2_COB_NSKEY_MAX];
struct c2_buf threadbuf = {lookupbuf, C2_COB_NSKEY_MAX};

/**
 Search for a record in the object index table.
 Most likely we want stat data for a given fid, so let's do that as well.

 @see cob_ns_lookup
 */
static int cob_oi_lookup(struct c2_cob *cob, struct c2_cob_oikey *oikey,
                         struct c2_db_tx *tx)
{
        struct c2_db_pair pair;
        struct c2_buf *buf = &threadbuf;
        int rc;

        if (cob->co_valid & CA_NSKEY)
                /* Don't need to lookup anything if nskey is already here */
                return 0;

        /* Find the name from the object index table */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_object_index,
			 oikey, sizeof *oikey,
			 buf->b_addr, buf->b_nob);
        rc = c2_table_lookup(tx, &pair);
        c2_db_pair_release(&pair);
        c2_db_pair_fini(&pair);

        if (rc)
                return rc;

        cob->co_id = oikey->cok_stobid;

        /* Save the nskey if wanted */
        if (cob->co_need & CA_NSKEY)
                cob_nskey_cache(cob, (struct c2_cob_nskey *)buf->b_addr);

        /* Use the nsrec to lookup stat data if wanted,
           since we have the nsrec here. */
        if (cob->co_need & CA_NSREC)
                cob_ns_lookup(cob, (struct c2_cob_nskey *)buf->b_addr, tx);

        /* And get the fabrec here too if needed */
        if (cob->co_need & CA_FABREC)
                cob_fab_lookup(cob, &cob->co_id, tx);

        return 0;
}

/** Search for a record in the fileattr_basic table
  @see cob_ns_lookup
  @see cob_oi_lookup
 */
static int cob_fab_lookup(struct c2_cob *cob, struct c2_stob_id *key,
                          struct c2_db_tx *tx)
{
        struct c2_db_pair pair;
        struct c2_cob_fabrec *rec;
        int rc;

        if (cob->co_valid & CA_FABREC) {
                C2_PRE(cob->co_fabrec != NULL);
                return 0;
        }

        /* lookup in fileattr_basic table */
        rec = cob->co_fabrec;
        /* allocate space if needed */
        if (rec == NULL) {
                C2_ALLOC_PTR(rec);
                if (rec == NULL) {
                        C2_ADDB_ADD(&cob->co_addb,
                                    &cob_addb_loc, c2_addb_oom);
                        return -ENOMEM;
                }
                cob->co_fabrec = rec;
        }

        c2_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
			 key, sizeof *key,
			 rec, sizeof *rec);
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
        /* TODO: implement a cache for cobs and check if this nskey is in the
         cache */
        return -ENOENT;
}

/**
   Lookup a filename in the namespace table

   Check if cached first; otherwise create a new cob and populate it with
   the contents of the namespace record; i.e. the stat data and fid.

   The stat data and the namespace key (filename) may be cached.

   This lookup adds a reference to the cob.
 */
int c2_cob_lookup(struct c2_cob_domain *dom, struct c2_cob_nskey *nskey,
                  struct c2_cob **out)
{
        struct c2_db_tx tx;
        int rc;

        rc = cob_cache_nscheck(dom, nskey, out);
        if (rc == 0) /* cached, took ref above */
                return 0;

        /* Get cob memory */
        rc = cob_new(dom, out);
        if (rc)
                return rc;

        rc = c2_db_tx_init(&tx, dom->cd_dbenv, 0);
	if (rc)
                goto out_free;
        rc = cob_ns_lookup(*out, nskey, &tx);
        c2_db_tx_commit(&tx);

out_free:
        if (rc)
                c2_cob_put(*out);
        else
                /* Save the nskey if wanted */
                cob_nskey_cache(*out, nskey);

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
        /* TODO: implement a cache for cobs and check if this oi is in the
           cache */
        return -ENOENT;
}

/**
   Locate by stob id

   Check if cached first; otherwise create a new cob and populate it with
   the contents of the oi record; i.e. the filename.

   We may also lookup file attributes in cob_oi_lookup.

   This lookup adds a reference to the cob.
 */
int c2_cob_locate(struct c2_cob_domain *dom, struct c2_stob_id *id,
                  struct c2_cob **out)
{
        struct c2_cob_oikey oikey;
        struct c2_db_tx   tx;
        int rc;

        C2_PRE(c2_stob_id_is_set(id));
        memcpy(&oikey.cok_stobid, id, sizeof(*id));
        oikey.cok_linkno = 0;

        rc = cob_cache_oicheck(dom, id, out);
        if (rc == 0) /* cached, took ref */
                return 0;

        /* Get cob memory */
        rc = cob_new(dom, out);
        if (rc)
                return rc;

        /* Let's assume we want to cache the nskey and lookup the nsrec as
           well */
        (*out)->co_need |= CA_NSKEY | CA_NSREC;

        rc = c2_db_tx_init(&tx, dom->cd_dbenv, 0);
	if (rc)
                goto out_free;
        rc = cob_oi_lookup(*out, &oikey, &tx);
        c2_db_tx_commit(&tx);

out_free:
        if (rc)
                c2_cob_put(*out);

	return rc;
}

C2_ADDB_EV_DEFINE(cob_eexist, "md_exists", 0x1, C2_ADDB_INVAL);

/**
   Add a new cob to the namespace.

   This doesn't create a new stob; just creates metadata table entries
   for it.

   This takes a reference on the cob in-memory struct.
 */
int c2_cob_create(struct c2_cob_domain *dom,
                  struct c2_cob_nskey *nskey,
                  struct c2_cob_nsrec *nsrec,
                  struct c2_cob **out)
{
        struct c2_cob_oikey oikey;
        struct c2_db_tx     tx;
        struct c2_db_pair   pair;
	int rc;

	C2_PRE(c2_stob_id_is_set(&nsrec->cnr_stobid));
        C2_PRE(c2_stob_id_is_set(&nskey->cnk_pfid));
        C2_PRE(nsrec->cnr_nlink == 1);

        /* Fid in object index key is the child fid of nsrec */
        oikey.cok_stobid = nsrec->cnr_stobid;
        oikey.cok_linkno = 0;

        /* Get cob memory */
        rc = cob_new(dom, out);
        if (rc)
                return rc;

        /* Populate the cob */

        /* Cache the nskey */
        cob_nskey_cache(*out, nskey);

        /* Cache the nsrec */
        if (cob_nsrec_new(*out) == 0) {
                /* Failure here just means we can't cache, not failure to add
                   to the ns table */
                memcpy((*out)->co_nsrec, nsrec, sizeof *nsrec);
                (*out)->co_valid |= CA_NSREC;
        }

        rc = c2_db_tx_init(&tx, dom->cd_dbenv, 0);
	if (rc)
                goto out_free;

        /* Add to object index table.  Table insert should fail if
           already exists. */
        c2_db_pair_setup(&pair, &dom->cd_object_index,
			 &oikey, sizeof oikey,
			 nskey, c2_cob_nskey_size(nskey));

        rc = c2_table_insert(&tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	if (rc)
                goto out_free;

        /* Add to namespace table */
        c2_db_pair_setup(&pair, &dom->cd_namespace,
			 nskey, c2_cob_nskey_size(nskey),
			 nsrec, sizeof *nsrec);

        rc = c2_table_insert(&tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	if (rc)
                goto out_free;

        rc = c2_db_tx_commit(&tx);

	return rc;

out_free:
        c2_cob_put(*out);
        C2_ADDB_ADD(&dom->cd_addb, &cob_addb_loc, cob_eexist, rc);
        return rc;
}

/** For assertions only */
static bool c2_cob_is_valid(struct c2_cob *cob)
{
        if (!c2_stob_id_is_set(&cob->co_id))
                return false;
        return true;
}

C2_ADDB_EV_DEFINE(cob_delete, "md_delete", 0x2, C2_ADDB_FLAG);

/**
   Delete the metadata for this cob.

   Caller must be holding a reference on this cob, which
   will be released here.
 */
int c2_cob_delete(struct c2_cob *cob)
{
        struct c2_cob_oikey oikey;
        struct c2_db_tx     tx;
        struct c2_db_pair   pair;
        int rc;

        C2_PRE(c2_cob_is_valid(cob));

        rc = c2_db_tx_init(&tx, cob->co_dom->cd_dbenv, 0);
	if (rc)
                goto out;

        /* We need the name key */
        cob->co_need |= CA_NSKEY;
        oikey.cok_stobid = cob->co_id;
        oikey.cok_linkno = 0;
        rc = cob_oi_lookup(cob, &oikey, &tx);
        if (rc)
                goto out;
        C2_POST(cob->co_valid & CA_NSKEY);

        /* Remove from the object index table */
        /* TODO loop over all hardlinks */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_object_index,
			 &oikey, sizeof oikey,
			 NULL, 0);
        rc = c2_table_delete(&tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
        if (rc)
                goto out;

        /* Remove from the namespace table */
        c2_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 cob->co_nskey, c2_cob_nskey_size(cob->co_nskey),
			 NULL, 0);
        rc = c2_table_delete(&tx, &pair);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
        if (rc)
                goto out;

        rc = c2_db_tx_commit(&tx);
out:
        /* If the tx failed, assume we're not going to do anything else about
           it */
        C2_ADDB_ADD(&cob->co_dom->cd_addb, &cob_addb_loc, cob_delete, rc == 0);
        c2_cob_put(cob);
        return rc;
}

/** TODO c2_cob_update(version, stats) */



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
