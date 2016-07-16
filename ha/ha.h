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
 * - TODO 2 cases TRANSIENT -> ONLINE: if m0d reconnected or it's restarted
 * - TODO can m0d receive TRANSIENT or FAILED about itself
 * - TODO race between FAILED about process and systemd restart
 * - TODO check types of process fid and profile fid
 *
 * Reconnect protocol
 * - Legend
 *   - HA1 - side with outgoging link (m0_ha with HA link from connect());
 *   - HA2 - side with incoming link (m0_ha with HA link from entrypoint
 *     server);
 *   - Cx - case #x
 *   - Sx - step #x
 *   - CxSy - case #x, step #y
 * - C1 HA2 is started, HA1 starts (first start after HA2 had started):
 *   - S1 HA1 sends entrypoint request with first_request flag set
 *   - S2 HA2 receives the request
 *   - S3 HA2 looks at the flag and makes new HA link
 *   - S4 HA2 sends entrypoint reply
 *   - S5 HA1 makes new link with the parameters from reply
 * - C2 HA1 and HA2 restart
 *   - the same as C1
 * - C3 HA1 restarts, HA2 is alive
 *   - the same as C1. Existing link is totally ignored in C1S3
 * - C4 HA2 restarts, HA1 is alive
 *   - S1 HA1 considers HA2 dead
 *   - S2 HA1 starts HA link reconnect
 *   - S3 HA1 tries to send entrypoint request to HA2 in infinite loop
 *   - S4 HA1 sends entrypoint request
 *   - S5 HA2 receives entrypoint request
 *   - S7 HA2 sees first_request flag is not set
 *   - S8 HA2 makes HA link with parameters from request
 *   - S9 HA2 sends entrypoint reply
 *   - S10 HA1 ends HA link reconnect
 * - C5 HA1 is alive, HA2 is alive, HA2 considers HA1 dead due to transient
 *   network failure
 *   - S1 C4S1, C4S2, C4S3
 *   - S2 HA2 terminates the process with HA1
 *   - S3 HA2 ensures that process is terminated
 *   - S4 C3
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
#include "lib/mutex.h"          /* m0_mutex */

#include "fid/fid.h"            /* m0_fid */
#include "module/module.h"      /* m0_module */
#include "ha/entrypoint.h"      /* m0_ha_entrypoint_client */

struct m0_uint128;
struct m0_rpc_machine;
struct m0_reqh;
struct m0_ha;
struct m0_ha_msg;
struct m0_ha_link;
struct m0_ha_entrypoint_req;

enum m0_ha_level {
	M0_HA_LEVEL_ASSIGNS,
	M0_HA_LEVEL_ADDR_STRDUP,
	M0_HA_LEVEL_LINK_SERVICE,
	M0_HA_LEVEL_ENTRYPOINT_SERVER_INIT,
	M0_HA_LEVEL_ENTRYPOINT_CLIENT_INIT,
	M0_HA_LEVEL_INIT,
	M0_HA_LEVEL_ENTRYPOINT_SERVER_START,
	M0_HA_LEVEL_INCOMING_LINKS,
	M0_HA_LEVEL_START,
	M0_HA_LEVEL_LINK_CTX_ALLOC,
	M0_HA_LEVEL_LINK_CTX_INIT,
	M0_HA_LEVEL_ENTRYPOINT_CLIENT_START,
	M0_HA_LEVEL_ENTRYPOINT_CLIENT_WAIT,
	M0_HA_LEVEL_LINK_ASSIGN,
	M0_HA_LEVEL_CONNECT,
};

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
	/** Remote address for m0_ha_connect(). */
	const char                         *hcf_addr;
	struct m0_fid                       hcf_process_fid;
	struct m0_fid                       hcf_profile_fid;

	/* m0_ha is resposible for the next fields */

	struct m0_ha_entrypoint_client_cfg  hcf_entrypoint_client_cfg;
	struct m0_ha_entrypoint_server_cfg  hcf_entrypoint_server_cfg;
};

struct ha_link_ctx;

struct m0_ha {
	struct m0_ha_cfg                h_cfg;
	struct m0_module                h_module;
	struct m0_mutex                 h_lock;
	struct m0_tl                    h_links_incoming;
	struct m0_tl                    h_links_outgoing;
	/** primary outgoing link */
	struct m0_ha_link              *h_link;
	/** struct ha_link_ctx for h_link */
	struct ha_link_ctx             *h_link_ctx;
	bool                            h_link_started;
	struct m0_reqh_service         *h_hl_service;
	struct m0_ha_entrypoint_client  h_entrypoint_client;
	struct m0_ha_entrypoint_server  h_entrypoint_server;
	struct m0_clink                 h_clink;
	uint64_t                        h_link_id_counter;
	uint64_t                        h_generation_counter;
};

M0_INTERNAL int m0_ha_init(struct m0_ha *ha, struct m0_ha_cfg *ha_cfg);
M0_INTERNAL int m0_ha_start(struct m0_ha *ha);
M0_INTERNAL void m0_ha_stop(struct m0_ha *ha);
M0_INTERNAL void m0_ha_fini(struct m0_ha *ha);

M0_INTERNAL void
m0_ha_entrypoint_reply(struct m0_ha                       *ha,
                       const struct m0_uint128            *req_id,
                       const struct m0_ha_entrypoint_rep  *rep,
                       struct m0_ha_link                 **hl_ptr);

M0_INTERNAL struct m0_ha_link *m0_ha_connect(struct m0_ha *ha);
M0_INTERNAL void m0_ha_disconnect(struct m0_ha *ha);

M0_INTERNAL void m0_ha_disconnect_incoming(struct m0_ha      *ha,
                                           struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_send(struct m0_ha           *ha,
                            struct m0_ha_link      *hl,
                            const struct m0_ha_msg *msg,
                            uint64_t               *tag);
M0_INTERNAL void m0_ha_delivered(struct m0_ha      *ha,
                                 struct m0_ha_link *hl,
                                 struct m0_ha_msg  *msg);

M0_INTERNAL void m0_ha_flush(struct m0_ha      *ha,
			     struct m0_ha_link *hl);

M0_INTERNAL struct m0_ha_link *m0_ha_outgoing_link(struct m0_ha *ha);
M0_INTERNAL struct m0_rpc_session *m0_ha_outgoing_session(struct m0_ha *ha);

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
