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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/13/2010
 */

#include <stdio.h>        /* fprintf */
#include <errno.h>
#include <err.h>
#include <sysexits.h>

#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "db/db.h"
#include "db/extmap.h"

int main(int argc, char **argv)
{
	const char           *db_name;
	const char           *emap_name;
	struct c2_dbenv       db;
	struct c2_emap        emap;
	struct c2_uint128     prefix;
	struct c2_db_tx       tx;
	struct c2_emap_cursor it;
	struct c2_emap_seg   *seg;
	int                   i;
	int                   result;

	if (argc != 3) {
		fprintf(stderr, "Usage: elist <db-dir> <emap-name>\n\n");
		return 1;
	}
	db_name = argv[1];
	emap_name = argv[2];

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_emap_init(&emap, &db, emap_name);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_ASSERT(result == 0);

	C2_SET0(&prefix);

	seg = c2_emap_seg_get(&it);
	while (1) {
		result = c2_emap_lookup(&emap, &tx, &prefix, 0, &it);
		if (result == -ENOENT)
			break;
		else if (result == -ESRCH) {
			prefix = seg->ee_pre;
			continue;
		} else if (result != 0)
			err(EX_SOFTWARE, "c2_emap_lookup(): %i", result);

		printf("%010lx:%010lx:\n", prefix.u_hi, prefix.u_lo);
		for (i = 0; ; ++i) {
			printf("\t%5.5i [%16lx .. %16lx) (%16lx): %16lx\n", i,
			       seg->ee_ext.e_start, seg->ee_ext.e_end,
			       c2_ext_length(&seg->ee_ext), seg->ee_val);
			if (c2_emap_ext_is_last(&seg->ee_ext))
				break;
			result = c2_emap_next(&it);
			C2_ASSERT(result == 0);
		}
		c2_emap_close(&it);
		if (++prefix.u_lo == 0)
			++prefix.u_hi;
	}
	result = c2_db_tx_commit(&tx);
	C2_ASSERT(result == 0);

	c2_emap_fini(&emap);
	c2_dbenv_fini(&db);
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
