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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 09/01/2015
 */
#pragma once

#ifndef __MERO_POOL_POOLMACH_INTERNAL_H__
#define __MERO_POOL_POOLMACH_INTERNAL_H__

struct m0_poolmach;
struct m0_be_seg;
struct m0_be_tx;
struct m0_poolmach_event_link;

M0_INTERNAL
int m0_poolmach__store_init(struct m0_be_seg          *be_seg,
			    struct m0_be_tx           *be_tx,
		            uint32_t                   nr_nodes,
			    uint32_t                   nr_devices,
			    uint32_t                   max_node_failures,
			    uint32_t                   max_device_failures,
			    struct m0_poolmach_state **state,
			    struct m0_poolmach        *pm);

M0_INTERNAL int m0_poolmach__store(struct m0_poolmach            *pm,
				   struct m0_be_tx               *tx,
				   struct m0_poolmach_event_link *event_link);

M0_INTERNAL
void m0_poolmach__state_init(struct m0_poolmach_state   *state,
			     struct m0_poolnode         *nodes_array,
			     uint32_t                    nr_nodes,
			     struct m0_pooldev          *devices_array,
			     uint32_t                    nr_devices,
			     struct m0_pool_spare_usage *spare_usage_array,
			     uint32_t                    max_node_failures,
			     uint32_t                    max_device_failures,
			     struct m0_poolmach         *poolmach);

static inline uint32_t m0_pm_devices_nr(uint32_t nr_devices)
{
	/**
	 * @todo Historically 0th device was used for ADDB device, but
	 * now 0th device is not used in pool machine.
	 *
	 * It's not possible to create exactly nr_devices in pool machine
	 * since m0t1fs client uses device indexes 1..nr_devices in queries to
	 * pool machine.
	 *
	 * Once m0t1fs client uses device indexes in range 0..nr_devices-1 this
	 * function can be deleted and value nr_devices can be used directly in
	 * pool machine code.
	 */
	/* nr_devices io devices and 1 for ADDB device. */
	return nr_devices + 1;
}

#endif /* __MERO_POOL_POOLMACH_INTERNAL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
