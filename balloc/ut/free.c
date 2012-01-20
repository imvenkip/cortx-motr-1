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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 09/02/2010
 */

#include "balloc/ut/balloc_ut.h"

extern	struct c2_balloc colibri_balloc;

int main(int argc, char **argv)
{
	const char           *db_name;
	struct c2_dbenv       db;
	struct c2_dtx         dtx;
	struct c2_ext         ext = { 0 };
	int                   result;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s <db-dir> start end\n", argv[0]);
		return 1;
	}
	db_name = argv[1];
	ext.e_start = atoll(argv[2]);
	ext.e_end   = atoll(argv[3]);

	if (c2_ext_is_empty(&ext)) {
		fprintf(stderr, "Invalid extent\n");
		return -EINVAL;
	}

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
	C2_ASSERT(result == 0);

	result = colibri_balloc.cb_ballroom.ab_ops->bo_init
		(&colibri_balloc.cb_ballroom, &db, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	if (result == 0) {
		result = colibri_balloc.cb_ballroom.ab_ops->bo_free(
			    &colibri_balloc.cb_ballroom, &dtx, &ext);
		printf("rc = %d: freed [%08llx,%08llx)\n", result,
			(unsigned long long)ext.e_start,
			(unsigned long long)ext.e_end);
	}
	result = c2_db_tx_commit(&dtx.tx_dbtx);
	C2_ASSERT(result == 0);
	colibri_balloc.cb_ballroom.ab_ops->bo_fini(&colibri_balloc.cb_ballroom);

	c2_dbenv_fini(&db);
	printf("done\n");
	return 0;
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
