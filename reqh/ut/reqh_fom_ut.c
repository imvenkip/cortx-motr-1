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
#include "stob/io_fop_k.h"
#else
#include "reqh/reqh_fops_u.h"
#include "stob/io_fop_u.h"
#endif

#include "stob/io_fop.h"
#include "reqh/reqh_fops.ff"
#include "rpc/rpc_opcodes.h"

/**
   @addtogroup reqh
   @{
 */

/**
 *  Server side structures and objects
 */

enum {
	PORT = 10001
};

enum {
        MAX_RPCS_IN_FLIGHT = 32,
};

static struct c2_stob_domain        *sdom;
static struct c2_net_domain          cl_ndom;
static struct c2_net_domain          srv_ndom;
static struct c2_cob_domain          cl_cob_domain;
static struct c2_cob_domain_id       cl_cob_dom_id;
static struct c2_cob_domain          srv_cob_domain;
static struct c2_cob_domain_id       srv_cob_dom_id;
static struct c2_rpcmachine          srv_rpc_mach;
static struct c2_rpcmachine          cl_rpc_mach;
static struct c2_net_end_point      *cl_rep;
static struct c2_net_end_point      *srv_rep;
static struct c2_rpc_conn            cl_conn;
static struct c2_dbenv               cl_db;
static struct c2_dbenv               srv_db;
static struct c2_rpc_session         cl_rpc_session;
static struct c2_fol                 srv_fol;

/**
 * Global reqh object
 */
struct c2_reqh		reqh;

/**
 * Sends create fop request.
 */
static void create_send()
{
	struct c2_clink                  clink;
	struct c2_rpc_item              *item;
	struct c2_fop                   *fop;
	struct c2_stob_io_create        *rh_io_fop;
	c2_time_t                        timeout;
	uint32_t                         i;

	for (i = 0; i < 10; ++i) {
		fop = c2_fop_alloc(&c2_stob_io_create_fopt, NULL);
		rh_io_fop = c2_fop_data(fop);
		rh_io_fop->fic_object.f_seq = i;
		rh_io_fop->fic_object.f_oid = i;

		item = &fop->f_item;
		item->ri_deadline = 0;
		item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
		item->ri_group = NULL;
		item->ri_session = &cl_rpc_session;
		c2_time_set(&timeout, 60, 0);
		c2_clink_init(&clink, NULL);
		c2_clink_add(&item->ri_chan, &clink);
		timeout = c2_time_add(c2_time_now(), timeout);
		c2_rpc_post(item);
		c2_rpc_reply_timedwait(&clink, timeout);
		c2_clink_del(&clink);
		c2_clink_fini(&clink);
	}
}

/**
 * Sends read fop request.
 */
static void read_send()
{
	struct c2_clink                  clink;
	struct c2_rpc_item              *item;
	c2_time_t                        timeout;
	struct c2_fop                   *fop;
	struct c2_stob_io_read         *rh_io_fop;
	uint32_t                         i;

	for (i = 0; i < 10; ++i) {
		fop = c2_fop_alloc(&c2_stob_io_read_fopt, NULL);
		rh_io_fop = c2_fop_data(fop);
		rh_io_fop->fir_object.f_seq = i;
		rh_io_fop->fir_object.f_oid = i;

		item = &fop->f_item;
		item->ri_deadline = 0;
		item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
		item->ri_group = NULL;
		item->ri_type = &fop->f_type->ft_rpc_item_type;
		item->ri_session = &cl_rpc_session;
		c2_time_set(&timeout, 60, 0);
		c2_clink_init(&clink, NULL);
		c2_clink_add(&item->ri_chan, &clink);
		timeout = c2_time_add(c2_time_now(), timeout);
		c2_rpc_post(item);
		c2_rpc_reply_timedwait(&clink, timeout);
		c2_clink_del(&clink);
		c2_clink_fini(&clink);
	}
}

/**
 * Sends write fop request.
 */
static void write_send()
{
	struct c2_clink                  clink;
	struct c2_rpc_item              *item;
	struct c2_fop                   *fop;
	struct c2_stob_io_write         *rh_io_fop;
	c2_time_t                        timeout;
	uint32_t                         i;

	for (i = 0; i < 10; ++i) {
		fop = c2_fop_alloc(&c2_stob_io_write_fopt, NULL);
		rh_io_fop = c2_fop_data(fop);
		rh_io_fop->fiw_object.f_seq = i;
		rh_io_fop->fiw_object.f_oid = i;

		item = &fop->f_item;
		item->ri_deadline = 0;
		item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
		item->ri_group = NULL;
		item->ri_type = &fop->f_type->ft_rpc_item_type;
		item->ri_session = &cl_rpc_session;
		c2_time_set(&timeout, 60, 0);
		c2_clink_init(&clink, NULL);
		c2_clink_add(&item->ri_chan, &clink);
		timeout = c2_time_add(c2_time_now(), timeout);
		c2_rpc_post(item);
		c2_rpc_reply_timedwait(&clink, timeout);
		c2_clink_del(&clink);
		c2_clink_fini(&clink);
	}
}

/**
 * Helper structures and functions for ad stob.
 * These are used while performing a stob operation.
 */
struct reqh_ut_balloc {
	struct c2_mutex  rb_lock;
	c2_bindex_t      rb_next;
	struct ad_balloc rb_ballroom;
};

static struct reqh_ut_balloc *getballoc(struct ad_balloc *ballroom)
{
	return container_of(ballroom, struct reqh_ut_balloc, rb_ballroom);
}

static int reqh_ut_balloc_init(struct ad_balloc *ballroom, struct c2_dbenv *db,
                            uint32_t bshift)
{
	struct reqh_ut_balloc *rb = getballoc(ballroom);

	c2_mutex_init(&rb->rb_lock);
	return 0;
}

static void reqh_ut_balloc_fini(struct ad_balloc *ballroom)
{
	struct reqh_ut_balloc *rb = getballoc(ballroom);

	c2_mutex_fini(&rb->rb_lock);
}

static int reqh_ut_balloc_alloc(struct ad_balloc *ballroom, struct c2_dtx *tx,
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

static int reqh_ut_balloc_free(struct ad_balloc *ballroom, struct c2_dtx *tx,
                            struct c2_ext *ext)
{
	return 0;
}

static const struct ad_balloc_ops reqh_ut_balloc_ops = {
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

static int client_init(char *dbname)
{
        int                                rc;
        c2_time_t                          timeout;
	struct c2_net_transfer_mc         *cl_tm;

        /* Init client side network domain */
        rc = c2_net_domain_init(&cl_ndom, &c2_net_bulk_sunrpc_xprt);
	C2_UT_ASSERT(rc == 0);

        cl_cob_dom_id.id =  101 ;

        /* Init the db */
        rc = c2_dbenv_init(&cl_db, dbname, 0);
	C2_UT_ASSERT(rc == 0);

        /* Init the cob domain */
        rc = c2_cob_domain_init(&cl_cob_domain, &cl_db,
			&cl_cob_dom_id);
	C2_UT_ASSERT(rc == 0);

        /* Init the rpcmachine */
        rc = c2_rpcmachine_init(&cl_rpc_mach, &cl_cob_domain,
                        &cl_ndom, "127.0.0.1:21435:1", NULL);
	C2_UT_ASSERT(rc == 0);

        cl_tm = &cl_rpc_mach.cr_tm;
	C2_UT_ASSERT(cl_tm != NULL);

        /* Create destination endpoint for client i.e server endpoint */
        rc = c2_net_end_point_create(&cl_rep, cl_tm, "127.0.0.1:21435:2");
	C2_UT_ASSERT(rc == 0);

        /* Init the connection structure */
        rc = c2_rpc_conn_init(&cl_conn, cl_rep, &cl_rpc_mach,
			MAX_RPCS_IN_FLIGHT);
	C2_UT_ASSERT(rc == 0);

        /* Create RPC connection */
        rc = c2_rpc_conn_establish(&cl_conn);
	C2_UT_ASSERT(rc == 0);

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));

        C2_UT_ASSERT(c2_rpc_conn_timedwait(&cl_conn, C2_RPC_CONN_ACTIVE |
                                   C2_RPC_CONN_FAILED, timeout));
        /* Init session */
        rc = c2_rpc_session_init(&cl_rpc_session, &cl_conn, 5);
	C2_UT_ASSERT(rc == 0);

        /* Create RPC session */
        rc = c2_rpc_session_establish(&cl_rpc_session);
	C2_UT_ASSERT(rc == 0);

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));
        /* Wait for session to become active */
        C2_UT_ASSERT(c2_rpc_session_timedwait(&cl_rpc_session,
                        C2_RPC_SESSION_IDLE, timeout));

	/* send fops */
	create_send();
	write_send();
	read_send();

        rc = c2_rpc_session_terminate(&cl_rpc_session);
	C2_UT_ASSERT(rc == 0);

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));
        /* Wait for session to terminate */
        C2_UT_ASSERT(c2_rpc_session_timedwait(&cl_rpc_session,
                        C2_RPC_SESSION_TERMINATED | C2_RPC_SESSION_FAILED,
                        timeout));

        /* Terminate RPC connection */
        rc = c2_rpc_conn_terminate(&cl_conn);
	C2_UT_ASSERT(rc == 0);

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));

        C2_UT_ASSERT(c2_rpc_conn_timedwait(&cl_conn, C2_RPC_CONN_TERMINATED |
                                   C2_RPC_CONN_FAILED, timeout));
        c2_rpc_session_fini(&cl_rpc_session);
        c2_rpc_conn_fini(&cl_conn);

	return rc;
}

static void client_fini()
{
	/* Fini the remote net endpoint. */
        c2_net_end_point_put(cl_rep);

        /* Fini the rpcmachine */
        c2_rpcmachine_fini(&cl_rpc_mach);

        /* Fini the net domain */
        c2_net_domain_fini(&cl_ndom);

        /* Fini the cob domain */
        c2_cob_domain_fini(&cl_cob_domain);

        /* Fini the db */
        c2_dbenv_fini(&cl_db);
}

static int server_init(const char *stob_path, const char *srv_db_name,
			struct c2_stob_id *backid, struct c2_stob_domain **bdom,
			struct c2_stob **bstore, struct c2_stob **reqh_addb_stob,
			struct c2_stob_id *rh_addb_stob_id)
{
        int                        rc;
	struct c2_net_transfer_mc *srv_tm;

        /* Init Bulk sunrpc transport */
        rc = c2_net_xprt_init(&c2_net_bulk_sunrpc_xprt);
        C2_UT_ASSERT(rc == 0);

        /* Init server side network domain */
	rc = c2_net_domain_init(&srv_ndom, &c2_net_bulk_sunrpc_xprt);
	C2_UT_ASSERT(rc == 0);

        srv_cob_dom_id.id = 102;

        /* Init the db */
        rc = c2_dbenv_init(&srv_db, srv_db_name, 0);
	C2_UT_ASSERT(rc == 0);

	rc = c2_fol_init(&srv_fol, &srv_db);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Locate and create (if necessary) the backing store object.
	 */

	rc = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type,
							  stob_path, bdom);
	C2_UT_ASSERT(rc == 0);

	rc = (*bdom)->sd_ops->sdo_stob_find(*bdom, backid, bstore);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT((*bstore)->so_state == CSS_UNKNOWN);

	rc = c2_stob_create(*bstore, NULL);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT((*bstore)->so_state == CSS_EXISTS);

	/*
	 * Create AD domain over backing store object.
	 */
	rc = ad_stob_type.st_op->sto_domain_locate(&ad_stob_type, "", &sdom);
	C2_UT_ASSERT(rc == 0);

	rc = c2_ad_stob_setup(sdom, &srv_db, *bstore, &rb.rb_ballroom);
	C2_UT_ASSERT(rc == 0);

	c2_stob_put(*bstore);

	/* Create or open a stob into which to store the record. */
	rc = (*bdom)->sd_ops->sdo_stob_find(*bdom, rh_addb_stob_id, reqh_addb_stob);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT((*reqh_addb_stob)->so_state == CSS_UNKNOWN);

	rc = c2_stob_create(*reqh_addb_stob, NULL);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT((*reqh_addb_stob)->so_state == CSS_EXISTS);

	/* Write addb record into stob */
	c2_addb_choose_store_media(C2_ADDB_REC_STORE_STOB, c2_addb_stob_add,
					  *reqh_addb_stob, NULL);

        /* Init the cob domain */
        rc = c2_cob_domain_init(&srv_cob_domain, &srv_db,
                        &srv_cob_dom_id);
        C2_UT_ASSERT(rc == 0);

	/* Initialising request handler */
	rc =  c2_reqh_init(&reqh, NULL, sdom, &srv_db, &srv_cob_domain, &srv_fol);
	C2_UT_ASSERT(rc == 0);

        /* Init the rpcmachine */
        rc = c2_rpcmachine_init(&srv_rpc_mach, &srv_cob_domain,
                        &srv_ndom, "127.0.0.1:21435:2", &reqh);
        C2_UT_ASSERT(rc == 0);

        /* Find first c2_rpc_chan from the chan's list
           and use its corresponding tm to create target end_point */
        srv_tm = &srv_rpc_mach.cr_tm;
	C2_UT_ASSERT(srv_tm != NULL);

        /* Create destination endpoint for server i.e client endpoint */
        rc = c2_net_end_point_create(&srv_rep, srv_tm, "127.0.0.1:21435:1");
	C2_UT_ASSERT(rc == 0);

	return rc;
}

/* Fini the server */
static void server_fini(struct c2_stob_domain *bdom,
		struct c2_stob *reqh_addb_stob)
{
        /* Fini the net endpoint. */
        c2_net_end_point_put(srv_rep);

        /* Fini the rpcmachine */
        c2_rpcmachine_fini(&srv_rpc_mach);

        /* Fini the net domain */
        c2_net_domain_fini(&srv_ndom);

        /* Fini the transport */
        c2_net_xprt_fini(&c2_net_bulk_sunrpc_xprt);

        /* Fini the cob domain */
        c2_cob_domain_fini(&srv_cob_domain);

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
 * Test function for reqh ut
 */
void test_reqh(void)
{
	int                      result;
	char                     opath[64];
	char                     cl_dpath[64];
	char                     srv_dpath[64];
	const char                *path;
	struct c2_stob_domain	  *bdom;
	struct c2_stob_id	   backid;
	struct c2_stob		  *bstore;
	struct c2_stob		  *reqh_addb_stob;
	struct c2_stob_id          reqh_addb_stob_id = {
					.si_bits = {
						.u_hi = 1,
						.u_lo = 2
					}
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

	sprintf(srv_dpath, "%s/sdb", path);
	sprintf(cl_dpath, "%s/cdb", path);
	/* Create listening thread to accept async reply's */

	server_init(path, srv_dpath, &backid, &bdom, &bstore, &reqh_addb_stob,
			&reqh_addb_stob_id);

	client_init(cl_dpath);
	/* Clean up network connections */

	client_fini();
	server_fini(bdom, reqh_addb_stob);
	c2_stob_io_fop_fini();
	if (c2_processor_is_initialized())
		c2_processors_fini();
}

const struct c2_test_suite reqh_ut = {
	.ts_name = "reqh-ut...",
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
