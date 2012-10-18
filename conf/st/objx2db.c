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
#include "conf/onwire.h"
#include "conf/conf_xcode.h"
#include "conf/obj.h"
#include "lib/getopts.h"
#include "lib/thread.h" /* LAMBDA */
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/buf.h"

#include <stdlib.h>
#include <stdio.h>

/* Constant names and paths */
static const char *D_PATH	= "./__config_db";
static const char *C_PATH	= "./conf_xc.txt";

int main(int argc, char *argv[])
{
	enum { KB = 1 << 10 };
	int   n;
	int   rc;
	char  command[256];
	char  buf[32*KB] = {0};
	const char *dbpath = D_PATH;
	const char *dbconf = C_PATH;
	struct confx_object *conf;
	FILE *conf_file;

	rc = c2_init();
	if (rc != 0)
		return rc;

	rc = C2_GETOPTS("objx2db", argc, argv,
			C2_STRINGARG('b', "path of database directory",
				     LAMBDA(void, (const char *str)
					    { dbpath = str; })),
			C2_STRINGARG('c', "path of config file",
				     LAMBDA(void, (const char *str)
					    { dbconf = str; })));
	if (rc != 0)
		goto cleanup;

	snprintf(command, sizeof command, "rm -rf %s; rm -f %s.errlog; "
		 "rm -f %s.msglog", dbpath, dbpath, dbpath);

	rc = system(command);
	if (rc != 0)
		goto cleanup;

	conf_file = fopen(dbconf, "r");
	if (conf_file == NULL)
		goto cleanup;

	n = fread(buf, 1, sizeof buf, conf_file);
	if (n <= 0) {
		rc = n;
		goto close_file;
	}
	buf[n] = '\0';

	n = c2_confx_obj_nr(buf);
	if (n <= 0) {
		rc = n;
		goto close_file;
	}

	C2_ALLOC_ARR(conf, n);
	if (conf == NULL) {
		rc = ENOMEM;
		goto close_file;
	}

	rc = c2_conf_parse(buf, conf, n);
	if (rc <= 0)
		goto conf_free;


	rc = c2_confx_db_create(dbpath, conf, n);
	c2_confx_fini(conf, n);
conf_free:
	c2_free(conf);
close_file:
	fclose(conf_file);
cleanup:
	c2_fini();
	return rc;
}
