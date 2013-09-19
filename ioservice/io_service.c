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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_IOSERVICE
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/tlist.h"
#include "lib/locality.h"
#include "mero/magic.h"
#include "rpc/rpc.h"
#include "rpc/rpclib.h"
#include "net/buffer_pool.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "ioservice/io_fops.h"
#include "ioservice/io_service.h"
#include "ioservice/io_device.h"
#include "pool/pool.h"
#include "net/lnet/lnet.h"
#include "mdservice/md_fops.h"
#include "layout/layout.h"
#include "layout/pdclust.h"

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

/**
 * Key for pool machine
 * For usage please see ioservice/io_device.c:m0_ios_poolmach_*()
 */
M0_INTERNAL unsigned poolmach_key;

/**
 * Key for ioservice cob domain
 */
static unsigned ios_cdom_key;

/**
 * Key for ios mds connection.
 */
static unsigned ios_mds_conn_key = 0;

static int ios_allocate(struct m0_reqh_service **service,
			struct m0_reqh_service_type *stype,
			struct m0_reqh_context *rctx);
static void ios_fini(struct m0_reqh_service *service);

static int ios_start(struct m0_reqh_service *service);
void ios_prepare_to_stop(struct m0_reqh_service *service);
static void ios_stop(struct m0_reqh_service *service);
static void ios_stats_post_addb(struct m0_reqh_service *service);

static void buffer_pool_not_empty(struct m0_net_buffer_pool *bp);
static void buffer_pool_low(struct m0_net_buffer_pool *bp);

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
	.rso_prepare_to_stop = ios_prepare_to_stop,
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
	m0_chan_broadcast(&buffer_desc->rios_bp_wait);
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
	RT_REG(io_finish);
	RT_REG(desc_io_finish);
	RT_REG(buffer_pool_low);
#undef RT_REG

	m0_addb_ctx_type_register(&m0_addb_ct_cob_create_fom);
	m0_addb_ctx_type_register(&m0_addb_ct_cob_delete_fom);
	m0_addb_ctx_type_register(&m0_addb_ct_cob_io_rw_fom);
	m0_reqh_service_type_register(&m0_ios_type);
	ios_cdom_key = m0_reqh_lockers_allot();
	poolmach_key = m0_reqh_lockers_allot();
	ios_mds_conn_key = m0_reqh_lockers_allot();
	return m0_ioservice_fop_init();
}

/**
 * Unregisters I/O service from mero node.
 */
M0_INTERNAL void m0_ios_unregister(void)
{
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
	m0_tl_for(m0_reqh_rpc_mach, &reqh->rh_rpc_machines, rpcmach) {
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
		m0_chan_init(&newbp->rios_bp_wait, &newbp->rios_bp.nbp_mutex);

		/* Pre-allocate network buffers */
		m0_net_buffer_pool_lock(&newbp->rios_bp);
		nbuffs = m0_net_buffer_pool_provision(&newbp->rios_bp,
						      M0_NET_BUFFER_POOL_SIZE);
		m0_net_buffer_pool_unlock(&newbp->rios_bp);
		if (nbuffs < M0_NET_BUFFER_POOL_SIZE) {
			rc = -ENOMEM;
			m0_chan_fini_lock(&newbp->rios_bp_wait);
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

		m0_chan_fini_lock(&bp->rios_bp_wait);
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
			struct m0_reqh_context *rctx)
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
		m0_addb_counter_init(&ios->rios_rwfom_stats[i]		\
				     .ais_##_n##_cntr, ios_rwfom_cntr_rts[j++])
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
				     .ais_##n##_cntr)
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
	struct m0_reqh_io_service *serv_obj;

	M0_PRE(service != NULL);

	serv_obj = container_of(service, struct m0_reqh_io_service, rios_gen);
	/** @todo what should be cob dom id? */
	rc = m0_ios_cdom_get(service->rs_reqh, &serv_obj->rios_cdom);
	if (rc != 0)
		return rc;

	rc = ios_create_buffer_pool(service);
	if (rc != 0) {
		/* Cleanup required for already created buffer pools. */
		ios_delete_buffer_pool(service);
		m0_ios_cdom_fini(service->rs_reqh);
		return rc;
	}

	rc = m0_ios_poolmach_init(service);
	if (rc != 0) {
		ios_delete_buffer_pool(service);
		m0_ios_cdom_fini(service->rs_reqh);
	}
	return rc;
}

void ios_prepare_to_stop(struct m0_reqh_service *service)
{
	M0_LOG(M0_DEBUG, "ioservice PREPARE ......");
	m0_ios_mds_conn_fini(service->rs_reqh);
	M0_LOG(M0_DEBUG, "ioservice PREPARE STOPPED");
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

	m0_ios_poolmach_fini(service);
	ios_delete_buffer_pool(service);
	m0_ios_cdom_fini(service->rs_reqh);
	m0_reqh_lockers_clear(service->rs_reqh, ios_cdom_key);
	M0_LOG(M0_DEBUG, "ioservice STOPPED");
}

M0_INTERNAL int m0_ios_cdom_get(struct m0_reqh *reqh,
				struct m0_cob_domain **out)
{
	/* XXX For now generating cob domain id randomly. Should be fixed */
	static uint64_t          cid = 37;
	int                      rc = 0;
	struct m0_cob_domain    *cdom;
	struct m0_cob_domain_id  cdom_id;
	struct m0_dtx            tx;
	struct m0_dbenv         *dbenv;
	struct m0_sm_group      *grp = m0_locality0_get()->lo_grp;

	M0_PRE(reqh != NULL);

	m0_rwlock_write_lock(&reqh->rh_rwlock);

	dbenv = reqh->rh_dbenv;
	cdom = m0_reqh_lockers_get(reqh, ios_cdom_key);
	if (cdom == NULL) {
		cdom = m0_alloc(sizeof *cdom);
		if (cdom == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		m0_reqh_lockers_set(reqh, ios_cdom_key, cdom);

		M0_LOG(M0_DEBUG, "key init for reqh=%p, key=%d",
		       reqh, ios_cdom_key);
		cdom_id.id = m0_rnd(1ULL << 47, &cid);
		rc = m0_cob_domain_init(cdom, reqh->rh_beseg, &cdom_id);
		if (rc != 0 && rc != -ENOENT)
			goto reqh_fini;
		if (rc == -ENOENT) {
			m0_sm_group_lock(grp);
			rc = m0_cob_domain_create(cdom, grp);
			m0_sm_group_unlock(grp);
		}
		if (rc != 0)
			goto cdom_fini;

		m0_sm_group_lock(grp);
		m0_dtx_init(&tx, reqh->rh_beseg->bs_domain, grp);
		m0_cob_tx_credit(cdom, M0_COB_OP_DOMAIN_MKFS, &tx.tx_betx_cred);
		rc = m0_dtx_open_sync(&tx);
		if (rc == 0) {
			rc = m0_cob_domain_mkfs(cdom, &M0_COB_SLASH_FID,
						&M0_COB_SESSIONS_FID,
						&tx.tx_betx);
			m0_dtx_done_sync(&tx);
		}
		m0_sm_group_unlock(grp);
		if (rc != 0)
			goto cdom_destroy;
	}
	*out = cdom;
	goto out;

cdom_destroy:
	m0_cob_domain_destroy(cdom, grp);
cdom_fini:
	m0_cob_domain_fini(cdom);
reqh_fini:
	m0_reqh_lockers_clear(reqh, ios_cdom_key);
out:
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
	return rc;
}

M0_INTERNAL void m0_ios_cdom_fini(struct m0_reqh *reqh)
{
	struct m0_cob_domain *cdom;

	M0_PRE(reqh != NULL);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	cdom = m0_reqh_lockers_get(reqh, ios_cdom_key);
	m0_cob_domain_fini(cdom);
	m0_free(cdom);
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
		struct m0_addb_io_stats *stats;

		stats = &serv_obj->rios_rwfom_stats[i];
#undef CNTR_POST
#define CNTR_POST(n)							\
		if (m0_addb_counter_nr(&stats->ais_##n##_cntr) > 0)	\
			M0_ADDB_POST_CNTR(&reqh->rh_addb_mc, cv,	\
					  &stats->ais_##n##_cntr)
		CNTR_POST(sizes);
		CNTR_POST(times);
#undef CNTR_POST
	}
}

enum {
	RPC_TIMEOUT          = 8, /* seconds */
	NR_SLOTS_PER_SESSION = 10,
	MAX_NR_RPC_IN_FLIGHT = 100,
};

static int m0_ios_mds_conn_init(struct m0_reqh *reqh,
				struct m0_ios_mds_conn *conn)
{
	struct m0_mero        *mero;
	struct m0_rpc_machine *rpc_machine;
	const char            *srv_ep_addr;
	int                    rc;
	M0_ENTRY();

	M0_PRE(reqh != NULL);
	mero = m0_cs_ctx_get(reqh);
	rpc_machine = m0_reqh_rpc_mach_tlist_head(&reqh->rh_rpc_machines);
	M0_ASSERT(mero != NULL);
	M0_ASSERT(rpc_machine != NULL);

	srv_ep_addr = mero->cc_mds_epx.ex_endpoint;

	M0_LOG(M0_DEBUG, "srv = %s", srv_ep_addr);

	if (srv_ep_addr == NULL) {
		M0_LOG(M0_WARN, "None of mdservice endpoints provided");
		M0_RETURN(0);
	}

	M0_LOG(M0_DEBUG, "Ios connecting to mds %s", srv_ep_addr);
	rc = m0_rpc_client_connect(&conn->imc_conn, &conn->imc_session,
				   rpc_machine, srv_ep_addr,
				   MAX_NR_RPC_IN_FLIGHT, NR_SLOTS_PER_SESSION);
	if (rc == 0) {
		conn->imc_connected = true;
		M0_LOG(M0_DEBUG, "Ios connected to mds %s", srv_ep_addr);
	} else {
		conn->imc_connected = false;
		M0_LOG(M0_ERROR, "Ios could not connect to mds %s: rc = %d",
				 srv_ep_addr, rc);
	}
	M0_RETURN(rc);
}

/* Assumes that reqh->rh_rwlock is locked for writing. */
static int ios_mds_conn_get_locked(struct m0_reqh *reqh,
				   struct m0_ios_mds_conn **out,
				   bool *new)
{
	M0_PRE(ios_mds_conn_key != 0);

	*new = false;
	*out = m0_reqh_lockers_get(reqh, ios_mds_conn_key);
	if (*out != NULL)
		return 0;

	M0_ALLOC_PTR(*out);
	if (*out == NULL)
		return -ENOMEM;
	*new = true;

	m0_reqh_lockers_set(reqh, ios_mds_conn_key, *out);
	M0_LOG(M0_DEBUG, "key init for reqh=%p, key=%d", reqh,
	       ios_mds_conn_key);
	return 0;
}

/**
 * Gets ioservice to mdservice connection. If it is newly allocated, establish
 * the connection.
 *
 * @param out the connection is returned here.
 *
 * @note This is a block operation in service.
 *       m0_fom_block_enter()/m0_fom_block_leave() must be used to notify fom.
 */
M0_INTERNAL int m0_ios_mds_conn_get(struct m0_reqh *reqh,
				    struct m0_ios_mds_conn **out)
{
	int  rc;
	bool new;

	M0_ENTRY("reqh %p", reqh);
	M0_PRE(reqh != NULL);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	rc = ios_mds_conn_get_locked(reqh, out, &new);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	if (new) {
		M0_ASSERT(rc == 0);
		rc = m0_ios_mds_conn_init(reqh, *out);
	}
	M0_RETURN(rc);
}

/**
 * Terminates and clears the ioservice to mdservice connection.
 */
M0_INTERNAL void m0_ios_mds_conn_fini(struct m0_reqh *reqh)
{
	struct m0_ios_mds_conn *imc;
	int                     rc;
	M0_PRE(reqh != NULL);
	M0_PRE(ios_mds_conn_key != 0);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	imc = m0_reqh_lockers_get(reqh, ios_mds_conn_key);
	if (imc != NULL)
		m0_reqh_lockers_clear(reqh, ios_mds_conn_key);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	M0_LOG(M0_DEBUG, "imc conn fini in reqh = %p, imc = %p", reqh, imc);
	if (imc != NULL && imc->imc_connected) {
		M0_LOG(M0_DEBUG, "destroy session for %p", imc);
		rc = m0_rpc_session_destroy(&imc->imc_session, M0_TIME_NEVER);
		if (rc != 0)
			M0_LOG(M0_ERROR, "Failed to terminate session %d", rc);

		M0_LOG(M0_DEBUG, "destroy conn for %p", imc);
		rc = m0_rpc_conn_destroy(&imc->imc_conn, M0_TIME_NEVER);
		if (rc != 0)
			M0_LOG(M0_ERROR, "Failed to terminate connection %d", rc);
	}
	m0_free(imc); /* free(NULL) is OK */
}

/**
 * Gets file attributes from mdservice.
 * @param reqh the request handler.
 * @param gfid the global fid of the file.
 * @param attr the returned attributes will be stored here.
 *
 * @note This is a block operation in service.
 *       m0_fom_block_enter()/m0_fom_block_leave() must be used to notify fom.
 */
M0_INTERNAL int m0_ios_mds_getattr(struct m0_reqh *reqh,
				   const struct m0_fid *gfid,
				   struct m0_cob_attr *attr)
{
	struct m0_ios_mds_conn    *imc;
	struct m0_fop             *req;
	struct m0_fop             *rep;
	struct m0_fop_getattr     *getattr;
	struct m0_fop_getattr_rep *getattr_rep;
	struct m0_fop_cob         *req_fop_cob;
	struct m0_fop_cob         *rep_fop_cob;
	int                        rc;

	rc = m0_ios_mds_conn_get(reqh, &imc);
	if (rc != 0)
		return rc;
	if (!imc->imc_connected)
		return -ENODEV;

	req = m0_fop_alloc(&m0_fop_getattr_fopt, NULL);
	if (req == NULL)
		return -ENOMEM;

	getattr = m0_fop_data(req);
	req_fop_cob = &getattr->g_body;
	req_fop_cob->b_tfid = *gfid;

	M0_LOG(M0_DEBUG, "ios getattr for %llu:%llu",
			 (unsigned long long)gfid->f_container,
			 (unsigned long long)gfid->f_key);

	rc = m0_rpc_client_call(req, &imc->imc_session, NULL, 0);
	M0_LOG(M0_DEBUG, "ios getattr for %llu:%llu rc:%d",
			 (unsigned long long)gfid->f_container,
			 (unsigned long long)gfid->f_key, rc);

	if (rc == 0) {
		rep = m0_rpc_item_to_fop(req->f_item.ri_reply);
		getattr_rep = m0_fop_data(rep);
		rep_fop_cob = &getattr_rep->g_body;
		if (rep_fop_cob->b_rc == 0)
			m0_md_cob_wire2mem(attr, rep_fop_cob);
		else
			rc = rep_fop_cob->b_rc;
	}
	m0_fop_put(req);
	return rc;
}

/**
 * Gets layout from mdservice with specified layout id.
 * @param reqh the request handler.
 * @param ldom the layout domain in which the layout will be created.
 * @param lid  the layout id to query.
 * @param l_out returned layout will be stored here.
 *
 * @note This is a block operation in service.
 *       m0_fom_block_enter()/m0_fom_block_leave() must be used to notify fom.
*/
M0_INTERNAL int m0_ios_mds_layout_get(struct m0_reqh *reqh,
				      struct m0_layout_domain *ldom,
				      uint64_t lid,
				      struct m0_layout **l_out)
{
	struct m0_ios_mds_conn    *imc;
	struct m0_fop             *req;
	struct m0_fop             *rep;
	struct m0_fop_layout      *layout;
	struct m0_fop_layout_rep  *layout_rep;
	struct m0_layout          *l;
	int                        rc;
	M0_ENTRY();

	l = m0_layout_find(ldom, lid);
	if (l != NULL) {
		*l_out = l;
		return 0;
	}

	rc = m0_ios_mds_conn_get(reqh, &imc);
	if (rc != 0)
		return rc;
	if (!imc->imc_connected)
		return -ENODEV;

	req = m0_fop_alloc(&m0_fop_layout_fopt, NULL);
	if (req == NULL)
		return -ENOMEM;

	layout = m0_fop_data(req);
	layout->l_op  = M0_LAYOUT_OP_LOOKUP;
	layout->l_lid = lid;

	M0_LOG(M0_DEBUG, "ios getlayout for %llu",
			 (unsigned long long)lid);
	rc = m0_rpc_client_call(req, &imc->imc_session, NULL, 0);
	M0_LOG(M0_DEBUG, "ios getlayout for %llu: rc %d",
			 (unsigned long long)lid, rc);
	if (rc == 0) {
		struct m0_bufvec               bv;
		struct m0_bufvec_cursor        cur;
		struct m0_layout_type         *lt;
		M0_ASSERT(l_out != NULL);

		rep = m0_rpc_item_to_fop(req->f_item.ri_reply);
		layout_rep = m0_fop_data(rep);

		bv = (struct m0_bufvec)
			M0_BUFVEC_INIT_BUF((void**)&layout_rep->lr_buf.b_addr,
				   (m0_bcount_t*)&layout_rep->lr_buf.b_count);
		m0_bufvec_cursor_init(&cur, &bv);

		lt = &m0_pdclust_layout_type;
		rc = lt->lt_ops->lto_allocate(ldom, lid, &l);
		if (rc == 0) {
			rc = m0_layout_decode(l, &cur, M0_LXO_BUFFER_OP, NULL);
			/* release lock held by ->lto_allocate() */
			m0_mutex_unlock(&l->l_lock);
			if (rc == 0) {
				/* m0_layout_put() should be called for l_out
				 * after use
				 */
				*l_out = l;
			} else {
				m0_layout_put(l);
			}
		}
	}

	m0_fop_put(req);
	M0_RETURN(rc);
}

static int _rpc_post(struct m0_fop                *fop,
		     struct m0_rpc_session        *session)
{
	struct m0_rpc_item *item;

	M0_PRE(fop != NULL);
	M0_PRE(session != NULL);

	item                     = &fop->f_item;
	item->ri_session         = session;
	item->ri_prio            = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline        = 0;
	item->ri_resend_interval = M0_TIME_NEVER;

	return m0_rpc_post(item);
}

struct mds_op {
	struct m0_fop mo_fop;

	void        (*mo_cb)(void *arg, int rc);
	void         *mo_arg;
	/** saved out pointer. returned data will be copied here */
	void         *mo_out;

	/* These arguments are saved in async call and used in callback */
	void         *mo_p1;   /* saved param1 */
	void         *mo_p2;   /* saved param2 */
};

static void mds_op_release(struct m0_ref *ref)
{
	struct mds_op *mds_op;
	struct m0_fop *fop;

	fop = container_of(ref, struct m0_fop, f_ref);
	mds_op = container_of(fop, struct mds_op, mo_fop);
	m0_fop_fini(fop);
	m0_free(mds_op);
}

static void getattr_rpc_item_reply_cb(struct m0_rpc_item *item)
{
	struct mds_op               *mdsop;
	struct m0_fop               *req;
	struct m0_fop               *rep;
	struct m0_fop_getattr_rep   *getattr_rep;
	struct m0_fop_cob           *rep_fop_cob;
	struct m0_cob_attr          *attr;
	int                          rc;

	M0_PRE(item != NULL);
	req = m0_rpc_item_to_fop(item);
	mdsop = container_of(req, struct mds_op, mo_fop);
	attr = mdsop->mo_out;

	rc = item->ri_error;

	M0_LOG(M0_DEBUG, "ios getattr replied :%d", rc);
	if (rc == 0) {
		rep = m0_rpc_item_to_fop(item->ri_reply);
		getattr_rep = m0_fop_data(rep);
		rep_fop_cob = &getattr_rep->g_body;
		if (rep_fop_cob->b_rc == 0)
			m0_md_cob_wire2mem(attr, rep_fop_cob);
		else
			rc = rep_fop_cob->b_rc;
	}

	mdsop->mo_cb(mdsop->mo_arg, rc);
}

const struct m0_rpc_item_ops getattr_fop_rpc_item_ops = {
	.rio_replied = getattr_rpc_item_reply_cb,
};

/**
 * getattr from mdservice asynchronously.
 */
M0_INTERNAL int m0_ios_mds_getattr_async(struct m0_reqh *reqh,
				         const struct m0_fid *gfid,
					 struct m0_cob_attr  *attr,
					 void (*cb)(void *arg, int rc),
					 void *arg)
{
	struct m0_ios_mds_conn    *imc;
	struct mds_op             *mdsop;
	struct m0_fop             *req;
	struct m0_fop_getattr     *getattr;
	struct m0_fop_cob         *req_fop_cob;
	int                        rc;

	/* This might block on first call. */
	rc = m0_ios_mds_conn_get(reqh, &imc);
	if (rc != 0)
		return rc;
	if (!imc->imc_connected)
		return -ENODEV;

	M0_ALLOC_PTR(mdsop);
	if (mdsop == NULL)
		return -ENOMEM;

	req = &mdsop->mo_fop;
	m0_fop_init(req, &m0_fop_getattr_fopt, NULL, &mds_op_release);
	rc = m0_fop_data_alloc(req);
	if (rc == 0) {
		req->f_item.ri_ops = &getattr_fop_rpc_item_ops;
	} else {
		m0_free(mdsop);
		return rc;
	}

	mdsop->mo_cb  = cb;
	mdsop->mo_arg = arg;
	mdsop->mo_out = attr;

	getattr = m0_fop_data(req);
	req_fop_cob = &getattr->g_body;
	req_fop_cob->b_tfid = *gfid;

	M0_LOG(M0_DEBUG, "ios getattr for %llu:%llu",
			 (unsigned long long)gfid->f_container,
			 (unsigned long long)gfid->f_key);

	rc = _rpc_post(req, &imc->imc_session);
	M0_LOG(M0_DEBUG, "ios getattr sent asynchronously: rc = %d", rc);

	m0_fop_put(req);
	return rc;
}

static void getlayout_rpc_item_reply_cb(struct m0_rpc_item *item)
{
	struct mds_op              *mdsop;
	struct m0_fop              *req;
	struct m0_fop              *rep;
	struct m0_fop_layout_rep   *layout_rep;
	struct m0_layout_domain    *ldom;
	uint64_t                    lid;
	struct m0_layout           *l;
	struct m0_layout          **l_out;
	int                         rc;

	M0_PRE(item != NULL);
	req = m0_rpc_item_to_fop(item);
	mdsop = container_of(req, struct mds_op, mo_fop);
	l_out = mdsop->mo_out;

	ldom = mdsop->mo_p1;
	lid  = (uint64_t)mdsop->mo_p2;

	rc = item->ri_error;

	M0_LOG(M0_DEBUG, "ios getlayout @[%p:%lu] replied :%d", ldom, lid, rc);
	if (rc == 0) {
		struct m0_bufvec        bv;
		struct m0_bufvec_cursor cur;
		struct m0_layout_type  *lt;

		rep = m0_rpc_item_to_fop(item->ri_reply);
		layout_rep = m0_fop_data(rep);

		bv = (struct m0_bufvec)
			M0_BUFVEC_INIT_BUF((void**)&layout_rep->lr_buf.b_addr,
				   (m0_bcount_t*)&layout_rep->lr_buf.b_count);
		m0_bufvec_cursor_init(&cur, &bv);

		lt = &m0_pdclust_layout_type;
		rc = lt->lt_ops->lto_allocate(ldom, lid, &l);
		if (rc == 0) {
			rc = m0_layout_decode(l, &cur, M0_LXO_BUFFER_OP, NULL);
			/* release lock held by ->lto_allocate() */
			m0_mutex_unlock(&l->l_lock);
			if (rc == 0) {
				*l_out = l;
			} else {
				m0_layout_put(l);
			}
		}
	}
	mdsop->mo_cb(mdsop->mo_arg, rc);
}

const struct m0_rpc_item_ops getlayout_fop_rpc_item_ops = {
	.rio_replied = getlayout_rpc_item_reply_cb,
};

/** getlayout asynchronously from mdservice */
M0_INTERNAL int m0_ios_mds_layout_get_async(struct m0_reqh *reqh,
					    struct m0_layout_domain *ldom,
					    uint64_t lid,
					    struct m0_layout **l_out,
					    void (*cb)(void *arg, int rc),
					    void *arg)
{
	struct m0_ios_mds_conn *imc;
	struct mds_op          *mdsop;
	struct m0_fop          *req;
	struct m0_fop_layout   *layout;
	int                     rc;
	M0_ENTRY();

	rc = m0_ios_mds_conn_get(reqh, &imc);
	if (rc != 0)
		return rc;
	if (!imc->imc_connected)
		return -ENODEV;

	M0_ALLOC_PTR(mdsop);
	if (mdsop == NULL)
		return -ENOMEM;

	req = &mdsop->mo_fop;
	m0_fop_init(req, &m0_fop_layout_fopt, NULL, &mds_op_release);
	rc = m0_fop_data_alloc(req);
	if (rc == 0) {
		req->f_item.ri_ops = &getlayout_fop_rpc_item_ops;
	} else {
		m0_free(mdsop);
		return rc;
	}

	mdsop->mo_cb  = cb;
	mdsop->mo_arg = arg;
	mdsop->mo_out = l_out;
	mdsop->mo_p1  = ldom;
	mdsop->mo_p2  = (void *)lid;
	layout        = m0_fop_data(req);
	layout->l_op  = M0_LAYOUT_OP_LOOKUP;
	layout->l_lid = lid;

	M0_LOG(M0_DEBUG, "ios getlayout for %llu",
			 (unsigned long long)lid);
	rc = _rpc_post(req, &imc->imc_session);
	M0_LOG(M0_DEBUG, "ios getlayout for %llu sent: rc %d",
			 (unsigned long long)lid, rc);
	m0_fop_put(req);
	M0_RETURN(rc);
}

#undef M0_TRACE_SUBSYSTEM

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
