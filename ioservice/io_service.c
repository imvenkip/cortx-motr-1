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
#include "rpc/rpc2.h"
#include "reqh/reqh.h"
#include "ioservice/io_service.h"
#include "colibri/colibri_setup.h"

C2_TL_DESCR_DEFINE(bufferpools, "rpc machines associated with reqh", ,
                   struct c2_rios_buffer_pool, rios_bp_linkage, rios_bp_magic,
                   C2_RIOS_BUFFER_POOL_MAGIC, C2_RIOS_BUFFER_POOL_HEAD);
C2_TL_DEFINE(bufferpools, , struct c2_rios_buffer_pool);


/**
 * These values are supposed to get from configuration cache. Since
 * configuration cache module not available these values defines as constants
 */
static const int network_buffer_pool_segment_size = 4096;
static const int network_buffer_pool_segment_nr   = 128;
static const int network_buffer_pool_threshold    = 8;
static const int network_buffer_pool_initial_size = 32;

static int c2_ioservice_locate(struct c2_reqh_service_type *stype,
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
        .rsto_service_locate = c2_ioservice_locate
};

/**
 * I/O Service operations.
 */
static const struct c2_reqh_service_ops c2_ioservice_ops = {
        .rso_start = c2_ioservice_start,
        .rso_stop  = c2_ioservice_stop,
        .rso_fini  = c2_ioservice_fini
};

/**
 * Buffer pool operations.
 */
struct c2_net_buffer_pool_ops buffer_pool_ops = {
        .nbpo_not_empty       = c2_io_buffer_pool_not_empty,
        .nbpo_below_threshold = c2_io_buffer_pool_low,
};

C2_REQH_SERVICE_TYPE_DECLARE(c2_ioservice_type, &c2_ioservice_type_ops,
                             "ioservice");

/**
 * Buffer pool operation function. This function get called when buffer pool
 * becomes non empty.
 * It sends signal to FOM waiting for network buffer.
 *
 * @param bp buffer pool pointer.
 * @pre bp != NULL
 */
static void c2_io_buffer_pool_not_empty(struct c2_net_buffer_pool *bp)
{
        struct c2_rios_buffer_pool *buffer_desc = NULL;

        C2_PRE(bp != NULL);

        buffer_desc = container_of(bp, struct c2_rios_buffer_pool,
                                   rios_bp);

        c2_chan_signal(&buffer_desc->rios_bp_wait);
}

/**
 * Buffer pool operation function.
 * This function get called when network buffer availability hits
 * lower threshold.
 *
 * @param bp buffer pool pointer.
 * @pre bp != NULL
 */
static void c2_io_buffer_pool_low(struct c2_net_buffer_pool *bp)
{
        /*
         * Currently io_sevice ignoring this signal.
         * But in future io_service may grow
         * buffer pool depending on some policy.
         */
}

/**
 * Registers I/O service with colibri node.
 * Colibri setup calls this function.
 */
int c2_ioservice_register(void)
{
        c2_reqh_service_type_register(&c2_ioservice_type);
        return c2_ioservice_fop_init();
}

/**
 * Unregisters I/O service from colibri node.
 */
void c2_ioservice_unregister(void)
{
        c2_reqh_service_type_unregister(&c2_ioservice_type);
	c2_ioservice_fop_fini();
}

/**
 * Create & initialize instance of buffer pool per domain.
 * 1. This function scans rpc_machines from request handler
 *    and creates buffer pool instance for each network domain.
 *    It also create color map for transfer machines for
 *    respective domain.
 * 2. Initialise all buffer pools and make provision for
 *    configured number of buffers for all each buffer pool.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static int ioservice_create_buffer_pool(struct c2_reqh_service *service)
{
        int                         nbuffs = 0;
        int                         colours;
        int                         rc     = 0;
        struct c2_rpc_machine      *rpcmach;
        struct c2_reqh_io_service  *serv_obj;
        struct c2_rios_buffer_pool *bp;
        struct c2_rios_buffer_pool *newbp;

        serv_obj = container_of(service, struct c2_reqh_io_service, rios_gen);

        c2_tlist_for(&c2_rhrpm_tl,
		     &service->rs_reqh->rh_rpc_machines, rpcmach) {
		/*
		 * Check buffer pool for network domain of rpc_machine
		 */
		c2_tlist_for(&bufferpools_tl, &serv_obj->rios_buffer_pools, bp) {

                        if (bp->rios_ndom == rpcmach->rm_tm.ntm_dom)
				/*
				 * Found buffer pool for domain.
				 * No need to create buffer pool
				 * for this domain.
				 */
                                return rc;
		} c2_tlist_endfor; /* bufferpools */

		/* Buffer pool for network domain not found create one */
		C2_ALLOC_PTR(newbp);
		if (newbp == NULL)
			return -ENOMEM;

		newbp->rios_ndom = rpcmach->rm_tm.ntm_dom;
		/*
		 * Initialize channel for sending availability of buffers
		 * with buffer pool to I/O FOMs.
		 */
		c2_chan_init(&newbp->rios_bp_wait);
		newbp->rios_bp_magic = C2_RIOS_BUFFER_POOL_MAGIC;
		colours = rpcmach->rm_tm.ntm_dom->nd_pool_colour_counter;
		rc = c2_net_buffer_pool_init(&newbp->rios_bp,
					     rpcmach->rm_tm.ntm_dom,
					     network_buffer_pool_threshold,
					     network_buffer_pool_segment_nr,
					     network_buffer_pool_segment_size,
					     colours, C2_0VEC_SHIFT);
		if (rc != 0)
			break;
		newbp->rios_bp.nbp_ops = &buffer_pool_ops;

		/* Pre-allocate network buffers */
		c2_net_buffer_pool_lock(&newbp->rios_bp);
		nbuffs = c2_net_buffer_pool_provision(&newbp->rios_bp,
					network_buffer_pool_initial_size);
		if (nbuffs <= 0) {
			rc = -1;
			break;
		}

		c2_net_buffer_pool_unlock(&newbp->rios_bp);

		bufferpools_tlink_init(newbp);
		bufferpools_tlist_add(&serv_obj->rios_buffer_pools, newbp);

        } c2_tlist_endfor; /* rpc_machines */

        return rc;
}

/**
 * Delete instances of buffer pool.
 * It go through buffer pool list and delete the instance.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void ioservice_delete_buffer_pool(struct c2_reqh_service *service)
{
        struct c2_reqh_io_service  *serv_obj;
        struct c2_rios_buffer_pool *bp;

        C2_PRE(service != NULL);

        serv_obj = container_of(service, struct c2_reqh_io_service, rios_gen);

        c2_tlist_for(&bufferpools_tl, &serv_obj->rios_buffer_pools, bp) {

                C2_ASSERT(bp != NULL);

                c2_chan_fini(&bp->rios_bp_wait);
                bufferpools_tlink_del_fini(bp);

                c2_net_buffer_pool_lock(&bp->rios_bp);
                c2_net_buffer_pool_fini(&bp->rios_bp);
		c2_free(bp);

        } c2_tlist_endfor; /* bufferpools */

        bufferpools_tlist_fini(&serv_obj->rios_buffer_pools);
}

/**
 * Allocate and initiate I/O Service instance.
 * This operation allocate & initiate service instance with its operation
 * vector.
 *
 * @param stype service type
 * @param service pointer to service instance.
 *
 * @pre stype != NULL && service != NULL
 */
static int c2_ioservice_locate(struct c2_reqh_service_type *stype,
                                       struct c2_reqh_service **service)
{
        int                        rc = 0;
        struct c2_reqh_service    *serv;
        struct c2_reqh_io_service *serv_obj;

        C2_PRE(stype != NULL && service != NULL);

        C2_ALLOC_PTR(serv_obj);
        if (serv_obj == NULL)
                return -ENOMEM;

        bufferpools_tlist_init(&serv_obj->rios_buffer_pools);
        serv_obj->rios_magic = C2_REQH_IO_SERVICE_MAGIC;
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
 *
 * @pre service != NULL
 */
static void c2_ioservice_fini(struct c2_reqh_service *service)
{
        struct c2_reqh_io_service *serv_obj;

        C2_PRE(service != NULL);

        serv_obj = container_of(service, struct c2_reqh_io_service, rios_gen);

        c2_free(serv_obj);
}

/**
 * Start I/O Service.
 * - Register I/O FOP with service
 * - Initiates buffer pool
 * - Initialize channel for service to wait for buffer pool notEmpty event.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static int c2_ioservice_start(struct c2_reqh_service *service)
{
        int			rc = 0;
	struct c2_colibri      *cc;
	struct c2_cobfid_setup *s;

        C2_PRE(service != NULL);

        rc = ioservice_create_buffer_pool(service);
	if (rc != 0)
		return rc;

	cc = c2_cs_ctx_get(service);
	C2_ASSERT(cc != NULL);
	rc = c2_cobfid_setup_get(&s, cc);
	C2_POST(ergo(rc == 0, s != NULL));

        return rc;
}

/**
 * Stop I/O Service.
 * - Free buffer pool
 * - Un-register I/O FOP with service
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void c2_ioservice_stop(struct c2_reqh_service *service)
{
	struct c2_colibri *cc;

        C2_PRE(service != NULL);

        ioservice_delete_buffer_pool(service);

	cc = c2_cs_ctx_get(service);
	C2_ASSERT(cc != NULL);
	c2_cobfid_setup_put(cc);
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
