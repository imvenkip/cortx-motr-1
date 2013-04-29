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
 * Original creation date: 29-Apr-2013
 */

#pragma once

#ifndef __MERO_HA_EPOCH_H__
#define __MERO_HA_EPOCH_H__


/**
 * @defgroup ha
 *
 * @{
 */

/* import */
#include "lib/types.h"
#include "lib/tlist.h"
#include "lib/rwlock.h"
#include "xcode/xcode_attr.h"
struct struct m0_xcode_obj;

/* export */
struct m0_ha_epoch;
struct m0_ha_domain;
struct m0_ha_epoch_monitor;

struct m0_ha_domain {
	uint64_t         hdo_epoch;
	struct m0_rwlock hdo_lock;
	struct m0_tl     hdo_watchers;
};

struct m0_ha_epoch_monitor {
	void (*hem_past)(struct m0_ha_epoch_monitor *self, uint64_t epoch);
	void (*hem_present)(struct m0_ha_epoch_monitor *self, uint64_t epoch);
	void (*hem_future)(struct m0_ha_epoch_monitor *self, uint64_t epoch);
	struct m0_tlink      hem_linkage;
	struct m0_ha_domain *hem_domain;
};

M0_EXTERN const uint64_t M0_HA_EPOCH_NONE;

M0_INTERN void m0_ha_domain_init(struct m0_ha_domain *dom, uint64_t epoch);
M0_INTERN void m0_ha_domain_fini(struct m0_ha_domain *dom);
M0_INTERN int m0_ha_epoch_set(struct m0_xcode_obj *obj, uint64_t epoch);
M0_INTERN int m0_ha_epoch_get(struct m0_xcode_obj *obj, uint64_t *epoch);
M0_INTERN void m0_ha_domain_monitor_add(struct m0_ha_domain *dom,
					struct m0_ha_epoch_monitor *mon);
M0_INTERN void m0_ha_domain_monitor_del(struct m0_ha_domain *dom,
					struct m0_ha_epoch_monitor *mon);
M0_INTERN uint64_t m0_ha_domain_get_read(struct m0_ha_domain *dom);
M0_INTERN void m0_ha_domain_put_read(struct m0_ha_domain *dom);
M0_INTERN uint64_t m0_ha_domain_get_write(struct m0_ha_domain *dom);
M0_INTERN void m0_ha_domain_put_write(struct m0_ha_domain *dom, uint64_t epoch);

struct m0_ha_epoch {
	uint64_t he_num;
} M0_XCA_RECORD;

/** @} end of ha group */

#endif /* __MERO_HA_EPOCH_H__ */

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
