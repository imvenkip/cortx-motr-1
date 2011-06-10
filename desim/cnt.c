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
 */
/*
 * Copyright 2009 ClusterStor.
 *
 * Nikita Danilov.
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <math.h>

#include "desim/sim.h"
#include "desim/cnt.h"

/**
   @addtogroup desim desim
   @{
 */

static struct c2_list cnts;

void cnt_init(struct cnt *cnt, struct cnt *parent, const char *format, ...)
{
	va_list valist;

	memset(cnt, 0, sizeof *cnt);
	cnt->c_min = ~0ULL;
	cnt->c_max = 0;
	va_start(valist, format);
	sim_name_vaset(&cnt->c_name, format, valist);
	va_end(valist);
	cnt->c_parent = parent;
	c2_list_add_tail(&cnts, &cnt->c_linkage);
}

void cnt_dump(struct cnt *cnt)
{
	cnt_t  avg;
	double sig;

	if (cnt->c_nr != 0) {
		avg = cnt->c_sum / cnt->c_nr;
		sig = sqrt(cnt->c_sq/cnt->c_nr - avg*avg);
		sim_log(NULL, SLL_INFO, "[%s: %llu (%llu) %llu %llu %f]\n",
			cnt->c_name, avg, cnt->c_nr, 
			cnt->c_min, cnt->c_max, sig);
	} else
		sim_log(NULL, SLL_INFO, "[%s: empty]\n", cnt->c_name);
}

void cnt_dump_all(void)
{
	struct cnt *scan;

	c2_list_for_each_entry(&cnts, scan, struct cnt, c_linkage)
		cnt_dump(scan);
}

void cnt_fini(struct cnt *cnt)
{
	if (cnt->c_name != NULL)
		free(cnt->c_name);
	c2_list_del(&cnt->c_linkage);
}


void cnt_mod(struct cnt *cnt, cnt_t val)
{
	do {
		cnt->c_sum += val;
		cnt->c_nr++;
		cnt->c_sq += val*val;
		if (val > cnt->c_max)
			cnt->c_max = val;
		if (val < cnt->c_min)
			cnt->c_min = val;
	} while ((cnt = cnt->c_parent) != NULL);
}

void cnt_global_init(void)
{
	c2_list_init(&cnts);
}

void cnt_global_fini(void)
{
	struct cnt *scan;
	struct cnt *next;

	c2_list_for_each_entry_safe(&cnts, scan, next, struct cnt, c_linkage)
		cnt_fini(scan);

	c2_list_fini(&cnts);
}

/** @} end of desim group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
