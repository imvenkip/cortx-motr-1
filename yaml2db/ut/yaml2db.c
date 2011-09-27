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

#include "colibri/init.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/misc.h"
#include "yaml2db/disk_conf_db.h"
#include "yaml2db/yaml2db.h"
#include "lib/ut.h"

/* Constant names and paths */
static const char *C_PATH = "./__conf_db";
static const char *c_name = "conf.yaml";
static const char *S_PATH = "./__conf_db_dirty_scanner_error";
static const char *s_name = "conf_dirty_scanner_error.yaml";
static const char *P_PATH = "./__conf_db_dirty_parser_error";
static const char *p_name = "conf_dirty_parser_error.yaml";
static const char *disk_str = "disks";
struct c2_yaml2db_ctx	 yctx;

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

static char *label_fields[]= { "LABEL1","LABEL2","LABEL3"};

static char *status_fields[] = {"ok","degraded","unresponsive"};

static char *setting_fields[] = {"use","ignore","decommission"};

/* Default number of records to be generated */
enum {
	REC_NR = 10,
};

/* Generate a configuration file */
int generate_conf_file(const char *c_name, int rec_nr)
{
	FILE *fp;
	int   cnt;
	int   index;
	char *str;

	C2_PRE(c_name != NULL);

	fp = fopen(c_name, "a");
	if (fp == NULL) {
		fprintf(stderr, "Failed to create configuration file\n");
		return -errno;
	}

	fprintf(fp,"%s:\n",disk_str);

	for (cnt = 0; cnt < rec_nr; ++cnt) {
		fprintf(fp,"  -");

		index = rand() % ARRAY_SIZE(label_fields);
		str = label_fields[index];
		fprintf(fp," %s : %s\n", disk_section_keys[0].ysk_key,str);

		index = rand() % ARRAY_SIZE(status_fields);
		str = status_fields[index];
		fprintf(fp,"    %s : %s\n", disk_section_keys[1].ysk_key,str);

		index = rand() % ARRAY_SIZE(setting_fields);
		str = setting_fields[index];
		fprintf(fp,"    %s : %s\n", disk_section_keys[2].ysk_key,str);
	}
	fclose(fp);

	return 0;
}

static int yaml2db_ut_init() 
{
	C2_SET0(&yctx);

	yctx.yc_type = C2_YAML2DB_CTX_PARSER;

	return 0;
}

void test_parse_and_load_success(void)
{
	int	rc;

	rc = generate_conf_file(c_name, REC_NR);
	C2_UT_ASSERT(rc == 0);

	yctx.yc_cname = c_name;
	yctx.yc_dpath = C_PATH;

	rc = yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	rc = yaml2db_doc_load(&yctx);
	C2_UT_ASSERT(rc == 0);

	rc = yaml2db_conf_load(&yctx, &disk_section, disk_str);
	C2_UT_ASSERT(rc == 0);

	yaml2db_fini(&yctx);
}

enum error_type {
	SCANNER_ERROR = 0,
	PARSER_ERROR,
	READER_ERROR
};

static int generate_dirty_conf_file(const char *c_name, enum error_type etype)
{
	FILE *fp;
	char  p = '%';

	C2_PRE(c_name != NULL);

	fp = fopen(c_name, "a");
	C2_UT_ASSERT(fp != NULL);

	switch(etype) {
	case(SCANNER_ERROR):	
		fprintf(fp,"&&&  ----");
		break;
	case(PARSER_ERROR):
		fprintf(fp,"%cYAML 1.2\n",p);
		fprintf(fp,"%cYAML 1.2\n",p);
		break;
	case(READER_ERROR):
		break;
	default:
		C2_IMPOSSIBLE("Invalid error type");
	}

	fclose(fp);

	return 0;
}

void test_parse_fail_scanner_error(void)
{
	int	rc;

	rc = generate_dirty_conf_file(s_name, SCANNER_ERROR);
	C2_UT_ASSERT(rc == 0);

	yctx.yc_cname = s_name;
	yctx.yc_dpath = S_PATH;

	rc = yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	printf("Testing failure path, expecting error message\n\n");
	rc = yaml2db_doc_load(&yctx);

	/* Used for pretty printing of CUnit output */
	printf("\n");

	yaml2db_fini(&yctx);
}

void test_parse_fail_parser_error(void)
{
	int	rc;

	rc = generate_dirty_conf_file(p_name, PARSER_ERROR);
	C2_UT_ASSERT(rc == 0);

	yctx.yc_cname = p_name;
	yctx.yc_dpath = P_PATH;

	rc = yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	printf("Testing failure path, expecting error message\n\n");
	rc = yaml2db_doc_load(&yctx);

	printf("\n");

	yaml2db_fini(&yctx);
}

const struct c2_test_suite yaml2db_ut = {
	.ts_name = "libyaml2db-ut",
	.ts_init = yaml2db_ut_init,
	.ts_fini = NULL, 
	.ts_tests = {
		{ "parse_load_success", test_parse_and_load_success },
		{ "parse_scanner_error", test_parse_fail_scanner_error },
		{ "parse_parser_error", test_parse_fail_parser_error },
		{ NULL, NULL }
	}
};

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
