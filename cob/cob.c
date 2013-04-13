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
 *
 * Mdstore changes: Yuriy Umanets <yuriy_umanets@xyratex.com>
 */

/*
 * Define the ADDB types in this file.
 */
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "cob/cob_addb.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_COB
#include "lib/trace.h"

#define M0_COB_KEY_LOG(logger, fmt, key, fid_member, str_member, ...)      \
	M0_ ## logger (fmt, (long)(key)->fid_member.f_container,           \
		       (long)(key)->fid_member.f_key,                      \
		       m0_bitstring_len_get(&((key)->str_member)),         \
		       (char *)m0_bitstring_buf_get(&((key)->str_member)), \
		       ## __VA_ARGS__)
#define M0_COB_NSKEY_LOG(logger, fmt, key, ...)\
	M0_COB_KEY_LOG(logger, fmt, key, cnk_pfid, cnk_name, ## __VA_ARGS__)

#include "lib/misc.h"   /* M0_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* M0_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/bitstring.h"

#include "cob/cob.h"

/**
   @addtogroup cob
   @{
*/

enum {
	M0_COB_NAME_MAX = 256,
	M0_COB_EA_MAX   = 4096
};

/**
   Storage virtual root. All cobs are placed in it.
 */
const struct m0_fid M0_COB_ROOT_FID = {
	.f_container = 1ULL,
	.f_key       = 1ULL
};

/**
   Root session fid. All sessions are placed in it.
*/
const struct m0_fid M0_COB_SESSIONS_FID = {
	.f_container = 1ULL,
	.f_key       = 2ULL
};

/**
   Metadata hierarchry root fid.
*/
const struct m0_fid M0_COB_SLASH_FID = {
	.f_container = 1ULL,
	.f_key       = 3ULL
};

const char M0_COB_ROOT_NAME[] = "ROOT";
const char M0_COB_SESSIONS_NAME[] = "SESSIONS";

struct m0_addb_ctx m0_cob_mod_ctx;

#define COB_FUNC_FAIL(loc, rc)						\
do {									\
	if (rc < 0)							\
		M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_COB_ADDB_LOC_##loc,	\
				  rc, &m0_cob_mod_ctx);			\
} while (0)

M0_INTERNAL int m0_cob_mod_init(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_cob_mod);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_cob_mod_ctx,
			 &m0_addb_ct_cob_mod, &m0_addb_proc_ctx);
	return 0;
}

M0_INTERNAL void m0_cob_mod_fini(void)
{
	m0_addb_ctx_fini(&m0_cob_mod_ctx);
}

M0_INTERNAL void m0_cob_oikey_make(struct m0_cob_oikey *oikey,
				   const struct m0_fid *fid, int linkno)
{
	oikey->cok_fid = *fid;
	oikey->cok_linkno = linkno;
}

M0_INTERNAL int m0_cob_nskey_make(struct m0_cob_nskey **keyh,
				  const struct m0_fid *pfid,
				  const char *name, size_t namelen)
{
	struct m0_cob_nskey *key;

	key = m0_alloc(sizeof *key + namelen);
	if (key == NULL)
		return -ENOMEM;
	key->cnk_pfid = *pfid;
	m0_bitstring_copy(&key->cnk_name, name, namelen);
	*keyh = key;
	return 0;
}

M0_INTERNAL int m0_cob_nskey_cmp(const struct m0_cob_nskey *k0,
				 const struct m0_cob_nskey *k1)
{
	int rc;

	M0_PRE(m0_fid_is_set(&k0->cnk_pfid));
	M0_PRE(m0_fid_is_set(&k1->cnk_pfid));

	rc = m0_fid_cmp(&k0->cnk_pfid, &k1->cnk_pfid);
	return rc ?: m0_bitstring_cmp(&k0->cnk_name, &k1->cnk_name);
}

M0_INTERNAL size_t m0_cob_nskey_size(const struct m0_cob_nskey *cnk)
{
	return sizeof *cnk +
		m0_bitstring_len_get(&cnk->cnk_name);
}

M0_INTERNAL int m0_cob_eakey_make(struct m0_cob_eakey **keyh,
				  const struct m0_fid *fid,
				  const char *name, size_t namelen)
{
	struct m0_cob_eakey *key;

	key = m0_alloc(sizeof *key + namelen);
	if (key == NULL)
		return -ENOMEM;
	key->cek_fid = *fid;
	m0_bitstring_copy(&key->cek_name, name, namelen);
	*keyh = key;
	return 0;
}

/**
   Make eakey for iterator. Allocate space for max possible name
   but put real string len into the struct.
*/
static int m0_cob_max_eakey_make(struct m0_cob_eakey **keyh,
				 const struct m0_fid *fid,
				 const char *name,
				 int namelen)
{
	struct m0_cob_eakey *key;

	key = m0_alloc(sizeof *key + M0_COB_NAME_MAX);
	if (key == NULL)
		return -ENOMEM;
	key->cek_fid = *fid;
	m0_bitstring_copy(&key->cek_name, name, namelen);
	*keyh = key;
	return 0;
}

M0_INTERNAL int m0_cob_eakey_cmp(const struct m0_cob_eakey *k0,
				 const struct m0_cob_eakey *k1)
{
	int rc;

	M0_PRE(m0_fid_is_set(&k0->cek_fid));
	M0_PRE(m0_fid_is_set(&k1->cek_fid));

	rc = m0_fid_cmp(&k0->cek_fid, &k1->cek_fid);
	return rc ?: m0_bitstring_cmp(&k0->cek_name, &k1->cek_name);
}

M0_INTERNAL size_t m0_cob_eakey_size(const struct m0_cob_eakey *cek)
{
	return sizeof *cek +
		m0_bitstring_len_get(&cek->cek_name);
}

static size_t m0_cob_earec_size(const struct m0_cob_earec *rec)
{
	return sizeof *rec + rec->cer_size;
}

/**
   Maximal possible earec size.
 */
M0_INTERNAL size_t m0_cob_max_earec_size(void)
{
	return sizeof(struct m0_cob_earec) + M0_COB_EA_MAX;
}

/**
   Maximal possible eakey size.
 */
static size_t m0_cob_max_eakey_size(const struct m0_cob_eakey *cek)
{
	return sizeof *cek + M0_COB_NAME_MAX;
}

/**
   Fabrec size taking into account symlink length.
 */
static size_t m0_cob_fabrec_size(const struct m0_cob_fabrec *rec)
{
	return sizeof *rec + rec->cfb_linklen;
}

M0_INTERNAL int m0_cob_fabrec_make(struct m0_cob_fabrec **rech,
				   const char *link, size_t linklen)
{
	struct m0_cob_fabrec *rec;

	rec = m0_alloc(sizeof(struct m0_cob_fabrec) + linklen);
	if (rec == NULL)
		return -ENOMEM;
	rec->cfb_linklen = linklen;
	if (linklen > 0)
		memcpy(rec->cfb_link, link, linklen);
	*rech = rec;
	return 0;
}

/**
   Maximal possible fabrec size.
 */
static size_t m0_cob_max_fabrec_size(void)
{
	return sizeof(struct m0_cob_fabrec) + M0_COB_NAME_MAX;
}

/**
   Allocate memory for maximal possible size of fabrec.
 */
static int m0_cob_max_fabrec_make(struct m0_cob_fabrec **rech)
{
	struct m0_cob_fabrec *rec;

	rec = m0_alloc(m0_cob_max_fabrec_size());
	if (rec == NULL)
		return -ENOMEM;
	rec->cfb_linklen = M0_COB_NAME_MAX;
	*rech = rec;
	return 0;
}

/**
   Make nskey for iterator. Allocate space for max possible name
   but put real string len into the struct.
*/
static int m0_cob_max_nskey_make(struct m0_cob_nskey **keyh,
				 const struct m0_fid *pfid,
				 const char *name,
				 int namelen)
{
	struct m0_cob_nskey *key;

	key = m0_alloc(sizeof *key + M0_COB_NAME_MAX);
	if (key == NULL)
		return -ENOMEM;
	key->cnk_pfid = *pfid;
	m0_bitstring_copy(&key->cnk_name, name, namelen);
	*keyh = key;
	return 0;
}

/**
   Key size for iterator in which case we don't know exact length of key
   and want to allocate it for worst case scenario, that is, for max
   possible name len.
 */
static size_t m0_cob_max_nskey_size(const struct m0_cob_nskey *cnk)
{
	return sizeof *cnk + M0_COB_NAME_MAX;
}

/**
   Namespace table definition.
*/
static int ns_cmp(struct m0_table *table, const void *key0, const void *key1)
{
	return m0_cob_nskey_cmp((const struct m0_cob_nskey *)key0,
				(const struct m0_cob_nskey *)key1);
}

static const struct m0_table_ops cob_ns_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = ~0
		},
		[TO_REC] = {
			.max_size = sizeof(struct m0_cob_nsrec)
		}
	},
	.key_cmp = ns_cmp
};

/**
   Object index table definition.
*/
static int oi_cmp(struct m0_table *table, const void *key0, const void *key1)
{
	const struct m0_cob_oikey *cok0 = key0;
	const struct m0_cob_oikey *cok1 = key1;
	int                        rc;

	M0_PRE(m0_fid_is_set(&cok0->cok_fid));
	M0_PRE(m0_fid_is_set(&cok1->cok_fid));

	rc = m0_fid_cmp(&cok0->cok_fid, &cok1->cok_fid);
	return rc ?: M0_3WAY(cok0->cok_linkno, cok1->cok_linkno);
}

static const struct m0_table_ops cob_oi_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct m0_cob_oikey)
		},
		[TO_REC] = {
			.max_size = ~0
		}
	},
	.key_cmp = oi_cmp
};

/**
   File attributes table definition
 */
static int fb_cmp(struct m0_table *table, const void *key0, const void *key1)
{
	const struct m0_cob_fabkey *cok0 = key0;
	const struct m0_cob_fabkey *cok1 = key1;

	M0_PRE(m0_fid_is_set(&cok0->cfb_fid));
	M0_PRE(m0_fid_is_set(&cok1->cfb_fid));

	return m0_fid_cmp(&cok0->cfb_fid, &cok1->cfb_fid);
}

static const struct m0_table_ops cob_fab_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct m0_cob_fabkey)
		},
		[TO_REC] = {
			.max_size = ~0
		}
	},
	.key_cmp = fb_cmp
};

/**
   Extended attributes table definition
 */
static int ea_cmp(struct m0_table *table, const void *key0, const void *key1)
{
	return m0_cob_eakey_cmp(key0, key1);
}

static const struct m0_table_ops cob_ea_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = ~0
		},
		[TO_REC] = {
			.max_size = ~0
		}
	},
	.key_cmp = ea_cmp
};

/**
   Omg table definition.
*/
static int omg_cmp(struct m0_table *table, const void *key0, const void *key1)
{
	const struct m0_cob_omgkey *cok0 = key0;
	const struct m0_cob_omgkey *cok1 = key1;
	return M0_3WAY(cok0->cok_omgid, cok1->cok_omgid);
}

static const struct m0_table_ops cob_omg_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct m0_cob_omgkey)
	},
		[TO_REC] = {
			.max_size = sizeof(struct m0_cob_omgrec)
	}
	},
	.key_cmp = omg_cmp
};

static char *cob_dom_id_make(char *buf, const struct m0_cob_domain_id *id,
			     const char *prefix)
{
	sprintf(buf, "%s%lu", prefix ? prefix : "", (unsigned long)id->id);
	return buf;
}

/**
   Set up a new cob domain

   Tables are identified by the domain id, which must be set before calling
   this function.
  */
int m0_cob_domain_init(struct m0_cob_domain *dom, struct m0_dbenv *env,
		       const struct m0_cob_domain_id *id)
{
	char table[16];
	int rc;

	dom->cd_id = *id;
	M0_PRE(dom->cd_id.id != 0);

	dom->cd_dbenv = env;

	/* Locate table based on domain id */
	rc = m0_table_init(&dom->cd_namespace, dom->cd_dbenv,
			   cob_dom_id_make(table, &dom->cd_id, "ns"),
			   0, &cob_ns_ops);
	if (rc != 0)
		return rc;
	rc = m0_table_init(&dom->cd_object_index, dom->cd_dbenv,
			   cob_dom_id_make(table, &dom->cd_id, "oi"),
			   0, &cob_oi_ops);
	if (rc != 0) {
		m0_table_fini(&dom->cd_namespace);
		return rc;
	}
	rc = m0_table_init(&dom->cd_fileattr_basic, dom->cd_dbenv,
			   cob_dom_id_make(table, &dom->cd_id, "fb"),
			   0, &cob_fab_ops);
	if (rc != 0) {
		m0_table_fini(&dom->cd_object_index);
		m0_table_fini(&dom->cd_namespace);
		return rc;
	}

	rc = m0_table_init(&dom->cd_fileattr_omg, dom->cd_dbenv,
			   cob_dom_id_make(table, &dom->cd_id, "fo"),
			   0, &cob_omg_ops);
	if (rc != 0) {
		m0_table_fini(&dom->cd_fileattr_basic);
		m0_table_fini(&dom->cd_object_index);
		m0_table_fini(&dom->cd_namespace);
		return rc;
	}

	rc = m0_table_init(&dom->cd_fileattr_ea, dom->cd_dbenv,
			   cob_dom_id_make(table, &dom->cd_id, "ea"),
			   0, &cob_ea_ops);
	if (rc != 0) {
		m0_table_fini(&dom->cd_fileattr_basic);
		m0_table_fini(&dom->cd_object_index);
		m0_table_fini(&dom->cd_namespace);
		m0_table_fini(&dom->cd_fileattr_omg);
		return rc;
	}

	return 0;
}

void m0_cob_domain_fini(struct m0_cob_domain *dom)
{
	m0_table_fini(&dom->cd_fileattr_ea);
	m0_table_fini(&dom->cd_fileattr_omg);
	m0_table_fini(&dom->cd_fileattr_basic);
	m0_table_fini(&dom->cd_object_index);
	m0_table_fini(&dom->cd_namespace);
}
M0_EXPORTED(m0_cob_domain_fini);

#ifndef __KERNEL__
#include <sys/stat.h>    /* S_ISDIR */

#define MKFS_ROOT_SIZE          4096
#define MKFS_ROOT_BLKSIZE       4096
#define MKFS_ROOT_BLOCKS        16

/**
 * Create initial files system structures, such as: entire storage root, root
 * cob for sessions and root cob for hierarchy. Latter is only one of them
 * visible to user on client.
 */
M0_INTERNAL int m0_cob_domain_mkfs(struct m0_cob_domain *dom,
				   const struct m0_fid *rootfid,
				   const struct m0_fid *sessfid,
				   struct m0_db_tx *tx)
{
	struct m0_cob_nskey  *nskey;
	struct m0_cob_nsrec   nsrec;
	struct m0_cob_omgkey  omgkey;
	struct m0_cob_omgrec  omgrec;
	struct m0_cob_fabrec *fabrec;
	struct m0_db_pair     pair;
	struct m0_cob        *cob;
	time_t                now;
	int                   rc;

	/**
	   Create terminator omgid record with id == ~0ULL.
	 */
	omgkey.cok_omgid = ~0ULL;

	M0_SET0(&omgrec);

	m0_db_pair_setup(&pair, &dom->cd_fileattr_omg,
			 &omgkey, sizeof omgkey, &omgrec, sizeof omgrec);

	rc = m0_table_insert(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	if (rc != 0)
		return rc;

	/**
	   Create root cob where all namespace is stored.
	 */
	M0_SET0(&nsrec);

	rc = m0_cob_alloc(dom, &cob);
	if (rc != 0)
		return rc;

	rc = m0_cob_nskey_make(&nskey, &M0_COB_ROOT_FID, M0_COB_ROOT_NAME,
			       strlen(M0_COB_ROOT_NAME));
	if (rc != 0) {
	    m0_cob_put(cob);
	    return rc;
	}

	nsrec.cnr_omgid = 0;
	nsrec.cnr_fid = *rootfid;

	nsrec.cnr_nlink = 2;
	nsrec.cnr_size = MKFS_ROOT_SIZE;
	nsrec.cnr_blksize = MKFS_ROOT_BLKSIZE;
	nsrec.cnr_blocks = MKFS_ROOT_BLOCKS;
	if (time(&now) < 0) {
		m0_cob_put(cob);
		m0_free(nskey);
		return errno;
	}
	nsrec.cnr_atime = nsrec.cnr_mtime = nsrec.cnr_ctime = now;

	omgrec.cor_uid = 0;
	omgrec.cor_gid = 0;
	omgrec.cor_mode = S_IFDIR |
			  S_IRUSR | S_IWUSR | S_IXUSR | /* rwx for owner */
			  S_IRGRP | S_IXGRP |           /* r-x for group */
			  S_IROTH | S_IXOTH;            /* r-x for others */

	rc = m0_cob_fabrec_make(&fabrec, NULL, 0);
	if (rc != 0) {
		m0_cob_put(cob);
		m0_free(nskey);
		return rc;
	}

	rc = m0_cob_create(cob, nskey, &nsrec, fabrec, &omgrec, tx);
	m0_cob_put(cob);
	if (rc != 0) {
		m0_free(nskey);
		m0_free(fabrec);
		return rc;
	}

	/**
	   Create root session.
	 */
	M0_SET0(&nsrec);

	rc = m0_cob_alloc(dom, &cob);
	if (rc != 0)
		return rc;

	rc = m0_cob_nskey_make(&nskey, &M0_COB_ROOT_FID, M0_COB_SESSIONS_NAME,
			       strlen(M0_COB_SESSIONS_NAME));
	if (rc != 0) {
		m0_cob_put(cob);
		return rc;
	}

	nsrec.cnr_omgid = 0;
	nsrec.cnr_fid = *sessfid;

	nsrec.cnr_nlink = 1;
	nsrec.cnr_size = MKFS_ROOT_SIZE;
	nsrec.cnr_blksize = MKFS_ROOT_BLKSIZE;
	nsrec.cnr_blocks = MKFS_ROOT_BLOCKS;
	nsrec.cnr_atime = nsrec.cnr_mtime = nsrec.cnr_ctime = now;

	omgrec.cor_uid = 0;
	omgrec.cor_gid = 0;
	omgrec.cor_mode = S_IFDIR |
			  S_IRUSR | S_IWUSR | S_IXUSR | /* rwx for owner */
			  S_IRGRP | S_IXGRP |           /* r-x for group */
			  S_IROTH | S_IXOTH;            /* r-x for others */

	rc = m0_cob_fabrec_make(&fabrec, NULL, 0);
	if (rc != 0) {
		m0_cob_put(cob);
		m0_free(nskey);
	}

	fabrec->cfb_version.vn_lsn = M0_LSN_RESERVED_NR + 2;
	fabrec->cfb_version.vn_vc = 0;

	rc = m0_cob_create(cob, nskey, &nsrec, fabrec, &omgrec, tx);
	m0_cob_put(cob);
	if (rc != 0) {
		m0_free(nskey);
		m0_free(fabrec);
		return rc;
	}
	return 0;
}
#endif

static void cob_free_cb(struct m0_ref *ref);

static void cob_init(struct m0_cob_domain *dom, struct m0_cob *cob)
{
	m0_ref_init(&cob->co_ref, 1, cob_free_cb);
	cob->co_fid = &cob->co_nsrec.cnr_fid;
	cob->co_nskey = NULL;
	cob->co_dom = dom;
	cob->co_flags = 0;
}

static void cob_fini(struct m0_cob *cob)
{
	if (cob->co_flags & M0_CA_NSKEY_FREE)
		m0_free(cob->co_nskey);
	if (cob->co_flags & M0_CA_FABREC)
		m0_free(cob->co_fabrec);
	m0_addb_ctx_fini(&cob->co_addb);
}

/**
   Return cob memory to the pool
 */
static void cob_free_cb(struct m0_ref *ref)
{
	struct m0_cob *cob;

	cob = container_of(ref, struct m0_cob, co_ref);
	cob_fini(cob);
	m0_free(cob);
}

M0_INTERNAL void m0_cob_get(struct m0_cob *cob)
{
	m0_ref_get(&cob->co_ref);
}

M0_INTERNAL void m0_cob_put(struct m0_cob *cob)
{
	m0_ref_put(&cob->co_ref);
}

M0_INTERNAL int m0_cob_alloc(struct m0_cob_domain *dom, struct m0_cob **out)
{
	struct m0_cob *cob;

	M0_ALLOC_PTR_ADDB(cob, &m0_addb_gmc, M0_COB_ADDB_LOC_ALLOC,
			  &m0_cob_mod_ctx);
	if (cob == NULL)
		return -ENOMEM;

	cob_init(dom, cob);
	*out = cob;

	return 0;
}

static int cob_ns_lookup(struct m0_cob *cob, struct m0_db_tx *tx);
static int cob_oi_lookup(struct m0_cob *cob, struct m0_db_tx *tx);
static int cob_fab_lookup(struct m0_cob *cob, struct m0_db_tx *tx);

/**
   Search for a record in the namespace table

   If the lookup fails, we return error and co_flags accurately reflects
   the missing fields.

   @see cob_oi_lookup
 */
static int cob_ns_lookup(struct m0_cob *cob, struct m0_db_tx *tx)
{
	struct m0_db_pair     pair;
	int                   rc;

	M0_PRE(cob->co_nskey != NULL &&
	       m0_fid_is_set(&cob->co_nskey->cnk_pfid));
	m0_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 cob->co_nskey, m0_cob_nskey_size(cob->co_nskey),
			 &cob->co_nsrec, sizeof cob->co_nsrec);
	rc = m0_table_lookup(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);

	if (rc == 0) {
		cob->co_flags |= M0_CA_NSREC;
		M0_ASSERT(cob->co_nsrec.cnr_linkno > 0 ||
			  cob->co_nsrec.cnr_nlink > 0);
		M0_POST(m0_fid_is_set(cob->co_fid));
	}
	return rc;
}

/**
   Search for a record in the object index table.
   Most likely we want stat data for a given fid, so let's do that as well.

   @see cob_ns_lookup
 */
static int cob_oi_lookup(struct m0_cob *cob, struct m0_db_tx *tx)
{
	struct m0_db_cursor  cursor;
	struct m0_cob_oikey  oldkey;
	struct m0_cob_nskey *nskey;
	int                  rc;

	if (cob->co_flags & M0_CA_NSKEY)
		return 0;

	if (cob->co_flags & M0_CA_NSKEY_FREE) {
		m0_free(cob->co_nskey);
		cob->co_flags &= ~M0_CA_NSKEY_FREE;
	}

	oldkey = cob->co_oikey;

	/*
	 * Find the name from the object index table. Note the key buffer
	 * is out of scope outside of this function, but the record is good
	 * until m0_db_pair_fini.
	 */
	m0_db_pair_setup(&cob->co_oipair, &cob->co_dom->cd_object_index,
			 &cob->co_oikey, sizeof cob->co_oikey, NULL, 0);

	/*
	 * We use cursor here because in some situations we need
	 * to find most suitable position instead of exact location.
	 */
	rc = m0_db_cursor_init(&cursor,
			       &cob->co_dom->cd_object_index, tx, 0);
	if (rc != 0) {
		M0_LOG(M0_DEBUG, "m0_db_cursor_init() failed with %d", rc);
		m0_db_pair_fini(&cob->co_oipair);
		return rc;
	}

	rc = m0_db_cursor_get(&cursor, &cob->co_oipair);
	if (rc != 0) {
		M0_LOG(M0_DEBUG, "m0_db_cursor_get() failed with %d", rc);
		goto out;
	}

	/*
	 * Found position should have same fid.
	 */
	if (!m0_fid_eq(&oldkey.cok_fid, &cob->co_oikey.cok_fid)) {
		rc = -ENOENT;
		goto out;
	}

	nskey = (struct m0_cob_nskey *)cob->co_oipair.dp_rec.db_buf.b_addr;
	rc = m0_cob_nskey_make(&cob->co_nskey, &nskey->cnk_pfid,
			       m0_bitstring_buf_get(&nskey->cnk_name),
			       m0_bitstring_len_get(&nskey->cnk_name));
	cob->co_flags |= (M0_CA_NSKEY | M0_CA_NSKEY_FREE);
out:
	m0_db_pair_fini(&cob->co_oipair);
	m0_db_cursor_fini(&cursor);
	return rc;
}

/**
   Search for a record in the fileattr_basic table.

   @see cob_ns_lookup
   @see cob_oi_lookup
 */
static int cob_fab_lookup(struct m0_cob *cob, struct m0_db_tx *tx)
{
	struct m0_cob_fabkey fabkey;
	struct m0_db_pair    pair;
	int                  rc;

	if (cob->co_flags & M0_CA_FABREC)
		return 0;

	fabkey.cfb_fid = *cob->co_fid;
	rc = m0_cob_max_fabrec_make(&cob->co_fabrec);
	if (rc != 0)
		return rc;
	m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
			 &fabkey, sizeof fabkey, cob->co_fabrec,
			 m0_cob_max_fabrec_size());
	rc = m0_table_lookup(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);

	if (rc == 0)
		cob->co_flags |= M0_CA_FABREC;
	else
		cob->co_flags &= ~M0_CA_FABREC;

	return rc;
}

/**
   Search for a record in the fileattr_omg table.
   @see cob_fab_lookup
 */
static int cob_omg_lookup(struct m0_cob *cob, struct m0_db_tx *tx)
{
	struct m0_cob_omgkey omgkey;
	struct m0_db_pair    pair;
	int                  rc;

	if (cob->co_flags & M0_CA_OMGREC)
		return 0;

	omgkey.cok_omgid = cob->co_nsrec.cnr_omgid;
	m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_omg,
			 &omgkey, sizeof omgkey,
			 &cob->co_omgrec, sizeof cob->co_omgrec);
	rc = m0_table_lookup(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);

	if (rc == 0)
		cob->co_flags |= M0_CA_OMGREC;
	else
		cob->co_flags &= ~M0_CA_OMGREC;

	return rc;
}

/**
   Load fab and omg records according with @need flags.
 */
static int cob_get_fabomg(struct m0_cob *cob, uint64_t flags,
			  struct m0_db_tx *tx)
{
	int rc = 0;

	if (flags & M0_CA_FABREC) {
		rc = cob_fab_lookup(cob, tx);
		if (rc != 0)
			return rc;
	}

	/*
	 * Get omg attributes as well if we need it.
	 */
	if (flags & M0_CA_OMGREC) {
		rc = cob_omg_lookup(cob, tx);
		if (rc != 0)
			return rc;
	}
	return rc;
}

M0_INTERNAL int m0_cob_lookup(struct m0_cob_domain *dom,
			      struct m0_cob_nskey *nskey, uint64_t flags,
			      struct m0_cob **out, struct m0_db_tx *tx)
{
	struct m0_cob *cob;
	int            rc;

	M0_ASSERT(out != NULL);
	*out = NULL;

	rc = m0_cob_alloc(dom, &cob);
	if (rc != 0)
		return rc;

	cob->co_nskey = nskey;
	cob->co_flags |= M0_CA_NSKEY;

	if (flags & M0_CA_NSKEY_FREE)
		cob->co_flags |= M0_CA_NSKEY_FREE;

	rc = cob_ns_lookup(cob, tx);
	if (rc != 0) {
		m0_cob_put(cob);
		return rc;
	}

	rc = cob_get_fabomg(cob, flags, tx);
	if (rc != 0) {
		m0_cob_put(cob);
		return rc;
	}

	*out = cob;
	return rc;
}

M0_INTERNAL int m0_cob_locate(struct m0_cob_domain *dom,
			      struct m0_cob_oikey *oikey, uint64_t flags,
			      struct m0_cob **out, struct m0_db_tx *tx)
{
	struct m0_cob *cob;
	int rc;

	M0_PRE(m0_fid_is_set(&oikey->cok_fid));

	/*
	 * Zero out "out" just in case that if we fail here, it is
	 * easier to find abnormal using of NULL cob.
	 */
	M0_ASSERT(out != NULL);
	*out = NULL;

	/* Get cob memory. */
	rc = m0_cob_alloc(dom, &cob);
	if (rc != 0)
		return rc;

	cob->co_oikey = *oikey;
	rc = cob_oi_lookup(cob, tx);
	if (rc != 0) {
		M0_LOG(M0_DEBUG, "cob_oi_lookup() failed with %d", rc);
		m0_cob_put(cob);
		return rc;
	}

	rc = cob_ns_lookup(cob, tx);
	if (rc != 0) {
		M0_LOG(M0_DEBUG, "cob_ns_lookup() failed with %d", rc);
		m0_cob_put(cob);
		return rc;
	}

	rc = cob_get_fabomg(cob, flags, tx);
	if (rc != 0) {
		M0_LOG(M0_DEBUG, "cob_get_fabomg() failed with %d", rc);
		m0_cob_put(cob);
		return rc;
	}

	*out = cob;
	return rc;
}

M0_INTERNAL int m0_cob_iterator_init(struct m0_cob *cob,
				     struct m0_cob_iterator *it,
				     struct m0_bitstring *name,
				     struct m0_db_tx *tx)
{
	int rc;

	/*
	 * Prepare entry key using passed started pos.
	 */
	rc = m0_cob_max_nskey_make(&it->ci_key, cob->co_fid,
				   m0_bitstring_buf_get(name),
				   m0_bitstring_len_get(name));
	if (rc != 0)
		return rc;

	/*
	 * Init iterator cursor with max possible key size.
	 */
	m0_db_pair_setup(&it->ci_pair, &cob->co_dom->cd_namespace,
			 it->ci_key, m0_cob_max_nskey_size(it->ci_key),
			 &it->ci_rec, sizeof it->ci_rec);

	rc = m0_db_cursor_init(&it->ci_cursor,
			       &cob->co_dom->cd_namespace, tx, 0);
	if (rc != 0) {
		m0_db_pair_release(&it->ci_pair);
		m0_db_pair_fini(&it->ci_pair);
		m0_free(it->ci_key);
		return rc;
	}
	it->ci_cob = cob;
	return rc;
}

M0_INTERNAL int m0_cob_iterator_get(struct m0_cob_iterator *it)
{
	int rc;
	M0_COB_NSKEY_LOG(ENTRY, "[%lx:%lx]/%.*s", it->ci_key);
	rc = m0_db_cursor_get(&it->ci_cursor, &it->ci_pair);
	if (rc == 0 && !m0_fid_eq(&it->ci_key->cnk_pfid, it->ci_cob->co_fid))
		rc = -ENOENT;
	M0_COB_NSKEY_LOG(LEAVE, "[%lx:%lx]/%.*s rc: %d", it->ci_key, rc);
	return rc;
}

M0_INTERNAL int m0_cob_iterator_next(struct m0_cob_iterator *it)
{
	int rc;
	M0_COB_NSKEY_LOG(ENTRY, "[%lx:%lx]/%.*s", it->ci_key);
	rc = m0_db_cursor_next(&it->ci_cursor, &it->ci_pair);
	if (rc == 0 && !m0_fid_eq(&it->ci_key->cnk_pfid, it->ci_cob->co_fid))
		rc = -ENOENT;
	M0_COB_NSKEY_LOG(LEAVE, "[%lx:%lx]/%.*s rc: %d", it->ci_key, rc);
	return rc;
}

M0_INTERNAL void m0_cob_iterator_fini(struct m0_cob_iterator *it)
{
	m0_db_pair_release(&it->ci_pair);
	m0_db_pair_fini(&it->ci_pair);
	m0_db_cursor_fini(&it->ci_cursor);
	m0_free(it->ci_key);
}

/**
   For assertions only.
 */
static bool m0_cob_is_valid(struct m0_cob *cob)
{
	return m0_fid_is_set(cob->co_fid);
}

M0_INTERNAL int m0_cob_alloc_omgid(struct m0_cob_domain *dom,
				   struct m0_db_tx *tx, uint64_t *omgid)
{
	struct m0_db_pair     pair;
	struct m0_cob_omgkey  omgkey;
	struct m0_cob_omgrec  omgrec;
	struct m0_db_cursor   cursor;
	int                   rc;

	M0_ENTRY();

	rc = m0_db_cursor_init(&cursor, &dom->cd_fileattr_omg, tx, 0);
	if (rc != 0)
		M0_RETURN(rc);
	/*
	 * Look for ~0ULL terminator record and do a step back to find last
	 * allocated omgid. Terminator record should be prepared in storage
	 * init time (mkfs or else).
	 */
	omgkey.cok_omgid = ~0ULL;

	m0_db_pair_setup(&pair, &dom->cd_fileattr_omg, &omgkey, sizeof omgkey,
			 &omgrec, sizeof omgrec);
	rc = m0_db_cursor_get(&cursor, &pair);
	/*
	 * In case of error, most probably due to no terminator record found,
	 * one needs to run mkfs.
	 */
	if (rc == 0) {
		rc = m0_db_cursor_prev(&cursor, &pair);
		if (omgid != NULL) {
			if (rc == 0) {
				/* We found last allocated omgid.
				 * Bump it by one. */
				*omgid = ++omgkey.cok_omgid;
			} else {
				/* No last allocated found, this alloc call is
				 * the first one. */
				*omgid = 0;
			}
		}
		rc = 0;
	}
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	m0_db_cursor_fini(&cursor);
	M0_RETURN(rc);
}

M0_INTERNAL int m0_cob_create(struct m0_cob *cob,
			      struct m0_cob_nskey *nskey,
			      struct m0_cob_nsrec *nsrec,
			      struct m0_cob_fabrec *fabrec,
			      struct m0_cob_omgrec *omgrec,
			      struct m0_db_tx *tx)
{
	struct m0_db_pair     pair;
	struct m0_cob_omgkey  omgkey;
	struct m0_cob_fabkey  fabkey;
	int                   rc;

	M0_ENTRY();
	M0_PRE(cob != NULL);
	M0_PRE(nskey != NULL);
	M0_PRE(nsrec != NULL);
	M0_PRE(fabrec != NULL);
	M0_PRE(omgrec != NULL);
	M0_PRE(m0_fid_is_set(&nsrec->cnr_fid));
	M0_PRE(m0_fid_is_set(&nskey->cnk_pfid));

	rc = m0_cob_alloc_omgid(cob->co_dom, tx, &nsrec->cnr_omgid);
	if (rc != 0)
		goto out;

	cob->co_nskey = nskey;
	cob->co_flags |= M0_CA_NSKEY;

	/*
	 * This is what name_add will use to create new name.
	 */
	cob->co_nsrec = *nsrec;
	cob->co_flags |= M0_CA_NSREC;
	cob->co_nsrec.cnr_cntr = 0;

	/*
	 * Intialize counter with 1 which is what will be used
	 * for adding second name. We do it this way to avoid
	 * doing special m0_cob_update() solely for having
	 * this field stored in db.
	 */
	nsrec->cnr_cntr = 1;

	/*
	 * Let's create name, statdata and object index.
	 */
	rc = m0_cob_name_add(cob, nskey, nsrec, tx);
	if (rc != 0)
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
	m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
			 &fabkey, sizeof fabkey, cob->co_fabrec,
			 m0_cob_fabrec_size(cob->co_fabrec));

	rc = m0_table_insert(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	if (rc != 0)
		goto out;

	/*
	 * Prepare omg key.
	 */
	omgkey.cok_omgid = nsrec->cnr_omgid;

	/*
	 * Now let's update omg attributes. Cache the omgrec.
	 */
	cob->co_omgrec = *omgrec;
	cob->co_flags |= M0_CA_OMGREC;

	/*
	 * Add to fileattr-omg table.
	 */
	m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_omg,
			 &omgkey, sizeof omgkey,
			 &cob->co_omgrec, sizeof cob->co_omgrec);

	rc = m0_table_insert(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	if (rc == 0)
		cob->co_flags |= M0_CA_NSKEY_FREE | M0_CA_FABREC;
out:
	COB_FUNC_FAIL(CREATE, rc);
	M0_RETURN(rc);
}

M0_INTERNAL int m0_cob_delete(struct m0_cob *cob, struct m0_db_tx *tx)
{
	struct m0_cob_fabkey fabkey;
	struct m0_cob_omgkey omgkey;
	struct m0_cob_oikey  oikey;
	bool                 sdname;
	struct m0_db_pair    pair;
	struct m0_cob       *sdcob;
	int                  rc;

	M0_PRE(m0_cob_is_valid(cob));
	M0_PRE(cob->co_flags & M0_CA_NSKEY);

	m0_cob_oikey_make(&oikey, cob->co_fid, 0);
	rc = m0_cob_locate(cob->co_dom, &oikey, 0, &sdcob, tx);
	if (rc != 0)
		goto out;
	sdname = (m0_cob_nskey_cmp(cob->co_nskey, sdcob->co_nskey) == 0);
	m0_cob_put(sdcob);

	/*
	 * Delete last name from namespace and object index.
	 */
	rc = m0_cob_name_del(cob, cob->co_nskey, tx);
	if (rc != 0)
		goto out;

	/*
	 * Is this a statdata name?
	 */
	if (sdname) {
		/*
		 * Remove from the fileattr_basic table.
		 */
		fabkey.cfb_fid = *cob->co_fid;
		m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
				 &fabkey, sizeof fabkey, NULL, 0);

		/*
		 * Ignore errors; it's a dangling table entry but causes
		 * no harm.
		 */
		m0_table_delete(tx, &pair);
		m0_db_pair_fini(&pair);

		/*
		 * @todo: Omgrec may be shared between multiple objects.
		 * Delete should take this into account as well as update.
		 */
		omgkey.cok_omgid = cob->co_nsrec.cnr_omgid;

		/*
		 * Remove from the fileattr_omg table.
		 */
		m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_omg,
				 &omgkey, sizeof omgkey, NULL, 0);

		/*
		 * Ignore errors; it's a dangling table entry but causes
		 * no harm.
		 */
		m0_table_delete(tx, &pair);
		m0_db_pair_fini(&pair);
	}
out:
	COB_FUNC_FAIL(DELETE, rc);
	return rc;
}

M0_INTERNAL int m0_cob_delete_put(struct m0_cob *cob, struct m0_db_tx *tx)
{
	int rc = m0_cob_delete(cob, tx);
	m0_cob_put(cob);
	return rc;
}

M0_INTERNAL int m0_cob_update(struct m0_cob *cob,
			      struct m0_cob_nsrec *nsrec,
			      struct m0_cob_fabrec *fabrec,
			      struct m0_cob_omgrec *omgrec,
			      struct m0_db_tx *tx)
{
	struct m0_cob_omgkey  omgkey;
	struct m0_cob_fabkey  fabkey;
	struct m0_db_pair     pair;
	int                   rc;

	M0_PRE(m0_cob_is_valid(cob));
	M0_PRE(cob->co_flags & M0_CA_NSKEY);

	if (nsrec != NULL) {
		M0_ASSERT(nsrec->cnr_nlink > 0);

		cob->co_nsrec = *nsrec;
		cob->co_flags |= M0_CA_NSREC;

		m0_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
				 cob->co_nskey,
				 m0_cob_nskey_size(cob->co_nskey),
				 &cob->co_nsrec, sizeof cob->co_nsrec);
		rc = m0_table_update(tx, &pair);
		m0_db_pair_release(&pair);
		m0_db_pair_fini(&pair);
		if (rc != 0)
			goto out;
	}

	if (fabrec != NULL) {
		fabkey.cfb_fid = *cob->co_fid;
		if (fabrec != cob->co_fabrec) {
			if (cob->co_flags & M0_CA_FABREC)
				m0_free(cob->co_fabrec);
			cob->co_fabrec = fabrec;
		}
		cob->co_flags |= M0_CA_FABREC;

		m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_basic,
				 &fabkey, sizeof fabkey, cob->co_fabrec,
				 m0_cob_fabrec_size(cob->co_fabrec));
		rc = m0_table_update(tx, &pair);
		m0_db_pair_release(&pair);
		m0_db_pair_fini(&pair);
	}

	if (omgrec != NULL) {
		/*
		 * @todo: Omgrec may be shared between multiple objects.
		 * We need to take this into account.
		 */
		omgkey.cok_omgid = cob->co_nsrec.cnr_omgid;

		cob->co_omgrec = *omgrec;
		cob->co_flags |= M0_CA_OMGREC;

		m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_omg,
				 &omgkey, sizeof omgkey,
				 &cob->co_omgrec, sizeof cob->co_omgrec);
		rc = m0_table_update(tx, &pair);
		m0_db_pair_release(&pair);
		m0_db_pair_fini(&pair);
	}
out:
	COB_FUNC_FAIL(UPDATE, rc);
	return rc;
}

M0_INTERNAL int m0_cob_name_add(struct m0_cob *cob,
				struct m0_cob_nskey *nskey,
				struct m0_cob_nsrec *nsrec,
				struct m0_db_tx *tx)
{
	struct m0_cob_oikey  oikey;
	struct m0_db_pair    pair;
	int                  rc;

	M0_PRE(cob != NULL);
	M0_PRE(nskey != NULL);
	M0_PRE(m0_fid_is_set(&nskey->cnk_pfid));
	M0_PRE(m0_cob_is_valid(cob));

	/**
	 * Add new name to object index table. Table insert should fail
	 * if name already exists.
	 */
	m0_cob_oikey_make(&oikey, &nsrec->cnr_fid,
			  cob->co_nsrec.cnr_cntr);

	m0_db_pair_setup(&pair, &cob->co_dom->cd_object_index,
			 &oikey, sizeof oikey, nskey,
			 m0_cob_nskey_size(nskey));

	rc = m0_table_insert(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	if (rc != 0)
		goto out;

	m0_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 nskey, m0_cob_nskey_size(nskey),
			 nsrec, sizeof *nsrec);

	rc = m0_table_insert(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
out:
	COB_FUNC_FAIL(NAME_ADD, rc);
	return rc;
}

M0_INTERNAL int m0_cob_name_del(struct m0_cob *cob,
				struct m0_cob_nskey *nskey,
				struct m0_db_tx *tx)
{
	struct m0_cob_oikey oikey;
	struct m0_cob_nsrec nsrec;
	struct m0_db_pair   pair;
	int                 rc;

	M0_PRE(m0_cob_is_valid(cob));
	M0_PRE(cob->co_flags & M0_CA_NSKEY);

	/*
	 * Kill name from namespace.
	 */
	m0_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 nskey, m0_cob_nskey_size(nskey),
			 &nsrec, sizeof nsrec);
	rc = m0_table_lookup(tx, &pair);
	if (rc != 0) {
		m0_db_pair_fini(&pair);
		goto out;
	}

	rc = m0_table_delete(tx, &pair);
	m0_db_pair_fini(&pair);
	if (rc != 0)
		goto out;

	/*
	 * Let's also kill object index entry.
	 */
	m0_cob_oikey_make(&oikey, cob->co_fid, nsrec.cnr_linkno);
	m0_db_pair_setup(&pair, &cob->co_dom->cd_object_index,
			 &oikey, sizeof oikey, NULL, 0);
	rc = m0_table_delete(tx, &pair);
	m0_db_pair_fini(&pair);

out:
	COB_FUNC_FAIL(NAME_DEL, rc);
	return rc;
}

M0_INTERNAL int m0_cob_name_update(struct m0_cob *cob,
				   struct m0_cob_nskey *srckey,
				   struct m0_cob_nskey *tgtkey,
				   struct m0_db_tx *tx)
{
	struct m0_cob_nsrec  nsrec;
	struct m0_db_pair    pair;
	struct m0_cob_oikey  oikey;
	int                  rc;

	M0_PRE(m0_cob_is_valid(cob));
	M0_PRE(srckey != NULL && tgtkey != NULL);

	/*
	 * Insert new record with nsrec found with srckey.
	 */
	m0_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 srckey, m0_cob_nskey_size(srckey),
			 &nsrec, sizeof nsrec);
	rc = m0_table_lookup(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	if (rc != 0)
		goto out;

	m0_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 tgtkey, m0_cob_nskey_size(tgtkey),
			 &nsrec, sizeof nsrec);
	rc = m0_table_insert(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	if (rc != 0)
		goto out;

	/*
	 * Kill old record. Error will be returned if
	 * nothing found.
	 */
	m0_db_pair_setup(&pair, &cob->co_dom->cd_namespace,
			 srckey, m0_cob_nskey_size(srckey),
			 NULL, 0);
	rc = m0_table_delete(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	if (rc != 0)
		goto out;

	/* Update object index */
	m0_cob_oikey_make(&oikey, cob->co_fid, nsrec.cnr_linkno);
	m0_db_pair_setup(&pair, &cob->co_dom->cd_object_index,
			 &oikey, sizeof oikey, tgtkey,
			 m0_cob_nskey_size(tgtkey));
	rc = m0_table_update(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	if (rc != 0)
		goto out;

	/*
	 * Update key to new one.
	 */
	if (cob->co_flags & M0_CA_NSKEY_FREE)
		m0_free(cob->co_nskey);
	m0_cob_nskey_make(&cob->co_nskey, &tgtkey->cnk_pfid,
			  m0_bitstring_buf_get(&tgtkey->cnk_name),
			  m0_bitstring_len_get(&tgtkey->cnk_name));
	cob->co_flags |= M0_CA_NSKEY_FREE;
out:
	COB_FUNC_FAIL(NAME_UPDATE, rc);
	return rc;
}

M0_INTERNAL int m0_cob_ea_get(struct m0_cob *cob,
                              struct m0_cob_eakey *eakey,
                              struct m0_cob_earec *out,
                              struct m0_db_tx *tx)
{
	struct m0_db_pair     pair;
	int                   rc;

	m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_ea,
			 eakey, m0_cob_eakey_size(eakey),
			 out, m0_cob_max_earec_size());
	rc = m0_table_lookup(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	return rc;
}

M0_INTERNAL int m0_cob_ea_set(struct m0_cob *cob,
			      struct m0_cob_eakey *eakey,
			      struct m0_cob_earec *earec,
			      struct m0_db_tx *tx)
{
	struct m0_db_pair     pair;
	int                   rc;

	M0_PRE(cob != NULL);
	M0_PRE(eakey != NULL);
	M0_PRE(m0_fid_is_set(&eakey->cek_fid));
	M0_PRE(m0_cob_is_valid(cob));

	m0_cob_ea_del(cob, eakey, tx);

	m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_ea,
			 eakey, m0_cob_eakey_size(eakey),
			 earec, m0_cob_earec_size(earec));

	rc = m0_table_insert(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);

	COB_FUNC_FAIL(EA_ADD, rc);
	return rc;
}

M0_INTERNAL int m0_cob_ea_del(struct m0_cob *cob,
			      struct m0_cob_eakey *eakey,
			      struct m0_db_tx *tx)
{
	struct m0_db_pair   pair;
	int                 rc;

	M0_PRE(m0_cob_is_valid(cob));

	m0_db_pair_setup(&pair, &cob->co_dom->cd_fileattr_ea,
			 eakey, m0_cob_eakey_size(eakey), NULL, 0);
	rc = m0_table_delete(tx, &pair);
	m0_db_pair_fini(&pair);

	COB_FUNC_FAIL(EA_DEL, rc);
	return rc;
}

M0_INTERNAL int m0_cob_ea_iterator_init(struct m0_cob *cob,
				        struct m0_cob_ea_iterator *it,
				        struct m0_bitstring *name,
				        struct m0_db_tx *tx)
{
	int rc;

	/*
	 * Prepare entry key using passed started pos.
	 */
	rc = m0_cob_max_eakey_make(&it->ci_key, cob->co_fid,
				   m0_bitstring_buf_get(name),
				   m0_bitstring_len_get(name));
	if (rc != 0)
		return rc;

        it->ci_rec = m0_alloc(m0_cob_max_earec_size());
	if (it->ci_rec == NULL) {
	        m0_free(it->ci_key);
		return rc;
        }

	/*
	 * Init iterator cursor with max possible key and rec size.
	 */
	m0_db_pair_setup(&it->ci_pair, &cob->co_dom->cd_fileattr_ea,
			 it->ci_key, m0_cob_max_eakey_size(it->ci_key),
			 it->ci_rec, m0_cob_max_earec_size());

	rc = m0_db_cursor_init(&it->ci_cursor,
			       &cob->co_dom->cd_fileattr_ea, tx, 0);
	if (rc != 0) {
		m0_db_pair_release(&it->ci_pair);
		m0_db_pair_fini(&it->ci_pair);
		m0_free(it->ci_key);
		m0_free(it->ci_rec);
		return rc;
	}
	it->ci_cob = cob;
	return rc;
}

M0_INTERNAL int m0_cob_ea_iterator_get(struct m0_cob_ea_iterator *it)
{
	return m0_db_cursor_get(&it->ci_cursor, &it->ci_pair);
}

M0_INTERNAL int m0_cob_ea_iterator_next(struct m0_cob_ea_iterator *it)
{
	int rc;

	rc = m0_db_cursor_next(&it->ci_cursor, &it->ci_pair);

	if (rc == 0 && !m0_fid_eq(&it->ci_key->cek_fid, it->ci_cob->co_fid))
		return -ENOENT;

	return rc;
}

M0_INTERNAL void m0_cob_ea_iterator_fini(struct m0_cob_ea_iterator *it)
{
	m0_db_pair_release(&it->ci_pair);
	m0_db_pair_fini(&it->ci_pair);
	m0_db_cursor_fini(&it->ci_cursor);
	m0_free(it->ci_key);
	m0_free(it->ci_rec);
}

/** @} end group cob */

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
