/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 07/15/2010
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "stob/stob.h"
#include "pool/pool.h"
#include "lib/misc.h"

/**
   @addtogroup pool

   XXX Stub code for now.
   @{
 */

C2_TL_DESCR_DEFINE(poolmach_events, "pool machine events list", static,
                   struct c2_pool_event, pe_linkage, pe_magic,
                   C2_POOL_EVENTS_LIST_MAGIC, C2_POOL_EVENTS_HEAD_MAGIC);

C2_TL_DEFINE(poolmach_events, static, struct c2_pool_event);


int c2_pool_init(struct c2_pool *pool, uint32_t width)
{
	pool->po_width = width;
	return 0;
}

void c2_pool_fini(struct c2_pool *lay)
{
}

int c2_pool_alloc(struct c2_pool *pool, struct c2_stob_id *id)
{
	static uint64_t seq = 3;

	id->si_bits.u_hi = (uint64_t)pool;
	id->si_bits.u_lo  = seq++;
	C2_POST(c2_stob_id_is_set(id));
	return 0;
}

void c2_pool_put(struct c2_pool *pool, struct c2_stob_id *id)
{
	C2_PRE(c2_stob_id_is_set(id));
}

int c2_pools_init(void)
{
	return 0;
}

void c2_pools_fini(void)
{
}

bool c2_poolmach_version_equal(const struct c2_pool_version_numbers *v1,
			       const struct c2_pool_version_numbers *v2)
{
	return !memcmp(v1, v2, sizeof *v1);
}

int  c2_poolmach_init(struct c2_poolmach *pm, struct c2_dtm *dtm)
{
	uint32_t i;
	C2_PRE(!pm->pm_is_initialised);

	/* TODO Init pool machine, build its state from persistent storage
	 * This involves to read latest pool machine state from storage,
	 * and build its state transit history (represented by list of events).
	 *
	 * XXX temoprarily, we build this from scratch.
	 */
	C2_SET0(&pm->pm_state);
	pm->pm_state.pst_version.pvn_version[PVE_READ]  = 0;
	pm->pm_state.pst_version.pvn_version[PVE_WRITE] = 0;
	pm->pm_state.pst_nr_nodes = 1;
	pm->pm_state.pst_nodes_array = c2_alloc(pm->pm_state.pst_nr_nodes *
						sizeof (struct c2_poolnode));
	for (i = 0; i < pm->pm_state.pst_nr_nodes; i++) {
		pm->pm_state.pst_nodes_array[i].pn_state = PNDS_ONLINE;
		/* TODO use real node id */
		pm->pm_state.pst_nodes_array[i].pn_id = NULL;
	}

	pm->pm_state.pst_nr_devices = 10;
	pm->pm_state.pst_devices_array = c2_alloc(pm->pm_state.pst_nr_devices *
						  sizeof (struct c2_pooldev));
	for (i = 0; i < pm->pm_state.pst_nr_devices; i++) {
		pm->pm_state.pst_devices_array[i].pd_state = PNDS_ONLINE;
		/* TODO use real device id */
		pm->pm_state.pst_devices_array[i].pd_id = NULL;
		pm->pm_state.pst_devices_array[i].pd_node = NULL;
	}

	pm->pm_state.pst_max_node_failures = 1;
	pm->pm_state.pst_max_device_failures = 1;
	poolmach_events_tlist_init(&pm->pm_state.pst_events_list);

	c2_rwlock_init(&pm->pm_lock);
	pm->pm_is_initialised = true;
	return 0;
}

void c2_poolmach_fini(struct c2_poolmach *pm)
{
	c2_rwlock_write_lock(&pm->pm_lock);
	/* TODO Sync the pool machine state onto persistent storage */
	c2_rwlock_write_unlock(&pm->pm_lock);

	pm->pm_is_initialised = false;
	c2_rwlock_fini(&pm->pm_lock);
}

int c2_poolmach_state_transit(struct c2_poolmach *pm,
			      struct c2_pool_event *event)
{
	C2_PRE(pm != NULL);
	C2_PRE(event != NULL);

	/* Step 1: lock the poolmach */
	c2_rwlock_write_lock(&pm->pm_lock);

	/* step 2: Update the state according to event */
	/* TODO: parse the event, updating node/device array. */

	/* step 3: Increase the version */
	++ pm->pm_state.pst_version.pvn_version[PVE_READ];
	++ pm->pm_state.pst_version.pvn_version[PVE_WRITE];

	/* Step 4: copy new version into event, and link it into list */
	event->pe_new_version = pm->pm_state.pst_version;
	poolmach_events_tlink_init_at(event, &pm->pm_state.pst_events_list);

	/* Step 5: unlock the poolmach */
	c2_rwlock_write_unlock(&pm->pm_lock);
	return 0;
}

int c2_poolmach_state_query(struct c2_poolmach *pm,
			    const struct c2_pool_version_numbers *from,
			    const struct c2_pool_version_numbers *to,
			    struct c2_tl *event_list_head)
{
	struct c2_pool_event *scan;
	struct c2_pool_event *event;
	int                   rc = 0;

	C2_PRE(pm != NULL && event_list_head != NULL);

	c2_rwlock_read_lock(&pm->pm_lock);
	if (from != NULL) {
		c2_tl_for(poolmach_events, &pm->pm_state.pst_events_list, scan){
			if (memcmp(&scan->pe_new_version, from, sizeof *from) == 0)
				break;
		} c2_tl_endfor;
	} else {
		scan = poolmach_events_tlist_head(&pm->pm_state.pst_events_list);
	}
	C2_ASSERT(scan != NULL);

	while (scan != NULL) {
		/* allocate a copy of the event, add it to output list. */
		event = c2_alloc(sizeof *event);
		if (event == NULL) {
			rc = -ENOMEM;
			/* TODO destroy all allocated event */
			break;
		}
		*event = *scan;
		poolmach_events_tlink_init_at(event, event_list_head);

		if (to != NULL && memcmp(&scan->pe_new_version, to, sizeof *to) == 0)
			break;
		scan = poolmach_events_tlist_next(&pm->pm_state.pst_events_list,
						  scan);
	}

	c2_rwlock_read_unlock(&pm->pm_lock);
	return rc;
}


int c2_poolmach_current_version_get(struct c2_poolmach *pm,
				    struct c2_pool_version_numbers *curr)
{
	C2_PRE(pm != NULL && curr != NULL);

	c2_rwlock_read_lock(&pm->pm_lock);
	*curr = pm->pm_state.pst_version;
	c2_rwlock_read_unlock(&pm->pm_lock);
	return 0;
}

int c2_poolmach_current_state_get(struct c2_poolmach *pm,
				  struct c2_poolmach_state **state_copy)
{
	return -ENOENT;
}

void c2_poolmach_state_free(struct c2_poolmach *pm,
			   struct c2_poolmach_state *state)
{
}

/** @} end group pool */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
