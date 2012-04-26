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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/04/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>	/* mkdir */
#include <sys/types.h>	/* mkdir */
#include <err.h>

#include "lib/ut.h"
#include "lib/misc.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/chan.h"
#include "lib/processor.h"
#include "lib/list.h"

#include "colibri/init.h"
#include "net/net.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"
#include "stob/stob.h"
#include "stob/ad.h"
#include "stob/linux.h"
#include "net/bulk_sunrpc.h"
#include "rpc/rpc2.h"
#include "fop/fop_item_type.h"
#include "rpc/rpc_base.h"
#include "xcode/bufvec_xcode.h"

#include "fop/fop_format_def.h"

#ifdef __KERNEL__
#include "reqh/reqh_fops_k.h"
#include "io_fop_k.h"
#else
#include "reqh/reqh_fops_u.h"
#include "io_fop_u.h"
#endif

#include "io_fop.h"
#include "reqh/reqh_fops.ff"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpclib.h"
#include "ut/rpc.h"
#include "balloc/balloc.h"

#include "mdstore/mdstore.h"
/**
   @addtogroup reqh
   @{
 */

/**
 *  Server side structures and objects
 */

#define CLIENT_ENDPOINT_ADDR	"127.0.0.1:21435:1"
#define SERVER_ENDPOINT_ADDR	"127.0.0.1:21435:2"
#define CLIENT_DB_NAME		"reqh_ut_stob/cdb"
#define SERVER_DB_NAME		"reqh_ut_stob/sdb"

enum {
	CLIENT_COB_DOM_ID	= 101,
	SESSION_SLOTS		= 5,
	MAX_RPCS_IN_FLIGHT	= 32,
	CONNECT_TIMEOUT		= 5,
};

static struct c2_stob_domain   *sdom;
static struct c2_md_store      srv_mdstore;
static struct c2_cob_domain_id srv_cob_dom_id;
static struct c2_rpc_machine   srv_rpc_mach;
static struct c2_dbenv         srv_db;
static struct c2_fol           srv_fol;

/**
 * Global reqh object
 */
static struct c2_reqh  reqh;

/**
 * Helper structures and functions for ad stob.
 * These are used while performing a stob operation.
 */
struct reqh_ut_balloc {
	struct c2_mutex     rb_lock;
	c2_bindex_t         rb_next;
	struct c2_ad_balloc rb_ballroom;
};

static struct reqh_ut_balloc *getballoc(struct c2_ad_balloc *ballroom)
{
	return container_of(ballroom, struct reqh_ut_balloc, rb_ballroom);
}

static int reqh_ut_balloc_init(struct c2_ad_balloc *ballroom, struct c2_dbenv *db,
			       uint32_t bshift, c2_bindex_t container_size,
			       c2_bcount_t groupsize, c2_bcount_t res_groups)
{
	struct reqh_ut_balloc *rb = getballoc(ballroom);

	c2_mutex_init(&rb->rb_lock);
	return 0;
}

static void reqh_ut_balloc_fini(struct c2_ad_balloc *ballroom)
{
	struct reqh_ut_balloc *rb = getballoc(ballroom);

	c2_mutex_fini(&rb->rb_lock);
}

static int reqh_ut_balloc_alloc(struct c2_ad_balloc *ballroom, struct c2_dtx *tx,
                             c2_bcount_t count, struct c2_ext *out)
{
	struct reqh_ut_balloc	*rb = getballoc(ballroom);

	c2_mutex_lock(&rb->rb_lock);
	out->e_start = rb->rb_next;
	out->e_end   = rb->rb_next + count;
	rb->rb_next += count;
	c2_mutex_unlock(&rb->rb_lock);
	return 0;
}

static int reqh_ut_balloc_free(struct c2_ad_balloc *ballroom, struct c2_dtx *tx,
                            struct c2_ext *ext)
{
	return 0;
}

static const struct c2_ad_balloc_ops reqh_ut_balloc_ops = {
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

static int server_init(const char *stob_path, const char *srv_db_name,
			struct c2_net_domain *net_dom, struct c2_stob_id *backid,
			struct c2_stob_domain **bdom, struct c2_stob **bstore,
			struct c2_stob **reqh_addb_stob,
			struct c2_stob_id *rh_addb_stob_id)
{
        int                        rc;
	struct c2_net_transfer_mc *srv_tm;

        srv_cob_dom_id.id = 102;

        /* Init the db */
        rc = c2_dbenv_init(&srv_db, srv_db_name, 0);
	C2_UT_ASSERT(rc == 0);

	rc = c2_fol_init(&srv_fol, &srv_db);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Locate and create (if necessary) the backing store object.
	 */

	rc = c2_stob_domain_locate(&c2_linux_stob_type, stob_path, bdom);
	C2_UT_ASSERT(rc == 0);

	rc = c2_stob_find(*bdom, backid, bstore);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT((*bstore)->so_state == CSS_UNKNOWN);

	rc = c2_stob_create(*bstore, NULL);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT((*bstore)->so_state == CSS_EXISTS);

	/*
	 * Create AD domain over backing store object.
	 */
	rc = c2_stob_domain_locate(&c2_ad_stob_type, "", &sdom);
	C2_UT_ASSERT(rc == 0);

	rc = c2_ad_stob_setup(sdom, &srv_db, *bstore, &rb.rb_ballroom,
			      BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCK_SHIFT,
			      BALLOC_DEF_BLOCKS_PER_GROUP,
			      BALLOC_DEF_RESERVED_GROUPS);
	C2_UT_ASSERT(rc == 0);

	c2_stob_put(*bstore);

	/* Create or open a stob into which to store the record. */
	rc = c2_stob_find(*bdom, rh_addb_stob_id, reqh_addb_stob);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT((*reqh_addb_stob)->so_state == CSS_UNKNOWN);

	rc = c2_stob_create(*reqh_addb_stob, NULL);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT((*reqh_addb_stob)->so_state == CSS_EXISTS);

	/* Write addb record into stob */
	c2_addb_choose_store_media(C2_ADDB_REC_STORE_STOB, c2_addb_stob_add,
					  *reqh_addb_stob, NULL);

        /* Init the cob domain */
        rc = c2_md_store_init(&srv_mdstore, &srv_cob_dom_id, &srv_db, 1);
        C2_UT_ASSERT(rc == 0);

	/* Initialising request handler */
	rc =  c2_reqh_init(&reqh, NULL, sdom, &srv_db, &srv_mdstore,
			   &srv_fol, NULL);
	C2_UT_ASSERT(rc == 0);

        /* Init the rpc_machine */
        rc = c2_rpc_machine_init(&srv_rpc_mach, &srv_mdstore.md_dom, net_dom,
				 SERVER_ENDPOINT_ADDR, &reqh);
        C2_UT_ASSERT(rc == 0);

        /* Find first c2_rpc_chan from the chan's list
           and use its corresponding tm to create target end_point */
        srv_tm = &srv_rpc_mach.rm_tm;
	C2_UT_ASSERT(srv_tm != NULL);

	return rc;
}

/* Fini the server */
static void server_fini(struct c2_stob_domain *bdom,
		struct c2_stob *reqh_addb_stob)
{
        /* Fini the rpc_machine */
        c2_rpc_machine_fini(&srv_rpc_mach);

        /* Fini the mdstore */
        c2_md_store_fini(&srv_mdstore);

	c2_addb_choose_store_media(C2_ADDB_REC_STORE_NONE);
	c2_stob_put(reqh_addb_stob);

	c2_reqh_fini(&reqh);
	C2_UT_ASSERT(sdom != NULL);
	sdom->sd_ops->sdo_fini(sdom);
	bdom->sd_ops->sdo_fini(bdom);
	c2_fol_fini(&srv_fol);
	c2_dbenv_fini(&srv_db);
}

/**
 * Sends create fop request.
 */
static void create_send(struct c2_rpc_session *session)
{
	int                      rc;
	uint32_t                 i;
	struct c2_fop            *fop;
	struct c2_stob_io_create *rh_io_fop;

	for (i = 0; i < 10; ++i) {
		fop = c2_fop_alloc(&c2_stob_io_create_fopt, NULL);
		rh_io_fop = c2_fop_data(fop);
		rh_io_fop->fic_object.f_seq = i;
		rh_io_fop->fic_object.f_oid = i;

		rc = c2_rpc_client_call(fop, session, &c2_fop_default_item_ops,
						CONNECT_TIMEOUT);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(fop->f_item.ri_error == 0);
		C2_UT_ASSERT(fop->f_item.ri_reply != 0);
	}
}

/**
 * Sends read fop request.
 */
static void read_send(struct c2_rpc_session *session)
{
	int                     rc;
	uint32_t                i;
	struct c2_fop           *fop;
	struct c2_stob_io_read  *rh_io_fop;

	for (i = 0; i < 10; ++i) {
		fop = c2_fop_alloc(&c2_stob_io_read_fopt, NULL);
		rh_io_fop = c2_fop_data(fop);
		rh_io_fop->fir_object.f_seq = i;
		rh_io_fop->fir_object.f_oid = i;

		rc = c2_rpc_client_call(fop, session, &c2_fop_default_item_ops,
						CONNECT_TIMEOUT);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(fop->f_item.ri_error == 0);
		C2_UT_ASSERT(fop->f_item.ri_reply != 0);
	}
}

/**
 * Sends write fop request.
 */
static void write_send(struct c2_rpc_session *session)
{
	int                      rc;
	uint32_t                 i;
	struct c2_fop            *fop;
	struct c2_stob_io_write  *rh_io_fop;

	for (i = 0; i < 10; ++i) {
		fop = c2_fop_alloc(&c2_stob_io_write_fopt, NULL);
		rh_io_fop = c2_fop_data(fop);
		rh_io_fop->fiw_object.f_seq = i;
		rh_io_fop->fiw_object.f_oid = i;

		rc = c2_rpc_client_call(fop, session, &c2_fop_default_item_ops,
						CONNECT_TIMEOUT);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(fop->f_item.ri_error == 0);
		C2_UT_ASSERT(fop->f_item.ri_reply != 0);
	}
}

/**
 * Test function for reqh ut
 */
void test_reqh(void)
{
	int                    result;
	char                   opath[64];
	const char             *path;
	struct c2_net_xprt     *xprt = &c2_net_bulk_sunrpc_xprt;
	struct c2_net_domain   net_dom = { };
	struct c2_dbenv        client_dbenv;
	struct c2_cob_domain   client_cob_dom;
	struct c2_stob_domain  *bdom;
	struct c2_stob_id      backid;
	struct c2_stob         *bstore;
	struct c2_stob         *reqh_addb_stob;

	struct c2_stob_id      reqh_addb_stob_id = {
					.si_bits = {
						.u_hi = 1,
						.u_lo = 2
					}
				};

	struct c2_rpc_client_ctx cctx = {
		.rcx_net_dom            = &net_dom,
		.rcx_local_addr         = CLIENT_ENDPOINT_ADDR,
		.rcx_remote_addr        = SERVER_ENDPOINT_ADDR,
		.rcx_db_name            = CLIENT_DB_NAME,
		.rcx_dbenv              = &client_dbenv,
		.rcx_cob_dom_id         = CLIENT_COB_DOM_ID,
		.rcx_cob_dom            = &client_cob_dom,
		.rcx_nr_slots           = SESSION_SLOTS,
		.rcx_timeout_s          = CONNECT_TIMEOUT,
		.rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
	};

	backid.si_bits.u_hi = 0x0;
	backid.si_bits.u_lo = 0xdf11e;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	path = "reqh_ut_stob";

	/* Initialize processors */
	if (!c2_processor_is_initialized()) {
		result = c2_processors_init();
		C2_UT_ASSERT(result == 0);
	}

	result = c2_stob_io_fop_init();
	C2_UT_ASSERT(result == 0);

	C2_UT_ASSERT(strlen(path) < ARRAY_SIZE(opath) - 8);

	result = mkdir(path, 0700);
	C2_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));
	sprintf(opath, "%s/o", path);

	result = mkdir(opath, 0700);
	C2_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = c2_net_xprt_init(xprt);
	C2_UT_ASSERT(result == 0);

	result = c2_net_domain_init(&net_dom, xprt);
	C2_UT_ASSERT(result == 0);

	server_init(path, SERVER_DB_NAME, &net_dom, &backid, &bdom, &bstore,
			&reqh_addb_stob, &reqh_addb_stob_id);

	result = c2_rpc_client_init(&cctx);
	C2_UT_ASSERT(result == 0);

	/* send fops */
	create_send(&cctx.rcx_session);
	write_send(&cctx.rcx_session);
	read_send(&cctx.rcx_session);

	result = c2_rpc_client_fini(&cctx);
	C2_UT_ASSERT(result == 0);

	server_fini(bdom, reqh_addb_stob);

	c2_net_domain_fini(&net_dom);
	c2_net_xprt_fini(xprt);
	c2_stob_io_fop_fini();

	if (c2_processor_is_initialized())
		c2_processors_fini();
}

const struct c2_test_suite reqh_ut = {
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
