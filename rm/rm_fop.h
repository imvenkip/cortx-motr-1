/* -*- C -*- */
#ifndef __COLIBRI_RM_FOP_H__
#define __COLIBRI_RM_FOP_H__

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

struct c2_fom;
struct c2_fom_type;

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
  * @todo Need to define ops vector.
  */

/**
  * Externs needed by ????
  */
extern struct c2_fop_type c2_fop_rm_right_borrow_fopt;
extern struct c2_fop_type c2_fop_rm_right_borrow_reply_fopt;
extern struct c2_fop_type c2_fop_rm_right_revoke_fopt;
extern struct c2_fop_type c2_fop_rm_right_revoke_reply_fopt;
extern struct c2_fop_type c2_fop_rm_right_cancel_fopt;

extern struct c2_fop_type_format c2_fop_rm_right_borrow_tfmt;
extern struct c2_fop_type_format c2_fop_rm_right_borrow_reply_tfmt;
extern struct c2_fop_type_format c2_fop_rm_right_revoke_tfmt;
extern struct c2_fop_type_format c2_fop_rm_right_revoke_reply_tfmt;
extern struct c2_fop_type_format c2_fop_rm_right_cancel_tfmt;

void rm_fop_fini(void);
int rm_fop_init(void);

/* __COLIBRI_RM_FOP_H__ */
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

