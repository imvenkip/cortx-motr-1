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
#  include "config.h"
#endif

#include "lib/misc.h"
#include "lib/ut.h"
#include "cfg/cfg.h"
#include "yaml2db/yaml2db.h"

/* Constant names and paths */
static const char f_path[] = "./__conf_db_failure";
static const char f_name[] = "conf_failure.yaml";
static const char f_err_fname[] = "conf_failure_error";
static const char mp_path[] = "./__conf_db_success_mp";
static const char mp_name[] = "conf_success_mp.yaml";
static const char ma_path[] = "./__conf_db_success_ma";
static const char ma_name[] = "conf_success_ma.yaml";
static const char s_path[] = "./__conf_db_dirty_scanner_error";
static const char s_name[] = "conf_dirty_scanner_error.yaml";
static const char s_err_fname[] = "conf_dirty_scanner_error";
static const char p_path[] = "./__conf_db_dirty_parser_error";
static const char p_name[] = "conf_dirty_parser_error.yaml";
static const char p_err_fname[] = "conf_dirty_parser_error";
static const char dev_str[] = "devices";
static const char parse_dump_fname[] = "parse.txt";
static const char emit_dump_fname[] = "emit.txt";

/* Global yaml2db context */
static struct c2_yaml2db_ctx yctx;

/* Static declaration of device section keys array */
static struct c2_yaml2db_section_key dev_section_keys[] = {
	/* Mandatory keys */
        [0] = {"label", true},
        [1] = {"interface", true},
        [2] = {"media", true},
        [3] = {"size", true},
        [4] = {"state", true},
	/* Non-mandatory keys */
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

static const char *interface_fields[] = {
        "ATA",
        "SATA",
        "SCSI",
        "SATA2",
        "SCSI2",
        "SAS",
        "SAS2"
};

static const char *media_fields[] = {
        "DISK",
        "SSD",
        "TAPE",
        "ROM",
};

/* Default number of records to be generated */
enum {
	REC_NR = 63,
};

/* Max string size */
enum {
        STR_SIZE_NR = 40,
};

/*
   Generate a configuration file
   Parameters:
   c_name - name of the file in which data has to be written
   rec_nr - number of device records to be generated
   skip_mandatory - true if mandatory fields should not be generated
   skip_optional - true if optional fields should not to generated
 */
static int generate_conf_file(const char *c_name, int rec_nr,
			      const bool skip_mandatory,
			      const bool skip_optional)
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

		if (!skip_optional) {
			/* set flags (optional) */
			fprintf(fp,"    %s : %d\n", dev_section_keys[5].ysk_key,
				rand() % 3);

			/* set filename (optional) */
			sprintf(str, "/dev/sda%d", cnt);
			fprintf(fp,"    %s : %s\n", dev_section_keys[6].ysk_key,
				str);

			/* set nodename (optional) */
			sprintf(str, "n%d", cnt);
			fprintf(fp,"    %s : %s\n", dev_section_keys[7].ysk_key,
				str);
		}
        }
        fclose(fp);

        return 0;
}

/*
   Do not generate mandatory fields in the yaml file and try to load that file.
   This should fail giving error message and each error message should also
   state which of the mandatory fields are missing
 */
static void mandatory_fields_absent(void)
{
	int                   rc;
        char                  str[STR_SIZE_NR];
	struct c2_ut_redirect redir;

	c2_stream_redirect(stderr, f_err_fname, &redir);

	/* Do not skip optional fields.
	   Skip mandatory fields and expect error */
	rc = generate_conf_file(f_name, REC_NR, true, false);
	C2_UT_ASSERT(rc == 0);

	C2_SET0(&yctx);

	yctx.yc_cname = f_name;
	yctx.yc_dpath = f_path;
	yctx.yc_type = C2_YAML2DB_CTX_PARSER;

	/* Reset any existing database */
	rc = c2_ut_db_reset(f_path);
	C2_UT_ASSERT(rc == 0);

	rc = c2_yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_yaml2db_doc_load(&yctx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_yaml2db_conf_load(&yctx, &dev_section, dev_str);
	C2_UT_ASSERT(rc == -EINVAL);

	rewind(stderr);
	C2_UT_ASSERT(fgets(str, STR_SIZE_NR, stderr) != NULL);
	C2_UT_ASSERT(strstr(str, "Error: Mandatory key not present") != NULL);

	c2_stream_restore(&redir);

	c2_yaml2db_fini(&yctx);
}

/*
   Do not generate optional fields. Parsing and loading should succeed.
 */
static void optional_fields_absent(void)
{
        int     rc;

	/* Do not skip mandatory fields. Skip optional fields */
        rc = generate_conf_file(ma_name, REC_NR, false, true);
        C2_UT_ASSERT(rc == 0);

	C2_SET0(&yctx);

        yctx.yc_cname = ma_name;
        yctx.yc_dpath = ma_path;
	yctx.yc_type = C2_YAML2DB_CTX_PARSER;

	/* Reset any existing database */
	rc = c2_ut_db_reset(ma_path);
	C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_init(&yctx);
        C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_doc_load(&yctx);
        C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_conf_load(&yctx, &dev_section, dev_str);
        C2_UT_ASSERT(rc == 0);

        c2_yaml2db_fini(&yctx);
}

/*
   Generate all mandatory as well as optional fields. Parsing and loading
   should succeed.
 */
static void optional_fields_present(void)
{
        int     rc;

	/* Do not skip mandatory as well as optional fields */
	rc = generate_conf_file(mp_name, REC_NR, false, false);
        C2_UT_ASSERT(rc == 0);

	C2_SET0(&yctx);

        yctx.yc_cname = mp_name;
        yctx.yc_dpath = mp_path;
	yctx.yc_dump_kv = true;
	yctx.yc_dump_fname = parse_dump_fname;
	yctx.yc_type = C2_YAML2DB_CTX_PARSER;

	/* Reset any existing database */
	rc = c2_ut_db_reset(mp_path);
	C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_init(&yctx);
        C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_doc_load(&yctx);
        C2_UT_ASSERT(rc == 0);

        rc = c2_yaml2db_conf_load(&yctx, &dev_section, dev_str);
        C2_UT_ASSERT(rc == 0);

        c2_yaml2db_fini(&yctx);
}

/*
   Emit the already existing database entries created in
   optional_fields_present(). Check it against the
   parsed version. Both of them should match.
 */
static void emit_verify(void)
{
	int  rc;
        char str[STR_SIZE_NR];

	C2_SET0(&yctx);

	yctx.yc_dpath = mp_path;
	yctx.yc_dump_kv = true;
	yctx.yc_dump_fname = emit_dump_fname;
	yctx.yc_type = C2_YAML2DB_CTX_EMITTER;

	rc = c2_yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_yaml2db_conf_emit(&yctx, &dev_section, dev_str);
	C2_UT_ASSERT(rc == 0);

	c2_yaml2db_fini(&yctx);

	/* Take diff of the dumps generated from parsing and emitting ops */
	sprintf(str, "diff %s %s", parse_dump_fname, emit_dump_fname);
	rc = system (str);
	C2_UT_ASSERT(rc == 0);
}

/* Parser error types */
enum error_type {
	SCANNER_ERROR = 0,
	PARSER_ERROR,
	READER_ERROR
};

static int generate_dirty_conf_file(const char *c_name,
				    const enum error_type etype)
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
	/* It is difficult to reproduce reader error, hence not used */
	case(READER_ERROR):
		break;
	default:
		C2_IMPOSSIBLE("Invalid error type");
	}

	fclose(fp);

	return 0;
}

/* Introduce a scanner error and check if the corresponding error is displayed
   by the code */
static void scanner_error_detect(void)
{
	int                   rc;
	char	              str[STR_SIZE_NR];
	struct c2_ut_redirect redir;

	c2_stream_redirect(stderr, s_err_fname, &redir);

	rc = generate_dirty_conf_file(s_name, SCANNER_ERROR);
	C2_UT_ASSERT(rc == 0);

	yctx.yc_cname = s_name;
	yctx.yc_dpath = s_path;

	/* Reset any existing database */
	rc = c2_ut_db_reset(s_path);
	C2_UT_ASSERT(rc == 0);

	rc = c2_yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_yaml2db_doc_load(&yctx);
	C2_UT_ASSERT(rc != 0);

	rewind(stderr);
	C2_UT_ASSERT(fgets(str, STR_SIZE_NR, stderr) != NULL);
	C2_UT_ASSERT(strstr(str, "Scanner error") != NULL);

	c2_stream_restore(&redir);

	c2_yaml2db_fini(&yctx);
}

/* Introduce a parser error and check if the corresponding error is displayed
   by the code */
static void parser_error_detect(void)
{
	int                   rc;
	char	              str[STR_SIZE_NR];
	struct c2_ut_redirect redir;

	c2_stream_redirect(stderr, p_err_fname, &redir);

	rc = generate_dirty_conf_file(p_name, PARSER_ERROR);
	C2_UT_ASSERT(rc == 0);

	yctx.yc_cname = p_name;
	yctx.yc_dpath = p_path;

	/* Reset any existing database */
	rc = c2_ut_db_reset(p_path);
	C2_UT_ASSERT(rc == 0);

	rc = c2_yaml2db_init(&yctx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_yaml2db_doc_load(&yctx);
	C2_UT_ASSERT(rc != 0);

	rewind(stderr);
	C2_UT_ASSERT(fgets(str, STR_SIZE_NR, stderr) != NULL);
	C2_UT_ASSERT(strstr(str, "Parser error") != NULL);

	c2_stream_restore(&redir);

	c2_yaml2db_fini(&yctx);
}

const struct c2_test_suite yaml2db_ut = {
	.ts_name = "yaml2db-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "detect-scanner-error", scanner_error_detect },
		{ "detect-parser-error", parser_error_detect },
		{ "mandatory-fields-absent", mandatory_fields_absent },
		{ "optional-fields-absent", optional_fields_absent },
		{ "optional-fields-present", optional_fields_present },
		{ "emit-and-verify", emit_verify },
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
