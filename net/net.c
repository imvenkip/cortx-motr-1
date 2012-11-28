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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/01/2010
 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "net/net_internal.h"

/**
   @addtogroup net
   @{
 */

/**
   Network module global mutex.
   This mutex is used to serialize domain init and fini.
   It is defined here so that it can get initialized and fini'd
   by the general initialization mechanism.
   Transport that deal with multiple domains can rely on this mutex being held
   across their xo_dom_init() and xo_dom_fini() methods.
 */
struct m0_mutex m0_net_mutex;

/** @} net */

const struct m0_addb_loc m0_net_addb_loc = {
	.al_name = "net"
};

const struct m0_addb_ctx_type m0_net_addb_ctx = {
	.act_name = "net"
};

struct m0_addb_ctx m0_net_addb;

M0_INTERNAL int m0_net_init()
{
	m0_mutex_init(&m0_net_mutex);
	m0_addb_ctx_init(&m0_net_addb, &m0_net_addb_ctx, &m0_addb_global_ctx);
	m0_xc_net_otw_types_init();

	return 0;
}

M0_INTERNAL void m0_net_fini()
{
	m0_xc_net_otw_types_fini();
	m0_addb_ctx_fini(&m0_net_addb);
	m0_mutex_fini(&m0_net_mutex);
}

int m0_net_xprt_init(struct m0_net_xprt *xprt)
{
	return 0;
}
M0_EXPORTED(m0_net_xprt_init);

void m0_net_xprt_fini(struct m0_net_xprt *xprt)
{
}
M0_EXPORTED(m0_net_xprt_fini);

M0_INTERNAL int m0_net_desc_copy(const struct m0_net_buf_desc *from_desc,
				 struct m0_net_buf_desc *to_desc)
{
	M0_PRE(from_desc->nbd_len > 0);
	M0_ALLOC_ARR_ADDB(to_desc->nbd_data, from_desc->nbd_len,
			  &m0_net_addb, &m0_net_addb_loc);
	if (to_desc->nbd_data == NULL)
		return -ENOMEM;
	memcpy(to_desc->nbd_data, from_desc->nbd_data, from_desc->nbd_len);
	to_desc->nbd_len = from_desc->nbd_len;
	return 0;
}
M0_EXPORTED(m0_net_desc_copy);

M0_INTERNAL void m0_net_desc_free(struct m0_net_buf_desc *desc)
{
	if (desc->nbd_len > 0) {
		M0_PRE(desc->nbd_data != NULL);
		m0_free(desc->nbd_data);
		desc->nbd_len = 0;
	}
	desc->nbd_data = NULL;
}
M0_EXPORTED(m0_net_desc_free);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
