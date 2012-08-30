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
 */
/*
 * Copyright 2009 ClusterStor.
 *
 * Nikita Danilov.
 */

#pragma once

#ifndef __COLIBRI_DESIM_CNT_H__
#define __COLIBRI_DESIM_CNT_H__

/**
   @addtogroup desim desim
   @{
 */

#include "lib/tlist.h"

typedef unsigned long long cnt_t;

struct cnt {
	cnt_t               c_sum;
	cnt_t               c_min;
	cnt_t               c_max;
	cnt_t               c_nr;
	double              c_sq;
	char               *c_name;
	struct c2_tlink     c_linkage;
	struct cnt         *c_parent;
	uint64_t            c_magic;
};

void cnt_init(struct cnt *cnt, struct cnt *parent, const char *name, ...)
              __attribute__((format(printf, 3, 4)));
void cnt_fini(struct cnt *cnt);
void cnt_dump(struct cnt *cnt);
void cnt_dump_all(void);

void cnt_mod(struct cnt *cnt, cnt_t val);

void cnt_global_init(void);
void cnt_global_fini(void);

#endif /* __COLIBRI_DESIM_CNT_H__ */

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
