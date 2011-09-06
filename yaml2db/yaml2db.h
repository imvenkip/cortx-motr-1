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

#ifndef __COLIBRI_YAML2DB_H__
#define __COLIBRI_YAML2DB_H__

#include "db/db.h"
#include "../yaml/include/yaml.h"

/**
   @defgroup yaml2db YAML to Database Parser

   @brief The yaml2db utility carries out following operations:
   - parsing the configuration file in yaml format on management server
   - storing the parsed information in database


   See <a href="https://docs.google.com/a/xyratex.com/document/d/1Y2FccWZFA9yWXJiC-kld0XUrexoindOpMiHEGkqc3Rc/edit?hl=en_US">DLD of Configuration (dev_enum) </a>
   for details on the design.

   @{

 */

/**
  yaml2db structure
*/
struct c2_yaml2db_ctx {
	/* YAML parser struct */
	yaml_parser_t		 yc_parser;
	/* YAML document structure */
	yaml_document_t		 yc_document;
	/* Root node of the yaml_document */
	yaml_node_t		 yc_root_node;
	/* Config file name */
	const char		*yc_cname;
	/* Database path */
	const char		*yc_dpath;
	/* Database environment */
	struct c2_dbenv		 yc_db;
	/* ADDB context for the context */
	struct c2_addb_ctx	 yc_addb;
	/* File pointer for YAML file */
	FILE			*yc_fp;
};

/**
  Enumeration of section types
 */
enum c2_yaml2db_sec_type {
	/* YAML sequence */
	C2_YAML_TYPE_SEQUENCE = 0,
	/* YAML mapping */
	C2_YAML_TYPE_MAPPING,
	/* Max section types */
	C2_YAML_TYPE_NR
};

/**
  Maximum number of keys in a section
 */
enum {
	C2_MAX_KEYS_IN_SECTION = 256,
};

/**
  Key structure for determining valid keys in a yaml2db section
 */
struct c2_yaml2db_section_key {
	/* Section key */
	const char	*ysk_key;
	/* Flag to determine if a key is mandatory or optional */
	bool		 ysk_mandatory;
};

/**
  yaml2db section
*/
struct c2_yaml2db_section {
	/* Name of the table in which this section is supposed to be stored */
	const char			 *ys_table_name;
	/* Table ops */
	const struct c2_table_ops	 *ys_table_ops;
	/* Type of section */
	enum c2_yaml2db_sec_type	  ys_section_type;
	/* Array of valid key structures */
	struct c2_yaml2db_section_key	  ys_valid_keys[C2_MAX_KEYS_IN_SECTION];
	/* Number of keys in the array */
	size_t				  ys_num_keys;
	/* Starting numeric value to be treated as database key for this
	   section in the table */
	int32_t				  ys_start_key;
};

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
