/* -*- C -*- */
#ifndef __COLIBRI_RM_FOMS_H__
#define __COLIBRI_RM_FOMS_H__

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
 * Original author: Rajesh Bhalerao <Rajesh_Bhalerao@xyratex.com>
 * Original creation date: 07/07/2011
 */

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "rm/rm.h"

/**
   @addtogroup rm

   This file includes data structures and function used by RM:fop layer.

   @{

 */
enum c2_rm_fom_phases {
	FOPH_RM_RIGHT_BORROW = FOPH_NR + 1,
	FOPH_RM_RIGHT_REVOKE = FOPH_NR + 1,
	FOPH_RM_RIGHT_CANCEL = FOPH_NR + 1,
	FOPH_RM_RIGHT_BORROW_WAIT = FOPH_NR + 2,
	FOPH_RM_RIGHT_REVOKE_WAIT = FOPH_NR + 2
};

/**
  * FOM to execute resource right request. The request could either be borrow,
  * revoke or cancel. This will also be used for replies.
  */
struct rm_borrow_fom {
	/** Generic c2_fom object */
	struct c2_fom	bom_fom;
	/** Incoming request */
	struct c2_rm_borrow_incoming bom_in;
};

struct rm_canoke_fom {
	/** Generic c2_fom object */
	struct c2_fom	ck_fom;
	/** Revoke or cancel request */
	struct c2_rm_revoke_incoming ck_in;
};

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
