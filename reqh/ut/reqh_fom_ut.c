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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/04/2011
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>	/* mkdir */
#include <sys/types.h>	/* mkdir */
#include <err.h>

#include "lib/memory.h"
#include "ut/ut.h"

#include "net/net.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "stob/stob.h"
#include "stob/ad.h"
#include "stob/linux.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "fop/fop_item_type.h"
#include "rpc/item.h"
#include "rpc/rpc_internal.h"
#include "fop/fom_generic.h"
#include "fop/fom_generic_xc.h"
#include "reqh/ut/io_fop_xc.h"
#include "reqh/ut/io_fop.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpclib.h"
#include "balloc/balloc.h"

#include "mdstore/mdstore.h"
/**
   @addtogroup reqh
   @{
 */

/**
 *  Server side structures and objects
 */

#define CLIENT_ENDPOINT_ADDR    "0@lo:12345:34:*"
#define SERVER_ENDPOINT_ADDR    "0@lo:12345:34:1"
#define CLIENT_DB_NAME		"reqh_ut_stob/cdb"
#define SERVER_DB_NAME		"reqh_ut_stob/sdb"

enum {
	CLIENT_COB_DOM_ID  = 101,
	SESSION_SLOTS      = 5,
	MAX_RPCS_IN_FLIGHT = 32,
	MAX_RETRIES        = 5,
};

static struct m0_stob_domain   *sdom;
static struct m0_mdstore        srv_mdstore;
static struct m0_cob_domain_id  srv_cob_dom_id;
static struct m0_rpc_machine    srv_rpc_mach;
static struct m0_dbenv          srv_db;
static struct m0_fol            srv_fol;
static struct m0_reqh_service  *reqh_ut_service;

/**
 * Global reqh object
 */
static struct m0_reqh  reqh;

/**
 * Helper structures and functions for ad stob.
 * These are used while performing a stob operation.
 */
struct reqh_ut_balloc {
	struct m0_mutex     rb_lock;
	m0_bindex_t         rb_next;
	struct m0_ad_balloc rb_ballroom;
};

static struct reqh_ut_balloc *getballoc(struct m0_ad_balloc *ballroom)
{
	return container_of(ballroom, struct reqh_ut_balloc, rb_ballroom);
}

static int reqh_ut_balloc_init(struct m0_ad_balloc *ballroom,
			       struct m0_dbenv *db,
			       uint32_t bshift,
			       m0_bindex_t container_size,
			       m0_bcount_t groupsize,
			       m0_bcount_t res_groups)
{
	struct reqh_ut_balloc *rb = getballoc(ballroom);

	m0_mutex_init(&rb->rb_lock);
	return 0;
}

static void reqh_ut_balloc_fini(struct m0_ad_balloc *ballroom)
{
	struct reqh_ut_balloc *rb = getballoc(ballroom);

	m0_mutex_fini(&rb->rb_lock);
}

static int reqh_ut_balloc_alloc(struct m0_ad_balloc *ballroom,
				struct m0_dtx *tx,
				m0_bcount_t count,
				struct m0_ext *out)
{
	struct reqh_ut_balloc	*rb = getballoc(ballroom);

	m0_mutex_lock(&rb->rb_lock);
	out->e_start = rb->rb_next;
	out->e_end   = rb->rb_next + count;
	rb->rb_next += count;
	m0_mutex_unlock(&rb->rb_lock);
	return 0;
}

static int reqh_ut_balloc_free(struct m0_ad_balloc *ballroom, struct m0_dtx *tx,
                            struct m0_ext *ext)
{
	return 0;
}

static const struct m0_ad_balloc_ops reqh_ut_balloc_ops = {
	.bo_init  = reqh_ut_balloc_init,
	.bo_fini  = reqh_ut_balloc_fini,
	.bo_alloc = reqh_ut_balloc_alloc,
	.bo_free  = reqh_ut_balloc_free,
};

static struct reqh_ut_balloc rb = {
	.rb_next = 0,
	.rb_ballroom = {
		.ab_ops = &reqh_ut_balloc_ops
	}
};

/* Buffer pool for TM receive queue. */
static struct m0_net_buffer_pool app_pool;

struct m0_stob_domain *reqh_ut_stob_domain_find(void)
{
	return sdom;
}

static int server_init(const char             *stob_path,
		       const char             *srv_db_name,
		       struct m0_net_domain   *net_dom,
		       struct m0_stob_id      *backid,
		       struct m0_stob_domain **bdom,
		       struct m0_stob        **bstore,
		       struct m0_stob        **reqh_addb_stob,
		       struct m0_stob_id      *rh_addb_stob_id)
{
        struct m0_db_tx              tx;
        int                          rc;
	struct m0_rpc_machine       *rpc_machine = &srv_rpc_mach;
	uint32_t		     bufs_nr;
	uint32_t		     tms_nr;
	struct m0_reqh_service_type *stype;
	struct stat		     info;
	int			     ino;

        srv_cob_dom_id.id = 102;

        /* Init the db */
        rc = m0_dbenv_init(&srv_db, srv_db_name, 0);
	M0_UT_ASSERT(rc == 0);

	rc = m0_fol_init(&srv_fol, &srv_db);
	M0_UT_ASSERT(rc == 0);

	/*
	 * Locate and create (if necessary) the backing store object.
	 */
	rc = lstat(stob_path, &info);
	M0_UT_ASSERT(rc == 0);
	rc = m0_linux_stob_domain_locate(stob_path, bdom, info.st_ino);
	M0_UT_ASSERT(rc == 0);

	rc = m0_stob_find(*bdom, backid, bstore);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT((*bstore)->so_state == CSS_UNKNOWN);

	rc = m0_stob_create(*bstore, NULL);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT((*bstore)->so_state == CSS_EXISTS);

	/*
	 * Create AD domain over backing store object.
	 */
	ino = m0_linux_stob_ino(*bstore);
	M0_UT_ASSERT(ino > 0);
	rc = m0_ad_stob_domain_locate("", &sdom, ino);
	M0_UT_ASSERT(rc == 0);

	rc = m0_ad_stob_setup(sdom, &srv_db, *bstore, &rb.rb_ballroom,
			      BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCK_SHIFT,
			      BALLOC_DEF_BLOCKS_PER_GROUP,
			      BALLOC_DEF_RESERVED_GROUPS);
	M0_UT_ASSERT(rc == 0);

	m0_stob_put(*bstore);

	/* Create or open a stob into which to store the record. */
	rc = m0_stob_find(*bdom, rh_addb_stob_id, reqh_addb_stob);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT((*reqh_addb_stob)->so_state == CSS_UNKNOWN);

	rc = m0_stob_create(*reqh_addb_stob, NULL);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT((*reqh_addb_stob)->so_state == CSS_EXISTS);

        /* Init mdstore without reading root cob. */
        rc = m0_mdstore_init(&srv_mdstore, &srv_cob_dom_id, &srv_db, 0);
        M0_UT_ASSERT(rc == 0);

        rc = m0_db_tx_init(&tx, &srv_db, 0);
        M0_UT_ASSERT(rc == 0);

        /* Create root session cob and other structures */
        rc = m0_rpc_root_session_cob_create(&srv_mdstore.md_dom, &tx);
        M0_UT_ASSERT(rc == 0);

        /* Comit and finalize old mdstore. */
        m0_db_tx_commit(&tx);
        m0_mdstore_fini(&srv_mdstore);

        /* Init new mdstore with open root flag. */
        rc = m0_mdstore_init(&srv_mdstore, &srv_cob_dom_id, &srv_db, 1);
        M0_UT_ASSERT(rc == 0);

	/* Initialising request handler */
	rc = M0_REQH_INIT(&reqh,
			  .rhia_dtm       = NULL,
			  .rhia_db        = &srv_db,
			  .rhia_mdstore   = &srv_mdstore,
			  .rhia_fol       = &srv_fol,
			  .rhia_svc       = NULL,
			  .rhia_addb_stob = NULL);
	M0_UT_ASSERT(rc == 0);
	m0_reqh_start(&reqh);

	tms_nr   = 1;
	bufs_nr  = m0_rpc_bufs_nr(M0_NET_TM_RECV_QUEUE_DEF_LEN, tms_nr);

	rc = m0_rpc_net_buffer_pool_setup(net_dom, &app_pool, bufs_nr, tms_nr);
	M0_UT_ASSERT(rc == 0);

	/* Init the rpcmachine */
        rc = m0_rpc_machine_init(rpc_machine, &srv_mdstore.md_dom, net_dom,
				 SERVER_ENDPOINT_ADDR, &reqh, &app_pool,
				 M0_BUFFER_ANY_COLOUR, 0,
				 M0_NET_TM_RECV_QUEUE_DEF_LEN);
        M0_UT_ASSERT(rc == 0);
	/* Start the rpcservice */
	stype = m0_reqh_service_type_find("rpcservice");
	M0_UT_ASSERT(stype != NULL);

	rc = m0_reqh_service_allocate(&reqh_ut_service, stype, NULL);
	M0_UT_ASSERT(rc == 0);
	m0_reqh_rpc_mach_tlink_init_at_tail(rpc_machine,
					    &reqh.rh_rpc_machines);
	m0_reqh_service_init(reqh_ut_service, &reqh, NULL);

	rc = m0_reqh_service_start(reqh_ut_service);
	M0_UT_ASSERT(rc == 0);
	return rc;
}

/* Fini the server */
static void server_fini(struct m0_stob_domain *bdom,
			struct m0_stob        *reqh_addb_stob)
{
	m0_reqh_rpc_mach_tlink_del_fini(&srv_rpc_mach);
        /* Fini the rpc_machine */
        m0_rpc_machine_fini(&srv_rpc_mach);

	m0_rpc_net_buffer_pool_cleanup(&app_pool);

        /* Fini the mdstore */
        m0_mdstore_fini(&srv_mdstore);

	m0_stob_put(reqh_addb_stob);
	m0_reqh_service_stop(reqh_ut_service);
	m0_reqh_service_fini(reqh_ut_service);
	m0_reqh_services_terminate(&reqh);
	m0_reqh_fini(&reqh);
	M0_UT_ASSERT(sdom != NULL);
	sdom->sd_ops->sdo_fini(sdom);
	bdom->sd_ops->sdo_fini(bdom);
	m0_fol_fini(&srv_fol);
	m0_dbenv_fini(&srv_db);
}

static void fop_send(struct m0_fop *fop, struct m0_rpc_session *session)
{
	int rc;

	rc = m0_rpc_client_call(fop, session, NULL, 0 /* deadline */);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fop->f_item.ri_error == 0);
	M0_UT_ASSERT(fop->f_item.ri_reply != 0);
	m0_fop_put(fop);
}

/** Sends create fop request. */
static void create_send(struct m0_rpc_session *session)
{
	uint32_t                  i;
	struct m0_fop            *fop;
	struct m0_stob_io_create *rh_io_fop;

	for (i = 0; i < 10; ++i) {
		fop = m0_fop_alloc(&m0_stob_io_create_fopt, NULL);
		rh_io_fop = m0_fop_data(fop);
		rh_io_fop->fic_object.f_seq = i;
		rh_io_fop->fic_object.f_oid = i;
		fop_send(fop, session);
	}
}

/** Sends read fop request. */
static void read_send(struct m0_rpc_session *session)
{
	uint32_t                i;
	struct m0_fop          *fop;
	struct m0_stob_io_read *rh_io_fop;

	for (i = 0; i < 10; ++i) {
		fop = m0_fop_alloc(&m0_stob_io_read_fopt, NULL);
		rh_io_fop = m0_fop_data(fop);
		rh_io_fop->fir_object.f_seq = i;
		rh_io_fop->fir_object.f_oid = i;
		fop_send(fop, session);
	}
}

/** Sends write fop request. */
static void write_send(struct m0_rpc_session *session)
{
	uint32_t                 i;
	struct m0_fop           *fop;
	struct m0_stob_io_write *rh_io_fop;
	uint8_t                 *buf;

	for (i = 0; i < 10; ++i) {
		fop = m0_fop_alloc(&m0_stob_io_write_fopt, NULL);
		rh_io_fop = m0_fop_data(fop);
		rh_io_fop->fiw_object.f_seq = i;
		rh_io_fop->fiw_object.f_oid = i;

		M0_ALLOC_ARR(buf, 1 << BALLOC_DEF_BLOCK_SHIFT);
		M0_ASSERT(buf != NULL);
		rh_io_fop->fiw_value.fi_buf   = buf;
		rh_io_fop->fiw_value.fi_count = 1 << BALLOC_DEF_BLOCK_SHIFT;

		fop_send(fop, session);
	}
}

/**
 * Test function for reqh ut
 */
void test_reqh(void)
{
	int                    result;
	char                   opath[64];
	const char            *path;
	struct m0_net_xprt    *xprt        = &m0_net_lnet_xprt;
	struct m0_net_domain   net_dom     = { };
	struct m0_net_domain   srv_net_dom = { };
	struct m0_dbenv        client_dbenv;
	struct m0_cob_domain   client_cob_dom;
	struct m0_stob_domain *bdom;
	struct m0_stob_id      backid;
	struct m0_stob        *bstore;
	struct m0_stob        *reqh_addb_stob;

	struct m0_stob_id      reqh_addb_stob_id = {
					.si_bits = {
						.u_hi = 1,
						.u_lo = 2
					}
				};

	struct m0_rpc_client_ctx cctx = {
		.rcx_net_dom            = &net_dom,
		.rcx_local_addr         = CLIENT_ENDPOINT_ADDR,
		.rcx_remote_addr        = SERVER_ENDPOINT_ADDR,
		.rcx_db_name            = CLIENT_DB_NAME,
		.rcx_dbenv              = &client_dbenv,
		.rcx_cob_dom_id         = CLIENT_COB_DOM_ID,
		.rcx_cob_dom            = &client_cob_dom,
		.rcx_nr_slots           = SESSION_SLOTS,
		.rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
	};

	backid.si_bits.u_hi = 0x0;
	backid.si_bits.u_lo = 0xdf11e;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	path = "reqh_ut_stob";

	result = m0_stob_io_fop_init();
	M0_UT_ASSERT(result == 0);

	M0_UT_ASSERT(strlen(path) < ARRAY_SIZE(opath) - 8);

	result = mkdir(path, 0700);
	M0_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));
	sprintf(opath, "%s/o", path);

	result = mkdir(opath, 0700);
	M0_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = m0_net_xprt_init(xprt);
	M0_UT_ASSERT(result == 0);

	result = m0_net_domain_init(&net_dom, xprt, &m0_addb_proc_ctx);
	M0_UT_ASSERT(result == 0);
	result = m0_net_domain_init(&srv_net_dom, xprt, &m0_addb_proc_ctx);
	M0_UT_ASSERT(result == 0);

	server_init(path, SERVER_DB_NAME, &srv_net_dom, &backid, &bdom, &bstore,
		    &reqh_addb_stob, &reqh_addb_stob_id);

	result = m0_rpc_client_start(&cctx);
	M0_UT_ASSERT(result == 0);

	/* send fops */
	create_send(&cctx.rcx_session);
	write_send(&cctx.rcx_session);
	read_send(&cctx.rcx_session);

	result = m0_rpc_client_stop(&cctx);
	M0_UT_ASSERT(result == 0);

	server_fini(bdom, reqh_addb_stob);

	m0_net_domain_fini(&net_dom);
	m0_net_domain_fini(&srv_net_dom);
	m0_net_xprt_fini(xprt);
	m0_stob_io_fop_fini();
}

const struct m0_test_suite reqh_ut = {
	.ts_name = "reqh-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "reqh", test_reqh },
		{ NULL, NULL }
	}
};

/** @} end group reqh */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
