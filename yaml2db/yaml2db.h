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
  yaml2db structure
*/
struct c2_yaml2db_ctx {
	/* YAML parser struct */
	yaml_parser_t		 yc_parser;
	/* YAML event structure */
	yaml_event_t		 yc_event;
	/* YAML document structure */
	yaml_document_t		 yc_document;
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
