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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/03/2011
 */
/*
 * Failure fops should be defined by not yet existing "failure" module. For the
 * time being, it makes sense to put them in cm/ or console/. ioservice is not
 * directly responsible for handling failures, it is intersected by copy-machine
 * (cm).
 */

#pragma once

#ifndef __COLIBRI_CONSOLE_FOP_H__
#define __COLIBRI_CONSOLE_FOP_H__

#include "fop/fop.h"
#include "rpc/rpc_opcodes.h"

/**
 * Init console FOP
 */
int c2_console_fop_init(void);
/**
 * Fini console FOP
 */
void c2_console_fop_fini(void);


/**
 * FOP definitions and corresponding fop type formats
 */
extern struct c2_fop_type_format c2_cons_fop_disk_tfmt;
extern struct c2_fop_type_format c2_cons_fop_device_tfmt;
extern struct c2_fop_type_format c2_cons_fop_reply_tfmt;
extern struct c2_fop_type_format c2_cons_fop_test_tfmt;

extern struct c2_fop_type c2_cons_fop_disk_fopt;
extern struct c2_fop_type c2_cons_fop_device_fopt;
extern struct c2_fop_type c2_cons_fop_reply_fopt;
extern struct c2_fop_type c2_cons_fop_test_fopt;

/* __COLIBRI_CONSOLE_FOP_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

