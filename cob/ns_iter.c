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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 11/21/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/memory.h"
#include "lib/misc.h"  /* SET0 */
#include "cob/ns_iter.h"

/**
 * @addtogroup cob_fid_ns_iter
 */

static bool ns_iter_invariant(const struct m0_cob_fid_ns_iter *iter)
{
	return iter != NULL && iter->cni_dbenv != NULL &&
	       iter->cni_cdom != NULL && m0_fid_is_set(&iter->cni_last_fid);
}

M0_INTERNAL int m0_cob_ns_iter_init(struct m0_cob_fid_ns_iter *iter,
				    struct m0_fid *gfid,
				    struct m0_dbenv *dbenv,
				    struct m0_cob_domain *cdom)
{
	M0_PRE(iter != NULL);

	M0_SET0(iter);

	iter->cni_dbenv = dbenv;
	iter->cni_cdom = cdom;
	iter->cni_last_fid.f_container = gfid->f_container;
	iter->cni_last_fid.f_key = gfid->f_key;

	M0_POST(ns_iter_invariant(iter));

	return 0;
}

M0_INTERNAL int m0_cob_ns_iter_next(struct m0_cob_fid_ns_iter *iter,
                                    struct m0_db_tx *tx,
                                    struct m0_fid *gfid)
{
	int                  rc;
        struct m0_cob_nskey *key = NULL;
        struct m0_db_pair    db_pair;
        struct m0_db_cursor  db_cursor;
	struct m0_table     *db_table;
	uint32_t             cob_idx = 0;
        char                 nskey_bs[UINT32_MAX_STR_LEN];
        uint32_t             nskey_bs_len;
	struct m0_fid        key_fid;

	M0_PRE(ns_iter_invariant(iter));
	M0_PRE(gfid != NULL);
	M0_PRE(tx != NULL);

	db_table = &iter->cni_cdom->cd_namespace;
        rc = m0_db_cursor_init(&db_cursor, db_table, tx, 0);
        if (rc != 0) {
                m0_db_tx_abort(tx);
                return rc;
        }

	M0_SET0(&nskey_bs);
        snprintf((char*)nskey_bs, UINT32_MAX_STR_LEN, "%u", (uint32_t)cob_idx);
        nskey_bs_len = strlen(nskey_bs);

	key_fid.f_container = iter->cni_last_fid.f_container;
	key_fid.f_key = iter->cni_last_fid.f_key;

        rc = m0_cob_nskey_make(&key, &key_fid, (char *)nskey_bs,
			       nskey_bs_len);

        m0_db_pair_setup(&db_pair, db_table, key, m0_cob_nskey_size(key),
			 NULL, 0);

        rc = m0_db_cursor_get(&db_cursor, &db_pair);
        if (rc != 0)
                goto cleanup;

	/*
	 * Assign the fetched value to gfid, which is treated as
	 * iterator output.
	 */
	gfid->f_container = key->cnk_pfid.f_container;
	gfid->f_key = key->cnk_pfid.f_key;

	/* Container (f_container) value remains same, typically 0. */
	iter->cni_last_fid.f_container = key->cnk_pfid.f_container;
	/* Increment the f_key by 1, to exploit m0_db_cursor_get() property. */
	iter->cni_last_fid.f_key = key->cnk_pfid.f_key + 1;

cleanup:
	m0_free(key);
        m0_db_pair_release(&db_pair);
        m0_db_pair_fini(&db_pair);
        m0_db_cursor_fini(&db_cursor);

	return rc;
}

M0_INTERNAL void m0_cob_ns_iter_fini(struct m0_cob_fid_ns_iter *iter)
{
	M0_PRE(ns_iter_invariant(iter));
	M0_SET0(iter);
}

/** @} end cob_fid_ns_iter */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
