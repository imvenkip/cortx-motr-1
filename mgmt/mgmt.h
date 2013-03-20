/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 5-Mar-2013
 */
#pragma once
#ifndef __MERO_MGMT_MGMT_H__
#define __MERO_MGMT_MGMT_H__

struct m0_reqh_service;

/**
   @defgroup mgmt Management Interfaces
   This module provides interfaces to manage Mero.

   @see @ref MGMT-DLD "Management Detailed Design"

   @{
 */

/** The name of the management service service-type */
#define M0_MGMT_SVC_TYPE_NAME "mgmt"

/**
   Management module initializer.
 */
M0_INTERNAL int m0_mgmt_init(void);

/**
   Management module finalizer.
 */
M0_INTERNAL void m0_mgmt_fini(void);

/**
   Allocate a management service.
 */
M0_INTERNAL int m0_mgmt_service_allocate(struct m0_reqh_service **service);

/** @} end mgmt group */
#endif /* __MERO_MGMT_MGMT_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
