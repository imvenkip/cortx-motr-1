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
#include "db/db.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/thread.h"
#include "../yaml/include/yaml.h"

struct yaml2db_ctx {
	/* YAML parser struct */
	yaml_parser_t	 yc_parser;
	/* YAML event structure */
	yaml_event_t	 yc_event;
	/* Config file name */
	const char			*yc_cname;
	/* Database path */
	const char			*yc_dpath;
	/* Database environment */
	struct c2_dbenv	 yc_db;
};

char *D_PATH = "__config_db";

/* Forward declarations of the functions */
int yaml2db_init(struct yaml2db_ctx *yctx);
void yaml2db_fini(struct yaml2db_ctx *yctx);
int yaml2db_parse(struct yaml2db_ctx *yctx);

/**
  Init function, which initialized the parser and sets input file
  @param yctx - yaml2db context
 */
int yaml2db_init(struct yaml2db_ctx *yctx)
{
	int		 rc = 0;
	FILE	*fp;
	char	 opath[64];
	char	 dpath[64];

	/* Initialize the parser. According to yaml documentation,
	   parser_initialize command returns 1 in case of success */
	rc = yaml_parser_initialize(&yctx->yc_parser);
	if(rc != 1)
		return rc;

	/* Open the config file in read mode */
	fp = fopen(yctx->yc_cname, "r");
	if (fp == NULL)
		return -EINVAL;

	/* Set the input file to the parser. This function inherently returns
	   void, so it is assumed that this will be successful in all cases*/
	yaml_parser_set_input_file(&yctx->yc_parser, fp);

	rc = mkdir(yctx->yc_dpath, 0700);
	if (rc != 0)
		goto cleanup;
	sprintf(opath, "%s/o", yctx->yc_dpath);

	rc = mkdir(opath, 0700);
	if (rc != 0)
		goto cleanup;
	sprintf(dpath, "%s/d", yctx->yc_dpath);

	rc = c2_dbenv_init(&yctx->yc_db, dpath, 0);
	if (rc != 0)
		goto cleanup;

	return 0;

cleanup:
	yaml_parser_delete(&yctx->yc_parser);
	return -EINVAL;
}

void yaml2db_fini(struct yaml2db_ctx *yctx)
{
	yaml_parser_delete(&yctx->yc_parser);
	c2_dbenv_fini(&yctx->yc_db);
}

/**
  Main function for yaml2db
*/
int main(int argc, char *argv[])
{
	int			 rc = 0;
	struct yaml2db_ctx	 yctx;
	const char		*c_name = NULL;
	const char		*d_path = NULL;

	/* Global c2_init */
	rc = c2_init();
	if (rc != 0)
		return rc;

	/* Parse command line options */
	rc = C2_GETOPTS("yaml2db", argc, argv,
		C2_STRINGARG('d', "path of database directory",
			LAMBDA(void, (const char *str) {d_path = str; })),
		C2_STRINGARG('f', "config file in yaml format",
			LAMBDA(void, (const char *str) {c_name = str; })));

	if (rc != 0)
		return rc;

	/* Config file has to be specified as a command line option */
	if (c_name == NULL) {
		printf("Error: Config file path not specified\n");
		return -EINVAL;
	}
	yctx.yc_cname = c_name;

	/* If database path not specified, set the default path */
	if (d_path != NULL)
		yctx.yc_dpath = d_path;
	else
		yctx.yc_dpath = D_PATH;

	/* Initialize the parser and database environment */
	rc = yaml2db_init(&yctx);
	if (rc != 0)
		return rc;

	/* Parse and store the configuration contents in the database */
//	yaml2db_parse(&yctx);

	/* Finalize the parser and database environment */
	yaml2db_fini(&yctx);

	c2_fini();

  return 0;
}
