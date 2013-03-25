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
 * Original author: Subhash Arya  <subhash_arya@xyratex.com>
 * Original creation date: 09/25/2012
 * Restructured by: Rohan Puri <Rohan_Puri@xyratex.com>
 * Restructured Date: 12/13/2012
 */

#pragma once

#ifndef __MERO_CM_UT_COMMON_SERVICE_H__
#define __MERO_CM_UT_COMMON_SERVICE_H__

#include "cm/cm.h"
#include "cm/cp.h"
#include "cm/ag.h"
#include "addb/addb.h"
#include "lib/misc.h"
#include "lib/finject.h"
#include "lib/memory.h"
#include "ut/ut.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"

extern struct m0_reqh           cm_ut_reqh;
extern struct m0_cm_cp          cm_ut_cp;
extern struct m0_cm             cm_ut;
extern struct m0_reqh_service  *cm_ut_service;

enum {
	AG_ID_NR = 4096,
	CM_UT_LOCAL_CP_NR = 4
};

extern struct m0_cm_type cm_ut_cmt;
extern const struct m0_cm_aggr_group_ops cm_ag_ut_ops;

void cm_ut_service_alloc_init();
void cm_ut_service_cleanup();

#endif /** __MERO_CM_UT_COMMON_SERVICE_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
