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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 08/13/2011
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "colibri/init.h"
#include "lib/arith.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/thread.h"
#include "yaml2db/disk_conf_db.h"
#include "yaml2db/yaml2db.h"

/* Constant names and paths */
static const char *D_PATH = "./__config_db";
static const char *disk_str = "disks";

enum {
	DISK_MAPPING_START_KEY = 100,
};


/* Static declaration of disk section keys array */
static struct c2_yaml2db_section_key disk_section_keys[] = {
	[0] = {"label", true},
	[1] = {"status", true},
	[2] = {"setting", true},
};

/* Static declaration of disk section table */
static struct c2_yaml2db_section disk_section = {
	.ys_table_name = "disk_table",
	.ys_table_ops = &c2_conf_disk_table_ops,
	.ys_start_key = DISK_MAPPING_START_KEY,
	.ys_section_type = C2_YAML_TYPE_MAPPING,
	.ys_num_keys = ARRAY_SIZE(disk_section_keys),
	.ys_valid_keys = disk_section_keys,
};

/**
  Main function for yaml2db
*/
int main(int argc, char *argv[])
{
	int			 rc = 0;
	struct c2_yaml2db_ctx	 yctx;
	const char		*c_name = NULL;
	const char		*d_path = NULL;
	bool			 emitter = false;

	/* Global c2_init */
	rc = c2_init();
	if (rc != 0)
		return rc;

	C2_SET0(&yctx);

	/* Parse command line options */
	rc = C2_GETOPTS("yaml2db", argc, argv,
		C2_STRINGARG('d', "path of database directory",
			LAMBDA(void, (const char *str) {d_path = str; })),
		C2_STRINGARG('f', "config file in yaml format",
			LAMBDA(void, (const char *str) {c_name = str; })),
		C2_FLAGARG('e', "emitter mode", &emitter));

	if (rc != 0)
		goto cleanup;

	/* Config file has to be specified as a command line option */
	if (c_name == NULL) {
		fprintf(stderr, "Error: Config file path not specified\n");
		rc = -EINVAL;
		goto cleanup;
	}
	yctx.yc_cname = c_name;

	/* If database path not specified, set the default path */
	if (d_path != NULL)
		yctx.yc_dpath = d_path;
	else
		yctx.yc_dpath = D_PATH;

	/* Based on the emitter flag, enable the yaml2db context type
	   default is parser type */
	if (emitter)
		yctx.yc_type = C2_YAML2DB_CTX_EMITTER;
	else
		yctx.yc_type = C2_YAML2DB_CTX_PARSER;

	/* Initialize the parser and database environment */
	rc = yaml2db_init(&yctx);
	if (rc != 0) {
		fprintf(stderr, "Error: yaml2db initialization failed \n");
		goto cleanup;
	}

	if (!emitter) {
		/* Load the information from yaml file to yaml_document,
		   and check for parsing errors internally */
		rc = yaml2db_doc_load(&yctx);
		if (rc != 0) {
			fprintf(stderr, "Error: document loading failed \n");
			goto cleanup_parser_db;
		}

		/* Parse the disk configuration that is loaded in the context */
		rc = yaml2db_conf_load(&yctx, &disk_section, disk_str);
		if (rc != 0)
			fprintf(stderr, "Error: config loading failed \n");

	} else {
		rc = yaml2db_conf_emit(&yctx, &disk_section, disk_str);
		if (rc != 0)
			fprintf(stderr, "Error: config emitting failed \n");
	}

cleanup_parser_db:
	yaml2db_fini(&yctx);

cleanup:
	c2_fini();

	return rc;
}

/** @} end of yaml2db group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
