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
 * Original creation date: 7-Apr-2016
 */

#pragma once

#ifndef __MERO_HA_LINK_H__
#define __MERO_HA_LINK_H__

/**
 * @defgroup ha
 *
 *   send()  >--\                   /--> recv() -> delivered()
 *               >--> transport >-->
 *
 *   wait_delivery(tag) - wait until something is consumed on the other end
 *   wait_arrival() - wait until something arrives
 *   flush() - wait until all sent messages are consumed
 *
 *   use case:
 *   send() -> transport -> recv () -> delivered()
 *
 * * Queue and queue pointers
 *
 * - queue
 * - pointers: undelivered, transfer, assign
 *
 * * Link state
 *
 * - outgoing
 *   - msg queue: messages in range [delivered, assign).
 * - incoming
 *   - msg queue
 *
 * * Protocols
 * ** Legend
 * - L1 - m0_ha_link #1
 * - L2 - m0_ha_link #2
 * - P1 - process with L1
 * - P2 - process with L2
 * - H1 - supervisor (TODO make better word) in P1, controls L1
 * - H2 - supervisor in P2, controls L2
 * ** Typical use case
 * - P1 is m0d/m0mkfs
 * - P2 is halond
 * - H1 and H2 are struct m0_ha
 * ** Connect protocol
 * - H1 makes new request generation
 * - H1 sends L1 params request to H2. Request includes generation
 * - H2 receives the request
 * - H2 makes new request generation
 * - H2 makes params for L1 and L2
 *   - makes connection id as concatenation of H1 request generation and
 *     H2 request generation
 *   - sets L1 params id_connection
 *   - sets L2 params id_connection to the same value
 *   - makes L1 link id distinct from all other link ids made by P2 during it's
 *     lifetime
 *   - makes L2 link id in the same way
 *   - sets L1 params
 *     - id_connection to connection id
 *     - id_local  to L1 link id
 *     - id_remote to L2 link id
 *     - XXX tag_even  to true or false
 *   - sets L2 params
 *     - id_connection to connection id
 *     - id_local  to L2 link id
 *     - id_remote to L1 link id
 *     - XXX tag_even  to !(L1 params tag_even)
 * - H2 calls init() for L2, then start() for L2 with L2 params
 * - H2 sends L1 params to H1 in params reply
 * - H1 receives L1 params
 * - H1 calls init() for L1, then start() for L1 with L1 params
 * - <now L1 and L2 can start m0_ha_msg transfer>
 * ** Reconnect protocol in case if L2 exists
 * - H1 makes new request generation
 * - H1 reconnec_begin() for L1, gets L1 params
 * - H1 sends L1 params and request generation to H2 using some transport
 *   (not L1 nor L2)
 * - H2 receives L1 params and request generation
 * - H2 looks for L2 by L1 params
 * - H2 successfully finds L2
 * - H2 reconnect_begin() for L2, gets L2 params
 * - H2 reconnect_end() for L2 with old L2 params
 * - H2 sends old L1 params to H1 using some transport (not L1 nor L2)
 * - H1 receives old L1 params
 * - H1 reconnect_end() for L1 with old L1 params
 * - <now L1 and L2 can resume m0_ha_msg transfer>
 * ** Reconnect protocol in case if L2 doesn't exist
 * - TODO
 *
 * @{
 */

#include "lib/chan.h"           /* m0_chan */
#include "lib/types.h"          /* bool */
#include "lib/tlist.h"          /* m0_tlink */
#include "lib/semaphore.h"      /* m0_semaphore */
#include "lib/time.h"           /* m0_time_t */

#include "sm/sm.h"              /* m0_sm */
#include "fop/fom.h"            /* m0_fom */
#include "fop/fop.h"            /* m0_fop */
#include "rpc/link.h"           /* m0_rpc_link */

#include "ha/link_fops.h"       /* m0_ha_link_params */
#include "ha/lq.h"              /* m0_ha_lq */


struct m0_reqh;
struct m0_reqh_service;
struct m0_rpc_machine;
struct m0_locality;
struct m0_ha_link_msg_fop;
struct m0_ha_msg;
struct m0_rpc_session;

enum m0_ha_link_state {
	M0_HA_LINK_STATE_FAILED,
};

struct m0_ha_link_conn_cfg {
	struct m0_ha_link_params  hlcc_params;
	/**
	 * Process fid of remote endpoint.
	 * If it's set to M0_FID0 then HA link makes decision about
	 * connect/disconnect timeouts.
	 * If it's not M0_FID0 then HA link relies on notifications
	 * from HA about service failures.
	 */
	struct m0_fid             hlcc_rpc_service_fid;
	/** Remote endpoint. */
	const char               *hlcc_rpc_endpoint;
	uint64_t                  hlcc_max_rpcs_in_flight;
	m0_time_t                 hlcc_connect_timeout;
	m0_time_t                 hlcc_disconnect_timeout;
	m0_time_t                 hlcc_resend_interval;
	uint64_t                  hlcc_nr_sent_max;
};

struct m0_ha_link_cfg {
	struct m0_reqh         *hlc_reqh;
	struct m0_reqh_service *hlc_reqh_service;
	struct m0_rpc_machine  *hlc_rpc_machine;
	struct m0_ha_lq_cfg     hlq_q_cfg_in;
	struct m0_ha_lq_cfg     hlq_q_cfg_out;
};

struct m0_ha_link {
	struct m0_ha_link_cfg       hln_cfg;
	struct m0_ha_link_conn_cfg  hln_conn_cfg;
	struct m0_ha_link_conn_cfg  hln_conn_reconnect_cfg;
	struct m0_rpc_link          hln_rpc_link;
	/** ha_link_service::hls_links */
	struct m0_tlink             hln_service_link;
	uint64_t                    hln_service_magic;
	struct m0_mutex             hln_lock;
	/** This lock is always taken before hln_lock. */
	struct m0_mutex             hln_chan_lock;
	struct m0_chan              hln_chan;
	struct m0_mutex             hln_sm_lock;
	/** Signals on specific state changes. Can be replaced with an m0_sm. */
	struct m0_chan              hln_sm_chan;
	struct m0_ha_lq             hln_q_in;
	struct m0_ha_lq             hln_q_out;
	/** ha_sl */
	struct m0_fom               hln_fom;
	struct m0_locality         *hln_fom_locality;
	bool                        hln_fom_is_stopping;
	bool                        hln_fom_enable_wakeup;
	struct m0_semaphore         hln_start_wait;
	struct m0_semaphore         hln_stop_cond;
	struct m0_semaphore         hln_stop_wait;
	bool                        hln_waking_up;
	struct m0_sm_ast            hln_waking_ast;
	struct m0_ha_msg           *hln_msg_to_send;
	/** It's protected by outgoing fom sm group lock */
	bool                        hln_confirmed_update;
	struct m0_fop               hln_outgoing_fop;
	struct m0_ha_link_msg_fop   hln_req_fop_data;
	bool                        hln_replied;
	bool                        hln_released;
	struct m0_clink             hln_rpc_wait;
	bool                        hln_rpc_event_occurred;
	bool                        hln_reconnect;
	bool                        hln_reconnect_cfg_is_set;
	int                         hln_rpc_rc;
	/** It's protected by outgoing fom sm group lock */
	int                         hln_reply_rc;
	bool                        hln_no_new_delivered;
};

M0_INTERNAL int  m0_ha_link_init (struct m0_ha_link     *hl,
				  struct m0_ha_link_cfg *hl_cfg);
M0_INTERNAL void m0_ha_link_fini (struct m0_ha_link *hl);
M0_INTERNAL void m0_ha_link_start(struct m0_ha_link          *hl,
                                  struct m0_ha_link_conn_cfg *hl_conn_cfg);
M0_INTERNAL void m0_ha_link_stop (struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_link_reconnect_begin(struct m0_ha_link        *hl,
                                            struct m0_ha_link_params *lp);
M0_INTERNAL void
m0_ha_link_reconnect_end(struct m0_ha_link                *hl,
                         const struct m0_ha_link_conn_cfg *hl_conn_cfg);
M0_INTERNAL void m0_ha_link_reconnect_cancel(struct m0_ha_link *hl);
M0_INTERNAL void
m0_ha_link_reconnect_params(const struct m0_ha_link_params *lp_alive,
			    struct m0_ha_link_params       *lp_alive_new,
			    struct m0_ha_link_params       *lp_dead_new,
			    const struct m0_uint128        *id_alive,
			    const struct m0_uint128        *id_dead,
			    const struct m0_uint128        *id_connection);

M0_INTERNAL struct m0_chan *m0_ha_link_chan(struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_link_state_register(struct m0_ha_link *hl,
					   struct m0_clink   *clink);
M0_INTERNAL void m0_ha_link_state_deregister(struct m0_ha_link *hl,
					     struct m0_clink   *clink);
M0_INTERNAL enum m0_ha_link_state m0_ha_link_state(struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_link_send(struct m0_ha_link      *hl,
                                 const struct m0_ha_msg *msg,
                                 uint64_t               *tag);
M0_INTERNAL struct m0_ha_msg *m0_ha_link_recv(struct m0_ha_link *hl,
                                              uint64_t          *tag);

M0_INTERNAL void m0_ha_link_delivered(struct m0_ha_link *hl,
                                      struct m0_ha_msg  *msg);
M0_INTERNAL bool m0_ha_link_msg_is_delivered(struct m0_ha_link *hl,
					     uint64_t           tag);
/** Returns M0_HA_MSG_TAG_INVALID if there is nothing to consume */
M0_INTERNAL uint64_t m0_ha_link_delivered_consume(struct m0_ha_link *hl);
M0_INTERNAL uint64_t m0_ha_link_not_delivered_consume(struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_link_wait_delivery(struct m0_ha_link *hl,
					  uint64_t           tag);
M0_INTERNAL void m0_ha_link_wait_arrival(struct m0_ha_link *hl);
M0_INTERNAL void m0_ha_link_wait_confirmation(struct m0_ha_link *hl,
                                              uint64_t           tag);

/**
 * Waits until all messages are sent and delivered.
 * Also waits until all messages received are consumend and delivery is
 * confirmed.
 */
M0_INTERNAL void m0_ha_link_flush(struct m0_ha_link *hl);
M0_INTERNAL void m0_ha_link_quiesce(struct m0_ha_link *hl);

M0_INTERNAL struct m0_rpc_session *
m0_ha_link_rpc_session(struct m0_ha_link *hl);

M0_INTERNAL int  m0_ha_link_mod_init(void);
M0_INTERNAL void m0_ha_link_mod_fini(void);

/* XXX move to internal */
extern const struct m0_fom_type_ops m0_ha_link_incoming_fom_type_ops;
extern const struct m0_fom_type_ops m0_ha_link_outgoing_fom_type_ops;

/** @} end of ha group */
#endif /* __MERO_HA_LINK_H__ */

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
