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

#include "lib/misc.h"  /* SET0 */
#include "cob/ns_iter.h"

C2_INTERNAL int c2_cob_ns_iter_init(struct c2_cob_ns_iter *iter,
				    struct c2_fid *gfid,
				    struct c2_dbenv *dbenv,
				    struct c2_cob_domain *cdom)
{
	C2_PRE(iter != NULL);

	C2_SET0(iter);

	iter->cni_dbenv = dbenv;
	iter->cni_cdom = cdom;
	iter->cni_last_fid.f_container = gfid->f_container;
	iter->cni_last_fid.f_key = gfid->f_key;

	return 0;
}

C2_INTERNAL int c2_cob_ns_iter_next(struct c2_cob_ns_iter *iter,
                                    struct c2_fid *gfid,
                                    struct c2_db_tx *tx)
{
	int                  rc;
        struct c2_cob_nskey  key;
        struct c2_cob_nsrec  rec;
        struct c2_db_pair    db_pair;
        struct c2_db_cursor  db_cursor;
	struct c2_table     *db_table;

	C2_PRE(iter != NULL);
	C2_PRE(gfid != NULL);
	C2_PRE(tx != NULL);

	db_table = &iter->cni_cdom->cd_namespace;
        rc = c2_db_cursor_init(&db_cursor, db_table, tx, 0);
        if (rc != 0) {
                c2_db_tx_abort(tx);
                return rc;
        }

	key.cnk_pfid.f_container = iter->cni_last_fid.f_container;
	key.cnk_pfid.f_key = iter->cni_last_fid.f_key;

        c2_db_pair_setup(&db_pair, db_table, &key, sizeof(struct c2_cob_nskey),
			 &rec, sizeof(struct c2_cob_nsrec));

        rc = c2_db_cursor_get(&db_cursor, &db_pair);
        if (rc != 0)
                goto cleanup;

	/*
	 * Assign the fetched value to gfid, which is treated as
	 * iterator output.
	 */
	gfid->f_container = key.cnk_pfid.f_container;
	gfid->f_key = key.cnk_pfid.f_key;

	/* Container (f_container) value remains same, typically 0. */
	iter->cni_last_fid.f_container = key.cnk_pfid.f_container;
	/* Increment the f_key by 1, to exploit c2_db_cursor_get() property. */
	iter->cni_last_fid.f_key = key.cnk_pfid.f_key++;

cleanup:
        c2_db_pair_release(&db_pair);
        c2_db_pair_fini(&db_pair);
        c2_db_cursor_fini(&db_cursor);

	if (rc == 0)
		c2_db_tx_commit(tx);
	else
		c2_db_tx_abort(tx);

	return rc;
}

C2_INTERNAL void c2_cob_ns_iter_fini(struct c2_cob_ns_iter *iter)
{
	C2_PRE(iter != NULL);
	C2_SET0(iter);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
