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
static const char *disk_table = "disk_table";
static const char *disk_str = "disks";

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
        c2_addb_ctx_init(&yctx->yc_addb, &yaml2db_ctx_type,
				&c2_addb_global_ctx);
	c2_addb_choose_default_level(AEL_WARN);
	/* Initialize the parser. According to yaml documentation,
	   parser_initialize command returns 1 in case of success */
	rc = yaml_parser_initialize(&yctx->yc_parser);
	if(rc != 1) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "yaml_parser_initialize", 0);
		rc = -EINVAL;
		goto cleanup;
	}

	/* Open the config file in read mode */
	yctx->yc_fp = fopen(yctx->yc_cname, "r");
	if (yctx->yc_fp == NULL) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "fopen", 0);
		yaml_parser_delete(&yctx->yc_parser);
		rc = -errno;
		goto cleanup;
	}

	/* Set the input file to the parser. */
	yaml_parser_set_input_file(&yctx->yc_parser, yctx->yc_fp);

	C2_ALLOC_ARR(opath, strlen(yctx->yc_dpath) + 2);
	if (opath == NULL) {
		C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, c2_addb_oom);
		rc = -ENOMEM;
		goto cleanup;
	}

	C2_ALLOC_ARR(dpath, strlen(yctx->yc_dpath) + 2);
	if (dpath == NULL) {
		C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc, c2_addb_oom);
		rc = -ENOMEM;
		goto cleanup;
	}

	rc = mkdir(yctx->yc_dpath, 0700);
	if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "mkdir", 0);
		rc = -errno;
		goto cleanup;
	}

	sprintf(opath, "%s/o", yctx->yc_dpath);

	rc = mkdir(opath, 0700);
	if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "mkdir", 0);
		rc = -errno;
		goto cleanup;
	}

	sprintf(dpath, "%s/d", yctx->yc_dpath);

	rc = c2_dbenv_init(&yctx->yc_db, dpath, 0);

	if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "c2_dbenv_init", 0);
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
	return rc;
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

/**
  Function to detect and print parsing errors
  @param parser - yaml_parser structure
 */
static void yaml_parser_error_detect(const yaml_parser_t *parser)
{
	C2_PRE(parser != NULL);

	switch (parser->error) {
	case YAML_MEMORY_ERROR:
		fprintf(stderr, "Memory error: Not enough memory\
			for parsing\n");
		break;
	case YAML_READER_ERROR:
		if (parser->problem_value != -1)
			fprintf(stderr, "Reader error: %s: #%X at %lu\n",
				parser->problem, parser->problem_value,
				parser->problem_offset);
		else
			fprintf(stderr, "Reader error: %s at %lu\n",
				parser->problem, parser->problem_offset);
		break;
	case YAML_SCANNER_ERROR:
		if (parser->context)
			fprintf(stderr, "Scanner error: %s at line %lu,\
				column %lu""%s at line %lu, column %lu\n",
				parser->context, parser->context_mark.line+1,
				parser->context_mark.column+1, parser->problem,
				parser->problem_mark.line+1,
				parser->problem_mark.column+1);
		else
			fprintf(stderr, "Scanner error: %s at line %lu,\
				column %lu\n", parser->problem,
				parser->problem_mark.line+1,
				parser->problem_mark.column+1);
		break;
	case YAML_PARSER_ERROR:
		if (parser->context)
			fprintf(stderr, "Parser error: %s at line %lu,\
				column %lu""%s at line %lu, column %lu\n",
				parser->context, parser->context_mark.line+1,
				parser->context_mark.column+1,parser->problem,
				parser->problem_mark.line+1,
				parser->problem_mark.column+1);
		else
			fprintf(stderr, "Parser error: %s at line %lu,\
				column %lu\n", parser->problem,
				parser->problem_mark.line+1,
				parser->problem_mark.column+1);
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
  Function to load the yaml document
  @param yctx - yaml2db context
  @retval 0 if successful, -errno otherwise
 */
static int yaml2db_doc_load(struct c2_yaml2db_ctx *yctx)
{
	int		 rc;
	yaml_node_t	*root_node;

	C2_PRE(yctx != NULL);

	rc = yaml_parser_load(&yctx->yc_parser, &yctx->yc_document);
	if (rc != 1) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "yaml_parser_load", 0);
		goto parser_error;
	}

	root_node = yaml_document_get_root_node(&yctx->yc_document);
	if (root_node == NULL) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail,
                                "yaml2db_document_get_root_node", 0);
                yaml_document_delete(&yctx->yc_document);
		return -EINVAL;
	}
	yctx->yc_root_node = *root_node;
	return 0;

parser_error:
	yaml_parser_error_detect(&yctx->yc_parser);
	return -EINVAL;
}

/**
  Function to read mapping
  @param yctx - yaml2db context
  @param table - database table in which disk structure has to be stored
  @param tx - database transaction
  @param node - yaml node used as an anchor from which sequence has to be read
  @retval 0 if success, -errno otherwise
*/
static int read_and_store_disk_mapping(struct c2_yaml2db_ctx *yctx,
		struct c2_table *table, struct c2_db_tx *tx, yaml_node_t *node)
{
	yaml_node_pair_t	*pair;
	yaml_node_t		*mnode_key;
	yaml_node_t		*mnode_value;
        struct c2_db_pair	 db_pair;
	int			 rc;
	int			 key = 0;

	C2_PRE(node != NULL);
	C2_PRE(yctx != NULL);

	for (pair = node->data.mapping.pairs.start;
			pair < node->data.mapping.pairs.top; pair++) {
		mnode_key = yaml_document_get_node(&yctx->yc_document,
				pair->key);
		mnode_value = yaml_document_get_node(&yctx->yc_document,
				pair->value);
		if (pair->key != 0 && pair->value != 0)
			key++;
			c2_db_pair_setup(&db_pair, table, &key, sizeof key,
					mnode_value->data.scalar.value,
					sizeof(mnode_value->data.scalar.value));
			rc = c2_table_insert(tx, &db_pair);
			if (rc != 0) {
				C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
						yaml2db_func_fail,
						"c2_table_insert", 0);
				c2_db_pair_release(&db_pair);
				c2_db_pair_fini(&db_pair);
				return -EINVAL;
			}
			c2_db_pair_release(&db_pair);
			c2_db_pair_fini(&db_pair);
	}
	return 0;
}

/**
  Function to read sequence
  @param yctx- YAML context structure
  @param node - yaml node used as an anchor from which sequence has to be read
  @retval 0 if success, -errno otherwise
*/
static int read_sequence(struct c2_yaml2db_ctx *yctx, yaml_node_t *node)
{
	yaml_node_item_t	*item;
	yaml_node_t		*snode;
        struct c2_table		 table;
        struct c2_db_tx		 tx;
	int			 rc;

	C2_PRE(yctx != NULL);
	C2_PRE(node != NULL);

        /* Initialize the table */
        rc = c2_table_init(&table, &yctx->yc_db, disk_table,
                        0, &disk_table_ops);
        if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "c2_table_init", 0);
                return -EINVAL;
	}

        /* Initialize the database transaction */
        rc = c2_db_tx_init(&tx, &yctx->yc_db, 0);
        if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "c2_db_tx_init", 0);
                c2_table_fini(&table);
                return -EINVAL;
        }

	for (item = node->data.sequence.items.start;
			item < node->data.sequence.items.top; item++) {
		snode = yaml_document_get_node(&yctx->yc_document, *item);
		if (snode == NULL)
			return -EINVAL;
		rc = read_and_store_disk_mapping(yctx, &table, &tx, snode);
		if (rc != 0) {
			C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
					yaml2db_func_fail,
					"read_and_store_disk_mapping", 0);
			return -EINVAL;
		}
	}
	c2_db_tx_commit(&tx);
	c2_table_fini(&table);
	return 0;
}

/**
  Function to return the yaml_node as an anchor, given a scalar value
  @param yctx - yaml2db context
  @param value - scalar value to be checked in the document
  @retval yaml_node_t pointer if successful, NULL otherwise
 */
static yaml_node_t *yaml2db_scalar_locate(struct c2_yaml2db_ctx *yctx,
		const char *value)
{
	yaml_node_t	*node;
	bool		 scalar_found = false;

	C2_PRE(yctx != NULL);

	for (node = yctx->yc_document.nodes.start;
			node < yctx->yc_document.nodes.top; node++) {
		if(node->type == YAML_SCALAR_NODE) {
			if (strcmp((char *)node->data.scalar.value,
					(char *) value) == 0){
				scalar_found = true;
				break;
			}
		}
	}

	if (scalar_found)
		return node;
	else
		return NULL;
}
/**
  Function to parse the yaml document
  @param yctx - yaml2db context
  @retval 0 if successful, -errno otherwise
 */
static int yaml2db_load_disk_conf(struct c2_yaml2db_ctx *yctx)
{
        int                      rc;
	yaml_node_t		*node;

        C2_PRE(yctx != NULL);

	node = yaml2db_scalar_locate(yctx, disk_str);
	if (node == NULL) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "yaml2db_scalar_locate", 0);
		return -EINVAL;
	}

	node++;
	rc = read_sequence(yctx, node);
	return rc;
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

	/* Load the information from yaml file to yaml_document, and check for
	   parsing errors internally */
	rc = yaml2db_doc_load(&yctx);
	if (rc != 0){
                C2_ADDB_ADD(&yctx.yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml2db_doc_load", 0);
		return rc;
	}

	/* Parse the disk configuration which is loaded in the context */
	rc = yaml2db_load_disk_conf(&yctx);
	if (rc != 0){
                C2_ADDB_ADD(&yctx.yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml2db_parse_disk_conf", 0);
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
