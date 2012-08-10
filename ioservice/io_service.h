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

#ifndef __COLIBRI_IOSERVICE_IO_SERVICE_H__
#define __COLIBRI_IOSERVICE_IO_SERVICE_H__

/**
 * @defgroup DLD_bulk_server_fspec_ioservice_operations I/O Service Operations
 * @see @ref DLD-bulk-server
 * @see @ref reqh
 *
 * I/O Service initialization and operations controlled by request handler.
 *
 * I/O Service defines service type operation vector -
 * - I/O Service type operation @ref c2_ioservice_alloc_and_init()<br>
 *   Request handler uses this service type operation to allocate and
 *   and initiate service instance.
 *
 * I/O Service defines service operation vector -
 * - I/O Service operation @ref c2_ioservice_start()<br>
 *   Initiate buffer_pool and register I/O FOP with service
 * - I/O Service operation @ref c2_ioservice_stop()<br>
 *   Free buffer_pool and unregister I/O FOP with service
 * - I/O Service operation @ref c2_ioservice_fini)<br>
 *   Free I/O Service instance.
 *
 * State transition diagram for I/O Service will be available at @ref reqh
 *
 *  @{
 */
#include "net/buffer_pool.h"
#include "reqh/reqh_service.h"
#include "lib/chan.h"
#include "lib/tlist.h"

int c2_ios_register(void);
void c2_ios_unregister(void);

enum {
        C2_RIOS_BUFFER_POOL_MAGIC = 0x62756666657273,   /* buffers */
        C2_RIOS_BUFFER_POOL_HEAD = 0x42554646455253,  /* BUFFERS */
        C2_REQH_IO_SERVICE_MAGIC = 0x72696f736f626a
};

/**
 * Data structure represents list of buffer pool per network domain.
 */
struct c2_rios_buffer_pool {
        /** Pointer to Network buffer pool. */
        struct c2_net_buffer_pool    rios_bp;
        /** Pointer to net domain owner of this buffer pool */
        struct c2_net_domain        *rios_ndom;
        /** Buffer pool wait channel. */
        struct c2_chan               rios_bp_wait;
        /** Linkage into netowrk buffer pool list */
        struct c2_tlink              rios_bp_linkage;
        /** Magic */
        uint64_t                     rios_bp_magic;
};

/**
 * Structure contains generic service structure and
 * service specific information.
 */
struct c2_reqh_io_service {
        /** Generic reqh service object */
        struct c2_reqh_service       rios_gen;
        /** Buffer pools belongs to this services */
        struct c2_tl                 rios_buffer_pools;
        /** magic to check io service object */
        uint64_t                     rios_magic;
};

/** @} end of io_service */

#endif /* __COLIBRI_IOSERVICE_IO_SERVICE_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
