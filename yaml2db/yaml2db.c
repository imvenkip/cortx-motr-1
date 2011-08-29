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

enum {
	DISK_MAPPING_START_KEY = 100,
};

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
  Check if the section structure is containing appropriate data
  @param ysec - section structure to be checked
  @retval true if necessary conditions satisfy, false otherwise
 */
static bool yaml2db_section_invariant(const struct c2_yaml2db_section *ysec)
{
	if (ysec == NULL)
		return false;

	if (ysec->ys_table_name == NULL)
		return false;

	if (ysec->ys_section_type >= C2_YAML_TYPE_NR)
		return false;

	if (ysec->ys_num_keys <= 0)
		return false;

	if (ysec->ys_start_key < 0)
		return false;

	return true;
}

/**
  Check if the context structure is containing appropriate data
  @param yctx - context structure to be checked
  @retval true if necessary conditions satisfy, false otherwise
 */
static bool yaml2db_context_invariant(const struct c2_yaml2db_ctx *yctx)
{
	if (yctx == NULL)
		return false;

	if (&yctx->yc_parser == NULL)
		return false;

	if (&yctx->yc_document == NULL)
		return false;

	if (&yctx->yc_root_node == NULL)
		return false;

	if (&yctx->yc_db == NULL)
		return false;

	if (&yctx->yc_addb == NULL)
		return false;

	return true;

}

/**
  Check if the key is a valid key for a given section
  @param key - key to be validated
  @param ysec - section structure against which the key needs to be validated
  @retval true if valid, false otherwise
 */
static bool validate_key_from_section(const yaml_char_t *key,
		struct c2_yaml2db_section *ysec)
{
	int cnt = 0;

        C2_PRE(yaml2db_section_invariant(ysec));
	C2_PRE(key != NULL);

	for (cnt = 0; cnt < ysec->ys_num_keys; cnt++)
		if (strcmp((char *)key,
			   (char *)ysec->ys_valid_keys[cnt]) == 0)
		       return true;
	return false;
}

/**
  Function to parse the yaml document
  @param yctx - yaml2db context
  @retval 0 if successful, -errno otherwise
 */
static int yaml2db_load_conf(struct c2_yaml2db_ctx *yctx,
		struct c2_yaml2db_section *ysec, const char *conf_param)
{
        int                      rc;
	yaml_node_t		*node;
	yaml_node_t		*s_node;
	yaml_node_t		*k_node;
	yaml_node_t		*v_node;
	yaml_node_item_t	*item;
	yaml_node_pair_t	*pair;
        struct c2_table		 table;
        struct c2_db_tx		 tx;
        struct c2_db_pair	 db_pair;
	int			 key = 0;

        C2_PRE(yaml2db_context_invariant(yctx));
        C2_PRE(yaml2db_section_invariant(ysec));

        /* Initialize the table */
        rc = c2_table_init(&table, &yctx->yc_db, ysec->ys_table_name,
                        0, ysec->ys_table_ops);
        if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "c2_table_init", 0);
                return rc;
	}

        /* Initialize the database transaction */
        rc = c2_db_tx_init(&tx, &yctx->yc_db, 0);
        if (rc != 0) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "c2_db_tx_init", 0);
                c2_table_fini(&table);
                return rc;
        }

	node = yaml2db_scalar_locate(yctx, conf_param);
	if (node == NULL) {
                C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
				yaml2db_func_fail, "yaml2db_scalar_locate", 0);
                c2_table_fini(&table);
		return -EINVAL;
	}

	node++;
	key = ysec->ys_start_key;
	c2_yaml2db_sequence_for_each(yctx, node, item, s_node) {
		c2_yaml2db_mapping_for_each (yctx, s_node, pair,
				k_node, v_node) {
			if(!validate_key_from_section(k_node->data.
						scalar.value, ysec))
				continue;
			c2_db_pair_setup(&db_pair, &table, &key, sizeof key,
					v_node->data.scalar.value,
					sizeof(v_node->data.scalar.value));
			rc = c2_table_insert(&tx, &db_pair);
			if (rc != 0) {
				C2_ADDB_ADD(&yctx->yc_addb, &yaml2db_addb_loc,
						yaml2db_func_fail,
						"c2_table_insert", 0);
				c2_db_pair_release(&db_pair);
				c2_db_pair_fini(&db_pair);
				c2_table_fini(&table);
				return rc;
			}
			c2_db_pair_release(&db_pair);
			c2_db_pair_fini(&db_pair);
			key++;
		}
	}
	c2_db_tx_commit(&tx);
	c2_table_fini(&table);
	return rc;
}

/**
  Load the disk section
  @param ysec - section structure to be loaded
  @retval 0 for success, -errno otherwise
 */
int yaml2db_load_disk_section(struct c2_yaml2db_section *ysec)
{
	ysec->ys_num_keys = 0;
	ysec->ys_table_name = disk_table;
	if (ysec->ys_table_name == NULL)
		return -EINVAL;
	ysec->ys_table_ops = &disk_table_ops; 
	ysec->ys_start_key = DISK_MAPPING_START_KEY;
	ysec->ys_section_type = C2_YAML_TYPE_MAPPING;


	return 0;
}

/**
  Main function for yaml2db
*/
int main(int argc, char *argv[])
{
	int			 	 rc = 0;
	struct c2_yaml2db_ctx	 	 yctx;
	struct c2_yaml2db_section	 ysec;
	const char			*c_name = NULL;
	const char			*d_path = NULL;

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

	if (rc != 0) {
		c2_fini();
		return rc;
	}

	/* Config file has to be specified as a command line option */
	if (c_name == NULL) {
		printf("Error: Config file path not specified\n");
		c2_fini();
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
	if (rc != 0) {
                C2_ADDB_ADD(&yctx.yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml2db_init", 0);
		c2_fini();
		return rc;
	}

	/* Load the information from yaml file to yaml_document, and check for
	   parsing errors internally */
	rc = yaml2db_doc_load(&yctx);
	if (rc != 0) {
                C2_ADDB_ADD(&yctx.yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml2db_doc_load", 0);
		goto cleanup;
	}

	/* Load the parameter specific section table */
	rc = yaml2db_load_disk_section(&ysec);
	if (rc != 0) {
                C2_ADDB_ADD(&yctx.yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml2db_load_disk_section", 0);
		goto cleanup;
	}


	/* Parse the disk configuration which is loaded in the context */
	rc = yaml2db_load_conf(&yctx, &ysec, disk_str);
	if (rc != 0) {
                C2_ADDB_ADD(&yctx.yc_addb, &yaml2db_addb_loc, yaml2db_func_fail,
                                "yaml2db_parse_disk_conf", 0);
	}

	/* Finalize the parser and database environment */
cleanup:
	yaml2db_fini(&yctx);
	c2_fini();

	return rc;
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
