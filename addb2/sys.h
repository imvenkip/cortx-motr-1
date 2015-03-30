/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 17-Mar-2015
 */

#pragma once

#ifndef __MERO_ADDB2_SYS_H__
#define __MERO_ADDB2_SYS_H__

/**
 * @defgroup addb2
 *
 * @{
 */

/* import */
#include "lib/mutex.h"
#include "lib/types.h"
#include "lib/tlist.h"
#include "lib/semaphore.h"
#include "sm/sm.h"
struct m0_addb2_trace_obj;
struct m0_addb2_storage;
struct m0_addb2_net;
struct m0_thread;
struct m0_stob;

/* export */
struct m0_addb2_config;
struct m0_addb2_sys;

struct m0_addb2_config {
	unsigned co_buffer_size;
	unsigned co_buffer_min;
	unsigned co_buffer_max;
	unsigned co_queue_max;
	unsigned co_pool_min;
	unsigned co_pool_max;
};

struct m0_addb2_sys {
	struct m0_addb2_config   sy_conf;
	struct m0_mutex          sy_lock;
	struct m0_addb2_storage *sy_stor;
	struct m0_addb2_net     *sy_net;
	struct m0_tl             sy_queue;
	m0_bcount_t              sy_queued;
	struct m0_sm_ast         sy_ast;
	struct m0_tl             sy_pool;
	struct m0_tl             sy_granted;
	struct m0_tl             sy_moribund;
	struct m0_tl             sy_deathrow;
	m0_bcount_t              sy_total;
	struct m0_semaphore      sy_wait;
	struct m0_thread        *sy_owner;
	unsigned                 sy_nesting;
};

void m0_addb2_sys_init(struct m0_addb2_sys *sys,
		       const struct m0_addb2_config *conf);
void m0_addb2_sys_fini(struct m0_addb2_sys *sys);
struct m0_addb2_mach *m0_addb2_sys_get(struct m0_addb2_sys *sys);
void m0_addb2_sys_put(struct m0_addb2_sys *sys, struct m0_addb2_mach *mach);

void m0_addb2_sys_sm_start(struct m0_addb2_sys *sys);
void m0_addb2_sys_sm_stop(struct m0_addb2_sys *sys);

int  m0_addb2_sys_net_start(struct m0_addb2_sys *sys);
void m0_addb2_sys_net_stop(struct m0_addb2_sys *sys);
int  m0_addb2_sys_net_start_with(struct m0_addb2_sys *sys, struct m0_tl *head);
int  m0_addb2_sys_stor_start(struct m0_addb2_sys *sys, struct m0_stob *stob,
			     m0_bcount_t size, bool format);
void m0_addb2_sys_stor_stop(struct m0_addb2_sys *sys);

int m0_addb2_sys_submit(struct m0_addb2_sys *sys,
			struct m0_addb2_trace_obj *obj);


/** @} end of addb2 group */
#endif /* __MERO_ADDB2_SYS_H__ */

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
