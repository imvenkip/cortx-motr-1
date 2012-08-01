/* -*- C -*- */
#ifndef __COLIBRI_RM_FOPS_H__
#define __COLIBRI_RM_FOPS_H__

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

#ifndef _KERNEL_
#include "rm/rm_u.h"
#else
#include "rm/rm_k.h"
#endif

#include "fop/fop.h"
#include "fop/fop_format.h"

/**
 * RM:fop opcodes are defined here
 */
enum c2_rm_fop_opcodes {
	C2_RM_FOP_BORROW = 51,
	C2_RM_FOP_BORROW_REPLY,
	C2_RM_FOP_REVOKE,
	C2_RM_FOP_REVOKE_REPLY,
	C2_RM_FOP_CANCEL
};

/**
 * Externs
 */
extern struct c2_fop_type c2_fop_rm_borrow_fopt;
extern struct c2_fop_type c2_fop_rm_borrow_rep_fopt;
extern struct c2_fop_type c2_fop_rm_revoke_fopt;
extern struct c2_fop_type c2_fop_rm_revoke_rep_fopt;

/**
 *
 */
int c2_rm_fop_init();
void c2_rm_fop_fini();

/* __COLIBRI_RM_FOPS_H__ */
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
