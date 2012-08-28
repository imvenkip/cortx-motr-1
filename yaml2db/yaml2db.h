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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 08/13/2011
 */

#pragma once

#ifndef __COLIBRI_YAML2DB_H__
#define __COLIBRI_YAML2DB_H__

#include "db/db.h"
#include "yaml.h"

/**
   @defgroup yaml2db YAML to Database Parser

   @brief The yaml2db utility carries out following operations:
   - parsing the configuration file in yaml format on management server
   - storing the parsed information in database


   See <a href="https://docs.google.com/a/xyratex.com/document/d/1Y2FccWZFA9yWXJiC-kld0XUrexoindOpMiHEGkqc3Rc/edit?hl=en_US">DLD of Configuration (dev_enum) </a>
   for details on the design.

   A typical yaml conf file will look like the following.
   (The combinations of entries are only for illustration, covering all
    possible values. They may or may not be valid in reality.
    @see c2_cfg_storage_device__val)
   @verbatim
    devices:
      - label     : LABEL1
        interface : ATA
	media     : DISK
	size      : 77631820
	state     : 1
	flags     : 0
	filename  : /dev/sda
	nodename  : n0
      - label     : LABEL2
        interface : SATA
	media     : SSD
	size      : 281910680
	state     : 0
	flags     : 1
	filename  : /dev/sda1
	nodename  : n10
      - label     : LABEL3
        interface : SCSI
	media     : TAPE
	size      : 124321
	state     : 0
	flags     : 2
	filename  : /dev/pt0
	nodename  : n100
      - label     : LABEL4
        interface : SATA2
	media     : ROM
	size      : 98124321
	state     : 0
	flags     : 2
	filename  : /dev/pcd0
	nodename  : n200
      - label     : LABEL5
        interface : SCSI2
	media     : DISK
	size      : 9860772073
	state     : 1
	flags     : 1
	filename  : /dev/sda2
	nodename  : n200
      - label     : LABEL6
        interface : SAS
	media     : DISK
	size      : 68926892
	state     : 0
	flags     : 1
	filename  : /dev/sda3
	nodename  : n200
      - label     : LABEL7
        interface : SAS2
	media     : DISK
	size      : 27232723
	state     : 1
	flags     : 0
	filename  : /dev/sdb
	nodename  : n100
   @endverbatim

   @{

 */

/**
  Enumeration of yaml2db context type
 */
enum c2_yaml2db_ctx_type {
	/** Parser */
	C2_YAML2DB_CTX_PARSER = 0,
	/** Emitter */
	C2_YAML2DB_CTX_EMITTER,
	/** Maximum number of context types */
	C2_YAML2DB_CTX_NR
};

/**
  yaml2db structure
 */
struct c2_yaml2db_ctx {
	/** YAML parser struct */
	yaml_parser_t			 yc_parser;
	/** Enumeration to decide whether the context belongs
	   to parser or emitter */
	enum c2_yaml2db_ctx_type	 yc_type;
	/** YAML document structure */
	yaml_document_t			 yc_document;
	/** Root node of the yaml_document */
	yaml_node_t			 yc_root_node;
	/** Config file name */
	const char			*yc_cname;
	/** Database path */
	const char			*yc_dpath;
	/** Database environment */
	struct c2_dbenv			 yc_db;
	/** Flag indicating if the database environment has been established */
	bool				 yc_db_init;
	/** ADDB context for the context */
	struct c2_addb_ctx		 yc_addb;
	/** File pointer for YAML file */
	FILE				*yc_fp;
	/** Flag indicating whether to dump the key-value pair to a file */
	bool				 yc_dump_kv;
	/** Dump file name */
	const char			*yc_dump_fname;
	/** File pointer for dump file */
	FILE				*yc_dp;
};

/**
  Enumeration of section types
 */
enum c2_yaml2db_sec_type {
	/** YAML sequence */
	C2_YAML_TYPE_SEQUENCE = 0,
	/** YAML mapping */
	C2_YAML_TYPE_MAPPING,
	/** Max section types */
	C2_YAML_TYPE_NR
};

/**
  Key structure for determining valid keys in a yaml2db section
 */
struct c2_yaml2db_section_key {
	/** Section key */
	const char	*ysk_key;
	/** Flag to determine if a key is mandatory or optional */
	bool		 ysk_mandatory;
};

/**
  yaml2db section
 */
struct c2_yaml2db_section {
	/** Name of the table in which this section is supposed to be stored */
	const char			 *ys_table_name;
	/** Table ops */
	const struct c2_table_ops	 *ys_table_ops;
	/** Type of section */
	enum c2_yaml2db_sec_type	  ys_section_type;
	/** Array of valid key structures */
	struct c2_yaml2db_section_key	 *ys_valid_keys;
	/** Number of keys in the array */
	size_t				  ys_num_keys;
	/** String representing the key for the record in yaml file */
	const char			 *ys_key_str;
	/** section ops */
	const struct c2_yaml2db_section_ops    *ys_ops;
};

/**
  Section ops
 */
struct c2_yaml2db_section_ops {
	/**
	  Populate the key for the section.
	  @param key - pointer to the in memory record key. Section specific
	  routine should appropriately cast the key and set it.
	  @param val_str - value string that has been parsed from the yaml file,
	  to be used to populate the key structure entry
	 */
	void (*so_key_populate) (void *key, const char *val_str);
	/**
	  Populate the value for the section.
	  @param ysec - pointer to yaml2db section
	  @param key - pointer to the in memory record value
	  @param key_str - key string that has been parsed from the yaml file,
	  to be mapped against corresponding entry in the value structure.
	  @param val_str - value string that has been parsed from the yaml file,
	  to be used to populate the value structure entry
	  @retval - 0 for success, -errno otherwise
	 */
	int (*so_val_populate) (struct c2_yaml2db_section *ysec, void *val,
				const char *key_str, const char *val_str);
	/**
	  Dump the key and value for a record to the file having file
	  descriptor fp.
	  @param fp - FILE pointer to file in which data has to be dumped
	  @param key - pointer to the key structure. Section specific dump
	  routine should appropriately cast the key and then use it.
	  @param val- pointer to the value structure. Section specific dump
	  routine should appropriately cast the value and then use it.
	 */
	void (*so_key_val_dump) (FILE *fp, void *key, void *val);
};

extern const struct c2_yaml2db_section_ops c2_yaml2db_dev_section_ops;

/**
  Iterates over a yaml sequence
  @param ctx - yaml2db context
  @param node - starting node of the sequence
  @param item - yaml_node_item_t pointer
  @param s_node - sequence node at index pointed by item
 */
#define c2_yaml2db_sequence_for_each(ctx, node, item, s_node) \
	for (item = (node)->data.sequence.items.start; \
	     item < (node)->data.sequence.items.top && \
	     (s_node = yaml_document_get_node(&(ctx)->yc_document, *item)); \
	     item++)

/**
  Iterates over a yaml mapping
  @param ctx - yaml2db context
  @param node - starting node of the sequence
  @param pair - yaml_node_pair_t pointer
  @param k_node - mapping node at index pointed by mapping pair key
  @param v_node - mapping node at index pointed by mapping pair value
 */
#define c2_yaml2db_mapping_for_each(ctx, node, pair, k_node, v_node) \
	for (pair = (node)->data.mapping.pairs.start; \
	     pair < (node)->data.mapping.pairs.top && \
	     (k_node = yaml_document_get_node(&(ctx)->yc_document, \
		     pair->key)) &&\
	     (v_node = yaml_document_get_node(&(ctx)->yc_document, \
		     pair->value)); \
	     pair++)

/**
  Init function, which initializes the parser and sets input file
  @param yctx - yaml2db context
  @retval 0 if success, -errno otherwise
 */
int c2_yaml2db_init(struct c2_yaml2db_ctx *yctx);

/**
  Fini function, which finalizes the parser and finies the db
  @param yctx - yaml2db context
 */
void c2_yaml2db_fini(struct c2_yaml2db_ctx *yctx);

/**
  Function to load the yaml document
  @param yctx - yaml2db context
  @retval 0 if successful, -errno otherwise
 */
int c2_yaml2db_doc_load(struct c2_yaml2db_ctx *yctx);

/**
  Function to parse the yaml document and load its corresponding entries
  into the database. Any existing entries are replaced.
  @param yctx - yaml2db context
  @param ysec - section context corrsponding to the given parameter
  @param conf_param - parameter for which configuration has to be loaded
  @retval 0 if successful, -errno otherwise
 */
int c2_yaml2db_conf_load(struct c2_yaml2db_ctx *yctx,
			 struct c2_yaml2db_section *ysec,
			 const char *conf_param);

/**
  Function to emit the yaml document
  @param yctx - yaml2db context
  @param ysec - section context corrsponding to the given parameter
  @param conf_param - parameter for which configuration has to be emitted
  @retval 0 if successful, -errno otherwise
 */
int c2_yaml2db_conf_emit(struct c2_yaml2db_ctx *yctx,
			 const struct c2_yaml2db_section *ysec,
			 const char *conf_param);

/** @} end of yaml2db group */

#endif /* __COLIBRI_YAML2DB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
