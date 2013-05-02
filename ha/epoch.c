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
 * Original creation date: 02-May-2013
 */


/**
 * @addtogroup ha
 *
 * @{
 */

#include "mero/magic.h"
#include "xcode/xcode.h"

#include "ha/epoch.h"
#include "ha/epoch_xc.h"

M0_TL_DESCR_DEFINE(ham, "ha epoch monitor", static, struct m0_ha_epoch_monitor,
		   hem_linkage, hem_magix,
		   M0_HA_EPOCH_MONITOR_MAGIC, M0_HA_DOMAIN_MAGIC);
M0_TL_DEFINE(ham, static, struct m0_ha_epoch_monitor);
M0_INTERNAL const uint64_t M0_HA_EPOCH_NONE = 0ULL;

M0_INTERNAL void m0_ha_domain_init(struct m0_ha_domain *dom, uint64_t epoch)
{
	dom->hdo_epoch = epoch;
	m0_rwlock_init(&dom->hdo_lock);
	ham_tlist_init(&dom->hdo_monitors);
}

M0_INTERNAL void m0_ha_domain_fini(struct m0_ha_domain *dom)
{
	ham_tlist_fini(&dom->hdo_monitors);
	m0_rwlock_fini(&dom->hdo_lock);
}

M0_INTERNAL int m0_ha_epoch_set(struct m0_xcode_obj *obj, uint64_t epoch)
{
	uint64_t *place;
	int       result;

	result = m0_xcode_find(obj, m0_ha_epoch_xc, (void **)&place);
	if (result == 0)
		*place = epoch;
	return !!result;
}

M0_INTERNAL int m0_ha_epoch_get(struct m0_xcode_obj *obj, uint64_t *epoch)
{
	uint64_t *place;
	int       result;

	result = m0_xcode_find(obj, m0_ha_epoch_xc, (void **)&place);
	if (result == 0)
		*epoch = *place;
	return !!result;
}

M0_INTERNAL void m0_ha_domain_monitor_add(struct m0_ha_domain *dom,
					struct m0_ha_epoch_monitor *mon)
{
	m0_rwlock_write_lock(&dom->hdo_lock);
	ham_tlink_init_at_tail(mon, &dom->hdo_monitors);
	m0_rwlock_write_unlock(&dom->hdo_lock);
}

M0_INTERNAL void m0_ha_domain_monitor_del(struct m0_ha_domain *dom,
					struct m0_ha_epoch_monitor *mon)
{
	m0_rwlock_write_lock(&dom->hdo_lock);
	ham_tlink_del(mon);
	m0_rwlock_write_unlock(&dom->hdo_lock);
}

M0_INTERNAL uint64_t m0_ha_domain_get_read(struct m0_ha_domain *dom)
{
	m0_rwlock_read_lock(&dom->hdo_lock);
	return dom->hdo_lock;
}

M0_INTERNAL void m0_ha_domain_put_read(struct m0_ha_domain *dom)
{
	m0_rwlock_read_unlock(&dom->hdo_lock);
}

M0_INTERNAL uint64_t m0_ha_domain_get_write(struct m0_ha_domain *dom)
{
	m0_rwlock_write_lock(&dom->hdo_lock);
	return dom->hdo_lock;
}

M0_INTERNAL void m0_ha_domain_put_write(struct m0_ha_domain *dom, uint64_t epoch)
{
	M0_PRE(epoch >= dom->hdo_epoch);
	dom->hdo_epoch = epoch;
	m0_rwlock_write_unlock(&dom->hdo_lock);
}


/** @} end of ha group */


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
