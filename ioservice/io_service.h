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
 * Original author: Rajanikant Chirmade <Rajanikant_Chirmade@xyratex.com>
 * Original creation date: 11/02/2011
 */

#pragma once

#ifndef __MERO_IOSERVICE_IO_SERVICE_H__
#define __MERO_IOSERVICE_IO_SERVICE_H__

/**
 * @defgroup DLD_bulk_server_fspec_ioservice_operations I/O Service Operations
 * @see @ref DLD-bulk-server
 * @see @ref reqh
 *
 * I/O Service initialization and operations controlled by request handler.
 *
 * I/O Service defines service type operation vector -
 * - I/O Service type operation @ref m0_ioservice_alloc_and_init()<br>
 *   Request handler uses this service type operation to allocate and
 *   and initiate service instance.
 *
 * I/O Service defines service operation vector -
 * - I/O Service operation @ref m0_ioservice_start()<br>
 *   Initiate buffer_pool and register I/O FOP with service
 * - I/O Service operation @ref m0_ioservice_stop()<br>
 *   Free buffer_pool and unregister I/O FOP with service
 * - I/O Service operation @ref m0_ioservice_fini)<br>
 *   Free I/O Service instance.
 *
 * State transition diagram for I/O Service will be available at @ref reqh
 *
 *  @{
 */
#include "addb/addb.h"
#include "net/buffer_pool.h"
#include "reqh/reqh_service.h"
#include "lib/chan.h"
#include "lib/tlist.h"

#include "ioservice/io_service_addb.h"

M0_INTERNAL int m0_ios_register(void);
M0_INTERNAL void m0_ios_unregister(void);

/**
 * Data structure represents list of buffer pool per network domain.
 */
struct m0_rios_buffer_pool {
        /** Pointer to Network buffer pool. */
        struct m0_net_buffer_pool    rios_bp;
        /** Pointer to net domain owner of this buffer pool */
        struct m0_net_domain        *rios_ndom;
        /** Buffer pool wait channel. */
        struct m0_chan               rios_bp_wait;
        /** Linkage into netowrk buffer pool list */
        struct m0_tlink              rios_bp_linkage;
        /** Magic */
        uint64_t                     rios_bp_magic;
};

/**
 */
struct m0_ios_rwfom_stats {
	/** counter to track fom I/O sizes */
	struct m0_addb_counter ifs_sizes_cntr;
	/** counter to track fom duration */
	struct m0_addb_counter ifs_times_cntr;
};

/**
 * Structure contains generic service structure and
 * service specific information.
 */
struct m0_reqh_io_service {
        /** Generic reqh service object */
        struct m0_reqh_service    rios_gen;
        /** Buffer pools belongs to this services */
        struct m0_tl              rios_buffer_pools;
	/** Read[0] and write[1] I/O FOM statistics */
	struct m0_ios_rwfom_stats rios_rwfom_stats[2];
        /** magic to check io service object */
        uint64_t                  rios_magic;
};

M0_INTERNAL bool m0_reqh_io_service_invariant(const struct m0_reqh_io_service
					      *rios);

/** @} end of io_service */

#endif /* __MERO_IOSERVICE_IO_SERVICE_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
