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

#ifndef __MERO_RM_FOPS_H__
#define __MERO_RM_FOPS_H__

#include "fop/fop.h"
#include "rm/rm_ff.h"

/**
 * Externs
 */
extern struct m0_fop_type m0_fop_rm_borrow_fopt;
extern struct m0_fop_type m0_fop_rm_borrow_rep_fopt;
extern struct m0_fop_type m0_fop_rm_revoke_fopt;
extern struct m0_fop_type m0_fom_error_rep_fopt;

/**
 * FOP init() and fini() functions.
 */
M0_INTERNAL int m0_rm_fop_init();
M0_INTERNAL void m0_rm_fop_fini();

/* __MERO_RM_FOPS_H__ */
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
