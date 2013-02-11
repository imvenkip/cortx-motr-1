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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 11-Feb-2013
 */


#pragma once

#ifndef __MERO_RPC_ITEM_SOURCE_H__
#define __MERO_RPC_ITEM_SOURCE_H__


/**
 * @defgroup rpc
 *
 * @{
 */

struct m0_rpc_item_source;
struct m0_rpc_item_source_ops;

struct m0_rpc_item_source {
	uint64_t                             ris_magic;
	const char                          *ris_name;
	const struct m0_rpc_item_source_ops *ris_ops;
	struct m0_rpc_conn                  *ris_conn;
	struct m0_tlink                      ris_tlink;
};

struct m0_rpc_item_source_ops {
	bool (*riso_has_item)(const struct m0_rpc_item_source *ris);

	void (*riso_conn_terminated)(struct m0_rpc_item_source *source);

	struct m0_rpc_item *(*riso_get_item)(struct m0_rpc_item_source *ris,
					     size_t max_payload_size);
};

int m0_rpc_item_source_init(struct m0_rpc_item_source *ris,
			    const char *name,
			    const struct m0_rpc_item_source_ops *ops);
void m0_rpc_item_source_fini(struct m0_rpc_item_source *ris);
void m0_rpc_item_source_register(struct m0_rpc_conn *conn,
				struct m0_rpc_item_source *ris);
void m0_rpc_item_source_deregister(struct m0_rpc_item_source *ris);

/** @} end of rpc group */

#endif /* __MERO_RPC_ITEM_SOURCE_H__ */


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
