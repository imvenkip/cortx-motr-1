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

#include "lib/misc.h"
#include "yaml2db/yaml2db.h"
#include "lib/ut.h"
#include "cfg/cfg.h"

/* Constant names and paths */
static const char *FAILURE_PATH = "./__conf_db_failure";
static const char *failure_name = "conf_failure.yaml";
static const char *success_path_mandatory_present = "./__conf_db_success_mp";
static const char *success_name_mandatory_present = "conf_success_mp.yaml";
static const char *success_path_mandatory_absent = "./__conf_db_success_ma";
static const char *success_name_mandatory_absent = "conf_success_ma.yaml";
static const char *S_PATH = "./__conf_db_dirty_scanner_error";
static const char *s_name = "conf_dirty_scanner_error.yaml";
static const char *P_PATH = "./__conf_db_dirty_parser_error";
static const char *p_name = "conf_dirty_parser_error.yaml";
static const char *dev_str = "devices";
struct c2_yaml2db_ctx	 yctx;

/* Static declaration of device section keys array */
static struct c2_yaml2db_section_key dev_section_keys[] = {
        [0] = {"label", true},
        [1] = {"interface", true},
        [2] = {"media", true},
        [3] = {"size", true},
        [4] = {"state", true},
        [5] = {"flags", false},
        [6] = {"filename", false},
        [7] = {"nodename", false},
};

/* Static declaration of device section table */
static struct c2_yaml2db_section dev_section = {
        .ys_table_name = "dev_table",
        .ys_table_ops = &c2_cfg_storage_device_table_ops,
        .ys_section_type = C2_YAML_TYPE_MAPPING,
        .ys_num_keys = ARRAY_SIZE(dev_section_keys),
        .ys_valid_keys = dev_section_keys,
        .ys_key_str = "label",
        .ys_ops = &c2_yaml2db_dev_section_ops,
};

static char *interface_fields[] = {
        "C2_CFG_DEVICE_INTERFACE_ATA",
        "C2_CFG_DEVICE_INTERFACE_SATA",
        "C2_CFG_DEVICE_INTERFACE_SCSI",
        "C2_CFG_DEVICE_INTERFACE_SATA2",
        "C2_CFG_DEVICE_INTERFACE_SCSI2",
        "C2_CFG_DEVICE_INTERFACE_SAS",
        "C2_CFG_DEVICE_INTERFACE_SAS2"
};

static char *media_fields[] = {
        "C2_CFG_DEVICE_MEDIA_DISK",
        "C2_CFG_DEVICE_MEDIA_SSD",
        "C2_CFG_DEVICE_MEDIA_TAPE",
        "C2_CFG_DEVICE_MEDIA_ROM",
};


/* Default number of records to be generated */
enum {
	REC_NR = 10,
};

enum {
        STR_SIZE_NR = 40,
};

/* Generate a configuration file */
int generate_conf_file(const char *c_name, int rec_nr, bool skip_mandatory,
		       bool skip_non_mandatory)
{
        FILE    *fp;
        int      cnt;
        int      index;
        char     str[STR_SIZE_NR];

        C2_PRE(c_name != NULL);

        fp = fopen(c_name, "a");
        if (fp == NULL) {
                fprintf(stderr, "Failed to create configuration file\n");
                return -errno;
        }

        fprintf(fp,"%s:\n",dev_str);
        if (rec_nr == 0)
                rec_nr = REC_NR;

        for (cnt = 0; cnt < rec_nr; ++cnt) {
                fprintf(fp,"  -");
		/* set label (mandatory) */
                sprintf(str, "LABEL%05d", cnt);
                fprintf(fp," %s : %s\n", dev_section_keys[0].ysk_key,str);

                if (!skip_mandatory) {
			/* set interface (mandatory) */
			index = rand() % ARRAY_SIZE(interface_fields);
			strcpy(str, interface_fields[index]);
			fprintf(fp,"    %s : %s\n", dev_section_keys[1].ysk_key,
				str);

			/* set media (mandatory) */
			index = rand() % ARRAY_SIZE(media_fields);
			strcpy(str, media_fields[index]);
			fprintf(fp,"    %s : %s\n", dev_section_keys[2].ysk_key,
				str);

			/* set size (mandatory) */
			fprintf(fp,"    %s : %d\n", dev_section_keys[3].ysk_key,
				rand() % 10000);

			/* set state (mandatory) */
			fprintf(fp,"    %s : %d\n", dev_section_keys[4].ysk_key,
				rand() % 2);
		}

		if (!skip_non_mandatory) {
			/* set flags (non-mandatory) */
			fprintf(fp,"    %s : %d\n", dev_section_keys[5].ysk_key,
				rand() % 3);

			/* set filename (non-mandatory) */
			sprintf(str, "/dev/sda%d", cnt);
			fprintf(fp,"    %s : %s\n", dev_section_keys[6].ysk_key,
				str);

			/* set nodename (non-mandatory) */
			sprintf(str, "n%d", cnt);
			fprintf(fp,"    %s : %s\n", dev_section_keys[7].ysk_key,
				str);
		}
        }
        fclose(fp);

        return 0;
}

static int yaml2db_ut_init() 
{
	C2_SET0(&yctx);

	yctx.yc_type = C2_YAML2DB_CTX_PARSER;
	c2_addb_choose_default_level(AEL_NONE);

	return 0;
}

void test_parse_and_load_failure(void)
{
	int	rc;

	/* Do not skip non-mandatory fields.
	   Skip mandatory fields and expect error */
	rc = generate_conf_file(failure_name, REC_NR, true, false);
	C2_UT_ASSERT(rc == 0);

	yctx.yc_cname = failure_name;
	yctx.yc_dpath = FAILURE_PATH;

	rc = c2_yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_yaml2db_doc_load(&yctx);
	C2_UT_ASSERT(rc == 0);

	printf("Testing failure path, expecting error message\n\n");
	rc = c2_yaml2db_conf_load(&yctx, &dev_section, dev_str);
	C2_UT_ASSERT(rc == -EINVAL);

	c2_yaml2db_fini(&yctx);
}

void test_parse_and_load_success_non_mandatory_absent(void)
{
        int     rc;

	/* Do not skip mandatory fields. Skip non-mandatory fields */
        rc = generate_conf_file(success_name_mandatory_absent, REC_NR,
				false, true);
        C2_UT_ASSERT(rc == 0);

        yctx.yc_cname = success_name_mandatory_absent;
        yctx.yc_dpath = success_path_mandatory_absent;

        rc = c2_yaml2db_init(&yctx);
        C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_doc_load(&yctx);
        C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_conf_load(&yctx, &dev_section, dev_str);
        C2_UT_ASSERT(rc == 0);

        c2_yaml2db_fini(&yctx);
}

void test_parse_and_load_success_non_mandatory_present(void)
{                       
        int     rc;     
        
	/* Do not skip mandatory as well as non-mandatory fields */
	rc = generate_conf_file(success_name_mandatory_present, REC_NR,
				false, false);
        C2_UT_ASSERT(rc == 0);
        
        yctx.yc_cname = success_name_mandatory_present;
        yctx.yc_dpath = success_path_mandatory_present;
        
        rc = c2_yaml2db_init(&yctx);
        C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_doc_load(&yctx);
        C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_conf_load(&yctx, &dev_section, dev_str);
        C2_UT_ASSERT(rc == 0);

        c2_yaml2db_fini(&yctx);
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

	rc = c2_yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	printf("Testing failure path, expecting error message\n\n");
	rc = c2_yaml2db_doc_load(&yctx);

	/* Used for pretty printing of CUnit output */
	printf("\n");

	c2_yaml2db_fini(&yctx);
}

void test_parse_fail_parser_error(void)
{
	int	rc;

	rc = generate_dirty_conf_file(p_name, PARSER_ERROR);
	C2_UT_ASSERT(rc == 0);

	yctx.yc_cname = p_name;
	yctx.yc_dpath = P_PATH;

	rc = c2_yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	printf("Testing failure path, expecting error message\n\n");
	rc = c2_yaml2db_doc_load(&yctx);

	printf("\n");

	c2_yaml2db_fini(&yctx);
}

const struct c2_test_suite yaml2db_ut = {
	.ts_name = "libyaml2db-ut",
	.ts_init = yaml2db_ut_init,
	.ts_fini = NULL, 
	.ts_tests = {
		{ "mandatory-key-absence", test_parse_and_load_failure },
		{ "parse-scanner-error", test_parse_fail_scanner_error },
		{ "parse-parser-error", test_parse_fail_parser_error },
		{ "parse-load-success-non-mandatory-absent",
			test_parse_and_load_success_non_mandatory_absent },
		{ "parse-load-success-non-mandatory-present",
			test_parse_and_load_success_non_mandatory_present },
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
