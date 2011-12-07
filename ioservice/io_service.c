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
 * Original author: Rajanikant Chirmade <Rajanikant_Chirmade@xyratex.com>
 * Original creation date: 11/02/2011
 */

/**
   @addtogroup DLD_bulk_server_fspec_ioservice_operations
   @{
 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/tlist.h"
#include "net/buffer_pool.h"
#include "reqh/reqh_service.h"
#include "ioservice/io_fops.h"
#include "rpc/rpccore.h"
#include "reqh/reqh.h"
#include "ioservice/io_service.h"

/** Required for accessing rpcmachine list */
C2_TL_DESCR_DEFINE(rpcmachines, "rpc machines associated with reqh", static,
                   struct c2_rpcmachine, cr_rh_linkage, cr_magic,
                   C2_REQH_MAGIC, C2_RPC_MAGIC);
C2_TL_DEFINE(rpcmachines, static, struct c2_rpcmachine);

const struct c2_tl_descr         nbp_colormap_tl;

/** 
 * These values are supposed to get from configuration cache. Since 
 * configuration cache module not available these values defines as constants
 */
static const int network_buffer_pool_segment_size=4096;
static const int network_buffer_pool_max_segment=16;
static const int network_buffer_pool_threshold=8;
static const int network_buffer_pool_initial_size=32;

static int c2_ioservice_alloc_and_init(struct c2_reqh_service_type *stype,
                                     struct c2_reqh_service **service);
static void c2_ioservice_fini(struct c2_reqh_service *service);

static int c2_ioservice_start(struct c2_reqh_service *service);
static void c2_ioservice_stop(struct c2_reqh_service *service);

static void c2_io_buffer_pool_not_empty(struct c2_net_buffer_pool *bp);
static void c2_io_buffer_pool_low(struct c2_net_buffer_pool *bp);

/**
 * I/O Service type operations.
 */
static const struct c2_reqh_service_type_ops c2_ioservice_type_ops = {
        .rsto_service_alloc_and_init = c2_ioservice_alloc_and_init
};

/**
 * I/O Service operations.
 */
static const struct c2_reqh_service_ops c2_ioservice_ops = {
        .rso_start = c2_ioservice_start,
        .rso_stop = c2_ioservice_stop,
        .rso_fini = c2_ioservice_fini
};

/**
 * Buffer pool operations.
 */
struct c2_net_buffer_pool_ops buffer_pool_ops = {
        .nbpo_not_empty       = c2_io_buffer_pool_not_empty,
        .nbpo_below_threshold = c2_io_buffer_pool_low,
};

C2_REQH_SERVICE_TYPE_DECLARE(c2_ioservice_type, &c2_ioservice_type_ops, "ioservice");

/**
 * Buffer pool operation function. This function get called when buffer pool
 * becomes non empty. 
 * It sends signal to FOM wating for network buffer.
 *
 * @param bp buffer pool pointer.
 * @pre bp != NULL
 */
static void c2_io_buffer_pool_not_empty(struct c2_net_buffer_pool *bp)
{
        struct c2_reqh_service    *service;

        C2_PRE(bp != NULL);

        service = container_of(bp, struct c2_reqh_service, rs_nb_pool);

        c2_chan_signal(&service->rs_nbp_wait);
}

/**
 * Buffer pool operation function.
 * This function get called when network buffer availability hits 
 * lower threshould.
 *
 * @param bp buffer pool pointer.
 * @pre bp != NULL
 */
static void c2_io_buffer_pool_low(struct c2_net_buffer_pool *bp)
{
}

/**
 * Registers I/O service with colibri node.
 * Colibri setup calls this function.
 */
int c2_ioservice_register(void)
{
        c2_reqh_service_type_register(&c2_ioservice_type);
        return 0;
}

/**
 * Unregisters I/O service from colibri node.
 */
void c2_ioservice_unregister(void)
{
        c2_reqh_service_type_unregister(&c2_ioservice_type);
}

/**
 * Allocate and initiate I/O Service instance.
 * This operation allocate & initiate service instance with its operation
 * vector.
 *
 *
 * @param stype service type
 * @param service pointer to service instance.
 */
static int c2_ioservice_alloc_and_init(struct c2_reqh_service_type *stype,
                                     struct c2_reqh_service **service)
{
        int                             rc = 0;                 
        struct c2_reqh_service         *serv;
        struct c2_reqh_io_service      *serv_obj;

        C2_PRE(stype != NULL && service != NULL);

        serv_obj= c2_alloc(sizeof *serv_obj);
        if (serv_obj == NULL)
                return -ENOMEM;

        serv_obj->rs_nb_pool.nbp_ops = &buffer_pool_ops;

        serv = &serv_obj->rios_gen;

        serv->rs_type = stype;
        serv->rs_ops = &c2_ioservice_ops;


        *service = serv;

        return rc;
}

/**
 * Finalise I/O Service instance.
 * This operation finish service instance and de-allocate it.
 *
 * @param service pointer to service instance.
 */
static void c2_ioservice_fini(struct c2_reqh_service *service)
{
        C2_PRE(service != NULL);

        c2_free(service);
}

/**
 * Start I/O Service.
 * - Register I/O FOP with service
 * - Initiates buffer pool
 * - Initialize channel for service to wait for buffer pool notEmpty event.
 *
 * @param service pointer to service instance.
 */
static int c2_ioservice_start(struct c2_reqh_service *service)
{
        int                        rc = 0;
        int                        colors;
        int                        nbuffs = 0;
        struct c2_net_domain      *ndom;
        struct c2_rpcmachine      *rpcmach = NULL;
        struct c2_reqh_io_service *serv_obj;

        C2_PRE(service != NULL);

        /** Register I/O service fops */
        rc = c2_ioservice_fop_init();
        if (rc != 0)
            return rc;

        serv_obj = container_of(service, struct c2_reqh_io_service, rios_gen);

        c2_tlist_init(&nbp_colormap_tl, &serv_obj->rios_nbp_color_map);

        /** find the domain */
        c2_tlist_for(&rpcmachines_tl,
                              &service->rs_reqh->rh_rpcmachines, rpcmach)
        {
                C2_ASSERT(rpcmach != NULL);
                c2_tlist_add(&nbp_colormap_tl, &serv_obj->rios_nbp_color_map, rpcmach->cr_tm);
        } c2_tlist_endfor;

        //ndom = rpcmach->cr_tm.ntm_dom;

        colors = c2_tlist_length(&nbp_colormap_tl,
                                 &serv_obj->rios_nbp_color_map);

        /** Initialize buffer pool */
        c2_net_buffer_pool_init(&serv_obj->rios_nb_pool, ndom,
                                network_buffer_pool_threshold,
                                network_buffer_pool_segment_size,
                                network_buffer_pool_max_segment, colors);

        /** Initialize channel to get siganl from buffer pool. */
        c2_chan_init(&service->rs_nbp_wait);


        /** Pre-allocate network buffers */
        c2_net_buffer_pool_lock(&serv_obj->rios_nb_pool);
        nbuffs = c2_net_buffer_pool_provision(&serv_obj->rios_nb_pool,
                                         network_buffer_pool_initial_size);
        if (nbuffs <= 0)
            rc = -1;

        c2_net_buffer_pool_unlock(&serv_obj->rios_nb_pool);

        return rc;
}

/**
 * Stop I/O Service.
 * - Free buffer pool
 * - Un-register I/O FOP with service
 *
 * @param service pointer to service instance.
 */
static void c2_ioservice_stop(struct c2_reqh_service *service)
{
        struct c2_reqh_io_service *serv_obj;

        serv_obj = container_of(service, struct c2_reqh_io_service, rios_gen);

        c2_ioservice_fop_fini();

        c2_net_buffer_pool_fini(&serv_obj->rios_nb_pool);
}

/** @} endgroup io_service */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
