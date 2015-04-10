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


/**
 * @addtogroup addb2
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_ADDB

#include "lib/trace.h"
#include "lib/time.h"
#include "lib/tlist.h"
#include "lib/arith.h"        /* min64u, max64u, m0_addu64_will_overflow */

#include "addb2/counter.h"

static const struct m0_addb2_sensor_ops sensor_ops;
static const struct m0_addb2_sensor_ops list_sensor_ops;
static const struct m0_addb2_sensor_ops clock_sensor_ops;
static void counter_warn(struct m0_addb2_counter *c, uint64_t val);
static void counter_data_init(struct m0_addb2_counter_data *d);

void m0_addb2_counter_add(struct m0_addb2_counter *counter, uint64_t label,
			  int idx)
{
	struct m0_addb2_counter_data *d = &counter->co_val;

	M0_CASSERT(sizeof *d / sizeof(uint64_t) * sizeof(uint64_t) ==
		   sizeof *d);

	M0_PRE(M0_IS0(counter));
	counter_data_init(d);
	m0_addb2_sensor_add(&counter->co_sensor, label,
			    sizeof *d / sizeof(uint64_t), idx, &sensor_ops);
}

void m0_addb2_counter_del(struct m0_addb2_counter *counter)
{
	m0_addb2_sensor_del(&counter->co_sensor);
}

void m0_addb2_counter_mod(struct m0_addb2_counter *counter, int64_t val)
{
	struct m0_addb2_counter_data *d   = &counter->co_val;
	uint64_t                      sq  = val * val;
	int64_t                       sum = d->cod_sum + val;

	if (d->cod_nr == ~0ULL)
		; /* overflown, do nothing. */
	else if ((val > 0 && (sum <= d->cod_sum ||  val > sq)) ||
		 (val < 0 && (sum >= d->cod_sum || -val > sq))) {
		counter_warn(counter, val);
		d->cod_nr = ~0ULL;
	} else {
		d->cod_nr ++;
		d->cod_sum = sum;
		d->cod_min = min64u(d->cod_min, val);
		d->cod_max = max64u(d->cod_max, val);
		d->cod_ssq += sq;
	}
}

void m0_addb2_list_counter_add(struct m0_addb2_list_counter *counter,
			       struct m0_tl *list, uint64_t label, int idx)
{
	counter->lc_list = list;
	m0_addb2_sensor_add(&counter->lc_sensor,
			    label, 1, idx, &list_sensor_ops);
}

void m0_addb2_list_counter_del(struct m0_addb2_list_counter *counter)
{
	m0_addb2_sensor_del(&counter->lc_sensor);
}

void m0_addb2_clock_add(struct m0_addb2_sensor *clock, uint64_t label, int idx)
{
	m0_addb2_sensor_add(clock, label, 1, idx, &clock_sensor_ops);
}

void m0_addb2_clock_del(struct m0_addb2_sensor *clock)
{
	m0_addb2_sensor_del(clock);
}

static void counter_snapshot(struct m0_addb2_sensor *s, uint64_t *area)
{
	struct m0_addb2_counter      *counter = M0_AMB(counter, s, co_sensor);
	struct m0_addb2_counter_data *d       = &counter->co_val;

	*(struct m0_addb2_counter_data *)area = *d;
	counter_data_init(d);
}

static void counter_fini(struct m0_addb2_sensor *s)
{;}

static const struct m0_addb2_sensor_ops sensor_ops = {
	.so_snapshot = &counter_snapshot,
	.so_fini     = &counter_fini
};

static void counter_warn(struct m0_addb2_counter *c, uint64_t val)
{
	struct m0_addb2_counter_data *d = &c->co_val;

	M0_LOG(M0_WARN, "addb2 counter overflows: @%"PRIx64
	       " nr:  %"PRIx64
	       " sum: %"PRIx64
	       " min: %"PRIx64
	       " max: %"PRIx64
	       " ssq: %"PRIx64
	       " val: %"PRIx64".", c->co_sensor.s_id,
	       d->cod_nr, d->cod_sum, d->cod_min, d->cod_max, d->cod_ssq, val);
}

static void counter_data_init(struct m0_addb2_counter_data *d)
{
	M0_SET0(d);
	d->cod_min = UINT64_MAX;
}

static void list_counter_snapshot(struct m0_addb2_sensor *s, uint64_t *area)
{
	struct m0_addb2_list_counter *counter = M0_AMB(counter, s, lc_sensor);

	area[0] = m0_list_length(&counter->lc_list->t_head);
}

static const struct m0_addb2_sensor_ops list_sensor_ops = {
	.so_snapshot = &list_counter_snapshot,
	.so_fini     = &counter_fini
};

static void clock_counter_snapshot(struct m0_addb2_sensor *s, uint64_t *area)
{
	area[0] = m0_time_now();
}

static const struct m0_addb2_sensor_ops clock_sensor_ops = {
	.so_snapshot = &clock_counter_snapshot,
	.so_fini     = &counter_fini
};

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_ADDB

/** @} end of addb2 group */

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
