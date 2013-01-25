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
 * Original creation date: 07-Dec-2012
 */

#define M0_ADDB_CT_CREATE_DEFINITION
#include "conf/addb.h"

struct m0_addb_ctx m0_conf_mod_ctx;

M0_INTERNAL int m0_conf_addb_init()
{
	m0_addb_ctx_type_register(&m0_addb_ct_conf_mod);
	m0_addb_ctx_type_register(&m0_addb_ct_conf_serv);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_conf_mod_ctx, &m0_addb_ct_conf_mod,
			 &m0_addb_proc_ctx);
	return 0;
}

M0_INTERNAL void m0_conf_addb_fini(void)
{
        m0_addb_ctx_fini(&m0_conf_mod_ctx);
}

#undef M0_ADDB_CT_CREATE_DEFINITION
