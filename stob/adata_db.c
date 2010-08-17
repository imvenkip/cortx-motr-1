#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>     /* asprintf */
#include <stdlib.h>
#include <string.h>

#include "lib/errno.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "balloc/balloc.h"
#include "linux.h"
#include "linux_internal.h"

/**
   @addtogroup stoblinux

   <b>Allocation data data-base.</b>

   @{
 */

void adata_fini(struct linux_domain *ldom)
{
	c2_table_fini(&ldom->sdl_mapping);
	c2_dbenv_fini(&ldom->sdl_dbenv);
}

struct ad_key {
	struct c2_stob_id ak_obj;
	c2_bindex_t       ak_offset;
};

struct ad_rec {
	struct c2_balloc_extent ar_ext;
};

#if 0
static void ad_pack(const struct c2_stob_id *obj, const struct adata_ext *ext, 
		    struct ad_key *key, struct ad_rec *rec)
{
	key->ak_obj    = *obj;
	key->ak_offset =  ext->e_logical + ext->e_physical.be_len;
	rec->ar_ext    =  ext->e_physical;
}
#endif

static void ad_open(const struct ad_key *key, const struct ad_rec *rec,
		    struct adata_ext *ext)
{
	C2_ASSERT(rec->ar_ext.be_len > 0);
	C2_ASSERT(key->ak_offset >= rec->ar_ext.be_len);
	ext->e_logical  = key->ak_offset - rec->ar_ext.be_len;
	ext->e_physical = rec->ar_ext;
}

static int ad_key_cmp(struct c2_table *table, 
		      const void *key0, const void *key1)
{
	const struct ad_key *a0 = key0;
	const struct ad_key *a1 = key1;
	return c2_stob_id_cmp(&a0->ak_obj, &a1->ak_obj) ?:
		 C2_3WAY(a0->ak_offset, a1->ak_offset);
}

const struct c2_table_ops linux_ad_ops = {
	.to = {
		[TO_KEY] = { 
			.max_size = sizeof(struct ad_key)
		},
		[TO_REC] = {
			.max_size = sizeof(struct ad_rec)
		},
	},
	.key_cmp = ad_key_cmp
};

int adata_init(struct linux_domain *ldom)
{
	char *dbname;
	int   result;

	result = asprintf(&dbname, "%s/db", ldom->sdl_path);
	if (result >= 0) {
		result = c2_dbenv_init(&ldom->sdl_dbenv, dbname, 0);
		if (result == 0)
			result = c2_table_init(&ldom->sdl_mapping, 
					       &ldom->sdl_dbenv, 
					       "ad", 0, &linux_ad_ops);
		free(dbname);
	} else
		result = -ENOMEM;
	return result;
}

int adata_lookup(struct linux_domain *ldom, struct c2_db_tx *tx,
		 const struct c2_stob_id *obj, c2_bindex_t offset, 
		 struct adata_ext *prev, struct adata_ext *next)
{
	struct ad_key       key;
	struct ad_rec       rec;
	int                 result;
	struct c2_db_cursor cur;
	struct c2_db_pair   cons;

	key.ak_obj    = *obj;
	key.ak_offset =  offset;

	c2_db_pair_setup(&cons, &ldom->sdl_mapping,
			 &key, sizeof key, &rec, sizeof rec);
	result = c2_db_cursor_init(&cur, &ldom->sdl_mapping, tx);
	if (result == 0) {
		result = c2_db_cursor_get(&cur, &cons);
		if (result == 0) {
			C2_ASSERT(c2_stob_id_cmp(&key.ak_obj, obj) >= 0);
			if (c2_stob_id_eq(&key.ak_obj, obj)) {
				C2_ASSERT(key.ak_offset >= offset);
				ad_open(&key, &rec, next);
				result |= ALR_GOT_NEXT;
			}
			c2_db_pair_release(&cons);			

			result = c2_db_cursor_prev(&cur, &cons);
			if (result == 0) {
				C2_ASSERT(c2_stob_id_cmp(&key.ak_obj, obj) < 0);
				if (c2_stob_id_eq(&key.ak_obj, obj)) {
					C2_ASSERT(key.ak_offset < offset);
					ad_open(&key, &rec, prev);
					result |= ALR_GOT_PREV;
				}
			}
		}
		c2_db_cursor_fini(&cur);
	}
	c2_db_pair_fini(&cons);
	return result;
}

/** @} end group stoblinux */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
