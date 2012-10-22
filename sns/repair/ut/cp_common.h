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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 10/22/2012
 */

#pragma once

#ifndef __COLIBRI_SNS_REPAIR_UT_CP_COMMON_H__
#define __COLIBRI_SNS_REPAIR_UT_CP_COMMON_H__

#include "lib/ut.h"
#include "sns/repair/cp.h"
#include "sns/repair/ag.h"

enum {
        SEG_NR = 16,
        SEG_SIZE = 256,
};

/* Populates the bufvec with a character value. */
void bv_populate(struct c2_bufvec *b, char data);

/* Compares 2 bufvecs and asserts if not equal. */
void bv_compare(struct c2_bufvec *b1, struct c2_bufvec *b2);

inline void bv_free(struct c2_bufvec *b);

void cp_prepare(struct c2_cm_cp *cp, struct c2_bufvec *bv,
                struct c2_sns_repair_ag *sns_ag,
                char data, struct c2_fom_ops *cp_fom_ops);

#endif /* __COLIBRI_SNS_REPAIR_UT_CP_COMMON_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
