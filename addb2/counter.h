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
 * Original creation date: 27-Jan-2015
 */

#pragma once

#ifndef __MERO_ADDB2_COUNTER_H__
#define __MERO_ADDB2_COUNTER_H__

/**
 * @addtogroup addb2
 *
 * @{
 */

#include "lib/types.h"
#include "lib/tlist.h"
#include "addb2/addb2.h"

struct m0_tl;

struct m0_addb2_counter_data {
	uint64_t cod_nr;
	int64_t  cod_min;
	int64_t  cod_max;
	int64_t  cod_sum;
	uint64_t cod_ssq;
};

struct m0_addb2_counter {
	struct m0_addb2_sensor       co_sensor;
	struct m0_addb2_counter_data co_val;
};

void m0_addb2_counter_add(struct m0_addb2_counter *counter, uint64_t label,
			  int idx);
void m0_addb2_counter_del(struct m0_addb2_counter *counter);
void m0_addb2_counter_mod(struct m0_addb2_counter *counter, int64_t val);

struct m0_addb2_list_counter {
	struct m0_addb2_sensor  lc_sensor;
	struct m0_tl           *lc_list;
};

void m0_addb2_list_counter_add(struct m0_addb2_list_counter *counter,
			       struct m0_tl *list, uint64_t label, int idx);
void m0_addb2_list_counter_del(struct m0_addb2_list_counter *counter);

void m0_addb2_clock_add(struct m0_addb2_sensor *clock, uint64_t label, int idx);
void m0_addb2_clock_del(struct m0_addb2_sensor *clock);

/** @} end of addb2 group */
#endif /* __MERO_ADDB2_COUNTER_H__ */

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
