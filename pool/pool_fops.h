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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 06/19/2013
 */

#pragma once

#ifndef __MERO_POOL_POOL_FOPS_H__
#define __MERO_POOL_POOL_FOPS_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"

extern struct m0_fop_type m0_fop_poolmach_query_fopt;
extern struct m0_fop_type m0_fop_poolmach_query_rep_fopt;
extern struct m0_fop_type m0_fop_poolmach_set_fopt;
extern struct m0_fop_type m0_fop_poolmach_set_rep_fopt;

M0_INTERNAL void m0_poolmach_fop_fini(void);
M0_INTERNAL int m0_poolmach_fop_init(void);

struct m0_fop_poolmach_query_rep {
	uint32_t  fpq_rc;
	uint32_t  fpq_state;
} M0_XCA_RECORD;

struct m0_fop_poolmach_query {
	uint32_t  fpq_type;
	uint32_t  fpq_index;
} M0_XCA_RECORD;

struct m0_fop_poolmach_set_rep {
	uint32_t  fps_rc;
} M0_XCA_RECORD;

struct m0_fop_poolmach_set {
	uint32_t  fps_type;
	uint32_t  fps_index;
	uint32_t  fps_state;
} M0_XCA_RECORD;

#endif /* __MERO_POOL_POOL_FOPS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
