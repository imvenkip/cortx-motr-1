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
 * Original author: Rajesh Bhalerao <Rajesh_Bhalerao@xyratex.com>
 * Original creation date: 07/07/2011
 */

#pragma once

#ifndef __COLIBRI_RM_FOMS_H__
#define __COLIBRI_RM_FOMS_H__

#include "fop/fop.h"
#include "rm/rm.h"
#include "rm/rm_internal.h"

/**
 * @addtogroup rm
 *
 * This file includes data structures used by RM:fop layer.
 *
 * @{
 *
 */
enum m0_rm_fom_phases {
	FOPH_RM_REQ_START = C2_FOPH_NR + 1,
	FOPH_RM_REQ_WAIT = C2_FOPH_NR + 2,
	FOPH_RM_REQ_FINISH = C2_FOPH_NR + 3,
};

struct rm_request_fom {
	/** Generic m0_fom object */
	struct m0_fom		     rf_fom;
	/** Incoming request */
	struct m0_rm_remote_incoming rf_in;
};

/** @} */

/* __COLIBRI_RM_FOMS_H__ */
#endif

/**
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
