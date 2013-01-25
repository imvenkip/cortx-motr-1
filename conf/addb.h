/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rohan Puri <rohan_puri@xyratex.com>
 * Original creation date: 04-Dec-2012
 */
#pragma once
#ifndef __MERO_CONF_ADDB_H__
#define __MERO_CONF_ADDB_H__

#include "addb/addb.h"  /* M0_ADDB_CT, m0_addb_ctx */

/**
 * @addtogroup confd
 * @{
 */

/**
 * Conf module ADDB context types.
 * Do not change the numbering.
 */
enum {
	M0_ADDB_CTXID_CONF_MOD  = 300,
	M0_ADDB_CTXID_CONF_SERV = 301
};

M0_ADDB_CT(m0_addb_ct_conf_mod, M0_ADDB_CTXID_CONF_MOD);
M0_ADDB_CT(m0_addb_ct_conf_serv, M0_ADDB_CTXID_CONF_SERV, "hi", "low");

/** Conf ADDB posting locations. */
enum {
	M0_CONF_ADDB_LOC_CONFD_ALLOCATE = 10
};

extern struct m0_addb_ctx m0_conf_mod_ctx;

/** ADDB module initialiser. */
M0_INTERNAL int m0_conf_addb_init(void);

/** ADDB module finaliser. */
M0_INTERNAL void m0_conf_addb_fini(void);

/** @} confd */
#endif /* __MERO_CONF_ADDB_H__ */
