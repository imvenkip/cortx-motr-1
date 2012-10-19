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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 25/09/2012
 */

#include "colibri/init.h"
#include "conf/preload.h"
#include "conf/obj_ops.h"
#include "conf/onwire.h"
#include "conf/conf_xcode.h"
#include "conf/obj.h"
#include "conf/reg.h"
#include "lib/getopts.h"
#include "lib/thread.h" /* LAMBDA */
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/buf.h"

#include <stdlib.h>

/* Constant names and paths */
static const char *D_PATH = "./__config_db";

int main(int argc, char *argv[])
{
	int i;
	int n;
	int rc;
	const char *dbpath = D_PATH;
	struct confx_object *db_obj;
	struct c2_conf_obj  *obj;
	struct c2_conf_reg   reg;


	rc = c2_init();
	if (rc != 0)
		return rc;

	rc = C2_GETOPTS("confd_test", argc, argv,
			C2_STRINGARG('b', "path of database directory",
				     LAMBDA(void, (const char *str)
					    { dbpath = str; })));
	if (rc != 0)
		goto cleanup;

	n = c2_confx_db_read(dbpath, &db_obj);
	if (n <= 0) {
		rc = n;
		goto cleanup;
	}

	c2_conf_reg_init(&reg);

	for (i = 0; i < n; ++i) {
		rc = c2_conf_obj_find(&reg, db_obj[i].o_conf.u_type,
				      &db_obj[i].o_id, &obj);
		if (rc != 0)
			break;

		rc = c2_conf_obj_fill(obj, &db_obj[i], &reg);
		if (rc != 0)
			break;
	}

	extern void c2_conf__reg2dot(const struct c2_conf_reg *reg);
	c2_conf__reg2dot(&reg);

	c2_conf_reg_fini(&reg);
	c2_confx_fini(db_obj, n);
	c2_free(db_obj);

cleanup:
	c2_fini();
	return rc;
}

