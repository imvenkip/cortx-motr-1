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

#pragma once

#ifndef __MERO_CONSOLE_CONSOLE_H__
#define __MERO_CONSOLE_CONSOLE_H__

#include <stdbool.h>
#include "lib/types.h"
#include "xcode/xcode_attr.h"

/**
   @defgroup console Console

   Build a standalone utility that

   - connects to a specified service.
   - constructs a fop of a specified fop type and with specified
     values of fields and sends it to the service.
   - waits fop reply.
   - outputs fop reply to the user.

   The console utility can send a DEVICE_FAILURE fop to a server. Server-side
   processing for fops of this type consists of calling a single stub function.
   Real implementation will be supplied by the middleware.cm-setup task.

   @{
*/

extern bool verbose;

/**
 * Failure fops should be defined by not yet existing "failure" module. For the
 * time being, it makes sense to put them in cm/ or console/. ioservice is not
 * directly responsible for handling failures, it is intersected by copy-machine
 * (cm).
 * <b>Console fop formats</b>
 */

struct m0_cons_fop_fid {
	uint64_t cons_seq;
	uint64_t cons_oid;
} M0_XCA_RECORD;

struct m0_cons_fop_buf {
	uint32_t  cons_size;
	uint8_t  *cons_buf;
} M0_XCA_SEQUENCE;

struct m0_cons_fop_test {
	struct m0_cons_fop_fid cons_id;
	uint8_t                cons_test_type;
	uint64_t               cons_test_id;
} M0_XCA_RECORD;

/**
 *  Device failure notification fop
 *  - id          : Console id.
 *  - notify_type : Device failure notification.
 *  - dev_id      : Device ID (Disk ID in case disk failure).
 *  - dev_name    : Device name (In case of disk it could be volume name).
 */
struct m0_cons_fop_device {
	struct m0_cons_fop_fid cons_id;
	uint32_t               cons_notify_type;
	uint64_t               cons_dev_id;
	struct m0_cons_fop_buf cons_dev_name;
} M0_XCA_RECORD;

/**
 *  Reply fop to the notification fop
 *  - notify_type : Notification type of request fop.
 *  - return      : Opcode of request fop.
 */
struct m0_cons_fop_reply {
	uint32_t cons_notify_type;
	uint32_t cons_return;
} M0_XCA_RECORD;

/** @} end of console group */

/* __MERO_CONSOLE_CONSOLE_H__ */
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
