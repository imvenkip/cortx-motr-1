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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 04-Mar-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"

#include "lib/memory.h"       /* M0_ALLOC_PTR, m0_free */
#include "lib/errno.h"
#include "lib/buf.h"          /* m0_buf_init */
#include "lib/finject.h"      /* M0_FI_ENABLED */
#include "lib/mutex.h"        /* m0_mutex_lock, m0_mutex_unlock */
#include "conf/helpers.h"     /* service_name_dup */
#include "conf/obj.h"
#include "conf/obj_ops.h"     /* M0_CONF_DIRNEXT, m0_conf_obj_get */
#include "conf/confc.h"
#include "conf/dir_iter.h"
#include "fop/fop.h"
#include "rpc/rpc_machine.h"
#include "rpc/link.h"
#include "rpc/rpclib.h"       /* m0_rpc_post_sync */
#include "sss/ss_fops.h"
#include "sss/process_fops.h"
#include "spiel/spiel.h"

#define SPIEL_CONF_OBJ_FIND(confc, profile, fid, conf_obj, filter, ...) \
	_spiel_conf_obj_find(confc, profile, fid, filter,               \
			     M0_COUNT_PARAMS(__VA_ARGS__) + 1,          \
			     (const struct m0_fid []){                  \
			     __VA_ARGS__, M0_FID0 },                    \
			     conf_obj)

enum {
	SPIEL_MAX_RPCS_IN_FLIGHT = 1,
	SPIEL_CONN_TIMEOUT       = 5, /* seconds */
};

static int spiel_send_cmd(struct m0_rpc_machine *rmachine,
			  const char            *remote_ep,
			  struct m0_fop         *cmd_fop)
{
	struct m0_rpc_link     *rlink;
	int                     rc;

	M0_ENTRY();

	M0_PRE(rmachine != NULL);
	M0_PRE(remote_ep != NULL);
	M0_PRE(cmd_fop != NULL);

	/* RPC link structure is too big to allocate it on stack */
	M0_ALLOC_PTR(rlink);
	if (rlink == NULL)
		return M0_ERR(-ENOMEM);

	rc = m0_rpc_link_init(rlink, rmachine, remote_ep,
			SPIEL_CONN_TIMEOUT, SPIEL_MAX_RPCS_IN_FLIGHT);
	if (rc == 0) {
		rc = m0_rpc_link_connect_sync(rlink) ?:
		     m0_rpc_post_sync(cmd_fop,
				      &rlink->rlk_sess,
				      NULL,
				      M0_TIME_IMMEDIATELY);

		/* disconnect should be called even if connect failed */
		m0_rpc_link_disconnect_sync(rlink);
		m0_rpc_link_fini(rlink);
	}

	m0_free(rlink);
	return M0_RC(rc);
}

/**
 * @brief Find object with given obj_fid in provided confc.
 *
 * Function uses @ref m0_conf_diter internally and traverses tree starting from
 * filesystem object.
 *
 * Should not be called directly, macro @ref SPIEL_CONF_OBJ_FIND
 * should be used instead.
 *
 * User should close returned non-NULL conf_obj using @ref m0_confc_close()
 *
 * @param confc     confc instance
 * @param profile   configuration profile
 * @param obj_fid   object FID to search for
 * @param filter    function to filter nodes during tree traversal.
 *                  Identical to filter argument in @ref m0_conf_diter_next_sync
 * @param nr_lvls   Number of directory levels to traverse
 *                  (@ref m0_conf__diter_init)
 * @param path      Path in configuration tree to traverse. Path should be
 *                  started from filesystem object
 * @param conf_obj  output value. Found object or NULL if object is not found
 */
static int _spiel_conf_obj_find(struct m0_confc       *confc,
				const struct m0_fid   *profile,
				const struct m0_fid   *obj_fid,
				bool (*filter)(const struct m0_conf_obj *obj),
				uint32_t               nr_lvls,
				const struct m0_fid   *path,
				struct m0_conf_obj   **conf_obj)
{
	int                  rc;
	struct m0_conf_obj  *fs_obj = NULL;
	struct m0_conf_obj  *tmp;
	struct m0_conf_diter it;

	M0_ENTRY();

	M0_PRE(confc != NULL);
	M0_PRE(obj_fid != NULL);
	M0_PRE(path != NULL);
	M0_PRE(nr_lvls > 0);
	M0_PRE(m0_fid_eq(&path[0], &M0_CONF_FILESYSTEM_NODES_FID));
	M0_PRE(conf_obj != NULL);

	*conf_obj = NULL;

	/* m0_conf_diter_init() doesn't accept profile as origin,
	 * so let's start with fs */
	rc = m0_confc_open_sync(&fs_obj, confc->cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				*profile,
				M0_CONF_PROFILE_FILESYSTEM_FID);

	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_conf__diter_init(&it, confc, fs_obj, nr_lvls, path);

	if (rc != 0) {
		m0_confc_close(fs_obj);
		return M0_ERR(rc);
	}

	while ((rc = m0_conf_diter_next_sync(&it, filter)) == M0_CONF_DIRNEXT) {
		tmp = m0_conf_diter_result(&it);

		if (m0_fid_eq(&tmp->co_id, obj_fid)) {
			*conf_obj = tmp;
			/* Pin object to protect it from removal */
			m0_mutex_lock(&confc->cc_lock);
			m0_conf_obj_get(*conf_obj);
			m0_mutex_unlock(&confc->cc_lock);
			rc = 0;
			break;
		}
	}

	m0_conf_diter_fini(&it);
	m0_confc_close(fs_obj);

	if (*conf_obj == NULL && rc == 0)
		rc = M0_ERR(-ENOENT);

	M0_POST(ergo(rc == 0, (*conf_obj)->co_nrefs > 0));

	return M0_RC(rc);
}

static int spiel_ss_ep_lookup(struct m0_conf_obj  *svc_dir,
			      char               **ss_ep)
{
	int                      rc;
	struct m0_conf_service  *svc;
	struct m0_conf_obj      *obj = NULL;

	M0_ENTRY();

	M0_PRE(ss_ep != NULL);
	M0_PRE(svc_dir != NULL);

	*ss_ep = NULL;

	rc = m0_confc_open_sync(&svc_dir, svc_dir, M0_FID0);
	if (rc != 0)
		return M0_ERR(rc);

	for (svc = NULL; (rc = m0_confc_readdir_sync(svc_dir, &obj)) > 0; ) {
		svc = M0_CONF_CAST(obj, m0_conf_service);
		if (svc->cs_type == M0_CST_SSS) {
			*ss_ep = m0_strdup(svc->cs_endpoints[0]);
			rc = 0;
			break;
		}
	}

	m0_confc_close(obj);
	m0_confc_close(svc_dir);

	if (rc == 0 && *ss_ep == NULL)
		rc = M0_ERR(-ENOENT);
	return M0_RC(rc);
}

/****************************************************/
/*                     Services                     */
/****************************************************/

/**
 * Find endpoint of SSS service which accepts commands for given service
 */
static int spiel_ss_ep_for_svc(const struct m0_conf_service  *s,
			       char                         **ss_ep)
{
	M0_PRE(s != NULL);
	M0_PRE(ss_ep != NULL);

	M0_ENTRY();

	/* m0_conf_process::pc_services dir is the parent for the service */
	return M0_RC(spiel_ss_ep_lookup(s->cs_obj.co_parent, ss_ep));
}

static bool _filter_svc(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE;
}

static int spiel_svc_conf_obj_find(struct m0_spiel         *spl,
				   const struct m0_fid     *svc_fid,
				   struct m0_conf_service **svc_obj)
{
	int                  rc;
	struct m0_conf_obj  *obj;
	struct m0_confc     *confc   = &spl->spl_rconfc.rc_confc;
	struct m0_fid       *profile = &spl->spl_profile;

	rc = SPIEL_CONF_OBJ_FIND(confc, profile, svc_fid, &obj, _filter_svc,
				 M0_CONF_FILESYSTEM_NODES_FID,
				 M0_CONF_NODE_PROCESSES_FID,
				 M0_CONF_PROCESS_SERVICES_FID);

	*svc_obj = (rc == 0) ? M0_CONF_CAST(obj, m0_conf_service) : NULL;

	return M0_RC(rc);
}

static int spiel_svc_fop_fill(struct m0_fop          *fop,
			      struct m0_conf_service *svc,
			      uint32_t                cmd)
{
	struct m0_sss_req      *ss_fop = m0_fop_data(fop);
	char                   *name;

	ss_fop->ss_cmd = cmd;
	ss_fop->ss_id  = svc->cs_obj.co_id;

	name = m0_conf_service_name_dup(svc);
	if (name == NULL)
		return M0_ERR(-ENOMEM);
	m0_buf_init(&ss_fop->ss_name, name, strlen(name));

	/* TODO: Check what parameters are used by which service types
	 * and fill ss_fop->ss_param appropriately */
	if (svc->cs_type == M0_CST_MGS && svc->cs_u.confdb_path != NULL) {
		m0_buf_init(&ss_fop->ss_param,
			    (void *)svc->cs_u.confdb_path,
			    strlen(svc->cs_u.confdb_path));
	}

	return 0;
}

static int spiel_svc_fop_fill_and_send(struct m0_spiel     *spl,
				       struct m0_fop       *fop,
				       const struct m0_fid *svc_fid,
				       uint32_t             cmd)
{
	int                     rc;
	struct m0_conf_service *svc;
	char                   *ss_ep = NULL;

	M0_ENTRY();
	M0_PRE(M0_IN(cmd, (M0_SERVICE_INIT, M0_SERVICE_START,
			   M0_SERVICE_STOP, M0_SERVICE_QUIESCE,
			   M0_SERVICE_HEALTH)));
	M0_PRE(m0_conf_fid_type(svc_fid) == &M0_CONF_SERVICE_TYPE);
	M0_PRE(spl != NULL);
	rc = spiel_svc_conf_obj_find(spl, svc_fid, &svc);
	if (rc != 0)
		return M0_ERR(rc);

	rc = spiel_svc_fop_fill(fop, svc, cmd) ?:
	     spiel_ss_ep_for_svc(svc, &ss_ep);
	if (rc == 0) {
		rc = spiel_send_cmd(spl->spl_rmachine, ss_ep, fop);
		m0_free(ss_ep);
	}

	m0_confc_close(&svc->cs_obj);
	return M0_RC(rc);
}

static struct m0_fop *spiel_svc_fop_alloc(struct m0_rpc_machine *mach)
{
	return m0_fop_alloc(&m0_fop_ss_fopt, NULL, mach);
}

static int spiel_sss_reply_rc(struct m0_fop *fop)
{
	struct m0_rpc_item *item    = fop->f_item.ri_reply;
	struct m0_fop      *rep_fop = m0_rpc_item_to_fop(item);

	return ((struct m0_sss_rep *)m0_fop_data(rep_fop))->ssr_rc;
}

static int spiel_svc_generic_handler(struct m0_spiel     *spl,
				     const struct m0_fid *svc_fid,
				     enum m0_sss_req_cmd  cmd)
{
	int            rc;
	struct m0_fop *fop;

	M0_ENTRY();

	if (m0_conf_fid_type(svc_fid) != &M0_CONF_SERVICE_TYPE)
		return M0_ERR(-EINVAL);

	fop = spiel_svc_fop_alloc(spl->spl_rmachine);

	if (fop == NULL)
		return M0_ERR(-ENOMEM);

	rc = spiel_svc_fop_fill_and_send(spl, fop, svc_fid, cmd) ?:
	     spiel_sss_reply_rc(fop);

	m0_fop_put_lock(fop);
	return M0_RC(rc);
}

int m0_spiel_service_init(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(spl, svc_fid, M0_SERVICE_INIT));
}
M0_EXPORTED(m0_spiel_service_init);

int m0_spiel_service_start(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(spl, svc_fid, M0_SERVICE_START));
}
M0_EXPORTED(m0_spiel_service_start);

int m0_spiel_service_stop(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(spl, svc_fid, M0_SERVICE_STOP));
}
M0_EXPORTED(m0_spiel_service_stop);

int m0_spiel_service_health(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(
				spl, svc_fid, M0_SERVICE_HEALTH));
}
M0_EXPORTED(m0_spiel_service_health);

int m0_spiel_service_quiesce(struct m0_spiel     *spl,
		             const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(
				spl, svc_fid, M0_SERVICE_QUIESCE));
}
M0_EXPORTED(m0_spiel_service_quiesce);


/****************************************************/
/*                     Devices                      */
/****************************************************/

int m0_spiel_device_attach(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_device_attach);

int m0_spiel_device_detach(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_device_detach);

int m0_spiel_device_format(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_device_format);


/****************************************************/
/*                   Processes                      */
/****************************************************/
static bool _filter_proc(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_PROCESS_TYPE;
}

/* Non static for use in UT only */
int spiel_proc_conf_obj_find(struct m0_spiel         *spl,
			     const struct m0_fid     *proc_fid,
			     struct m0_conf_process **proc_obj)
{
	struct m0_conf_obj *obj;
	struct m0_confc    *confc   = &spl->spl_rconfc.rc_confc;
	struct m0_fid      *profile = &spl->spl_profile;
	int                 rc;

	rc = SPIEL_CONF_OBJ_FIND(confc, profile, proc_fid, &obj, _filter_proc,
				 M0_CONF_FILESYSTEM_NODES_FID,
				 M0_CONF_NODE_PROCESSES_FID);

	*proc_obj = rc == 0 ? M0_CONF_CAST(obj, m0_conf_process) : NULL;

	return M0_RC(rc);
}

static int spiel_process_command_send(struct m0_spiel     *spl,
				      const struct m0_fid *proc_fid,
				      struct m0_fop       *fop)
{
	struct m0_conf_process *process;
	char                   *ep;
	int                     rc;

	M0_ENTRY();

	M0_PRE(spl != NULL);
	M0_PRE(proc_fid != NULL);
	M0_PRE(fop != NULL);
	M0_PRE(m0_conf_fid_type(proc_fid) == &M0_CONF_PROCESS_TYPE);
	M0_PRE(spl != NULL);
	rc = spiel_proc_conf_obj_find(spl, proc_fid, &process);
	if (rc != 0)
		return M0_ERR(rc);

	rc = spiel_ss_ep_lookup(&process->pc_services->cd_obj, &ep);
	m0_confc_close(&process->pc_obj);

	if (rc == 0)
		rc = spiel_send_cmd(spl->spl_rmachine, ep, fop);
	m0_free(ep);

	if (rc != 0) {
		m0_fop_fini(fop);
		m0_free(fop);
	}

	return M0_RC(rc);
}

static int spiel_process_reply_status(struct m0_fop *fop)
{
	struct m0_ss_process_rep *rep;

	rep = m0_ss_fop_process_rep(m0_rpc_item_to_fop(fop->f_item.ri_reply));
	return rep->sspr_rc;
}

static int spiel_process_command_execute(struct m0_spiel     *spl,
					 const struct m0_fid *proc_fid,
					 int                  cmd)
{
	struct m0_fop *fop;
	int            rc;

	M0_ENTRY();

	M0_PRE(spl != NULL);
	M0_PRE(proc_fid != NULL);
	M0_PRE(m0_conf_fid_type(proc_fid) == &M0_CONF_PROCESS_TYPE);

	fop = m0_ss_process_fop_create(spl->spl_rmachine, cmd, proc_fid);
	if (fop == NULL)
		return M0_RC(-ENOMEM);
	rc = spiel_process_command_send(spl, proc_fid, fop);
	if (rc == 0) {
		rc = spiel_process_reply_status(fop);
		m0_fop_put_lock(fop);
	}
	return M0_RC(rc);
}

int m0_spiel_process_stop(struct m0_spiel *spl, const struct m0_fid *proc_fid)
{
	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	return M0_RC(spiel_process_command_execute(spl, proc_fid,
						   M0_PROCESS_STOP));
}
M0_EXPORTED(m0_spiel_process_stop);

int m0_spiel_process_reconfig(struct m0_spiel     *spl,
			      const struct m0_fid *proc_fid)
{
	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	return M0_RC(spiel_process_command_execute(spl, proc_fid,
						   M0_PROCESS_RECONFIG));
}
M0_EXPORTED(m0_spiel_process_reconfig);

int m0_spiel_process_health(struct m0_spiel     *spl,
			    const struct m0_fid *proc_fid)
{
	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	return M0_RC(spiel_process_command_execute(spl, proc_fid,
						   M0_PROCESS_HEALTH));
}
M0_EXPORTED(m0_spiel_process_health);

int m0_spiel_process_quiesce(struct m0_spiel     *spl,
			     const struct m0_fid *proc_fid)
{
	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	return M0_RC(spiel_process_command_execute(spl, proc_fid,
						   M0_PROCESS_QUIESCE));
}
M0_EXPORTED(m0_spiel_process_quiesce);

static int spiel_running_svcs_list_fill(struct m0_bufs               *bufs,
					struct m0_spiel_running_svc **svcs)
{
	struct m0_ss_process_svc_item *src;
	struct m0_spiel_running_svc   *services;
	int                            i;

	M0_ENTRY();

	M0_ALLOC_ARR(services, bufs->ab_count);
	if (services == NULL)
		return M0_ERR(-ENOMEM);
	for(i = 0; i < bufs->ab_count; ++i) {
		src = (struct m0_ss_process_svc_item *)bufs->ab_elems[i].b_addr;
		services[i].spls_fid = src->ssps_fid;
		services[i].spls_name = m0_strdup(src->ssps_name);
		if (services[i].spls_name == NULL)
			goto err;
	}
	*svcs = services;

	return M0_RC(bufs->ab_count);
err:
	for(i = 0; i < bufs->ab_count; ++i)
		m0_free(services[i].spls_name);
	m0_free(services);
	return M0_ERR(-ENOMEM);
}

int m0_spiel_process_list_services(struct m0_spiel              *spl,
				   const struct m0_fid          *proc_fid,
				   struct m0_spiel_running_svc **services)
{
	struct m0_fop                     *fop;
	struct m0_ss_process_svc_list_rep *rep;
	int                                rc;

	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	fop = m0_ss_process_fop_create(spl->spl_rmachine,
				       M0_PROCESS_RUNNING_LIST, proc_fid);
	if (fop == NULL)
		return M0_RC(-ENOMEM);
	rc = spiel_process_command_send(spl, proc_fid, fop);
	if (rc == 0) {
		rep = m0_ss_fop_process_svc_list_rep(
				m0_rpc_item_to_fop(fop->f_item.ri_reply));
		rc = rep->sspr_rc;
		if (rc == 0)
			rc = spiel_running_svcs_list_fill(&rep->sspr_services,
							  services);
		m0_fop_put_lock(fop);
	}

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_process_list_services);

/****************************************************/
/*                      Pools                       */
/****************************************************/

int m0_spiel_pool_repair_start(struct m0_spiel     *spl,
			       const struct m0_fid *pool_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_repair_start);

int m0_spiel_pool_repair_quiesce(struct m0_spiel     *spl,
				 const struct m0_fid *pool_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_repair_quiesce);

int m0_spiel_pool_rebalance_start(struct m0_spiel     *spl,
				  const struct m0_fid *pool_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_rebalance_start);

int m0_spiel_pool_rebalance_quiesce(struct m0_spiel     *spl,
				    const struct m0_fid *pool_fid)
{
	int rc = 0;
	M0_ENTRY();

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_pool_rebalance_quiesce);


#undef M0_TRACE_SUBSYSTEM
