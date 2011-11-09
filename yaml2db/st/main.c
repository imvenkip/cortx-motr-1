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
#include "yaml2db/yaml2db.h"
#include "cfg/cfg.h"

/* Constant names and paths */
static const char *D_PATH = "./__config_db";
static const char *dev_str = "devices";

/* Static declaration of device section keys array */
static struct c2_yaml2db_section_key dev_section_keys[] = {
	[0] = {"label", true},
	[1] = {"interface", true},
	[2] = {"media", true},
	[3] = {"size", true},
	[4] = {"state", true},
	[5] = {"flags", true},
	[6] = {"filename", true},
	[7] = {"nodename", true},
};

/* Section op to populate the device key */
void device_key_populate(void *key, const char *val_str)
{
	struct c2_cfg_storage_device__key *dev_key = key;

	strcpy (dev_key->csd_uuid.cu_uuid, val_str);
}

/* Section op to populate the device value*/
void device_val_populate (struct c2_yaml2db_section *ysec, void *val,
			 const char *key_str, const char *val_str)
{
	struct c2_cfg_storage_device__val *dev_val = val;

	if (strcmp(key_str,"interface") == 0){
		if (strcmp(val_str, "C2_CFG_DEVICE_INTERFACE_ATA") == 0)
			dev_val->csd_type = C2_CFG_DEVICE_INTERFACE_ATA;
		if (strcmp(val_str, "C2_CFG_DEVICE_INTERFACE_SATA") == 0)
			dev_val->csd_type = C2_CFG_DEVICE_INTERFACE_SATA;
		if (strcmp(val_str, "C2_CFG_DEVICE_INTERFACE_SCSI") == 0)
			dev_val->csd_type = C2_CFG_DEVICE_INTERFACE_SCSI;
		if (strcmp(val_str, "C2_CFG_DEVICE_INTERFACE_SATA2") == 0)
			dev_val->csd_type = C2_CFG_DEVICE_INTERFACE_SATA2;
		if (strcmp(val_str, "C2_CFG_DEVICE_INTERFACE_SCSI2") == 0)
			dev_val->csd_type = C2_CFG_DEVICE_INTERFACE_SCSI2;
		if (strcmp(val_str, "C2_CFG_DEVICE_INTERFACE_SAS") == 0)
			dev_val->csd_type = C2_CFG_DEVICE_INTERFACE_SAS;
		if (strcmp(val_str, "C2_CFG_DEVICE_INTERFACE_SAS2") == 0)
			dev_val->csd_type = C2_CFG_DEVICE_INTERFACE_SAS2;
		return;
	}
	if (strcmp(key_str,"media") == 0){
		if (strcmp(val_str, "C2_CFG_DEVICE_MEDIA_DISK") == 0)
			dev_val->csd_media = C2_CFG_DEVICE_MEDIA_DISK;
		if (strcmp(val_str, "C2_CFG_DEVICE_MEDIA_SSD") == 0)
			dev_val->csd_media = C2_CFG_DEVICE_MEDIA_SSD;
		if (strcmp(val_str, "C2_CFG_DEVICE_MEDIA_TAPE") == 0)
			dev_val->csd_media = C2_CFG_DEVICE_MEDIA_TAPE;
		if (strcmp(val_str, "C2_CFG_DEVICE_MEDIA_ROM") == 0)
			dev_val->csd_media = C2_CFG_DEVICE_MEDIA_ROM;
		return;
	}
	if (strcmp(key_str,"size") == 0){
		sscanf(val_str, "%lu", &dev_val->csd_size);
		return;
	}
	if (strcmp(key_str,"state") == 0){
		sscanf(val_str, "%lu", &dev_val->csd_last_state);
		return;
	}
	if (strcmp(key_str,"flags") == 0){
		sscanf(val_str, "%lu", &dev_val->csd_flags);
		return;
	}
	if (strcmp(key_str,"filename") == 0){
		sscanf(val_str, "%s", dev_val->csd_filename);
		return;
	}
	if (strcmp(key_str,"nodename") == 0){
		sscanf(val_str, "%s", dev_val->csd_nodename);
		return;
	}
}

/* yaml2db section op to dump the key-value data to a file */
void device_key_val_dump(FILE *fp, void *key, void *val)
{
	struct c2_cfg_storage_device__key *dev_key = key;
	struct c2_cfg_storage_device__val *dev_val = val;

	C2_PRE(fp != NULL);
	C2_PRE(key != NULL);
	C2_PRE(val != NULL);

	fprintf(fp, "%s \t %u:%u:%lu:%lu:%lu:%s:%s\n",
		dev_key->csd_uuid.cu_uuid,
		dev_val->csd_type, dev_val->csd_media, dev_val->csd_size,
		dev_val->csd_last_state, dev_val->csd_flags,
		dev_val->csd_filename, dev_val->csd_nodename);
}

/* Section ops */
static struct c2_yaml2db_section_ops dev_section_ops = {
	.so_key_populate = device_key_populate,
	.so_val_populate = device_val_populate,
	.so_key_val_dump = device_key_val_dump,
};

/* Static declaration of device section table */
static struct c2_yaml2db_section dev_section = {
	.ys_table_name = "dev_table",
	.ys_table_ops = &c2_cfg_storage_device_table_ops,
	.ys_section_type = C2_YAML_TYPE_MAPPING,
	.ys_num_keys = ARRAY_SIZE(dev_section_keys),
	.ys_valid_keys = dev_section_keys,
	.ys_key_str = "label",
	.ys_ops = &dev_section_ops,
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
	REC_NR = 1,
};

/* Max string size */
enum {
	STR_SIZE_NR = 40,
};

/* Generate a configuration file */
int generate_conf_file(const char *c_name, int rec_nr)
{
	FILE	*fp;
	int	 cnt;
	int	 index;
	char	 str[STR_SIZE_NR];

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

		sprintf(str, "LABEL%05d", cnt);
		fprintf(fp," %s : %s\n", dev_section_keys[0].ysk_key,str);

		index = rand() % ARRAY_SIZE(interface_fields);
		strcpy(str, interface_fields[index]);
		fprintf(fp,"    %s : %s\n", dev_section_keys[1].ysk_key,str);

		index = rand() % ARRAY_SIZE(media_fields);
		strcpy(str, media_fields[index]);
		fprintf(fp,"    %s : %s\n", dev_section_keys[2].ysk_key,str);

		fprintf(fp,"    %s : %d\n", dev_section_keys[3].ysk_key,rand());
		fprintf(fp,"    %s : %d\n", dev_section_keys[4].ysk_key,
			rand() % 2);
		fprintf(fp,"    %s : %d\n", dev_section_keys[5].ysk_key,
			rand() % 3);
		sprintf(str, "/dev/sda%d", cnt);
		fprintf(fp,"    %s : %s\n", dev_section_keys[6].ysk_key,str);
		sprintf(str, "n%d", cnt);
		fprintf(fp,"    %s : %s\n", dev_section_keys[7].ysk_key,str);
	}
	fclose(fp);

	return 0;
}

/**
  Main function for yaml2db
*/
int main(int argc, char *argv[])
{
	int			 rc = 0;
	int			 rec_nr = 0;
	bool			 emitter = false;
	bool			 dump = false;
	bool			 generate = false;
	const char		*c_name = NULL;
	const char		*dump_fname = NULL;
	const char		*d_path = NULL;
	struct c2_yaml2db_ctx	 yctx;

	/* Global c2_init */
	rc = c2_init();
	if (rc != 0)
		return rc;

	C2_SET0(&yctx);

	/* Parse command line options */
	rc = C2_GETOPTS("yaml2db", argc, argv,
		C2_STRINGARG('b', "path of database directory",
			LAMBDA(void, (const char *str) {d_path = str; })),
		C2_STRINGARG('c', "config file in yaml format",
			LAMBDA(void, (const char *str) {c_name = str; })),
		C2_FLAGARG('d', "dump the key value contents", &dump),
		C2_STRINGARG('f', "dump file name",
			LAMBDA(void, (const char *str) {dump_fname = str; })),
		C2_FLAGARG('g', "generate yaml config file", &generate),
		C2_FORMATARG('n', "no. of records to be create", "%i", &rec_nr),
		C2_FLAGARG('e', "emitter mode", &emitter));

	if (rc != 0)
		goto cleanup;

	/* Config file has to be specified as a command line option */
	if (!emitter && c_name == NULL) {
		fprintf(stderr, "Error: Config file path not specified\n");
		rc = -EINVAL;
		goto cleanup;
	}
	yctx.yc_cname = c_name;

	/* If generate flag is set, generate a yaml config file that can be
	   used for testing */
	if (generate) {
	       generate_conf_file(c_name, rec_nr);
	       return 0;
	}

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

	if (dump) {
		yctx.yc_dump_kv = true;
		yctx.yc_dump_fname = dump_fname;
	}

	/* Initialize the parser and database environment */
	rc = c2_yaml2db_init(&yctx);
	if (rc != 0) {
		fprintf(stderr, "Error: yaml2db initialization failed \n");
		goto cleanup;
	}

	if (!emitter) {
		/* Load the information from yaml file to yaml_document,
		   and check for parsing errors internally */
		rc = c2_yaml2db_doc_load(&yctx);
		if (rc != 0) {
			fprintf(stderr, "Error: document loading failed \n");
			goto cleanup_parser_db;
		}

		/* Parse the dev configuration that is loaded in the context */
		rc = c2_yaml2db_conf_load(&yctx, &dev_section, dev_str);
		if (rc != 0)
			fprintf(stderr, "Error: config loading failed \n");

	} else {
		rc = c2_yaml2db_conf_emit(&yctx, &dev_section, dev_str);
		if (rc != 0)
			fprintf(stderr, "Error: config emitting failed \n");
	}

cleanup_parser_db:
	c2_yaml2db_fini(&yctx);

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
