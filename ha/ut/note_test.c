/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Atsuro Hoshino <atsuro_hoshino@xyratex.com>
 * Original creation date: 19-Sep-2013
 */

#include "conf/confc.h"
#include "conf/ut/file_helpers.h"
#include "fid/fid.h"
#include "fop/fom_generic.h"
#include "lib/buf.h"
#include "lib/errno.h"
#include "lib/string.h"
#include "lib/time.h"
#include "lib/uuid.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpclib.h"
#include "ut/cs_service.h"
#include "ut/ut.h"

#include "ha/note.c"
#include "ha/note_foms.h"
#include "ha/note_foms_internal.h"
#include "ha/note_fops.h"
#include "ha/note_fops_xc.h"
#include "ha/note_xc.h"

#define _QUOTE(s) #s
#define QUOTE(s) _QUOTE(s)

/* See "ha/ut/Makefile.sub" for M0_HA_UT_DIR */
#define M0_HA_UT_PATH(name)   QUOTE(M0_HA_UT_DIR) "/" name

#define CLIENT_DB_NAME        "ha_ut_client.db"
#define CLIENT_ENDPOINT_ADDR  "0@lo:12345:34:*"

#define SERVER_DB_NAME        "ha_ut_server.db"
#define SERVER_STOB_NAME      "ha_ut_server.stob"
#define SERVER_ADDB_STOB_NAME "linuxstob:ha_ut_server.addb_stob"
#define SERVER_LOG_NAME       "ha_ut_server.log"
#define SERVER_ENDPOINT_ADDR  "0@lo:12345:34:1"
#define SERVER_ENDPOINT       "lnet:" SERVER_ENDPOINT_ADDR

/* See "ha/ut/conf-str.txt" */
static const struct m0_fid conf_obj_id_fs = M0_FID_TINIT('f', 2, 1);

static struct m0_net_xprt    *xprt = &m0_net_lnet_xprt;
static struct m0_net_domain   client_net_dom;
static struct m0_rpc_session *session;

enum {
	CLIENT_COB_DOM_ID  = 16,
	SESSION_SLOTS      = 1,
	MAX_RPCS_IN_FLIGHT = 1,
};

static struct m0_rpc_client_ctx cctx = {
	.rcx_net_dom            = &client_net_dom,
	.rcx_local_addr         = CLIENT_ENDPOINT_ADDR,
	.rcx_remote_addr        = SERVER_ENDPOINT_ADDR,
	.rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
};

static char *server_argv[] = {
	"ha_ut", "-T", "AD", "-D", SERVER_DB_NAME,
	"-S", SERVER_STOB_NAME, "-A", SERVER_ADDB_STOB_NAME,
	"-e", SERVER_ENDPOINT, "-s", "ds1", "-s", "ds2", "-s", "addb", "-w",
	"10", "-s", "confd", "-c", M0_HA_UT_PATH("conf-str.txt")
};

static struct m0_rpc_server_ctx sctx = {
	.rsx_xprts            = &xprt,
	.rsx_xprts_nr         = 1,
	.rsx_argv             = server_argv,
	.rsx_argc             = ARRAY_SIZE(server_argv),
	.rsx_log_file_name    = SERVER_LOG_NAME,
};

extern struct m0_reqh_service_type m0_rpc_service_type;

static struct m0_mutex        chan_lock;
static struct m0_chan         chan;
static struct m0_clink        clink;

static struct m0_sm_group g_grp;

static struct {
	bool             run;
	struct m0_thread thread;
} g_ast;

/* ----------------------------------------------------------------
 * Dummy FOMs for unit test
 * ---------------------------------------------------------------- */

/*
 * Common FOM functions. These are implemented here because we are mocking
 * the HA side.
 *
 * Crucially, the GET FOP is only used in requesting status updates from HA.
 * As such, there does not exist an implementation of the relevant FOM inside
 * Mero, since Mero will never handle this message.
 *
 * The SET FOP is used in two ways. It is used by the HA subsystem to send
 * verified status change notification to Mero, and an implementation of the
 * FOM to manage this is included in Mero (see note_foms.c). However, it is also
 * used to send tentative status notification from a Mero instance to HA. This
 * test therefore includes a fom for handling the SET FOP on the HA side.
 */

static int ha_state_ut_fom_create(const struct m0_fom_ops *ops,
				  struct m0_fop *fop, struct m0_fom **m,
				  struct m0_reqh *reqh)
{
	struct m0_fom              *fom;
	struct m0_ha_state_set_fom *fom_obj;

	M0_ALLOC_PTR(fom_obj);
	M0_UT_ASSERT(fom_obj != NULL);
	fom = &fom_obj->fp_gen;
	m0_fom_init(fom, &fop->f_type->ft_fom_type, ops,
		    fop, NULL, reqh);
	fom_obj->fp_fop = fop;
	*m = fom;
	return 0;
}

/*
 * Get FOM ops for unit test
 */

static int ha_state_ut_fom_get_tick(struct m0_fom *fom)
{
	struct m0_ha_state_set_fom     *fom_obj;
	struct m0_fop                  *fop;
	struct m0_ha_state_fop_get     *fop_get;
	struct m0_ha_state_fop_get_rep *fop_get_rep;
	int                             i;

	fom_obj = container_of(fom, struct m0_ha_state_set_fom, fp_gen);
	fop = m0_fop_reply_alloc(fom->fo_fop, &m0_ha_state_get_rep_fopt);
	M0_UT_ASSERT(fop != NULL);

	fop_get = m0_fop_data(fom_obj->fp_fop);

	fop_get_rep                  = m0_fop_data(fop);
	fop_get_rep->hsgr_rc         = 0;
	fop_get_rep->hsgr_note.nv_nr = fop_get->hsg_ids.ni_nr;

	M0_ALLOC_ARR(fop_get_rep->hsgr_note.nv_note, fop_get->hsg_ids.ni_nr);
	M0_UT_ASSERT(fop_get_rep->hsgr_note.nv_note != NULL);
	for (i = 0; i < fop_get->hsg_ids.ni_nr; ++i) {
		fop_get_rep->hsgr_note.nv_note[i].no_state = M0_NC_ACTIVE;
	}

	m0_rpc_reply_post(m0_fop_to_rpc_item(fom_obj->fp_fop),
			  m0_fop_to_rpc_item(fop));
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	return M0_FSO_WAIT;
}

static const struct m0_fom_ops ha_state_ut_get_fom_ops = {
	.fo_tick          = ha_state_ut_fom_get_tick,
	.fo_fini          = m0_ha_state_set_fom_fini,
	.fo_home_locality = m0_ha_state_set_fom_home_locality,
	.fo_addb_init     = m0_ha_state_set_fom_addb_init
};

static int ha_state_ut_get_fom_create(struct m0_fop *fop, struct m0_fom **m,
				      struct m0_reqh *reqh)
{
	return ha_state_ut_fom_create(&ha_state_ut_get_fom_ops, fop, m, reqh);
}

const struct m0_fom_type_ops ha_state_ut_get_fom_type_ops = {
	.fto_create = ha_state_ut_get_fom_create
};

/*
 * Set FOM ops for unit test
 */

static int ha_state_ut_fom_set_tick(struct m0_fom *fom)
{
	struct m0_fop			*fop;
	struct m0_ha_nvec		*note;
	struct m0_fid                    u;
	struct m0_ha_state_set_fom	*fom_obj;

	fom_obj = container_of(fom, struct m0_ha_state_set_fom, fp_gen);
	fop = m0_fop_reply_alloc(fom->fo_fop, &m0_fop_generic_reply_fopt);
	M0_UT_ASSERT(fop != NULL);

	note = m0_fop_data(fom_obj->fp_fop);
	u = note->nv_note[0].no_id;

	M0_UT_ASSERT(note->nv_nr == 1);
	M0_UT_ASSERT(note->nv_note[0].no_state == M0_NC_ACTIVE);
	M0_UT_ASSERT(m0_fid_eq(&u,& conf_obj_id_fs));

	m0_rpc_reply_post(m0_fop_to_rpc_item(fom_obj->fp_fop),
			  m0_fop_to_rpc_item(fop));
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	return M0_FSO_WAIT;
}

static const struct m0_fom_ops ha_state_ut_set_fom_ops = {
	.fo_tick          = ha_state_ut_fom_set_tick,
	.fo_fini          = m0_ha_state_set_fom_fini,
	.fo_home_locality = m0_ha_state_set_fom_home_locality,
	.fo_addb_init     = m0_ha_state_set_fom_addb_init
};

static int ha_state_ut_set_fom_create(struct m0_fop *fop, struct m0_fom **m,
				      struct m0_reqh *reqh)
{
	return ha_state_ut_fom_create(&ha_state_ut_set_fom_ops, fop, m, reqh);
}

const struct m0_fom_type_ops ha_state_ut_set_fom_type_ops = {
	.fto_create = ha_state_ut_set_fom_create
};

/*
 * Init and fini
 */

static int ha_state_ut_fop_init(void)
{
	m0_xc_note_init();
	m0_xc_note_fops_init();

	M0_FOP_TYPE_INIT(&m0_ha_state_get_fopt,
			 .name      = "HA State Get",
			 .opcode    = M0_HA_NOTE_GET_OPCODE,
			 .xt        = m0_ha_state_fop_get_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			 .fom_ops   = &ha_state_ut_get_fom_type_ops,
			 .sm        = &m0_generic_conf,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_ha_state_get_rep_fopt,
			 .name      = "HA State Get Reply",
			 .opcode    = M0_HA_NOTE_GET_REP_OPCODE,
			 .xt        = m0_ha_state_fop_get_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_ha_state_set_fopt,
			 .name      = "HA State Set",
			 .opcode    = M0_HA_NOTE_SET_OPCODE,
			 .xt        = m0_ha_nvec_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			 .fom_ops   = &ha_state_ut_set_fom_type_ops,
			 .sm        = &m0_generic_conf,
			 .svc_type  = &m0_rpc_service_type);
	return 0;
}

static void ha_state_ut_fop_fini(void)
{
	m0_fop_type_fini(&m0_ha_state_get_fopt);
	m0_fop_type_fini(&m0_ha_state_get_rep_fopt);
	m0_fop_type_fini(&m0_ha_state_set_fopt);
	m0_xc_note_fini();
	m0_xc_note_fops_fini();
}

/* ----------------------------------------------------------------
 * Auxiliary functions
 * ---------------------------------------------------------------- */

static void start_rpc_client_and_server(void)
{
	int rc;

	rc = m0_net_domain_init(&client_net_dom, xprt);
	M0_ASSERT(rc == 0);
	rc = m0_rpc_server_start(&sctx);
	M0_ASSERT(rc == 0);
	rc = m0_rpc_client_start(&cctx);
	M0_ASSERT(rc == 0);
}

static void stop_rpc_client_and_server(void)
{
	int rc;

	rc = m0_rpc_client_stop(&cctx);
	M0_ASSERT(rc == 0);
	m0_rpc_server_stop(&sctx);
	m0_net_domain_fini(&client_net_dom);
}

static void ast_thread(int _ M0_UNUSED)
{
	while (g_ast.run) {
		m0_chan_wait(&g_grp.s_clink);
		m0_sm_group_lock(&g_grp);
		m0_sm_asts_run(&g_grp);
		m0_sm_group_unlock(&g_grp);
	}
}

static int ast_thread_init(void)
{
	m0_sm_group_init(&g_grp);
	g_ast.run = true;
	return M0_THREAD_INIT(&g_ast.thread, int, NULL, &ast_thread, 0,
			      "ast_thread");
}

static void ast_thread_fini(void)
{
	g_ast.run = false;
	m0_clink_signal(&g_grp.s_clink);
	m0_thread_join(&g_ast.thread);
	m0_thread_fini(&g_ast.thread);
	m0_sm_group_fini(&g_grp);
}

static void done_get_chan_init(void)
{
	m0_mutex_init(&chan_lock);
	m0_chan_init(&chan, &chan_lock);
	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&chan, &clink);
}

static void done_get_chan_fini(void)
{
	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
	m0_chan_fini_lock(&chan);
	m0_mutex_fini(&chan_lock);
}

static void local_confc_init(struct m0_confc *confc)
{
	int             rc;
	char            local_conf[4096];
	struct m0_fid   fid0 = M0_FID_TINIT('p', 2, 0);

	rc = m0_ut_file_read(M0_HA_UT_PATH("conf-str.txt"), local_conf,
			     sizeof local_conf);
	M0_UT_ASSERT(rc == 0);

	/*
	 * All configuration object need to be preloaded, since
	 * m0_ha_state_accept() function traverses all the descendants appearing
	 * in preloaded data. When missing configuration object were found,
	 * unit test for m0_ha_state_accept() will likely to show an error
	 * similar to below:
	 *
	 *  FATAL : [lib/assert.c:43:m0_panic] panic:
	 *    obj->co_status == M0_CS_READY m0_conf_obj_put()
	 */
	rc = m0_confc_init(confc, &g_grp, &fid0,
			   SERVER_ENDPOINT_ADDR, &(cctx.rcx_rpc_machine),
			   local_conf);
	M0_UT_ASSERT(rc == 0);
}

static void local_confc_fini(struct m0_confc *confc)
{
	m0_confc_fini(confc);
}

static void compare_ha_state(struct m0_confc confc,
			     enum m0_ha_obj_state state)
{
	struct m0_conf_obj *dir;
	struct m0_conf_obj *svc;
	struct m0_conf_obj *node;
	int                 rc;

	rc = m0_confc_open_sync(&dir, confc.cc_root,
				M0_CONF_PROFILE_FILESYSTEM_FID,
				M0_CONF_FILESYSTEM_SERVICES_FID);
	M0_UT_ASSERT(rc == 0);

	for (svc = NULL; (rc = m0_confc_readdir_sync(dir, &svc)) > 0; ) {
		M0_UT_ASSERT(svc->co_ha_state == state);
		rc = m0_confc_open_sync(&node, svc, M0_CONF_SERVICE_NODE_FID);
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(node->co_ha_state == state);
		m0_confc_close(node);
	}
	m0_confc_close(svc);
	m0_confc_close(dir);
}

/* ----------------------------------------------------------------
 * Unit tests
 * ---------------------------------------------------------------- */

static void test_ha_state_get(void)
{
	struct m0_ha_note n1 = { conf_obj_id_fs, M0_NC_UNKNOWN };
	struct m0_ha_nvec nvec = { 1, &n1 };
	int rc;

	rc = m0_ha_state_get(session, &nvec, &chan);
	M0_UT_ASSERT(rc == 0);

	m0_chan_wait(&clink);
	M0_UT_ASSERT(n1.no_state == M0_NC_ACTIVE);
}

static void test_ha_state_set(void)
{
	struct m0_fid     u = conf_obj_id_fs;
	struct m0_ha_note n1;
	struct m0_ha_nvec nvec;

	n1.no_id    = u;
	n1.no_state = M0_NC_ACTIVE;

	nvec.nv_nr   = 1;
	nvec.nv_note = &n1;

	m0_ha_state_set(session, &nvec);
}

static void test_ha_state_accept(void)
{
	struct m0_confc confc;
	struct m0_fid u[] = {
		M0_FID_TINIT('s',2,2),
		M0_FID_TINIT('s',2,3),
		M0_FID_TINIT('s',2,4),
		M0_FID_TINIT('n',2,5),
		M0_FID_TINIT('n',2,9)
	};
	struct m0_ha_note *n;

	M0_ALLOC_ARR(n, ARRAY_SIZE(u));
	struct m0_ha_nvec  nvec;
	int                i;

	local_confc_init(&confc);

	nvec.nv_nr   = ARRAY_SIZE(u);
	nvec.nv_note = n;

	/* To initialize */
	for (i = 0; i < ARRAY_SIZE(u); ++i) {
		n[i].no_id    = u[i];
		n[i].no_state = M0_NC_ACTIVE;
	}
	m0_ha_state_accept(&confc, &nvec);
	compare_ha_state(confc, M0_NC_ACTIVE);

	/* To check updates */
	for (i = 0; i < ARRAY_SIZE(u); ++i) {
		n[i].no_state = M0_NC_OFFLINE;
	}
	m0_ha_state_accept(&confc, &nvec);
	compare_ha_state(confc, M0_NC_OFFLINE);

	local_confc_fini(&confc);
	m0_free(n);
}

/* -------------------------------------------------------------------
 * Test suite
 * ------------------------------------------------------------------- */

static int ha_state_ut_init(void)
{
	int rc;

	rc = ha_state_ut_fop_init();
	M0_ASSERT(rc == 0);

	rc = ast_thread_init();
	M0_ASSERT(rc == 0);

	done_get_chan_init();
	start_rpc_client_and_server();
	session = &cctx.rcx_session;

	return 0;
}

static int ha_state_ut_fini(void)
{
	stop_rpc_client_and_server();
	done_get_chan_fini();
	ast_thread_fini();
	ha_state_ut_fop_fini();
	return 0;
}

struct m0_ut_suite ha_state_ut = {
	.ts_name = "ha_state-ut",
	.ts_init = ha_state_ut_init,
	.ts_fini = ha_state_ut_fini,
	.ts_tests = {
		{ "ha_state_get", test_ha_state_get },
		{ "ha_state_set", test_ha_state_set },
		{ "ha_state_accept", test_ha_state_accept },
		{ NULL, NULL }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
