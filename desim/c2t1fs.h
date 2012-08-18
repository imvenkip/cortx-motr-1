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
 * Original creation date: 2012-Jun-16
 */

#pragma once

#ifndef __COLIBRI_DESIM_C2T1FS_H__
#define __COLIBRI_DESIM_C2T1FS_H__

/**
   @addtogroup desim desim
   @{
 */

#include "lib/tlist.h"
#include "lib/types.h"                   /* c2_bcount_t */

#include "pool/pool.h"
#include "layout/pdclust.h"

struct net_conf;
struct net_srv;
struct c2t1fs_thread;
struct c2t1fs_client;

struct c2t1fs_thread {
	struct sim_thread         cth_thread;
	struct c2t1fs_client     *cth_client;
	unsigned                  cth_id;
	struct c2_pdclust_layout *cth_pdclust;
};

struct c2t1fs_client {
	struct c2t1fs_thread *cc_thread;
	unsigned              cc_id;
	struct c2t1fs_conn {
		unsigned        cs_inflight;
		struct sim_chan cs_wakeup;
	} *cc_srv;
	struct c2t1fs_conf   *cc_conf;
};

struct c2t1fs_conf {
	unsigned                 ct_nr_clients;
	unsigned                 ct_nr_threads;
	unsigned                 ct_nr_servers;
	unsigned                 ct_nr_devices;
	struct c2_pool           ct_pool;
	uint32_t                 ct_N;
	uint32_t                 ct_K;
	uint64_t                 ct_unitsize;
	unsigned long            ct_client_step;
	unsigned long            ct_thread_step;
	unsigned                 ct_inflight_max;
	c2_bcount_t              ct_total;
	sim_time_t               ct_delay_min;
	sim_time_t               ct_delay_max;
	struct net_conf         *ct_net;
	struct net_srv          *ct_srv0;
	struct net_srv          *ct_srv;
	struct c2t1fs_client    *ct_client;
};

void c2t1fs_init(struct sim *s, struct c2t1fs_conf *conf);
void c2t1fs_fini(struct c2t1fs_conf *conf);

#endif /* __COLIBRI_DESIM_C2T1FS_H__ */

/** @} end of desim group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
