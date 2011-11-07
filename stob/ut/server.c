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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/21/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <unistd.h>    /* sleep */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/getopts.h"
#include "lib/arith.h"  /* min64u */
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "net/net.h"
#include "net/usunrpc/usunrpc.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "stob/ad.h"
#include "colibri/init.h"
#include "rpc/rpccore.h"
#include "rpc/rpclib.h"
#include "dtm/dtm.h"

#include "stob/io_fop.h"
#include "stob/io_fop_u.h"
#include "ioservice/io_fops.h"

#include "lib/processor.h"
#include "reqh/reqh.h"

/**
   @addtogroup stob
   @{
 */

#define SERVER_ENDPOINT_ADDR	"127.0.0.1:12346:1"
#define CLIENT_ENDPOINT_ADDR	"127.0.0.1:12347:1"
#define SERVER_DB_NAME		"stob_ut_server"

enum {
	SERVER_COB_DOM_ID	= 19,
	SESSION_SLOTS		= 1,
	MAX_RPCS_IN_FLIGHT	= 1,
	CONNECT_TIMEOUT		= 5,
};

extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;

static struct c2_addb_ctx server_addb_ctx;

static const struct c2_addb_loc server_addb_loc = {
	.al_name = "server"
};

const struct c2_addb_ctx_type server_addb_ctx_type = {
	.act_name = "t1-server",
};

#define SERVER_ADDB_ADD(name, rc)                                       \
C2_ADDB_ADD(&server_addb_ctx, &server_addb_loc, c2_addb_func_fail, (name), (rc))

static bool                   stop = false;
static struct c2_stob_domain *dom;
static struct c2_fol          fol;
static struct c2_reqh	      reqh;

extern struct c2_fop_type c2_addb_record_fopt; /* opcode = 14 */

struct mock_balloc {
	struct c2_mutex  mb_lock;
	c2_bindex_t      mb_next;
	struct ad_balloc mb_ballroom;
};

static struct mock_balloc *b2mock(struct ad_balloc *ballroom)
{
	return container_of(ballroom, struct mock_balloc, mb_ballroom);
}

static int mock_balloc_init(struct ad_balloc *ballroom, struct c2_dbenv *db,
			    uint32_t bshift)
{
	struct mock_balloc *mb = b2mock(ballroom);

	c2_mutex_init(&mb->mb_lock);
	return 0;
}

static void mock_balloc_fini(struct ad_balloc *ballroom)
{
	struct mock_balloc *mb = b2mock(ballroom);

	c2_mutex_fini(&mb->mb_lock);
}

static int mock_balloc_alloc(struct ad_balloc *ballroom, struct c2_dtx *tx,
			     c2_bcount_t count, struct c2_ext *out)
{
	struct mock_balloc *mb = b2mock(ballroom);
	c2_bcount_t giveout;

	c2_mutex_lock(&mb->mb_lock);
	giveout = min64u(count, 500000);
	out->e_start = mb->mb_next;
	out->e_end   = mb->mb_next + giveout;
	mb->mb_next += giveout + 1;
	/*
	printf("allocated %8lx/%8lx bytes: [%8lx .. %8lx)\n", giveout, count,
	       out->e_start, out->e_end); */
	c2_mutex_unlock(&mb->mb_lock);
	return 0;
}

static int mock_balloc_free(struct ad_balloc *ballroom, struct c2_dtx *tx,
			    struct c2_ext *ext)
{
	printf("freed     %8lx bytes: [%8lx .. %8lx)\n", c2_ext_length(ext),
	       ext->e_start, ext->e_end);
	return 0;
}

static const struct ad_balloc_ops mock_balloc_ops = {
	.bo_init  = mock_balloc_init,
	.bo_fini  = mock_balloc_fini,
	.bo_alloc = mock_balloc_alloc,
	.bo_free  = mock_balloc_free,
};

static struct mock_balloc mb = {
	.mb_next = 0,
	.mb_ballroom = {
		.ab_ops = &mock_balloc_ops
	}
};

static const struct c2_table_ops c2_addb_record_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof (uint64_t) },
		[TO_REC] = { .max_size = 4096 }
	},
	.key_cmp = NULL
};

extern c2_bindex_t addb_stob_offset;
extern uint64_t c2_addb_db_seq;

/**
   Simple server for unit-test purposes.

   Synopsis:

       server [options]

   "path" is a path to a directory that the server will create if necessary that
   would contain objects (a path to a storage object domain, c2_stob_domain,
   technically).

   "ip_addr:port:id" is an address of the RPC service created by this server.

   Server supports create, read and write commands.
 */
int main(int argc, char **argv)
{
	int         rc;
	const char  *path;
	const char  *addr = NULL;
	char        opath[64];
	char        dpath[64];
	int         i = 0;

	struct c2_stob_domain   *bdom;
	struct c2_stob_id       backid;
	struct c2_stob          *bstore;
	struct c2_net_domain    net_dom = { };
	struct c2_dbenv         db;
	struct c2_stob	        *addb_stob;
	struct c2_table	        addb_table;
	struct c2_net_xprt      *xprt = &c2_net_bulk_sunrpc_xprt;

	struct c2_stob_id       addb_stob_id = {
		.si_bits = {
			.u_hi = 0xADDBADDBADDBADDB,
			.u_lo = 0x210B210B210B210B
		}
	};

	struct c2_rpc_ctx       server_rctx = {
		.rx_net_dom            = &net_dom,
		.rx_reqh               = &reqh,
		.rx_local_addr         = SERVER_ENDPOINT_ADDR,
		.rx_remote_addr        = CLIENT_ENDPOINT_ADDR,
		.rx_db_name            = SERVER_DB_NAME,
		.rx_cob_dom_id         = SERVER_COB_DOM_ID,
		.rx_nr_slots           = SESSION_SLOTS,
		.rx_timeout_s          = CONNECT_TIMEOUT,
		.rx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
	};

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	backid.si_bits.u_hi = 0x8;
	backid.si_bits.u_lo = 0xf00baf11e;
	path = "__s";

	rc = C2_GETOPTS("server", argc, argv,
			    C2_VOIDARG('T', "parse trace log produced earlier",
				       LAMBDA(void, (void) {
					       c2_trace_parse();
					       exit(0);
					       })),
			    C2_STRINGARG('d', "path to object store",
				       LAMBDA(void, (const char *string) {
					       path = string; })),
			    C2_FORMATARG('o', "back store object id", "%lu",
					 &backid.si_bits.u_lo),
			    C2_STRINGARG('a', "rpc service addr",
				       LAMBDA(void, (const char *string) {
					       addr = string; })),
			    );
	if (rc != 0)
		return rc;

	printf("path=%s, back store object=%llx.%llx, service=%s\n",
		path, (unsigned long long)backid.si_bits.u_hi,
		(unsigned long long)backid.si_bits.u_lo, addr);

	if (addr != NULL)
		server_rctx.rx_local_addr = addr;

	c2_addb_ctx_init(&server_addb_ctx, &server_addb_ctx_type,
			 &c2_addb_global_ctx);

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = c2_processors_init();
	C2_ASSERT(rc == 0);

	rc = c2_stob_io_fop_init();
	C2_ASSERT(rc == 0);

	C2_ASSERT(strlen(path) < ARRAY_SIZE(opath) - 8);

	rc = mkdir(path, 0700);
	C2_ASSERT(rc == 0 || (rc == -1 && errno == EEXIST));
	sprintf(opath, "%s/o", path);
	rc = mkdir(opath, 0700);
	C2_ASSERT(rc == 0 || (rc == -1 && errno == EEXIST));

	sprintf(dpath, "%s/d", path);

	/*
	 * Initialize the data-base and fol.
	 */
	rc = c2_dbenv_init(&db, dpath, 0);
	C2_ASSERT(rc == 0);

	rc = c2_fol_init(&fol, &db);
	C2_ASSERT(rc == 0);

	/*
	 * Locate and create (if necessary) the backing store object.
	 */

	rc = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type,
							  path, &bdom);
	C2_ASSERT(rc == 0);

	rc = bdom->sd_ops->sdo_stob_find(bdom, &backid, &bstore);
	C2_ASSERT(rc == 0);
	C2_ASSERT(bstore->so_state == CSS_UNKNOWN);

	rc = c2_stob_create(bstore, NULL);
	C2_ASSERT(rc == 0);
	C2_ASSERT(bstore->so_state == CSS_EXISTS);

	/*
	 * Create AD domain over backing store object.
	 */
	rc = ad_stob_type.st_op->sto_domain_locate(&ad_stob_type, "", &dom);
	C2_ASSERT(rc == 0);

	rc = c2_ad_stob_setup(dom, &db, bstore, &mb.mb_ballroom);
	C2_ASSERT(rc == 0);

	c2_stob_put(bstore);

	/* create or open a stob into which to store the record. */
	rc = bdom->sd_ops->sdo_stob_find(bdom, &addb_stob_id, &addb_stob);
	C2_ASSERT(rc == 0);
	C2_ASSERT(addb_stob->so_state == CSS_UNKNOWN);

	rc = c2_stob_create(addb_stob, NULL);
	C2_ASSERT(rc == 0);
	C2_ASSERT(addb_stob->so_state == CSS_EXISTS);
	/* XXX The stob tail postion should be maintained & initialized */

	/* write addb record into stob */
	/*
	 * TODO
	 * Init the stob appending file offset position.
	 * This should be stored somewhere transactionally.
	 */
	/*
	addb_stob_offset = 0;
	c2_addb_choose_store_media(C2_ADDB_REC_STORE_STOB, c2_addb_stob_add,
				   addb_stob, NULL);
	*/

	rc = c2_table_init(&addb_table, &db,
			       "addb_record", 0,
			       &c2_addb_record_ops);
	C2_ASSERT(rc == 0);
	/*
	 * TODO
	 * The db addb seqno should be loaded and initialized.
	 */

	c2_addb_db_seq = 0;
	c2_addb_choose_store_media(C2_ADDB_REC_STORE_DB, c2_addb_db_add,
				   &addb_table, &db);
	/*
	 * Set up the service.
	 */
	rc = c2_net_xprt_init(xprt);
	C2_ASSERT(rc == 0);

	rc = c2_net_domain_init(&net_dom, xprt);
	C2_ASSERT(rc == 0);

	rc = c2_reqh_init(&reqh, NULL, dom, &db, NULL, &fol);
	C2_ASSERT(rc == 0);

	rc = c2_rpc_server_init(&server_rctx);
	C2_ASSERT(rc == 0);

	while (!stop) {
		sleep(1);
                //printf("allocated: %li\n", c2_allocated());
                if (i++ % 5 == 0)
                        printf("busy: in=%5.2f out=%5.2f\n",
                               (float)c2_net_domain_stats_get(&net_dom, NS_STATS_IN) / 100,
                               (float)c2_net_domain_stats_get(&net_dom, NS_STATS_OUT) / 100);
        }

	c2_rpc_server_fini(&server_rctx);

	c2_reqh_fini(&reqh);
	c2_net_domain_fini(&net_dom);
	c2_net_xprt_fini(xprt);

	/*
	 * TODO
	 * the stob file offset or db seqno should be updated and
	 * stored somewhere, e.g. in global configuration db.
	 */
	c2_addb_choose_store_media(C2_ADDB_REC_STORE_NONE);

	c2_table_fini(&addb_table);

	c2_stob_put(addb_stob);

	dom->sd_ops->sdo_fini(dom);
	bdom->sd_ops->sdo_fini(bdom);
	c2_stob_io_fop_fini();
	c2_fol_fini(&fol);
	c2_dbenv_fini(&db);
	c2_processors_fini();
	c2_fini();
	c2_addb_ctx_fini(&server_addb_ctx);

	return 0;
}

/** @} end group stob */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
