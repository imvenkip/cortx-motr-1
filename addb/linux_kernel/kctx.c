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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation: 10/08/2012
 */

/* This file is designed to be included by addb/addb.c */

#include "m0t1fs/linux_kernel/m0t1fs.h"
#include "m0t1fs/m0t1fs_addb.h"

/**
   @ingroup addb_pvt
   @{
 */

/**
   Construct the node uuid from the kernel's node_uuid parameter.
 */
static int addb_node_uuid_string_get(char buf[M0_UUID_STRLEN + 1])
{
	const char *s;

	s = m0t1fs_param_node_uuid_get();
	if (s == NULL)
		return -EINVAL;
	strncpy(buf, s, M0_UUID_STRLEN);
	buf[M0_UUID_STRLEN] = '\0';
	return 0;
}

/**
   Create the kernel module container context.
 */
static void addb_ctx_proc_ctx_create(void)
{
	addb_proc_ctx_fields[0] = (uint64_t)addb_init_time;

	m0_addb_proc_ctx.ac_type   = &m0_addb_ct_kmod;
	m0_addb_proc_ctx.ac_id     = (uint64_t)addb_init_time;
	m0_addb_proc_ctx.ac_parent = &m0_addb_node_ctx;
	++m0_addb_node_ctx.ac_cntr;
	m0_addb_proc_ctx.ac_depth  = m0_addb_node_ctx.ac_depth + 1;
	m0_addb_proc_ctx.ac_magic  = M0_ADDB_CTX_MAGIC;
}

/** @} end group addb_pvt */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
