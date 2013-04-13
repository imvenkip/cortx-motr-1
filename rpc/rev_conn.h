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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 9-Apr-2013
 */


#pragma once

#ifndef __MERO_RPC_REV_CONN_H__
#define __MERO_RPC_REV_CONN_H__

#include "fop/fom.h"
#include "lib/tlist.h"
#include "rpc/conn.h"
#include "rpc/session.h"
#include "rpc/rpc_machine.h"

/**
   @defgroup rev_conn Reverse connection

   @{
 */

enum m0_rev_connection_fom_type {
	M0_REV_CONNECT,
	M0_REV_DISCONNECT,
};

enum {
	M0_REV_CONN_TIMEOUT   = 10,
};

struct m0_reverse_connection {
	enum m0_rev_connection_fom_type   rcf_ft;
	char                             *rcf_rem_ep;
	struct m0_fom                     rcf_fom;
	struct m0_tlink                   rcf_link;
	struct m0_rpc_conn               *rcf_conn;
	struct m0_rpc_session           **rcf_sess;
	struct m0_rpc_machine            *rcf_rpcmach;
	struct m0_fom_callback            rcf_fomcb;
	uint64_t                          rcf_magic;
};

enum m0_rev_conn_states {
	M0_RCS_CONN = M0_FOM_PHASE_INIT,
	M0_RCS_FINI = M0_FOM_PHASE_FINISH,
	M0_RCS_CONN_WAIT,
	M0_RCS_SESSION,
	M0_RCS_SESSION_WAIT,
	M0_RCS_FAILURE,
};

extern struct m0_fom_type      rev_conn_fom_type;
extern const struct m0_fom_ops rev_conn_fom_ops;

M0_INTERNAL void m0_rev_conn_fom_type_init(void);

/** @} end of rev_conn group */

#endif /* __MERO_RPC_REV_CONN_H__ */


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
