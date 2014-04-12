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

#include "lib/arith.h"    /* M0_3WAY, m0_uint128 */
#include "lib/misc.h"     /* M0_SET0 */
#include "lib/assert.h"
#include "db/db.h"
#include "db/extmap.h"

int main(int argc, char **argv)
{
	const char           *db_name;
	const char           *emap_name;
	struct m0_dbenv       db;
	struct m0_emap        emap;
	struct m0_uint128     prefix;
	struct m0_db_tx       tx;
	struct m0_emap_cursor it;
	struct m0_emap_seg   *seg;
	int                   i;
	int                   result;

	if (argc != 3) {
		fprintf(stderr, "Usage: elist <db-dir> <emap-name>\n\n");
		return 1;
	}
	db_name = argv[1];
	emap_name = argv[2];

	result = m0_dbenv_init(&db, db_name, 0, true);
	M0_ASSERT(result == 0);

	result = m0_emap_init(&emap, &db, emap_name);
	M0_ASSERT(result == 0);

	result = m0_db_tx_init(&tx, &db, 0);
	M0_ASSERT(result == 0);

	M0_SET0(&prefix);

	seg = m0_emap_seg_get(&it);
	while (1) {
		result = m0_emap_lookup(&emap, &tx, &prefix, 0, &it);
		if (result == -ENOENT)
			break;
		else if (result == -ESRCH) {
			prefix = seg->ee_pre;
			continue;
		} else if (result != 0)
			err(EX_SOFTWARE, "m0_emap_lookup(): %i", result);

		printf("%010lx:%010lx:\n", prefix.u_hi, prefix.u_lo);
		for (i = 0; ; ++i) {
			printf("\t%5.5i [%16lx .. %16lx) (%16lx): %16lx\n", i,
			       seg->ee_ext.e_start, seg->ee_ext.e_end,
			       m0_ext_length(&seg->ee_ext), seg->ee_val);
			if (m0_emap_ext_is_last(&seg->ee_ext))
				break;
			result = m0_emap_next(&it);
			M0_ASSERT(result == 0);
		}
		m0_emap_close(&it);
		if (++prefix.u_lo == 0)
			++prefix.u_hi;
	}
	result = m0_db_tx_commit(&tx);
	M0_ASSERT(result == 0);

	m0_emap_fini(&emap);
	m0_dbenv_fini(&db);
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
