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

#include "lib/misc.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_OTHER
#include "lib/trace.h"
#include "mero/magic.h"
#include "rpc/rpc_machine.h"
#include "reqh/reqh.h"

#include "ha/epoch.h"

M0_TL_DESCR_DEFINE(m0_ham, "ha epoch monitor", M0_INTERNAL,
		   struct m0_ha_epoch_monitor, hem_linkage, hem_magix,
		   M0_HA_EPOCH_MONITOR_MAGIC, M0_HA_DOMAIN_MAGIC);
M0_TL_DEFINE(m0_ham, M0_INTERNAL, struct m0_ha_epoch_monitor);
M0_INTERNAL const uint64_t M0_HA_EPOCH_NONE = 0ULL;

static int default_mon_future(struct m0_ha_epoch_monitor *self,
			      uint64_t epoch, const struct m0_rpc_item *item)
{
	return M0_HEO_OBEY;
}

M0_INTERNAL void m0_ha_domain_init(struct m0_ha_domain *dom, uint64_t epoch)
{
	dom->hdo_epoch = epoch;
	m0_rwlock_init(&dom->hdo_lock);
	m0_ham_tlist_init(&dom->hdo_monitors);
	dom->hdo_default_mon = (struct m0_ha_epoch_monitor) {
		.hem_future = default_mon_future
	};
	m0_ha_domain_monitor_add(dom, &dom->hdo_default_mon);
}

M0_INTERNAL void m0_ha_domain_fini(struct m0_ha_domain *dom)
{
	m0_ha_domain_monitor_del(dom, &dom->hdo_default_mon);
	m0_ham_tlist_fini(&dom->hdo_monitors);
	m0_rwlock_fini(&dom->hdo_lock);
}

M0_INTERNAL void m0_ha_domain_monitor_add(struct m0_ha_domain *dom,
					struct m0_ha_epoch_monitor *mon)
{
	m0_rwlock_write_lock(&dom->hdo_lock);
	m0_ham_tlink_init_at(mon, &dom->hdo_monitors);
	m0_rwlock_write_unlock(&dom->hdo_lock);
}

M0_INTERNAL void m0_ha_domain_monitor_del(struct m0_ha_domain *dom,
					struct m0_ha_epoch_monitor *mon)
{
	m0_rwlock_write_lock(&dom->hdo_lock);
	m0_ham_tlist_del(mon);
	m0_rwlock_write_unlock(&dom->hdo_lock);
}

M0_INTERNAL uint64_t m0_ha_domain_get_read(struct m0_ha_domain *dom)
{
	m0_rwlock_read_lock(&dom->hdo_lock);
	return dom->hdo_epoch;
}

M0_INTERNAL void m0_ha_domain_put_read(struct m0_ha_domain *dom)
{
	m0_rwlock_read_unlock(&dom->hdo_lock);
}

M0_INTERNAL uint64_t m0_ha_domain_get_write(struct m0_ha_domain *dom)
{
	m0_rwlock_write_lock(&dom->hdo_lock);
	return dom->hdo_epoch;
}

M0_INTERNAL void m0_ha_domain_put_write(struct m0_ha_domain *dom, uint64_t epoch)
{
	M0_PRE(epoch >= dom->hdo_epoch);
	dom->hdo_epoch = epoch;
	m0_rwlock_write_unlock(&dom->hdo_lock);
}

M0_INTERNAL int m0_ha_global_init(void)
{
	return 0;
}

M0_INTERNAL void m0_ha_global_fini(void)
{
}

M0_INTERNAL int m0_ha_epoch_check(const struct m0_rpc_item *item)
{
	struct m0_ha_domain             *ha_dom;
	uint64_t                         item_epoch = item->ri_ha_epoch;
	uint64_t                         epoch;
	struct m0_ha_epoch_monitor      *mon;
	int                              rc = 0;

	ha_dom = &item->ri_rmachine->rm_reqh->rh_hadom;
	M0_LOG(M0_DEBUG, "mine=%lu rcvd=%lu",
				(unsigned long)ha_dom->hdo_epoch,
				(unsigned long)item_epoch);
	if (item_epoch == ha_dom->hdo_epoch)
		return 0;

	epoch = m0_ha_domain_get_write(ha_dom);

	/*
	 * Domain epoch could be changed before we took the lock
	 * with m0_ha_domain_get_write(), so let's check it again.
	 */
	if (epoch == item_epoch)
		goto out;

	m0_tl_for(m0_ham, &ha_dom->hdo_monitors, mon) {
		if (item_epoch > epoch && mon->hem_future != NULL)
			rc = mon->hem_future(mon, epoch, item);
		else if (mon->hem_past != NULL)
			rc = mon->hem_past(mon, epoch, item);
		else
			continue;

		if (rc == M0_HEO_CONTINUE) {
			continue;
		} else if (rc == M0_HEO_OK) {
			break;
		} else if (rc == M0_HEO_OBEY) {
			M0_LOG(M0_DEBUG, "old=%lu new=%lu",
						(unsigned long)epoch,
						(unsigned long)item_epoch);
			epoch = item_epoch;
			break;
		} else if (M0_IN(rc, (M0_HEO_DROP, M0_HEO_ERROR))) {
			rc = 1;
			break;
		}
	} m0_tl_endfor;

out:
	m0_ha_domain_put_write(ha_dom, epoch);

	return rc;
}

#undef M0_TRACE_SUBSYSTEM

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
