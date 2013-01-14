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

/**
   @addtogroup DLD_bulk_server_fspec_ios_operations
   @{
 */

#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "ioservice/io_service_addb.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/tlist.h"
#include "mero/magic.h"
#include "net/buffer_pool.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "ioservice/io_fops.h"
#include "ioservice/io_service.h"
#include "ioservice/io_device.h"
#include "pool/pool.h"

M0_TL_DESCR_DEFINE(bufferpools, "rpc machines associated with reqh",
		   M0_INTERNAL,
                   struct m0_rios_buffer_pool, rios_bp_linkage, rios_bp_magic,
                   M0_IOS_BUFFER_POOL_MAGIC, M0_IOS_BUFFER_POOL_HEAD_MAGIC);
M0_TL_DEFINE(bufferpools, M0_INTERNAL, struct m0_rios_buffer_pool);


/**
 * These values are supposed to be fetched from configuration cache. Since
 * configuration cache module is not available, these values are defined as
 * constants.
 */
enum {
	M0_NET_BUFFER_POOL_SIZE = 32,
};

static int ios_allocate(struct m0_reqh_service **service,
			struct m0_reqh_service_type *stype,
			const char *arg);
static void ios_fini(struct m0_reqh_service *service);

static int ios_start(struct m0_reqh_service *service);
static void ios_stop(struct m0_reqh_service *service);
static void ios_stats_post_addb(struct m0_reqh_service *service);

static void buffer_pool_not_empty(struct m0_net_buffer_pool *bp);
static void buffer_pool_low(struct m0_net_buffer_pool *bp);

static bool     ios_cdom_is_initialised;
static unsigned ios_cdom_key;

/**
 * I/O Service type operations.
 */
static const struct m0_reqh_service_type_ops ios_type_ops = {
        .rsto_service_allocate = ios_allocate
};

/**
 * I/O Service operations.
 */
static const struct m0_reqh_service_ops ios_ops = {
	.rso_start           = ios_start,
	.rso_stop            = ios_stop,
	.rso_fini            = ios_fini,
	.rso_stats_post_addb = ios_stats_post_addb
};

/**
 * Buffer pool operations.
 */
struct m0_net_buffer_pool_ops buffer_pool_ops = {
	.nbpo_not_empty       = buffer_pool_not_empty,
	.nbpo_below_threshold = buffer_pool_low,
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_ios_type, &ios_type_ops, "ioservice",
			     &m0_addb_ct_ios_serv);

/**
 * Buffer pool operation function. This function gets called when buffer pool
 * becomes non empty.
 * It sends signal to FOM waiting for network buffer.
 *
 * @param bp buffer pool pointer.
 * @pre bp != NULL
 */
static void buffer_pool_not_empty(struct m0_net_buffer_pool *bp)
{
        struct m0_rios_buffer_pool *buffer_desc;

	M0_PRE(bp != NULL);

        buffer_desc = container_of(bp, struct m0_rios_buffer_pool, rios_bp);

	m0_chan_signal(&buffer_desc->rios_bp_wait);
}

/**
 * Buffer pool operation function.
 * This function gets called when network buffer availability hits
 * lower threshold.
 *
 * @param bp buffer pool pointer.
 * @pre bp != NULL
 */
static void buffer_pool_low(struct m0_net_buffer_pool *bp)
{
	/*
	 * Currently ioservice is ignoring this signal.
	 * But in future io_service may grow
	 * buffer pool depending on some policy.
	 */
}

/**
   Array of ADDB record types corresponding to the fields of
   struct m0_ios_rwfom_stats, times the number of elements in
   the struct m0_reqh_io_service::rios_rwfom_stats array.

   Must match the traversal order in ios_allocate().
 */
static struct m0_addb_rec_type *ios_rwfom_cntr_rts[] = {
	/* read[0] */
	&m0_addb_rt_ios_rfom_sizes,
	&m0_addb_rt_ios_rfom_times,
	/* write[1] */
	&m0_addb_rt_ios_wfom_sizes,
	&m0_addb_rt_ios_wfom_times,
};

/**
 * Registers I/O service with mero node.
 * Mero setup calls this function.
 */
M0_INTERNAL int m0_ios_register(void)
{
	int i;

	/* The onwire version-number structure is declared as a struct,
	 * not a sequence (which is more like an array.
	 * This avoid dynamic memory for every request and reply fop.
	 */
	M0_CASSERT(sizeof (struct m0_pool_version_numbers) ==
		   sizeof (struct m0_fv_version));

	m0_addb_ctx_type_register(&m0_addb_ct_ios_serv);
	for (i = 0; i < ARRAY_SIZE(ios_rwfom_cntr_rts); ++i)
		m0_addb_rec_type_register(ios_rwfom_cntr_rts[i]);
#undef RT_REG
#define RT_REG(n) m0_addb_rec_type_register(&m0_addb_rt_ios_##n)
	RT_REG(rwfom_finish);
	RT_REG(ccfom_finish);
	RT_REG(cdfom_finish);
#undef RT_REG

	m0_addb_ctx_type_register(&m0_addb_ct_cob_create_fom);
	m0_addb_ctx_type_register(&m0_addb_ct_cob_delete_fom);
	m0_addb_ctx_type_register(&m0_addb_ct_cob_io_rw_fom);
	m0_reqh_service_type_register(&m0_ios_type);
	ios_cdom_key = m0_reqh_key_init();
	return m0_ioservice_fop_init();
}

/**
 * Unregisters I/O service from mero node.
 */
M0_INTERNAL void m0_ios_unregister(void)
{
	ios_cdom_key = 0;
	m0_reqh_service_type_unregister(&m0_ios_type);
	m0_ioservice_fop_fini();
}

M0_INTERNAL bool m0_reqh_io_service_invariant(const struct m0_reqh_io_service
					      *rios)
{
        return rios->rios_magic == M0_IOS_REQH_SVC_MAGIC;
}

/**
 * Create & initialise instance of buffer pool per domain.
 * 1. This function scans rpc_machines from request handler
 *    and creates buffer pool instance for each network domain.
 *    It also creates color map for transfer machines for
 *    respective domain.
 * 2. Initialises all buffer pools and make provision for
 *    configured number of buffers for each buffer pool.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static int ios_create_buffer_pool(struct m0_reqh_service *service)
{
	int                         nbuffs;
	int                         colours;
	int                         rc = 0;
	struct m0_rpc_machine      *rpcmach;
	struct m0_reqh_io_service  *serv_obj;
	struct m0_rios_buffer_pool *bp;
	m0_bcount_t                 segment_size;
	uint32_t                    segments_nr;
	struct m0_reqh             *reqh;

	serv_obj = container_of(service, struct m0_reqh_io_service, rios_gen);
	M0_ASSERT(m0_reqh_io_service_invariant(serv_obj));

	reqh = service->rs_reqh;
	m0_rwlock_read_lock(&reqh->rh_rwlock);
	m0_tlist_for(&m0_reqh_rpc_mach_tl, &reqh->rh_rpc_machines, rpcmach) {
		M0_ASSERT(m0_rpc_machine_bob_check(rpcmach));
		struct m0_rios_buffer_pool *newbp;
		bool                        bufpool_found = false;
		/*
		 * Check buffer pool for network domain of rpc_machine
		 */
		m0_tl_for(bufferpools, &serv_obj->rios_buffer_pools, bp) {

			if (bp->rios_ndom == rpcmach->rm_tm.ntm_dom) {
				/*
				 * Found buffer pool for domain.
				 * No need to create buffer pool
				 * for this domain.
				 */
				bufpool_found = true;
				break;
			}
		} m0_tl_endfor; /* bufferpools */

		if (bufpool_found)
			continue;

		/* Buffer pool for network domain not found, create one */
		IOS_ALLOC_PTR(newbp, &service->rs_addb_ctx, CREATE_BUF_POOL);
		if (newbp == NULL)
			return -ENOMEM;

		newbp->rios_ndom = rpcmach->rm_tm.ntm_dom;
		newbp->rios_bp_magic = M0_IOS_BUFFER_POOL_MAGIC;

		colours = m0_list_length(&newbp->rios_ndom->nd_tms);

		segment_size = m0_rpc_max_seg_size(newbp->rios_ndom);
		segments_nr  = m0_rpc_max_segs_nr(newbp->rios_ndom);

		rc = m0_net_buffer_pool_init(&newbp->rios_bp,
					      newbp->rios_ndom,
					      M0_NET_BUFFER_POOL_THRESHOLD,
					      segments_nr, segment_size,
					      colours, M0_0VEC_SHIFT);
		if (rc != 0) {
			m0_free(newbp);
			break;
		}

		newbp->rios_bp.nbp_ops = &buffer_pool_ops;
		/*
		 * Initialise channel for sending availability of buffers
		 * with buffer pool to I/O FOMs.
		 */
		m0_chan_init(&newbp->rios_bp_wait);

		/* Pre-allocate network buffers */
		m0_net_buffer_pool_lock(&newbp->rios_bp);
		nbuffs = m0_net_buffer_pool_provision(&newbp->rios_bp,
						      M0_NET_BUFFER_POOL_SIZE);
		m0_net_buffer_pool_unlock(&newbp->rios_bp);
		if (nbuffs < M0_NET_BUFFER_POOL_SIZE) {
			rc = -ENOMEM;
			m0_chan_fini(&newbp->rios_bp_wait);
			m0_net_buffer_pool_fini(&newbp->rios_bp);
			m0_free(newbp);
			break;
		}

		bufferpools_tlink_init(newbp);
		bufferpools_tlist_add(&serv_obj->rios_buffer_pools, newbp);

	} m0_tl_endfor; /* rpc_machines */
	m0_rwlock_read_unlock(&reqh->rh_rwlock);

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
static void ios_delete_buffer_pool(struct m0_reqh_service *service)
{
	struct m0_reqh_io_service  *serv_obj;
	struct m0_rios_buffer_pool *bp;

	M0_PRE(service != NULL);

	serv_obj = container_of(service, struct m0_reqh_io_service, rios_gen);
	M0_ASSERT(m0_reqh_io_service_invariant(serv_obj));

	m0_tl_for(bufferpools, &serv_obj->rios_buffer_pools, bp) {

		M0_ASSERT(bp != NULL);

		m0_chan_fini(&bp->rios_bp_wait);
		bufferpools_tlink_del_fini(bp);
		m0_net_buffer_pool_fini(&bp->rios_bp);
		m0_free(bp);

	} m0_tl_endfor; /* bufferpools */

	bufferpools_tlist_fini(&serv_obj->rios_buffer_pools);
}

/**
 * Allocates and initiates I/O Service instance.
 * This operation allocates & initiates service instance with its operation
 * vector.
 *
 * @param stype service type
 * @param service pointer to service instance.
 *
 * @pre stype != NULL && service != NULL
 */
static int ios_allocate(struct m0_reqh_service **service,
			struct m0_reqh_service_type *stype,
			const char *arg __attribute__((unused)))
{
	int                        i;
	int                        j;
	struct m0_reqh_io_service *ios;

	M0_PRE(service != NULL && stype != NULL);

	IOS_ALLOC_PTR(ios, &m0_ios_addb_ctx, SERVICE_ALLOC);
	if (ios == NULL) {
		return -ENOMEM;
	}

	for (i = 0, j = 0; i < ARRAY_SIZE(ios->rios_rwfom_stats); ++i) {
#undef CNTR_INIT
#define CNTR_INIT(_n)							\
		m0_addb_counter_init(&ios->rios_rwfom_stats[i]	\
				     .ifs_##_n##_cntr, ios_rwfom_cntr_rts[j++])
		CNTR_INIT(sizes);
		CNTR_INIT(times);
#undef CNTR_INIT
	}
	M0_ASSERT(j == ARRAY_SIZE(ios_rwfom_cntr_rts));

        bufferpools_tlist_init(&ios->rios_buffer_pools);
        ios->rios_magic = M0_IOS_REQH_SVC_MAGIC;

        *service = &ios->rios_gen;
	(*service)->rs_ops = &ios_ops;

	return 0;
}

/**
 * Finalise I/O Service instance.
 * This operation finalises service instance and de-allocate it.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void ios_fini(struct m0_reqh_service *service)
{
	struct m0_reqh_io_service *serv_obj;
	int                        i;

	M0_PRE(service != NULL);

	serv_obj = container_of(service, struct m0_reqh_io_service, rios_gen);
	M0_ASSERT(m0_reqh_io_service_invariant(serv_obj));

	for (i = 0; i < ARRAY_SIZE(serv_obj->rios_rwfom_stats); ++i) {
#undef CNTR_FINI
#define CNTR_FINI(n)							\
		m0_addb_counter_fini(&serv_obj->rios_rwfom_stats[i]	\
				     .ifs_##n##_cntr)
		CNTR_FINI(sizes);
		CNTR_FINI(times);
#undef CNTR_FINI
	}
	m0_free(serv_obj);
}

/**
 * Start I/O Service.
 * - Registers I/O FOP with service
 * - Initiates buffer pool
 * - Initialises channel for service to wait for buffer pool notEmpty event.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static int ios_start(struct m0_reqh_service *service)
{
	int			   rc;
	struct m0_cob_domain      *cdom;
	struct m0_reqh_io_service *serv_obj;

	M0_PRE(service != NULL);
	M0_PRE(!ios_cdom_is_initialised);

	rc = m0_ios_cdom_get(service->rs_reqh, &cdom, service->rs_uuid);
	if (rc != 0)
		return rc;

	serv_obj = container_of(service, struct m0_reqh_io_service, rios_gen);
	serv_obj->rios_cdom = *cdom;

	rc = ios_create_buffer_pool(service);
	if (rc != 0) {
		/* Cleanup required for already created buffer pools. */
		ios_delete_buffer_pool(service);
		return rc;
	}

	rc = m0_ios_poolmach_init(service->rs_reqh);

	return rc;
}

/**
 * Stops I/O Service.
 * - Frees buffer pool
 * - Un-registers I/O FOP with service
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void ios_stop(struct m0_reqh_service *service)
{
	M0_PRE(service != NULL);
	m0_ios_poolmach_fini(service->rs_reqh);
	ios_delete_buffer_pool(service);
	m0_ios_cdom_fini(service->rs_reqh);
	m0_reqh_key_fini(service->rs_reqh, ios_cdom_key);
}

M0_INTERNAL int m0_ios_cdom_get(struct m0_reqh *reqh,
				struct m0_cob_domain **out, uint64_t sid)
{
	int                      rc;
	struct m0_cob_domain    *cdom;
	struct m0_cob_domain_id  cdom_id;
	struct m0_db_tx          tx;
	struct m0_dbenv         *dbenv;

	M0_PRE(reqh != NULL);

	m0_rwlock_write_lock(&reqh->rh_rwlock);

	dbenv = reqh->rh_dbenv;
	cdom = m0_reqh_key_find(reqh, ios_cdom_key, sizeof *cdom);
	if (!ios_cdom_is_initialised) {
		cdom_id.id = sid;
		rc = m0_cob_domain_init(cdom, dbenv, &cdom_id);
		if (rc != 0)
			goto reqh_fini;

		rc = m0_db_tx_init(&tx, dbenv, 0);
		if (rc != 0)
			goto cdom_fini;

		rc = m0_cob_domain_mkfs(cdom, &M0_COB_SLASH_FID,
					&M0_COB_ROOT_FID, &tx);
		if (rc != 0) {
			m0_db_tx_abort(&tx);
			goto cdom_fini;
		}
		m0_db_tx_commit(&tx);
		ios_cdom_is_initialised = true;
	}
	*out = cdom;
	goto out;

cdom_fini:
	m0_cob_domain_fini(cdom);
reqh_fini:
	m0_reqh_key_fini(reqh, ios_cdom_key);
out:
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
	return rc;
}

M0_INTERNAL void m0_ios_cdom_fini(struct m0_reqh *reqh)
{
	struct m0_cob_domain *cdom;

	M0_PRE(reqh != NULL);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	cdom = m0_reqh_key_find(reqh, ios_cdom_key, sizeof *cdom);
	m0_cob_domain_fini(cdom);
	ios_cdom_is_initialised = false;
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
}

/**
   Posts ADDB statistics from an I/O service instance.
 */
static void ios_stats_post_addb(struct m0_reqh_service *service)
{
	struct m0_reqh_io_service *serv_obj;
	struct m0_reqh            *reqh = service->rs_reqh;
	struct m0_addb_ctx        *cv[] = { &service->rs_addb_ctx, NULL };
	int                        i;

	serv_obj = container_of(service, struct m0_reqh_io_service, rios_gen);
	M0_ASSERT(m0_reqh_io_service_invariant(serv_obj));

	for (i = 0; i < ARRAY_SIZE(serv_obj->rios_rwfom_stats); ++i) {
#undef CNTR_POST
#define CNTR_POST(n)							\
		M0_ADDB_POST_CNTR(&reqh->rh_addb_mc, cv, &serv_obj->	\
				  rios_rwfom_stats[i].ifs_##n##_cntr)
		CNTR_POST(sizes);
		CNTR_POST(times);
#undef CNTR_POST
	}
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
