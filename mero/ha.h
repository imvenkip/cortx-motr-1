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
 * Original creation date: 9-Jun-2016
 */

#pragma once

#ifndef __MERO_MERO_HA_H__
#define __MERO_MERO_HA_H__

/**
 * @defgroup mero
 *
 * @{
 */

#include "lib/tlist.h"  /* m0_tl */
#include "lib/types.h"  /* uint64_t */
#include "lib/mutex.h"  /* m0_mutex */
#include "ha/ha.h"      /* m0_ha */

struct m0_rpc_machine;
struct m0_reqh;
struct m0_ha_link;
struct m0_ha_note_handler;
struct m0_ha_keepalive_handler;

struct m0_mero_ha_cfg {
	const char            *mhc_addr;
	struct m0_rpc_machine *mhc_rpc_machine;
	struct m0_reqh        *mhc_reqh;
};

struct m0_mero_ha {
	struct m0_mero_ha_cfg           mh_cfg;
	struct m0_ha                    mh_ha;
	struct m0_ha_link              *mh_link;
	/*
	 * Is not protected by any lock.
	 * User is responsible for non-concurrent modifications.
	 * Handlers can be added only between m0_mero_ha_init() and
	 * m0_mero_ha_start().
	 */
	struct m0_tl                    mh_handlers;
	bool                            mh_can_add_handler;
	/* m0_ha_note_set(), m0_ha_note_get() handler */
	struct m0_ha_note_handler      *mh_note_handler;
	struct m0_ha_keepalive_handler *mh_keepalive_handler;
};

struct m0_mero_ha_handler {
	struct m0_tlink   mhf_link;
	uint64_t          mhf_magic;
	void             *mhf_data;
	void            (*mhf_msg_received_cb)(struct m0_mero_ha         *mha,
	                                       struct m0_mero_ha_handler *mhf,
	                                       struct m0_ha_msg          *msg,
	                                       struct m0_ha_link         *hl,
	                                       void                      *data);
};

M0_INTERNAL void m0_mero_ha_cfg_make(struct m0_mero_ha_cfg *mha_cfg,
				     struct m0_reqh        *reqh,
				     struct m0_rpc_machine *rmach,
				     const char            *addr);

M0_INTERNAL int m0_mero_ha_init(struct m0_mero_ha     *mha,
                                struct m0_mero_ha_cfg *mha_cfg);
M0_INTERNAL int m0_mero_ha_start(struct m0_mero_ha *mha);
M0_INTERNAL void m0_mero_ha_stop(struct m0_mero_ha *mha);
M0_INTERNAL void m0_mero_ha_fini(struct m0_mero_ha *mha);

M0_INTERNAL void m0_mero_ha_connect(struct m0_mero_ha *mha);
M0_INTERNAL void m0_mero_ha_disconnect(struct m0_mero_ha *mha);

M0_INTERNAL void m0_mero_ha_handler_attach(struct m0_mero_ha         *mha,
                                           struct m0_mero_ha_handler *mhf);
M0_INTERNAL void m0_mero_ha_handler_detach(struct m0_mero_ha         *mha,
                                           struct m0_mero_ha_handler *mhf);

extern const struct m0_ha_ops m0_mero_ha_ops;

/** @} end of mero group */
#endif /* __MERO_MERO_HA_H__ */

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
