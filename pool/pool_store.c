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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 05/03/2013
 */

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"      /* M0_LOG */
#include "lib/errno.h"
#include "lib/memory.h"
#include "stob/stob.h"
#include "pool/pool.h"
#include "lib/misc.h"
#include "lib/uuid.h"

/**
   @addtogroup pool

   @{
 */

struct m0_poolnode_rec {
	enum m0_pool_nd_state pn_state;
	struct m0_uuid        pn_node_id;
};

struct m0_pooldev_rec {
	enum m0_pool_nd_state pd_state;
	struct m0_uuid        pd_dev_id;
	struct m0_uuid        pd_node_id;
};

struct m0_pool_spare_usage_rec {
	uint32_t              psu_device_index;
	enum m0_pool_nd_state psu_device_state;
};

struct m0_poolmach_state_rec {
	struct m0_pool_version_numbers psr_version;
	uint32_t                       psr_nr_nodes;
	uint32_t                       psr_nr_devices;
	uint32_t                       psr_max_node_failures;
	uint32_t                       psr_max_device_failures;
};

struct m0_pool_event_rec {
	struct m0_pool_event           per_event;
};

static const struct m0_table_ops m0_poolmach_events_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof (struct m0_pool_version_numbers)
			},
		[TO_REC] = {
			.max_size = sizeof (struct m0_pool_event_rec)
			}
	},
	.key_cmp = NULL
};

static const struct m0_table_ops m0_poolmach_store_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof (uint64_t)
			},
		[TO_REC] = {
			.max_size = ~0
			}
	},
	.key_cmp = NULL
};

/** key for poolmach state */
static uint64_t poolmach_state_key = 1ULL;

/** key for device state array record */
static uint64_t poolmach_device_state_key = 2ULL;

/** key for node state array record */
static uint64_t poolmach_node_state_key = 3ULL;

/** key for spare space slot array record */
static uint64_t poolmach_spare_key = 4ULL;

static int load_from_db(struct m0_table *table,
			struct m0_db_tx *tx,
			void            *key,
			int             size_of_key,
			void            *rec,
			int             size_of_rec)
{
	struct m0_db_pair pair;
	int               rc;

	m0_db_pair_setup(&pair, table,
			 key, size_of_key,
			 rec, size_of_rec);

	rc = m0_table_lookup(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	return rc;
}

static int store_into_db(struct m0_table *table,
			 struct m0_db_tx *tx,
			 void            *key,
			 int              size_of_key,
			 void            *rec,
			 int              size_of_rec)
{
	struct m0_db_pair pair;
	int               rc;

	m0_db_pair_setup(&pair, table,
			 key, size_of_key,
			 rec, size_of_rec);

	rc = m0_table_update(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	return rc;
}

static int m0_poolmach_events_load(struct m0_poolmach *pm,
			           struct m0_db_tx    *tx)
{
	struct m0_db_pair              pair;
	struct m0_pool_version_numbers event_key = {
					.pvn_version = {
						[PVE_READ]  = 0ULL,
						[PVE_WRITE] = 0ULL
					}
				       };
	struct m0_pool_event_rec       event_rec;
	struct m0_db_cursor            cursor;
	struct m0_pool_event_link      *event_link;
	int                            rc;

	M0_ENTRY();

	rc = m0_db_cursor_init(&cursor, &pm->pm_events_table, tx, 0);
	if (rc != 0)
		M0_RETURN(rc);

	m0_db_pair_setup(&pair, &pm->pm_events_table, &event_key, sizeof event_key,
			 &event_rec, sizeof event_rec);
	rc = m0_db_cursor_get(&cursor, &pair);
	if (rc == -ENOENT) {
		rc = 0;
		goto out;
	}
	do {
		M0_ALLOC_PTR(event_link);
		if (event_link == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		event_link->pel_event = event_rec.per_event;
		event_link->pel_new_version = event_key;
		poolmach_events_tlink_init_at_tail(event_link,
					 &pm->pm_state.pst_events_list);
		rc = m0_db_cursor_next(&cursor, &pair);
	} while (rc == 0);

out:
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	m0_db_cursor_fini(&cursor);
	M0_RETURN(rc);
}

M0_INTERNAL int m0_poolmach_event_store(struct m0_poolmach *pm,
					struct m0_db_tx    *tx,
					struct m0_pool_event_link *event_link)
{
	struct m0_pool_version_numbers event_key;
	struct m0_pool_event_rec       event_rec;
	int                            rc;

	event_key = event_link->pel_new_version;
	event_rec.per_event = event_link->pel_event;

	rc = store_into_db(&pm->pm_events_table, tx,
			   &event_key, sizeof (event_key),
			   &event_rec, sizeof (event_rec));
	return rc;
}


/**
 * Store all pool machine state into db.
 *
 * Existing records are overwritten.
 */
M0_INTERNAL int m0_poolmach_store(struct m0_poolmach *pm,
				  struct m0_db_tx    *tx)
{
	uint32_t                        nr_nodes;
	uint32_t                        nr_devices;
	uint32_t                        max_node_failures;
	uint32_t                        max_device_failures;
	struct m0_poolmach_state_rec    poolmach_rec;
	struct m0_poolnode_rec         *poolnode_rec;
	struct m0_pooldev_rec          *pooldev_rec;
	struct m0_pool_spare_usage_rec *pool_spare_usage_rec;
	uint32_t                        i;
	int                             rc;

	nr_nodes            = pm->pm_state.pst_nr_nodes;
	nr_devices          = pm->pm_state.pst_nr_devices;
	max_node_failures   = pm->pm_state.pst_max_node_failures;
	max_device_failures = pm->pm_state.pst_max_device_failures;

	M0_ALLOC_ARR(poolnode_rec, nr_nodes);
	M0_ALLOC_ARR(pooldev_rec, nr_devices);
	M0_ALLOC_ARR(pool_spare_usage_rec, max_device_failures);
	if (poolnode_rec == NULL ||
	    pooldev_rec == NULL ||
	    pool_spare_usage_rec == NULL) {
		rc = -ENOMEM;
		goto out_free;
	}

	poolmach_rec.psr_version = pm->pm_state.pst_version;
	poolmach_rec.psr_nr_nodes = nr_nodes;
	poolmach_rec.psr_nr_devices = nr_devices;
	poolmach_rec.psr_max_node_failures = max_node_failures;
	poolmach_rec.psr_max_device_failures = max_device_failures;
	rc = store_into_db(&pm->pm_table, tx,
			   &poolmach_state_key, sizeof poolmach_state_key,
			   &poolmach_rec, sizeof poolmach_rec);
	if (rc != 0)
		goto out_free;

	for (i = 0; i < nr_nodes; i++) {
		poolnode_rec[i].pn_state =
				pm->pm_state.pst_nodes_array[i].pn_state;
		/* @todo retrieve uuid: poolnode_rec[i].pn_node_id = uuid */
	}
	rc = store_into_db(&pm->pm_table, tx,
			   &poolmach_node_state_key,
			   sizeof poolmach_node_state_key,
			   poolnode_rec,
			   nr_nodes * sizeof (*poolnode_rec));
	if (rc != 0)
		goto out_free;


	for (i = 0; i < nr_devices; i++) {
		pooldev_rec[i].pd_state =
				pm->pm_state.pst_devices_array[i].pd_state;
		/* @todo pooldev_rec[i].pd_node_id = uuid */
		/* @todo pooldev_rec[i].pd_dev_id = uuid */
	}
	rc = store_into_db(&pm->pm_table, tx,
			   &poolmach_device_state_key,
			   sizeof poolmach_device_state_key,
			   pooldev_rec,
			   nr_devices * sizeof (*pooldev_rec));
	if (rc != 0)
		goto out_free;

	for (i = 0; i < max_device_failures; i++) {
		pool_spare_usage_rec[i].psu_device_index =
			pm->pm_state.pst_spare_usage_array[i].psu_device_index;
		pool_spare_usage_rec[i].psu_device_state =
			pm->pm_state.pst_spare_usage_array[i].psu_device_state;
	}
	rc = store_into_db(&pm->pm_table, tx,
			   &poolmach_spare_key,
			   sizeof poolmach_spare_key,
			   pool_spare_usage_rec,
			   max_device_failures * sizeof (*pool_spare_usage_rec));

out_free:
	m0_free(poolnode_rec);
	m0_free(pooldev_rec);
	m0_free(pool_spare_usage_rec);
	return rc;
}

static int m0_poolmach_load(struct m0_poolmach *pm,
			    struct m0_dbenv    *dbenv,
			    struct m0_db_tx    *tx,
			    uint32_t            nr_nodes,
			    uint32_t            nr_devices,
			    uint32_t            max_node_failures,
			    uint32_t            max_device_failures)
{
	struct m0_poolmach_state_rec    poolmach_rec;
	struct m0_poolnode_rec         *poolnode_rec;
	struct m0_pooldev_rec          *pooldev_rec;
	struct m0_pool_spare_usage_rec *pool_spare_usage_rec;
	uint32_t                        i;
	int                             rc;

	M0_ALLOC_ARR(poolnode_rec, nr_nodes);
	M0_ALLOC_ARR(pooldev_rec, nr_devices + 1);
	M0_ALLOC_ARR(pool_spare_usage_rec, max_device_failures);
	if (poolnode_rec == NULL ||
	    pooldev_rec == NULL ||
	    pool_spare_usage_rec == NULL) {
		/* m0_free(NULL) is OK */
		rc = -ENOMEM;
		goto out_free;
	}

	rc = load_from_db(&pm->pm_table, tx,
			  &poolmach_state_key, sizeof poolmach_state_key,
			  &poolmach_rec, sizeof poolmach_rec);
	if (rc != 0)
		goto out_free;

	if (poolmach_rec.psr_nr_nodes != nr_nodes ||
	    poolmach_rec.psr_nr_devices != nr_devices + 1 ||
	    poolmach_rec.psr_max_node_failures != max_node_failures ||
	    poolmach_rec.psr_max_device_failures != max_device_failures) {
		M0_LOG(M0_ERROR, "Pool Machine persistent state doesn't match");
		rc = -ESTALE;
		goto out_free;
	}
	pm->pm_state.pst_version = poolmach_rec.psr_version;

	rc = load_from_db(&pm->pm_table, tx,
			  &poolmach_node_state_key,
			  sizeof poolmach_node_state_key,
			  poolnode_rec,
			  nr_nodes * sizeof (*poolnode_rec));
	if (rc != 0)
		goto out_free;

	rc = load_from_db(&pm->pm_table, tx,
			  &poolmach_device_state_key,
			  sizeof poolmach_device_state_key,
			  pooldev_rec,
			  (nr_devices + 1) * sizeof (*pooldev_rec));
	if (rc != 0)
		goto out_free;

	rc = load_from_db(&pm->pm_table, tx,
			  &poolmach_spare_key,
			  sizeof poolmach_spare_key,
			  pool_spare_usage_rec,
			  max_device_failures * sizeof (*pool_spare_usage_rec));
	if (rc != 0)
		goto out_free;

	for (i = 0; i < nr_nodes; i++) {
		pm->pm_state.pst_nodes_array[i].pn_state =
						poolnode_rec[i].pn_state;
	}

	for (i = 0; i < nr_devices; i++) {
		pm->pm_state.pst_devices_array[i].pd_state =
						pooldev_rec[i].pd_state;
	}

	for (i = 0; i < max_device_failures; i++) {
		pm->pm_state.pst_spare_usage_array[i].psu_device_index =
				pool_spare_usage_rec[i].psu_device_index;
		pm->pm_state.pst_spare_usage_array[i].psu_device_state =
				pool_spare_usage_rec[i].psu_device_state;
	}

	rc = m0_poolmach_events_load(pm, tx);
out_free:
	m0_free(poolnode_rec);
	m0_free(pooldev_rec);
	m0_free(pool_spare_usage_rec);
	return rc;
}

M0_INTERNAL int m0_poolmach_store_init(struct m0_poolmach *pm,
				       struct m0_dbenv    *dbenv,
				       struct m0_dtm      *dtm,
				       uint32_t            nr_nodes,
				       uint32_t            nr_devices,
				       uint32_t            max_node_failures,
				       uint32_t            max_device_failures)
{
	struct m0_db_tx                 init_tx;
	int                             rc;

	M0_PRE(!pm->pm_is_initialised);
	M0_PRE(dbenv != NULL);
	M0_ENTRY();

	rc = m0_table_init(&pm->pm_table, dbenv,
			   "poolmach_persistent_state", 0,
			   &m0_poolmach_store_ops);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_table_init(&pm->pm_events_table, dbenv,
			   "poolmach_events", 0,
			   &m0_poolmach_events_table_ops);
	if (rc != 0) {
		m0_table_fini(&pm->pm_table);
		M0_RETURN(rc);
	}

	rc = m0_db_tx_init(&init_tx, dbenv, 0);
	if (rc != 0) {
		m0_table_fini(&pm->pm_table);
		m0_table_fini(&pm->pm_events_table);
		M0_RETURN(rc);
	}

	rc = m0_poolmach_load(pm, dbenv, &init_tx, nr_nodes, nr_devices,
			      max_node_failures, max_device_failures);
	if (rc == -ENOENT) {
		rc = m0_poolmach_store(pm, &init_tx);
	}
	if (rc == 0)
		rc = m0_db_tx_commit(&init_tx);
	else
		m0_db_tx_abort(&init_tx);

	if (rc != 0) {
		m0_table_fini(&pm->pm_table);
		m0_table_fini(&pm->pm_events_table);
	}
	return rc;
}

#undef M0_TRACE_SUBSYSTEM
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
