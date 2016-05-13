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
 * @{
 */

#include "lib/chan.h"           /* m0_chan */
#include "lib/types.h"          /* bool */
#include "lib/tlist.h"          /* m0_tlink */
#include "lib/semaphore.h"      /* m0_semaphore */

#include "fop/fom.h"            /* m0_fom */
#include "fop/fop.h"            /* m0_fop */
#include "sm/sm.h"              /* m0_sm_ast */

#include "ha/msg_queue.h"       /* m0_ha_msg_queue */


struct m0_reqh;
struct m0_reqh_service;
struct m0_locality;
struct m0_ha_link_msg_fop;
struct m0_ha_msg;

struct m0_ha_link_cfg {
	struct m0_reqh             *hlc_reqh;
	struct m0_reqh_service     *hlc_reqh_service;
	struct m0_rpc_session      *hlc_rpc_session;
	struct m0_uint128           hlc_link_id_local;
	struct m0_uint128           hlc_link_id_remote;
	struct m0_ha_msg_queue_cfg  hlc_q_in_cfg;
	struct m0_ha_msg_queue_cfg  hlc_q_out_cfg;
	bool                        hlc_tag_even;
};

struct m0_ha_link {
	struct m0_ha_link_cfg      hln_cfg;
	/** ha_link_service::hls_links */
	struct m0_tlink            hln_service_link;
	uint64_t                   hln_service_magic;
	struct m0_mutex            hln_lock;
	/** This lock is always taken before hln_lock. */
	struct m0_mutex            hln_chan_lock;
	struct m0_chan             hln_chan;
	struct m0_ha_msg_queue     hln_q_in;
	struct m0_ha_msg_queue     hln_q_out;
	/** ha_sl */
	struct m0_tl               hln_sent;
	uint64_t                   hln_tag_current;
	struct m0_fom              hln_fom;
	struct m0_locality        *hln_fom_locality;
	bool                       hln_fom_is_stopping;
	struct m0_semaphore        hln_start_wait;
	struct m0_semaphore        hln_stop_cond;
	struct m0_semaphore        hln_stop_wait;
	bool                       hln_waking_up;
	struct m0_sm_ast           hln_waking_ast;
	struct m0_ha_msg_qitem    *hln_qitem_to_send;
	struct m0_fop              hln_outgoing_fop;
	struct m0_ha_link_msg_fop *hln_req_fop_data;
	bool                       hln_replied;
};

M0_INTERNAL int  m0_ha_link_init (struct m0_ha_link     *hl,
				  struct m0_ha_link_cfg *hl_cfg);
M0_INTERNAL void m0_ha_link_fini (struct m0_ha_link *hl);
M0_INTERNAL void m0_ha_link_start(struct m0_ha_link *hl);
M0_INTERNAL void m0_ha_link_stop (struct m0_ha_link *hl);

M0_INTERNAL struct m0_chan *m0_ha_link_chan(struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_link_send(struct m0_ha_link      *hl,
                                 const struct m0_ha_msg *msg,
                                 uint64_t               *tag);
M0_INTERNAL struct m0_ha_msg *m0_ha_link_recv(struct m0_ha_link *hl,
                                              uint64_t          *tag);

M0_INTERNAL void m0_ha_link_delivered(struct m0_ha_link *hl,
                                      struct m0_ha_msg  *msg);
M0_INTERNAL bool m0_ha_link_msg_is_delivered(struct m0_ha_link *hl,
					     uint64_t           tag);

M0_INTERNAL void m0_ha_link_wait_delivery(struct m0_ha_link *hl,
					  uint64_t           tag);
M0_INTERNAL void m0_ha_link_wait_arrival(struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_link_flush(struct m0_ha_link *hl);
M0_INTERNAL void m0_ha_link_quiesce(struct m0_ha_link *hl);

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
