/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Facundo Domínguez <facundo.d.laumann@seagate.com>
 * Original creation date: 11-May-2015
 */

/*
 * dummyha offers some basic interactions with mero for the purpose
 * of integration tests.
 */

#include <string.h>  /* strerror */
#include <stdio.h>
#include <unistd.h>  /* read */

#include "ha/note.h"
#include "ha/note_fops.h"
#include "ha/note_fops_xc.h"
#include "lib/getopts.h"
#include "mero/init.h"
#include "module/instance.h"      /* m0 */
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpclib.h"
#include "rpc/service.h"
#include "ut/cs_service.h"      /* m0_cs_default_stypes */
#include "ha/epoch.h"

/* ----------------------------------------------------------------
 * Dummy FOMs for the notification interface fops
 * ---------------------------------------------------------------- */

static int ha_state_ut_fom_create(const struct m0_fom_ops *ops,
				  struct m0_fop *fop, struct m0_fom **m,
				  struct m0_reqh *reqh)
{
	M0_ASSERT(m != NULL);
	M0_ALLOC_PTR(*m);
	M0_ASSERT(*m != NULL);
	m0_fom_init(*m, &fop->f_type->ft_fom_type, ops, fop, NULL, reqh);
	return 0;
}

static void ha_state_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
	m0_free(fom);
}

static  size_t ha_state_fom_home_locality(const struct m0_fom *fom)
{
	return m0_fop_opcode(fom->fo_fop);
}

/*
 * Get FOM ops
 */

static int ha_state_ut_fom_get_tick(struct m0_fom *fom)
{
	struct m0_fop          *reply_fop;
	struct m0_ha_nvec      *req;
	struct m0_ha_state_fop *reply;
	int                     i;

	reply_fop = m0_fop_reply_alloc(fom->fo_fop, &m0_ha_state_get_rep_fopt);
	M0_ASSERT(reply_fop != NULL);

	req = m0_fop_data(fom->fo_fop);

	reply                = m0_fop_data(reply_fop);
	reply->hs_rc         = 0;
	reply->hs_note.nv_nr = req->nv_nr;

	M0_ALLOC_ARR(reply->hs_note.nv_note, req->nv_nr);
	M0_ASSERT(reply->hs_note.nv_note != NULL);
	for (i = 0; i < req->nv_nr; ++i) {
		reply->hs_note.nv_note[i] = (struct m0_ha_note) {
			.no_id    = req->nv_note[i].no_id,
			.no_state = M0_NC_ONLINE
		};
	}

	m0_rpc_reply_post(m0_fop_to_rpc_item(fom->fo_fop),
			  m0_fop_to_rpc_item(reply_fop));
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	return M0_FSO_WAIT;
}

static const struct m0_fom_ops ha_state_ut_get_fom_ops = {
	.fo_tick          = &ha_state_ut_fom_get_tick,
	.fo_fini          = &ha_state_fom_fini,
	.fo_home_locality = &ha_state_fom_home_locality,
};

static int ha_state_ut_get_fom_create(struct m0_fop *fop, struct m0_fom **m,
				      struct m0_reqh *reqh)
{
	return ha_state_ut_fom_create(&ha_state_ut_get_fom_ops, fop, m, reqh);
}

const struct m0_fom_type_ops ha_state_ut_get_fom_type_ops = {
	.fto_create = &ha_state_ut_get_fom_create
};

/*
 * Set FOM ops
 */

static int ha_state_ut_fom_set_tick(struct m0_fom *fom)
{
	struct m0_fop     *fop;

	fop = m0_fop_reply_alloc(fom->fo_fop, &m0_fop_generic_reply_fopt);
	M0_ASSERT(fop != NULL);

	printf("HA: Received notification from mero.\n");

	m0_rpc_reply_post(m0_fop_to_rpc_item(fom->fo_fop),
			  m0_fop_to_rpc_item(fop));
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	return M0_FSO_WAIT;
}

static const struct m0_fom_ops ha_state_ut_set_fom_ops = {
	.fo_tick          = ha_state_ut_fom_set_tick,
	.fo_fini          = ha_state_fom_fini,
	.fo_home_locality = ha_state_fom_home_locality,
};

static int ha_state_ut_set_fom_create(struct m0_fop *fop, struct m0_fom **m,
				      struct m0_reqh *reqh)
{
	return ha_state_ut_fom_create(&ha_state_ut_set_fom_ops, fop, m, reqh);
}

const struct m0_fom_type_ops ha_state_ut_set_fom_type_ops = {
	.fto_create = ha_state_ut_set_fom_create
};

static struct m0                instance;

/* RPC server initialization */

static void quit_dialog(void)
{
	int  rc __attribute__((unused));
	char ch;

	printf("Press Enter to terminate\n");
	rc = read(0, &ch, sizeof ch);
}


#define SERVER_DB_FILE_NAME        "dummyha_server.db"
#define SERVER_STOB_FILE_NAME      "dummyha_server.stob"
#define SERVER_ADDB_STOB_FILE_NAME "linuxstob:dummyha_server_addb.stob"
#define SERVER_LOG_FILE_NAME       "dummyha_server.log"

static struct m0_net_domain client_net_dom;
static struct m0_ha_domain client_ha_dom;

static int run_server(const char *local_endpoint)
{
	int rc;
	static struct m0_fid process_fid = M0_FID_TINIT('r', 0, 1);
	const uint32_t tms_nr   = 1;
	const uint32_t bufs_nr  =
		m0_rpc_bufs_nr(M0_NET_TM_RECV_QUEUE_DEF_LEN, tms_nr);
	struct m0_net_buffer_pool buffer_pool;
	struct m0_rpc_machine rpc_machine;
	struct m0_reqh *reqh;

	rc = m0_net_domain_init(&client_net_dom, &m0_net_lnet_xprt);
	if (rc != 0)
		goto pool_fini;

	m0_ha_domain_init(&client_ha_dom, 0);

	rc = m0_rpc_net_buffer_pool_setup( &client_net_dom, &buffer_pool
		                         , bufs_nr, tms_nr);
	if (rc != 0)
		goto pool_fini;

	M0_ALLOC_PTR(reqh);
	M0_ASSERT(reqh != NULL);
	rc = M0_REQH_INIT(reqh,
			  .rhia_dtm          = (void*)1,
			  .rhia_mdstore      = (void*)1,
			  .rhia_fid          = &process_fid);
	if (rc != 0)
		goto pool_fini;
	m0_reqh_start(reqh);
	rc = m0_rpc_machine_init(&rpc_machine,
				&client_net_dom,
				local_endpoint, reqh,
				&buffer_pool,
				M0_BUFFER_ANY_COLOUR,
				1 << 17, // MAX_MSG_SIZE,
				2 //QUEUE_LEN
				);
	if (rc != 0)
		goto pool_fini;

	quit_dialog();

	m0_rpc_machine_fini(&rpc_machine);
	m0_reqh_services_terminate(reqh);
	m0_reqh_fini(reqh);
	free(reqh);
	m0_rpc_net_buffer_pool_cleanup(&buffer_pool);

	return 0;

pool_fini:
	m0_rpc_net_buffer_pool_cleanup(&buffer_pool);
	return rc;
}

int main(int argc, char *argv[])
{
	int rc;
	const char *local_endpoint;

	m0_ha_state_get_fom_type_ops = &ha_state_ut_get_fom_type_ops;
	m0_ha_state_set_fom_type_ops = &ha_state_ut_set_fom_type_ops;

	rc = m0_init(&instance);
	if (rc != 0) {
		fprintf(stderr, "m0_init: %i %s\n", -rc, strerror(-rc));
		goto end;
	}

	rc = M0_GETOPTS(basename(argv[0]), argc, argv,
		    M0_HELPARG('h'),
		    M0_STRINGARG('l', "local rpc address",
				LAMBDA(void, (const char *str) {
					local_endpoint = str;
				})),
		    );
	if (local_endpoint == NULL) {
		fprintf(stderr, "Missing -l argument.\n");
		rc = -1;
		goto fini;
	}

	rc = run_server(local_endpoint);

fini:
	m0_fini();
end:
	return rc < 0 ? -rc : rc;
}
