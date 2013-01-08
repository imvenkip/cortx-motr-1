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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 12/18/2012
 */

#pragma once

#ifndef __MERO_YAML2DB_YAML2DB_ADDB_H__
#define __MERO_YAML2DB_YAML2DB_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup yaml2db
   @{
 */

/*
 ******************************************************************************
 * Yaml2db ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_YAML2DB_MOD = 700,
};

M0_ADDB_CT(m0_addb_ct_yaml2db_mod, M0_ADDB_CTXID_YAML2DB_MOD);

/*
 ******************************************************************************
 * Yaml2db ADDB posting locations.
 ******************************************************************************
 */
enum {
	M0_YAML2DB_ADDB_LOC_CONF_EMIT_1 = 20,
	M0_YAML2DB_ADDB_LOC_CONF_EMIT_2 = 21,
	M0_YAML2DB_ADDB_LOC_CONF_EMIT_3 = 22,
	M0_YAML2DB_ADDB_LOC_CONF_EMIT_4 = 23,
	M0_YAML2DB_ADDB_LOC_CONF_EMIT_5 = 24,
	M0_YAML2DB_ADDB_LOC_CONF_EMIT_6 = 25,
	M0_YAML2DB_ADDB_LOC_CONF_LOAD_1 = 40,
	M0_YAML2DB_ADDB_LOC_CONF_LOAD_2 = 41,
	M0_YAML2DB_ADDB_LOC_CONF_LOAD_3 = 42,
	M0_YAML2DB_ADDB_LOC_CONF_LOAD_4 = 43,
	M0_YAML2DB_ADDB_LOC_CONF_LOAD_5 = 44,
	M0_YAML2DB_ADDB_LOC_CONF_LOAD_6 = 45,
	M0_YAML2DB_ADDB_LOC_DOC_LOAD_1  = 60,
	M0_YAML2DB_ADDB_LOC_DOC_LOAD_2  = 61,
	M0_YAML2DB_ADDB_LOC_INIT_1      = 80,
	M0_YAML2DB_ADDB_LOC_INIT_2      = 81,
	M0_YAML2DB_ADDB_LOC_INIT_3      = 82,
};

/** @} */ /* end of yaml2db group */

#endif /* __MERO_YAML2DB_YAML2DB_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
