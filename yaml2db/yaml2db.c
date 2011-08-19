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
#include "yaml2db/yaml2db.h"

/* Constant names and paths */
static const char *D_PATH = "__config_db";
//static const char *disk_str = "disks";
//static const char *disk_table = "disk_table";

/* DB Table ops */
static int test_key_cmp(struct c2_table *table,
                        const void *key0, const void *key1)
{
        const uint64_t *u0 = key0;
        const uint64_t *u1 = key1;

        return C2_3WAY(*u0, *u1);
}

/* Table ops for disk table */
static const struct c2_table_ops disk_table_ops = {
        .to = {
                [TO_KEY] = { .max_size = 84 },
                [TO_REC] = { .max_size = 84 }
        },
        .key_cmp = test_key_cmp
};

/* ADDB Instrumentation yaml2db */
static const struct c2_addb_ctx_type yaml2db_ctx_type = {
        .act_name = "yaml2db"
};

static const struct c2_addb_loc yaml2db_addb_loc = {
        .al_name = "yaml2db"
};

C2_ADDB_EV_DEFINE(yaml2db_func_fail, "yaml2db_func_fail",
                C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);


/**
  Init function, which initializes the parser and sets input file
  @param yctx - yaml2db context
  @retval 0 if success, -errno otherwise
 */
static int yaml2db_init(struct c2_yaml2db_ctx *yctx)
{
	int	 rc = 0;
	char	*opath = NULL;
	char	*dpath = NULL;

	C2_PRE(yctx != NULL);

	/* Initialize the ADDB context */
        c2_addb_ctx_init(&yctx->yc_addb, &yaml2db_ctx_type, &c2_addb_global_ctx);
        c2_addb_choose_default_level(AEL_WARN);

	/* Initialize the parser. According to yaml documentation,
	   parser_initialize command returns 1 in case of success */
	rc = yaml_parser_initialize(&yctx->yc_parser);
	if(rc != 1) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml_parser_initialize", 0);
		return -EINVAL;
	}

	/* Open the config file in read mode */
	yctx->yc_fp = fopen(yctx->yc_cname, "r");
	if (yctx->yc_fp == NULL) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "fopen", 0);
		yaml_parser_delete(&yctx->yc_parser);
		return -EINVAL;
	}

	/* Set the input file to the parser. */
	yaml_parser_set_input_file(&yctx->yc_parser, yctx->yc_fp);

	C2_ALLOC_ARR(opath, strlen(yctx->yc_dpath) + 2);
	if (opath == NULL) {
		C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, c2_addb_oom);
		goto cleanup;
	}

	C2_ALLOC_ARR(dpath, strlen(yctx->yc_dpath) + 2);
	if (dpath == NULL) {
		C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, c2_addb_oom);
		goto cleanup;
	}

	rc = mkdir(yctx->yc_dpath, 0700);
	if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "mkdir", 0);
		goto cleanup;
	}

	sprintf(opath, "%s/o", yctx->yc_dpath);

	rc = mkdir(opath, 0700);
	if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "mkdir", 0);
		goto cleanup;
	}

	sprintf(dpath, "%s/d", yctx->yc_dpath);

	rc = c2_dbenv_init(&yctx->yc_db, dpath, 0);

	if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "c2_dbenv_init", 0);
		goto cleanup;
	}

	c2_free(opath);
	c2_free(dpath);
	return 0;

cleanup:
	c2_free(opath);
	c2_free(dpath);
        c2_addb_ctx_fini(&yctx->yc_addb);
	fclose(yctx->yc_fp);
	yaml_parser_delete(&yctx->yc_parser);
	return -EINVAL;
}

/**
  Fini function, which finalizes the parser and finies the db
  @param yctx - yaml2db context
 */
static void yaml2db_fini(struct c2_yaml2db_ctx *yctx)
{
	C2_PRE(yctx != NULL);

	yaml_parser_delete(&yctx->yc_parser);
	fclose(yctx->yc_fp);
	c2_dbenv_fini(&yctx->yc_db);
        c2_addb_ctx_fini(&yctx->yc_addb);
}

#if 0
/**
  Parsing and setting the disk configuration
  @param yctx - yaml2db context
 */
static void yaml2db_disk_conf(struct c2_yaml2db_ctx *yctx)
{
	int			rc;
	struct			c2_table table;
	struct			c2_db_tx tx;
	uint64_t		key = 0;
	struct c2_db_pair	db_pair;

	C2_PRE(yctx != NULL);

	/* Initialize the table */
	rc = c2_table_init(&table, &yctx->yc_db, disk_table,
			0, &disk_table_ops);
	if (rc != 0)
		return;

	/* Initialize the database transaction */
	rc = c2_db_tx_init(&tx, &yctx->yc_db, 0);
	if (rc != 0) {
		c2_table_fini(&table);
		return;
	}

	do{
		yaml_parser_parse(&yctx->yc_parser, &yctx->yc_event);
		if (yctx->yc_event.type == YAML_SCALAR_EVENT) {
			yaml_parser_parse(&yctx->yc_parser, &yctx->yc_event);
			/* Key to be initialized per disk info */
			key += 100;
			c2_db_pair_setup(&db_pair, &table, &key, sizeof key,
				 yctx->yc_event.data.scalar.value,
				 sizeof(yctx->yc_event.data.scalar.value));
			rc = c2_table_insert(&tx, &db_pair);
			if (rc != 0)
				return;
			/* Parse the status keyword */
			yaml_parser_parse(&yctx->yc_parser, &yctx->yc_event);
			yaml_parser_parse(&yctx->yc_parser, &yctx->yc_event);
			key++;
			c2_db_pair_setup(&db_pair, &table, &key, sizeof key,
				 yctx->yc_event.data.scalar.value,
				 sizeof(yctx->yc_event.data.scalar.value));
			rc = c2_table_insert(&tx, &db_pair);
			if (rc != 0)
				return;
			/* Parse the setting keyword */
			yaml_parser_parse(&yctx->yc_parser, &yctx->yc_event);
			yaml_parser_parse(&yctx->yc_parser, &yctx->yc_event);
			key++;
			c2_db_pair_setup(&db_pair, &table, &key, sizeof key,
				 yctx->yc_event.data.scalar.value,
				 sizeof(yctx->yc_event.data.scalar.value));
			rc = c2_table_insert(&tx, &db_pair);
			if (rc != 0)
				return;
		}
	} while(yctx->yc_event.type != YAML_SEQUENCE_END_EVENT);

	c2_db_tx_commit(&tx);
}

/**
  Finds whether the event has disks info that can be parsed
  @param event - yaml event
  @retval true if disk info present, false otherwise
 */
static bool event_has_disks(const yaml_event_t *event)
{
	C2_PRE(event != NULL);

	if (strcmp((char *)event->data.scalar.value, (char *)disk_str) == 0)
		return true;
	return false;
}
#endif


static void yaml_parser_error_detect(yaml_parser_t *parser)
{
	C2_PRE(parser != NULL);

	switch (parser->error) {
	case YAML_MEMORY_ERROR:
		fprintf(stderr, "Memory error: Not enough memory for parsing\n");
		break;
	case YAML_READER_ERROR:
		if (parser->problem_value != -1) {
			fprintf(stderr, "Reader error: %s: #%X at %lu\n",
				parser->problem, parser->problem_value,
				parser->problem_offset);
		}
		else {
			fprintf(stderr, "Reader error: %s at %lu\n",
					parser->problem, parser->problem_offset);
		}
		break;
	case YAML_SCANNER_ERROR:
		if (parser->context) {
			fprintf(stderr, "Scanner error: %s at line %lu,\
				column %lu""%s at line %lu, column %lu\n",
				parser->context, parser->context_mark.line+1,
				parser->context_mark.column+1, parser->problem,
				parser->problem_mark.line+1,
				parser->problem_mark.column+1);
		} else {
			fprintf(stderr, "Scanner error: %s at line %lu,\
				column %lu\n", parser->problem,
				parser->problem_mark.line+1,
				parser->problem_mark.column+1);
		}
		break;
	case YAML_PARSER_ERROR:
		if (parser->context) {
			fprintf(stderr, "Parser error: %s at line %lu,\
				column %lu""%s at line %lu, column %lu\n",
				parser->context, parser->context_mark.line+1,
				parser->context_mark.column+1,parser->problem,
				parser->problem_mark.line+1,
				parser->problem_mark.column+1);
		} else {
			fprintf(stderr, "Parser error: %s at line %lu,\
				column %lu\n", parser->problem,
				parser->problem_mark.line+1,
				parser->problem_mark.column+1);
		}
		break;
	case YAML_COMPOSER_ERROR:
	case YAML_WRITER_ERROR:
	case YAML_EMITTER_ERROR:
	case YAML_NO_ERROR:
		break;
	default:
		C2_IMPOSSIBLE("Invalid error");
	}
}

/**
  Parsing function
  @param yctx - yaml2db context
  @retval 0 if successful, -errno otherwise
 */
static int yaml2db_parse(struct c2_yaml2db_ctx *yctx)
{
	int		 rc;
	yaml_node_t	*root_node;

	C2_PRE(yctx != NULL);

	rc = yaml_parser_load(&yctx->yc_parser, &yctx->yc_document);
	if (rc != 1) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml_parser_load", 0);
		goto parser_error;
	}

	root_node = yaml_document_get_root_node(&yctx->yc_document);
	if (root_node == NULL) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml2db_document_get_root_node", 0);
                yaml_document_delete(&yctx->yc_document);
		return -EINVAL;
	}
	return 0;

parser_error:
	yaml_parser_error_detect(&yctx->yc_parser);
	return -EINVAL;
}

/**
  Main function for yaml2db
*/
int main(int argc, char *argv[])
{
	int			 rc = 0;
	struct c2_yaml2db_ctx	 yctx;
	const char		*c_name = NULL;
	const char		*d_path = NULL;

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
	if (rc != 0){
                C2_ADDB_ADD(&yctx.yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml2db_init", 0);
		return rc;
	}

	/* Parse and store the configuration contents in the database */
	rc = yaml2db_parse(&yctx);
	if (rc != 0){
                C2_ADDB_ADD(&yctx.yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml2db_parse", 0);
		return rc;
	}


	/* Finalize the parser and database environment */
	yaml2db_fini(&yctx);

	c2_fini();

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
