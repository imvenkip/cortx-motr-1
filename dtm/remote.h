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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 18-Apr-2013
 */


#pragma once

#ifndef __MERO_DTM_REMOTE_H__
#define __MERO_DTM_REMOTE_H__


/**
 * @defgroup dtm
 *
 * @{
 */

#include "xcode/xcode_attr.h"
#include "dtm/update.h"              /* m0_dtm_history_id */
#include "dtm/update_xc.h"           /* m0_dtm_history_id_xc */

/* import */
#include "dtm/history.h"
struct m0_dtm_oper;
struct m0_dtm_update;
struct m0_rpc_conn;

/* export */
struct m0_dtm_remote;
struct m0_dtm_rpc_remote;
struct m0_dtm_remote_ops;

struct m0_dtm_remote {
	const struct m0_dtm_remote_ops *re_ops;
	struct m0_dtm_controlh          re_fol;
};

struct m0_dtm_remote_ops {
	void (*reo_persistent)(struct m0_dtm_remote *dtm,
			       struct m0_dtm_history *history);
	void (*reo_fixed)(struct m0_dtm_remote *dtm,
			  struct m0_dtm_history *history);
	void (*reo_known)(struct m0_dtm_remote *dtm,
			  struct m0_dtm_history *history);
};

M0_INTERNAL void m0_dtm_remote_init(struct m0_dtm_remote *remote,
				    struct m0_dtm *local);
M0_INTERNAL void m0_dtm_remote_fini(struct m0_dtm_remote *remote);

M0_INTERNAL void m0_dtm_remote_add(struct m0_dtm_remote *dtm,
				   struct m0_dtm_oper *oper,
				   struct m0_dtm_history *history,
				   struct m0_dtm_update *update);

struct m0_dtm_rpc_remote {
	struct m0_dtm_remote  rpr_dtm;
	struct m0_rpc_conn   *rpr_conn;
};

M0_INTERNAL void m0_dtm_rpc_remote_init(struct m0_dtm_rpc_remote *remote,
					struct m0_dtm *local,
					struct m0_rpc_conn *conn);
M0_INTERNAL void m0_dtm_rpc_remote_fini(struct m0_dtm_rpc_remote *remote);

struct m0_dtm_notice {
	struct m0_dtm_history_id dno_id;
	uint64_t                 dno_ver;
	uint8_t                  dno_opcode;
} M0_XCA_RECORD;

/** @} end of dtm group */

#endif /* __MERO_DTM_REMOTE_H__ */


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
