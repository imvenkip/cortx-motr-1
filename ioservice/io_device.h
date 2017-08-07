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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 28/06/2012
 */

#pragma once

#ifndef __MERO_IOSERVICE_IO_DEVICE_H__
#define __MERO_IOSERVICE_IO_DEVICE_H__

/**
   @page io_calls_params_dld-fspec I/O calls Parameters Functional Specification

   - @ref io_calls_params_dld-fspec-ds
   - @ref io_calls_params_dldDFS "Detailed Functional Specification"

   @section DLD-fspec-ds Data Structures
   - m0_fop_cob_rw
   - m0_fop_cob_rw_reply
   - m0_fop_cob_common
   - m0_fop_cob_op_reply

   @see @ref io_calls_params_dldDFS "Detailed Functional Specification"
   @see @ref poolmach "pool machine"
 */

/**
   @defgroup io_calls_params_dldDFS Detailed Functional Specification

   @see The @ref io_calls_params_dld "DLD of I/O calls Parameters" its
   @ref io_calls_params_dld-fspec "Functional Specification"

   @{
 */
struct m0_reqh;
struct m0_poolmach;
struct m0_reqh_service;

struct m0_ios_poolmach_args {
	uint32_t nr_sdevs;
	uint32_t nr_nodes;
};

/**
 * Initializes the pool machine. This will create a shared reqh key
 * and call m0_poolmach_init() internally.
 */
M0_INTERNAL int m0_ios_poolmach_init(struct m0_reqh_service *service);

/**
 * Gets the shared pool machine.
 */
M0_INTERNAL struct m0_poolmach *m0_ios_poolmach_get(const struct m0_reqh *reqh);

/**
 * Finializes the pool machine when it is no longer used.
 */
M0_INTERNAL void m0_ios_poolmach_fini(struct m0_reqh_service *service);

/** @} */ /* io_calls_params_dldDFS end group */

#endif /*  __MERO_IOSERVICE_IO_DEVICE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
