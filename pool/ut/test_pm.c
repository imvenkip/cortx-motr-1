/* -*- C -*- */
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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 08/15/2012
 */

#include "ut/ut.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "pool/pool.h"
#include "cob/cob.h"
#include "ut/be.h"
#include "be/ut/helper.h"

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"      /* M0_LOG */

enum {
	PM_TEST_DEFAULT_DEVICE_NUMBER      = 10,
	PM_TEST_DEFAULT_NODE_NUMBER        = 1,
	PM_TEST_DEFAULT_MAX_DEVICE_FAILURE = 1,
	PM_TEST_DEFAULT_MAX_NODE_FAILURE   = 1
};

static struct m0_sm_group       *sm_grp;
static struct m0_be_ut_backend   ut_be;
static struct m0_be_ut_seg       ut_seg;
static struct m0_be_seg         *be_seg;

/* import from pool/pool_store.c */
M0_INTERNAL int m0_poolmach_store_destroy(struct m0_poolmach *pm,
					  struct m0_be_seg   *be_seg,
					  struct m0_sm_group *sm_grp,
					  struct m0_dtm      *dtm);

static int seg_init()
{
	/* Init BE */
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, 1ULL << 24);
	be_seg = ut_seg.bus_seg;
	sm_grp = m0_be_ut_backend_sm_group_lookup(&ut_be);
	return 0;
}

static int seg_fini()
{
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
	return 0;
}

static void pm_test_init_fini(void)
{
	struct m0_poolmach pm;
	int                rc = 0;

	M0_SET0(&pm);
	rc = m0_poolmach_init(&pm, be_seg, sm_grp, NULL,
					 PM_TEST_DEFAULT_NODE_NUMBER,
					 PM_TEST_DEFAULT_DEVICE_NUMBER,
					 PM_TEST_DEFAULT_MAX_NODE_FAILURE,
					 PM_TEST_DEFAULT_MAX_DEVICE_FAILURE);
	M0_UT_ASSERT(rc == 0);
	m0_poolmach_fini(&pm);
}

static void pm_test_transit(void)
{
	struct m0_poolmach             pm;
	int                            rc;
	bool                           equal;
	struct m0_pool_event           events[4];
	struct m0_pool_version_numbers v0;
	struct m0_pool_version_numbers v1;
	struct m0_pool_version_numbers v2;
	struct m0_pool_event           e_invalid;
	struct m0_pool_version_numbers v_invalid;
	struct m0_tl                   events_list;
	struct m0_pool_event_link     *scan;
	uint32_t                       count;
	uint32_t                       index;
	struct m0_be_tx_credit         cred = {};
	struct m0_be_tx                tx;

	M0_SET0(&pm);
	rc = m0_poolmach_init(&pm, be_seg, sm_grp, NULL,
					 PM_TEST_DEFAULT_NODE_NUMBER,
					 PM_TEST_DEFAULT_DEVICE_NUMBER,
					 PM_TEST_DEFAULT_MAX_NODE_FAILURE,
					 PM_TEST_DEFAULT_MAX_DEVICE_FAILURE);
	M0_UT_ASSERT(rc == 0);
	m0_poolmach_store_credit(&pm, &cred);

	rc = m0_poolmach_current_version_get(&pm, &v0);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_current_version_get(&pm, &v1);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_current_version_get(&pm, &v2);
	M0_UT_ASSERT(rc == 0);

	equal = m0_poolmach_version_equal(&v0, &v1);
	M0_UT_ASSERT(equal);
	equal = m0_poolmach_version_equal(&v0, &v2);
	M0_UT_ASSERT(equal);

	events[0].pe_type  = M0_POOL_DEVICE;
	events[0].pe_index = 1;
	events[0].pe_state = M0_PNDS_FAILED;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &events[0], &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);

	events[1].pe_type  = M0_POOL_DEVICE;
	events[1].pe_index = 3;
	events[1].pe_state = M0_PNDS_OFFLINE;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &events[1], &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);

	rc = m0_poolmach_current_version_get(&pm, &v1);
	M0_UT_ASSERT(rc == 0);

	events[2].pe_type  = M0_POOL_DEVICE;
	events[2].pe_index = 3;
	events[2].pe_state = M0_PNDS_ONLINE;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &events[2], &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);

	events[3].pe_type  = M0_POOL_NODE;
	events[3].pe_index = 0;
	events[3].pe_state = M0_PNDS_OFFLINE;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &events[3], &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);

	rc = m0_poolmach_current_version_get(&pm, &v2);
	M0_UT_ASSERT(rc == 0);

	equal = m0_poolmach_version_equal(&v0, &v1);
	M0_UT_ASSERT(!equal);
	equal = m0_poolmach_version_equal(&v0, &v2);
	M0_UT_ASSERT(!equal);
	equal = m0_poolmach_version_equal(&v1, &v2);
	M0_UT_ASSERT(!equal);
	m0_poolmach_event_list_dump(&pm);
	m0_poolmach_version_dump(&v0);
	m0_poolmach_version_dump(&v1);
	m0_poolmach_version_dump(&v2);

	/* case 1: from v0 to v1 */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     &v0,
				     &v1,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 2);
	index = 0;
	m0_tl_for(poolmach_events, &events_list, scan) {
		struct m0_pool_event *e = &scan->pel_event;
		M0_UT_ASSERT(events[index].pe_state == e->pe_state);
		M0_UT_ASSERT(events[index].pe_type  == e->pe_type);
		M0_UT_ASSERT(events[index].pe_index == e->pe_index);

		index++;
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

	/* case 2: from v1 to v2 */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     &v1,
				     &v2,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 2);
	index = 2;
	m0_tl_for(poolmach_events, &events_list, scan) {
		struct m0_pool_event *e = &scan->pel_event;
		M0_UT_ASSERT(events[index].pe_state == e->pe_state);
		M0_UT_ASSERT(events[index].pe_type  == e->pe_type);
		M0_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

	/* case 3: from v0 to v2 */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     &v0,
				     &v2,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 4);
	index = 0;
	m0_tl_for(poolmach_events, &events_list, scan) {
		struct m0_pool_event *e = &scan->pel_event;
		M0_UT_ASSERT(events[index].pe_state == e->pe_state);
		M0_UT_ASSERT(events[index].pe_type  == e->pe_type);
		M0_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

	/* case 4: from NULL to v1 */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     NULL,
				     &v1,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 2);
	index = 0;
	m0_tl_for(poolmach_events, &events_list, scan) {
		struct m0_pool_event *e = &scan->pel_event;
		M0_UT_ASSERT(events[index].pe_state == e->pe_state);
		M0_UT_ASSERT(events[index].pe_type  == e->pe_type);
		M0_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

	/* case 5: from v1 to NULL */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     &v1,
				     NULL,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 2);
	index = 2;
	m0_tl_for(poolmach_events, &events_list, scan) {
		struct m0_pool_event *e = &scan->pel_event;
		M0_UT_ASSERT(events[index].pe_state == e->pe_state);
		M0_UT_ASSERT(events[index].pe_type  == e->pe_type);
		M0_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

	/* case 6: from NULL to NULL */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     NULL,
				     NULL,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 4);
	index = 0;
	m0_tl_for(poolmach_events, &events_list, scan) {
		struct m0_pool_event *e = &scan->pel_event;
		M0_UT_ASSERT(events[index].pe_state == e->pe_state);
		M0_UT_ASSERT(events[index].pe_type  == e->pe_type);
		M0_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

	/* this is an invalid version */
	v_invalid.pvn_version[PVE_READ]  = -1;
	v_invalid.pvn_version[PVE_WRITE] = -2;

	/* case 7: from v0 to invalid */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     &v0,
				     &v_invalid,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 4);
	index = 0;
	m0_tl_for(poolmach_events, &events_list, scan) {
		struct m0_pool_event *e = &scan->pel_event;
		M0_UT_ASSERT(events[index].pe_state == e->pe_state);
		M0_UT_ASSERT(events[index].pe_type  == e->pe_type);
		M0_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

	/* case 8: from NULL to invalid */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     NULL,
				     &v_invalid,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 4);
	index = 0;
	m0_tl_for(poolmach_events, &events_list, scan) {
		struct m0_pool_event *e = &scan->pel_event;
		M0_UT_ASSERT(events[index].pe_state == e->pe_state);
		M0_UT_ASSERT(events[index].pe_type  == e->pe_type);
		M0_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

	/* case 9: from invalid to v2 */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     &v_invalid,
				     &v2,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 0);

	/* case 10: from invalid to NULL */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     &v_invalid,
				     NULL,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 0);

	/* case 11: from invalid to invalid */
	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(&pm,
				     &v_invalid,
				     &v_invalid,
				     &events_list);
	M0_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	M0_UT_ASSERT(count == 0);

	/* invalid event. case 1: invalid type*/
	e_invalid.pe_type  = M0_POOL_NODE + 5;
	e_invalid.pe_index = 0;
	e_invalid.pe_state = M0_PNDS_OFFLINE;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &e_invalid, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == -EINVAL);

	/* invalid event. case 2: invalid index */
	e_invalid.pe_type  = M0_POOL_NODE;
	e_invalid.pe_index = 100;
	e_invalid.pe_state = M0_PNDS_OFFLINE;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &e_invalid, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == -EINVAL);

	/* invalid event. case 3: invalid state */
	e_invalid.pe_type  = M0_POOL_NODE;
	e_invalid.pe_index = 0;
	e_invalid.pe_state = M0_PNDS_SNS_REBALANCING + 1;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &e_invalid, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == -EINVAL);

	/* invalid event. case 4: invalid state */
	e_invalid.pe_type  = M0_POOL_DEVICE;
	e_invalid.pe_index = 0;
	e_invalid.pe_state = M0_PNDS_NR;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &e_invalid, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == -EINVAL);

	/* Destroy poolmach persistent storage. We will have some different
	 * poolmach parameters in next test case.
	 */
	m0_poolmach_store_destroy(&pm, be_seg, sm_grp, NULL);
	/* finally */
	m0_poolmach_fini(&pm);
}

static void pm_test_spare_slot(void)
{
	struct m0_poolmach    pm;
	int                   rc = 0;
	struct m0_pool_event  event;
	enum m0_pool_nd_state state_out;
	enum m0_pool_nd_state target_state;
	enum m0_pool_nd_state state;
	uint32_t              spare_slot;
	struct m0_be_tx       tx;
	struct m0_be_tx_credit cred = {};

	M0_SET0(&pm);
	rc = m0_poolmach_init(&pm, be_seg, sm_grp, NULL,
					 PM_TEST_DEFAULT_NODE_NUMBER,
					 PM_TEST_DEFAULT_DEVICE_NUMBER,
					 PM_TEST_DEFAULT_MAX_NODE_FAILURE,
					 2 /* two spare device */);
	M0_UT_ASSERT(rc == 0);
	m0_poolmach_store_credit(&pm, &cred);

	event.pe_type  = M0_POOL_DEVICE;
	event.pe_index = 1;


	/* FAILED */
	target_state = M0_PNDS_FAILED;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_device_state(&pm, 1, &state_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(state_out == target_state);

	rc = m0_poolmach_device_state(&pm, PM_TEST_DEFAULT_DEVICE_NUMBER,
					&state_out);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_device_state(&pm, PM_TEST_DEFAULT_DEVICE_NUMBER + 1,
					&state_out);
	M0_UT_ASSERT(rc == -EINVAL);
	rc = m0_poolmach_device_state(&pm, 100, &state_out);
	M0_UT_ASSERT(rc == -EINVAL);

	for (state = M0_PNDS_ONLINE; state < M0_PNDS_NR; state++) {
		if (state == M0_PNDS_SNS_REPAIRING)
			continue;
		/* transit to other state other than the above one is invalid */
		event.pe_state = state;
		m0_ut_be_tx_begin(&tx, &ut_be, &cred);
		rc = m0_poolmach_state_transit(&pm, &event, &tx);
		m0_ut_be_tx_end(&tx);
		M0_UT_ASSERT(rc == -EINVAL);
	}

	rc = m0_poolmach_device_state(&pm, 1, &state_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(state_out == target_state);


	/* transit to SNS_REPAIRING */
	target_state = M0_PNDS_SNS_REPAIRING;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_device_state(&pm, 1, &state_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(state_out == target_state);
	/* the first spare slot is used by device 1 */
	rc = m0_poolmach_sns_repair_spare_query(&pm, 1, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 0);
	/* no spare slot is used by device 2 */
	rc = m0_poolmach_sns_repair_spare_query(&pm, 2, &spare_slot);
	M0_UT_ASSERT(rc == -ENOENT);
	for (state = M0_PNDS_ONLINE; state < M0_PNDS_NR; state++) {
		if (state == M0_PNDS_SNS_REPAIRED)
			continue;
		/* transit to other state other than the above one is invalid */
		event.pe_state = state;
		m0_ut_be_tx_begin(&tx, &ut_be, &cred);
		rc = m0_poolmach_state_transit(&pm, &event, &tx);
		m0_ut_be_tx_end(&tx);
		M0_UT_ASSERT(rc == -EINVAL);
	}


	/* transit to SNS_REPAIRED */
	target_state = M0_PNDS_SNS_REPAIRED;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_device_state(&pm, 1, &state_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(state_out == target_state);
	/* the first spare slot is used by device 1 */
	rc = m0_poolmach_sns_repair_spare_query(&pm, 1, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 0);
	/* no spare slot is used by device 2 */
	rc = m0_poolmach_sns_repair_spare_query(&pm, 2, &spare_slot);
	M0_UT_ASSERT(rc == -ENOENT);
	for (state = M0_PNDS_ONLINE; state < M0_PNDS_NR; state++) {
		if (state == M0_PNDS_SNS_REBALANCING)
			continue;
		/* transit to other state other than the above one is invalid */
		event.pe_state = state;
		m0_ut_be_tx_begin(&tx, &ut_be, &cred);
		rc = m0_poolmach_state_transit(&pm, &event, &tx);
		m0_ut_be_tx_end(&tx);
		M0_UT_ASSERT(rc == -EINVAL);
	}


	/* transit to SNS_REBALANCING */
	target_state = M0_PNDS_SNS_REBALANCING;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_device_state(&pm, 1, &state_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(state_out == target_state);
	/* the first spare slot is used by device 1 */
	rc = m0_poolmach_sns_rebalance_spare_query(&pm, 1, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 0);
	for (state = M0_PNDS_ONLINE; state < M0_PNDS_NR; state++) {
		if (state == M0_PNDS_ONLINE)
			continue;
		/* transit to other state other than the above one is invalid */
		event.pe_state = state;
		m0_ut_be_tx_begin(&tx, &ut_be, &cred);
		rc = m0_poolmach_state_transit(&pm, &event, &tx);
		m0_ut_be_tx_end(&tx);
		M0_UT_ASSERT(rc == -EINVAL);
	}

	/* transit to ONLINE */
	target_state = M0_PNDS_ONLINE;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_device_state(&pm, 1, &state_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(state_out == target_state);
	/* the first spare slot is not used any more */
	rc = m0_poolmach_sns_repair_spare_query(&pm, 1, &spare_slot);
	M0_UT_ASSERT(rc == -ENOENT);

	/* Destroy poolmach persistent storage. We will have some different
	 * poolmach parameters in next test case.
	 */
	m0_poolmach_store_destroy(&pm, be_seg, sm_grp, NULL);
	/* finally */
	m0_poolmach_fini(&pm);
}

static void pm_test_multi_fail(void)
{
	struct m0_poolmach    pm;
	int                   rc = 0;
	struct m0_pool_event  event;
	enum m0_pool_nd_state state_out;
	enum m0_pool_nd_state target_state;
	uint32_t              spare_slot;
	struct m0_be_tx       tx;
	struct m0_be_tx_credit cred = {};

	M0_SET0(&pm);
	rc = m0_poolmach_init(&pm, be_seg, sm_grp, NULL,
					 PM_TEST_DEFAULT_NODE_NUMBER,
					 PM_TEST_DEFAULT_DEVICE_NUMBER,
					 PM_TEST_DEFAULT_MAX_NODE_FAILURE,
					 3 /*three spare device */);
	M0_UT_ASSERT(rc == 0);
	m0_poolmach_store_credit(&pm, &cred);

	event.pe_type  = M0_POOL_DEVICE;

	/* device 1 FAILED */
	event.pe_index = 1;
	target_state = M0_PNDS_FAILED;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_device_state(&pm, 1, &state_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(state_out == target_state);

	/* device 2 FAILED */
	event.pe_index = 2;
	target_state = M0_PNDS_FAILED;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_device_state(&pm, 2, &state_out);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(state_out == target_state);

	/* transit device 1 to SNS_REPAIRING */
	event.pe_index = 1;
	target_state = M0_PNDS_SNS_REPAIRING;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	/* the first spare slot is used by device 1 */
	rc = m0_poolmach_sns_repair_spare_query(&pm, 1, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 0);

	/* transit device 2 to SNS_REPAIRING */
	event.pe_index = 2;
	target_state = M0_PNDS_SNS_REPAIRING;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	/* the second spare slot is used by device 2 */
	rc = m0_poolmach_sns_repair_spare_query(&pm, 2, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 1);


	/* transit device 1 to SNS_REPAIRED */
	event.pe_index = 1;
	target_state = M0_PNDS_SNS_REPAIRED;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	/* the first spare slot is used by device 1 */
	rc = m0_poolmach_sns_repair_spare_query(&pm, 1, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 0);

	/* transit device 2 to SNS_REPAIRED */
	event.pe_index = 2;
	target_state = M0_PNDS_SNS_REPAIRED;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	/* the second spare slot is used by device 2 */
	rc = m0_poolmach_sns_repair_spare_query(&pm, 2, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 1);


	/* transit device 1 to SNS_REBALANCING */
	event.pe_index = 1;
	target_state = M0_PNDS_SNS_REBALANCING;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	/* the first spare slot is used by device 1 */
	rc = m0_poolmach_sns_rebalance_spare_query(&pm, 1, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 0);

	/* transit device 2 to SNS_REBALANCING */
	event.pe_index = 2;
	target_state = M0_PNDS_SNS_REBALANCING;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	/* the second spare slot is used by device 2 */
	rc = m0_poolmach_sns_rebalance_spare_query(&pm, 2, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 1);


	/* transit device 2 to ONLINE */
	event.pe_index = 2;
	target_state = M0_PNDS_ONLINE;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_sns_repair_spare_query(&pm, 2, &spare_slot);
	M0_UT_ASSERT(rc == -ENOENT);

	/* transit device 3 to FAILED */
	event.pe_index = 3;
	target_state = M0_PNDS_FAILED;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	target_state = M0_PNDS_SNS_REPAIRING;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_sns_repair_spare_query(&pm, 3, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 1);

	/* transit device 1 to ONLINE */
	event.pe_index = 1;
	target_state = M0_PNDS_ONLINE;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_sns_repair_spare_query(&pm, 1, &spare_slot);
	M0_UT_ASSERT(rc == -ENOENT);

	/* transit device 4 to FAILED */
	event.pe_index = 4;
	target_state = M0_PNDS_FAILED;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	target_state = M0_PNDS_SNS_REPAIRING;
	event.pe_state = target_state;
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	rc = m0_poolmach_state_transit(&pm, &event, &tx);
	m0_ut_be_tx_end(&tx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_poolmach_sns_repair_spare_query(&pm, 4, &spare_slot);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spare_slot == 0);

	/* We will keep the poolmach in persistent storage. It will be loaded
	 * in next test case.
	 */
	/* finally */
	m0_poolmach_fini(&pm);
}

/* load from last test case */
static void pm_test_load_from_persistent_storage(void)
{
	struct m0_poolmach pm;
	int                rc = 0;

	M0_SET0(&pm);
	rc = m0_poolmach_init(&pm, be_seg, sm_grp, NULL,
					 PM_TEST_DEFAULT_NODE_NUMBER,
					 PM_TEST_DEFAULT_DEVICE_NUMBER,
					 PM_TEST_DEFAULT_MAX_NODE_FAILURE,
					 3);
	M0_UT_ASSERT(rc == 0);
	m0_poolmach_fini(&pm);

	/* Use some different parameters. Error should be returned. */
	rc = m0_poolmach_init(&pm, be_seg, sm_grp, NULL,
					 PM_TEST_DEFAULT_NODE_NUMBER + 1,
					 PM_TEST_DEFAULT_DEVICE_NUMBER,
					 PM_TEST_DEFAULT_MAX_NODE_FAILURE,
					 3);
	M0_UT_ASSERT(rc == -EINVAL);

	/* Now with proper parameters, it should work again. */
	rc = m0_poolmach_init(&pm, be_seg, sm_grp, NULL,
					 PM_TEST_DEFAULT_NODE_NUMBER,
					 PM_TEST_DEFAULT_DEVICE_NUMBER,
					 PM_TEST_DEFAULT_MAX_NODE_FAILURE,
					 3);
	M0_UT_ASSERT(rc == 0);

	/* Destroy poolmach persistent storage.
	 */
	m0_poolmach_store_destroy(&pm, be_seg, sm_grp, NULL);
	m0_poolmach_fini(&pm);
}

const struct m0_test_suite poolmach_ut = {
	.ts_name = "poolmach-ut",
	.ts_init = seg_init,
	.ts_fini = seg_fini,
	.ts_tests = {
		{ "pm_test init & fini",   pm_test_init_fini                  },
		{ "pm_test state transit", pm_test_transit                    },
		{ "pm_test spare slot",    pm_test_spare_slot                 },
		{ "pm_test multi fail",    pm_test_multi_fail                 },
		{ "pm_test load",         pm_test_load_from_persistent_storage},
		{ NULL,                    NULL                               }
	}
};
M0_EXPORTED(poolmach_ut);
#undef M0_TRACE_SUBSYSTEM
