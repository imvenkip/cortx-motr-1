/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 26-Apr-2016
 */

#pragma once

#ifndef __MERO_HA_HA_H__
#define __MERO_HA_HA_H__

/**
 * @defgroup ha
 *
 * Source structure
 * - ha/entrypoint_fops.h - entrypoint fops + req <-> req_fop, rep <-> rep_fop
 * - ha/entrypoint.h      - entrypoint client & server (transport)
 *
 * entrypoint request handling
 * - func Mero send entrypoint request
 * - cb   HA   accept entrypoint request
 * - func HA   reply to the entrypoint request
 * - cb   Mero receive entrypoint reply
 *
 * m0_ha_link management
 * - func Mero connect to HA
 * - cb   HA   accept incoming connection
 * - func Mero disconnect from HA
 * - cb   HA   node disconnected
 * - func HA   disconnect from Mero
 * - cb   HA   node is dead
 *
 * m0_ha_msg handling
 * - func both send msg
 * - cb   both recv msg
 * - func both msg delivered
 * - cb   both msg is delivered
 * - cb   HA   undelivered msg
 *
 * error handling
 * - cb   both ENOMEM
 * - cb   both connection failed
 *
 * use cases
 * - normal loop
 *   - Mero entrypoint request
 *   - HA   accept request, send reply
 *   - HA   reply to the request
 *   - Mero receive entrypoint reply
 *   - Mero connect to HA
 *   - HA   accept incoming connection
 *   - main loop
 *     - both send msg
 *     - both recv msg
 *     - both msg delivered
 *   - Mero disconnect from HA
 *   - HA   node disconnected
 * - HA restart
 *   - Mero entrypoint request, reply
 *   - Mero connect to HA
 *   - HA   accept incoming connection
 *   - < HA restarts >
 *   - < Mero considers HA dead >
 *   - Mero disconnect from HA
 *   - goto normal loop (but the same incarnation in the entrypoint request)
 * - Mero restart
 *   - Mero entrypoint request, reply
 *   - Mero connect to HA
 *   - HA   accept incoming connection
 *   - < Mero restarts >
 *   - < HA considers Mero dead >
 *   - HA   send msg to local link that Mero is dead
 *   - HA   receive msg from local link that Mero is dead
 *   - HA   (cb) node is dead
 *   - HA   disconnect from Mero
 *   goto normal loop
 * @{
 */

#include "lib/tlist.h"          /* m0_tl */
#include "lib/types.h"          /* uint64_t */

#include "ha/entrypoint.h"      /* m0_ha_entrypoint_client */

struct m0_uint128;
struct m0_rpc_machine;
struct m0_reqh;
struct m0_ha;
struct m0_ha_msg;
struct m0_ha_link;
struct m0_ha_entrypoint_req;

struct m0_ha_ops {
	void (*hao_entrypoint_request)
		(struct m0_ha                      *ha,
		 const struct m0_ha_entrypoint_req *req,
		 const struct m0_uint128           *req_id);
	void (*hao_entrypoint_replied)(struct m0_ha                *ha,
	                               struct m0_ha_entrypoint_rep *rep);
	void (*hao_msg_received)(struct m0_ha      *ha,
	                         struct m0_ha_link *hl,
	                         struct m0_ha_msg  *msg,
	                         uint64_t           tag);
	void (*hao_msg_is_delivered)(struct m0_ha      *ha,
	                             struct m0_ha_link *hl,
	                             uint64_t           tag);
	void (*hao_msg_is_not_delivered)(struct m0_ha      *ha,
	                                 struct m0_ha_link *hl,
	                                 uint64_t           tag);
	void (*hao_link_connected)(struct m0_ha            *ha,
	                           const struct m0_uint128 *req_id,
	                           struct m0_ha_link       *hl);
	void (*hao_link_reused)(struct m0_ha            *ha,
	                        const struct m0_uint128 *req_id,
	                        struct m0_ha_link       *hl);
	void (*hao_link_is_disconnecting)(struct m0_ha      *ha,
	                                  struct m0_ha_link *hl);
	void (*hao_link_disconnected)(struct m0_ha      *ha,
	                              struct m0_ha_link *hl);
	/* not implemented yet */
	void (*hao_error_no_memory)(struct m0_ha *ha, int unused);
};

struct m0_ha_cfg {
	struct m0_ha_ops                    hcf_ops;
	struct m0_rpc_machine              *hcf_rpc_machine;
	struct m0_reqh                     *hcf_reqh;
	struct m0_ha_entrypoint_client_cfg  hcf_entrypoint_client_cfg;
	struct m0_ha_entrypoint_server_cfg  hcf_entrypoint_server_cfg;
};

struct m0_ha {
	struct m0_ha_cfg                h_cfg;
	struct m0_tl                    h_links_incoming;
	struct m0_tl                    h_links_outgoing;
	struct m0_reqh_service         *h_hl_service;
	struct m0_ha_entrypoint_client  h_entrypoint_client;
	struct m0_ha_entrypoint_server  h_entrypoint_server;
	uint64_t                        h_link_id_counter;
};

M0_INTERNAL void m0_ha_cfg_make(struct m0_ha_cfg      *ha_cfg,
                                struct m0_reqh        *reqh,
                                struct m0_rpc_machine *rmach);

M0_INTERNAL int m0_ha_init(struct m0_ha *ha, struct m0_ha_cfg *ha_cfg);
M0_INTERNAL int m0_ha_start(struct m0_ha *ha);
M0_INTERNAL void m0_ha_stop(struct m0_ha *ha);
M0_INTERNAL void m0_ha_fini(struct m0_ha *ha);

M0_INTERNAL void
m0_ha_entrypoint_reply(struct m0_ha                       *ha,
                       const struct m0_uint128            *req_id,
                       const struct m0_ha_entrypoint_rep  *rep,
                       struct m0_ha_link                 **hl_ptr);

M0_INTERNAL struct m0_ha_link *m0_ha_connect(struct m0_ha *ha,
                                             const char   *ep);
M0_INTERNAL void m0_ha_disconnect(struct m0_ha      *ha,
                                  struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_disconnect_incoming(struct m0_ha      *ha,
                                           struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_send(struct m0_ha           *ha,
                            struct m0_ha_link      *hl,
                            const struct m0_ha_msg *msg,
                            uint64_t               *tag);
M0_INTERNAL void m0_ha_delivered(struct m0_ha      *ha,
                                 struct m0_ha_link *hl,
                                 struct m0_ha_msg  *msg);

M0_INTERNAL int  m0_ha_mod_init(void);
M0_INTERNAL void m0_ha_mod_fini(void);

/** @} end of ha group */
#endif /* __MERO_HA_HA_H__ */

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
