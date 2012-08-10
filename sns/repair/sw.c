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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 16/04/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"

#include "sns/repair/cm.h"
#include "cm/sw.h"
#include "lib/finject.h"

/**
  @addtogroup snsrepair

  @{
*/

static size_t sw_size_cal(struct c2_cm_sw *slw);
static int sw_advance(struct c2_cm_sw *slw);
static int sw_slide(struct c2_cm_sw *slw);
static int sw_expand(struct c2_cm_sw *slw);

const struct c2_cm_sw_ops sw_ops = {
	.swo_size_cal = sw_size_cal,
	.swo_advance  = sw_advance,
	.swo_slide    = sw_slide,
	.swo_expand   = sw_expand
};

static size_t sw_size_cal(struct c2_cm_sw *slw)
{
	return 0;
}

static int sw_advance(struct c2_cm_sw *slw)
{
	return 0;
}

static int sw_slide(struct c2_cm_sw *slw)
{
	return 0;
}

static int sw_expand(struct c2_cm_sw *slw)
{
	return 0;
}

/** @} snsrepair */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
