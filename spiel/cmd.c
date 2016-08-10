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
#include "lib/finject.h"        /* M0_FI_ENABLED */

#include "conf/obj_ops.h"       /* m0_conf_obj_get */
#include "conf/preload.h"       /* m0_confx_string_free */
#include "fid/fid_list.h"       /* m0_fid_item */
#include "rpc/rpclib.h"         /* m0_rpc_post_with_timeout_sync */
#include "sns/cm/trigger_fop.h" /* trigger_fop */
#include "sss/device_fops.h"    /* m0_sss_device_fop_create */
#include "sss/ss_fops.h"
#include "sss/process_fops.h"   /* m0_ss_fop_process_rep */
#include "spiel/spiel.h"
#include "spiel/spiel_internal.h"
#include "spiel/cmd_internal.h"

/**
 * @addtogroup spiel-api-fspec-intr
 * @{
 */

M0_TL_DESCR_DEFINE(spiel_string, "list of endpoints",
		   static, struct spiel_string_entry, sse_link, sse_magic,
		   M0_STATS_MAGIC, M0_STATS_HEAD_MAGIC);
M0_TL_DEFINE(spiel_string, static, struct spiel_string_entry);

static void spiel_fop_destroy(struct m0_fop *fop)
{
	m0_rpc_machine_lock(m0_fop_rpc_machine(fop));
	m0_fop_fini(fop);
	m0_rpc_machine_unlock(m0_fop_rpc_machine(fop));
	m0_free(fop);
}

static bool _filter_svc(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE;
}

static bool _filter_controller(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_CONTROLLER_TYPE;
}

static int spiel_node_process_endpoint_add(struct m0_spiel_core *spc,
				           struct m0_conf_obj   *node,
				           struct m0_tl         *list)
{
	struct spiel_string_entry *entry;
	struct m0_conf_diter       it;
	struct m0_conf_process    *p;
	struct m0_conf_service    *svc;
	int                        rc;

	M0_ENTRY("conf_node: %p", &node);

	rc = m0_conf_diter_init(&it, spc->spc_confc, node,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	if (rc != 0)
		return M0_ERR(rc);

	while ((rc = m0_conf_diter_next_sync(&it, _filter_svc)) ==
		     M0_CONF_DIRNEXT) {
		svc = M0_CONF_CAST(m0_conf_diter_result(&it), m0_conf_service);
		if (svc->cs_type == M0_CST_IOS) {
			p = M0_CONF_CAST(m0_conf_obj_grandparent(&svc->cs_obj),
					 m0_conf_process);
			M0_ALLOC_PTR(entry);
			if (entry == NULL) {
				rc = M0_ERR(-ENOMEM);
				break;
			}
			entry->sse_string = m0_strdup(p->pc_endpoint);
			spiel_string_tlink_init_at(entry, list);
			break;
		}
	}

	m0_conf_diter_fini(&it);
	return M0_RC(rc);
}

/**
 * Finds mo_conf_node by m0_conf_disk and collect endpoints from its node.
 *
 * Disk -> Controller -> Node -> Processes -> SSS services.
 */
static int spiel_endpoints_for_device_generic(struct m0_spiel_core *spc,
					      const struct m0_fid  *device_fid,
					      struct m0_tl         *list)
{
	struct m0_confc           *confc = spc->spc_confc;
	struct m0_conf_diter       it;
	struct m0_conf_obj        *fs;
	struct m0_conf_obj        *disk;
	struct m0_conf_obj        *ctrl_obj;
	struct m0_conf_controller *ctrl;
	struct m0_conf_obj        *node_obj;
	int                        rc;

	M0_ENTRY();
	M0_PRE(device_fid != NULL);
	M0_PRE(list != NULL);

	if (m0_conf_fid_type(device_fid) != &M0_CONF_DISK_TYPE)
		return M0_ERR(-EINVAL);

	rc = m0_confc_open_sync(&fs, confc->cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				spc->spc_profile,
				M0_CONF_PROFILE_FILESYSTEM_FID);
	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_conf_diter_init(&it, confc, fs,
				M0_CONF_FILESYSTEM_RACKS_FID,
				M0_CONF_RACK_ENCLS_FID,
				M0_CONF_ENCLOSURE_CTRLS_FID);
	if (rc != 0) {
		m0_conf_diter_fini(&it);
		return M0_ERR(rc);
	}

	while ((rc = m0_conf_diter_next_sync(&it, _filter_controller)) ==
							M0_CONF_DIRNEXT) {
		ctrl_obj = m0_conf_diter_result(&it);
		rc = m0_confc_open_sync(&disk, ctrl_obj,
					M0_CONF_CONTROLLER_DISKS_FID,
					*device_fid);

		if (rc == 0) {
			ctrl = M0_CONF_CAST(ctrl_obj, m0_conf_controller);
			rc = m0_confc_open_by_fid_sync(confc,
					       &ctrl->cc_node->cn_obj.co_id,
					       &node_obj);
			if (rc == 0) {
				spiel_node_process_endpoint_add(spc, node_obj,
							        list);
				m0_confc_close(node_obj);
			}
			m0_confc_close(disk);
		}
	}

	m0_conf_diter_fini(&it);
	m0_confc_close(fs);

	return M0_RC(rc);
}

static int spiel_cmd_send(struct m0_rpc_machine *rmachine,
			  const char            *remote_ep,
			  struct m0_fop         *cmd_fop,
			  m0_time_t              timeout)
{
	struct m0_rpc_link *rlink;
	m0_time_t           conn_timeout;
	int                 rc;

	M0_ENTRY("lep=%s ep=%s", m0_rpc_machine_ep(rmachine), remote_ep);

	M0_PRE(rmachine != NULL);
	M0_PRE(remote_ep != NULL);
	M0_PRE(cmd_fop != NULL);

	/* RPC link structure is too big to allocate it on stack */
	M0_ALLOC_PTR(rlink);
	if (rlink == NULL)
		return M0_ERR(-ENOMEM);

	rc = m0_rpc_link_init(rlink, rmachine, NULL, remote_ep,
			      SPIEL_MAX_RPCS_IN_FLIGHT);
	if (rc == 0) {
		conn_timeout = m0_time_from_now(SPIEL_CONN_TIMEOUT, 0);
		if (M0_FI_ENABLED("timeout"))
			timeout = M0_TIME_ONE_SECOND;
		rc = m0_rpc_link_connect_sync(rlink, conn_timeout) ?:
		     m0_rpc_post_with_timeout_sync(cmd_fop,
						    &rlink->rlk_sess,
						    NULL,
						    M0_TIME_IMMEDIATELY,
						    timeout);

		if (rlink->rlk_connected) {
			conn_timeout = m0_time_from_now(SPIEL_CONN_TIMEOUT, 0);
			m0_rpc_link_disconnect_sync(rlink, conn_timeout);
		}
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
	M0_PRE(profile != NULL);
	M0_PRE(obj_fid != NULL);
	M0_PRE(path != NULL);
	M0_PRE(nr_lvls > 0);
	M0_PRE(m0_fid_eq(&path[0], &M0_CONF_FILESYSTEM_NODES_FID) ||
	       m0_fid_eq(&path[0], &M0_CONF_FILESYSTEM_POOLS_FID));
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
			m0_conf_obj_get_lock(*conf_obj);
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

/**
 * Iterates through directory specified by the path and calls iterator callback
 * on every directory item having the object pinned during callback
 * execution. In the course of execution, provides ability to test filesystem
 * object for correctness, e.g. the required filesystem really exists in the
 * profile.
 */
static int _spiel_conf_dir_iterate(struct m0_confc     *confc,
				   const struct m0_fid *profile,
				   void                *ctx,
				   bool (*iter_cb)
				   (const struct m0_conf_obj *item, void *ctx),
				   bool (*fs_test_cb)
				   (const struct m0_conf_obj *item, void *ctx),
				   uint32_t             nr_lvls,
				   const struct m0_fid *path)
{
	int                  rc;
	bool                 loop;
	struct m0_conf_obj  *fs_obj = NULL;
	struct m0_conf_obj  *obj;
	struct m0_conf_diter it;

	M0_ENTRY();

	M0_PRE(confc != NULL);
	M0_PRE(path != NULL);
	M0_PRE(nr_lvls > 0);
	M0_PRE(m0_fid_eq(&path[0], &M0_CONF_FILESYSTEM_NODES_FID));

	rc = m0_confc_open_sync(&fs_obj, confc->cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				*profile,
				M0_CONF_PROFILE_FILESYSTEM_FID);

	if (rc != 0)
		return M0_ERR(rc);

	if (fs_test_cb != NULL && !fs_test_cb(fs_obj, ctx)) {
		m0_confc_close(fs_obj);
		return M0_ERR(-ENOENT);
	}

	rc = m0_conf__diter_init(&it, confc, fs_obj, nr_lvls, path);

	if (rc != 0) {
		m0_confc_close(fs_obj);
		return M0_ERR(rc);
	}

	loop = true;
	while (loop &&
	       (m0_conf_diter_next_sync(&it, NULL)) == M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		/* Pin obj to protect it from removal while being in use */
		m0_conf_obj_get_lock(obj);
		loop = iter_cb(obj, ctx);
		m0_mutex_lock(&confc->cc_lock);
		m0_conf_obj_put(obj);
		m0_mutex_unlock(&confc->cc_lock);
	}

	m0_conf_diter_fini(&it);
	m0_confc_close(fs_obj);

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
	struct m0_conf_process *p;

	M0_PRE(s != NULL);
	M0_PRE(ss_ep != NULL);

	M0_ENTRY();

	/* m0_conf_process::pc_services dir is the parent for the service */
	p = M0_CONF_CAST(m0_conf_obj_grandparent(&s->cs_obj), m0_conf_process);
	M0_ASSERT(!m0_conf_obj_is_stub(&p->pc_obj));
	/* All services within a process share the same endpoint. */
	*ss_ep = m0_strdup(p->pc_endpoint);
	return M0_RC(*ss_ep == NULL ? -ENOENT : 0);
}

static int spiel_svc_conf_obj_find(struct m0_spiel_core    *spc,
				   const struct m0_fid     *svc,
				   struct m0_conf_service **out)
{
	struct m0_conf_obj *obj;
	int                 rc;

	rc = SPIEL_CONF_OBJ_FIND(spc->spc_confc, &spc->spc_profile, svc, &obj,
				 _filter_svc, M0_CONF_FILESYSTEM_NODES_FID,
				 M0_CONF_NODE_PROCESSES_FID,
				 M0_CONF_PROCESS_SERVICES_FID);
	if (rc == 0)
		*out = M0_CONF_CAST(obj, m0_conf_service);
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

static int spiel_svc_fop_fill_and_send(struct m0_spiel_core *spc,
				       struct m0_fop        *fop,
				       const struct m0_fid  *svc_fid,
				       uint32_t              cmd)
{
	int                     rc;
	struct m0_conf_service *svc;
	char                   *ss_ep = NULL;

	M0_ENTRY();
	M0_PRE(M0_IN(cmd, (M0_SERVICE_INIT, M0_SERVICE_START,
			   M0_SERVICE_STOP, M0_SERVICE_QUIESCE,
			   M0_SERVICE_HEALTH, M0_SERVICE_STATUS)));
	M0_PRE(m0_conf_fid_type(svc_fid) == &M0_CONF_SERVICE_TYPE);
	M0_PRE(spc != NULL);
	rc = spiel_svc_conf_obj_find(spc, svc_fid, &svc);
	if (rc != 0)
		return M0_ERR(rc);

	rc = spiel_svc_fop_fill(fop, svc, cmd) ?:
	     spiel_ss_ep_for_svc(svc, &ss_ep);
	if (rc == 0) {
		rc = spiel_cmd_send(spc->spc_rmachine, ss_ep, fop,
				    M0_TIME_NEVER);
		m0_free(ss_ep);
	}

	m0_confc_close(&svc->cs_obj);
	return M0_RC(rc);
}

static struct m0_fop *spiel_svc_fop_alloc(struct m0_rpc_machine *mach)
{
	return m0_fop_alloc(&m0_fop_ss_fopt, NULL, mach);
}

static struct m0_sss_rep *spiel_sss_reply_data(struct m0_fop *fop)
{
	struct m0_rpc_item *item    = fop->f_item.ri_reply;
	struct m0_fop      *rep_fop = m0_rpc_item_to_fop(item);

	return (struct m0_sss_rep *)m0_fop_data(rep_fop);
}

static int spiel_svc_generic_handler(struct m0_spiel_core *spc,
				     const struct m0_fid  *svc_fid,
				     enum m0_sss_req_cmd   cmd,
				     int                  *status)
{
	int            rc;
	struct m0_fop *fop;

	M0_ENTRY();

	if (m0_conf_fid_type(svc_fid) != &M0_CONF_SERVICE_TYPE)
		return M0_ERR(-EINVAL);

	fop = spiel_svc_fop_alloc(spc->spc_rmachine);

	if (fop == NULL)
		return M0_ERR(-ENOMEM);

	rc = spiel_svc_fop_fill_and_send(spc, fop, svc_fid, cmd) ?:
	     spiel_sss_reply_data(fop)->ssr_rc;

	if (rc == 0 && status != NULL)
		*status = spiel_sss_reply_data(fop)->ssr_state;

	m0_fop_put_lock(fop);
	return M0_RC(rc);
}

int m0_spiel_service_init(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(&spl->spl_core, svc_fid,
					       M0_SERVICE_INIT, NULL));
}
M0_EXPORTED(m0_spiel_service_init);

int m0_spiel_service_start(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(&spl->spl_core, svc_fid,
					       M0_SERVICE_START, NULL));
}
M0_EXPORTED(m0_spiel_service_start);

int m0_spiel_service_stop(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(&spl->spl_core, svc_fid,
					       M0_SERVICE_STOP, NULL));
}
M0_EXPORTED(m0_spiel_service_stop);

int m0_spiel_service_health(struct m0_spiel *spl, const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(&spl->spl_core, svc_fid,
					       M0_SERVICE_HEALTH, NULL));
}
M0_EXPORTED(m0_spiel_service_health);

int m0_spiel_service_quiesce(struct m0_spiel     *spl,
		             const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(&spl->spl_core, svc_fid,
					       M0_SERVICE_QUIESCE, NULL));
}
M0_EXPORTED(m0_spiel_service_quiesce);

int m0_spiel_service_status(struct m0_spiel *spl, const struct m0_fid *svc_fid,
			    int *status)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(&spl->spl_core, svc_fid,
					       M0_SERVICE_STATUS, status));
}
M0_EXPORTED(m0_spiel_service_status);


/****************************************************/
/*                     Devices                      */
/****************************************************/

static int spiel_device_command_fop_send(struct m0_spiel_core *spc,
					 const char           *endpoint,
					 const struct m0_fid  *dev_fid,
					 int                   cmd,
					 uint32_t             *ha_state)
{
	struct m0_fop                *fop;
	struct m0_sss_device_fop_rep *rep;
	int                           rc;

	fop  = m0_sss_device_fop_create(spc->spc_rmachine, cmd, dev_fid);
	if (fop == NULL)
		return M0_RC(-ENOMEM);

	rc = spiel_cmd_send(spc->spc_rmachine, endpoint, fop,
			    cmd == M0_DEVICE_FORMAT ?
				   SPIEL_DEVICE_FORMAT_TIMEOUT :
				   M0_TIME_NEVER);
	if (rc == 0) {
		rep = m0_sss_fop_to_dev_rep(
				m0_rpc_item_to_fop(fop->f_item.ri_reply));
		rc = rep->ssdp_rc;
		if (ha_state != NULL)
			*ha_state = rep->ssdp_ha_state;
		m0_fop_put_lock(fop);
	} else {
		spiel_fop_destroy(fop);
	}
	return M0_RC(rc);
}

/**
 * Search all processes that have IO services, fetch their endpoints and send
 * command FOPs.
 */
static int spiel_device_command_send(struct m0_spiel_core *spc,
				     const struct m0_fid  *dev_fid,
				     int                   cmd,
				     uint32_t             *ha_state)
{
	struct m0_tl               endpoints;
	struct spiel_string_entry *ep;
	int                        rc;

	M0_ENTRY();

	if (m0_conf_fid_type(dev_fid) != &M0_CONF_DISK_TYPE)
		return M0_ERR(-EINVAL);

	spiel_string_tlist_init(&endpoints);

	rc = spiel_endpoints_for_device_generic(spc, dev_fid, &endpoints);
	if (rc == 0 && spiel_string_tlist_is_empty(&endpoints))
		rc = M0_ERR(-ENOENT);

	if (rc == 0)
		m0_tl_teardown(spiel_string, &endpoints, ep) {
			rc = rc ?:
			spiel_device_command_fop_send(spc, ep->sse_string,
						      dev_fid, cmd, ha_state);
			m0_free((char *)ep->sse_string);
			m0_free(ep);
		}

	spiel_string_tlist_fini(&endpoints);

	return M0_RC(rc);
}

int m0_spiel_device_attach(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	return m0_spiel_device_attach_state(spl, dev_fid, NULL);
}
M0_EXPORTED(m0_spiel_device_attach);

int m0_spiel_device_attach_state(struct m0_spiel     *spl,
				 const struct m0_fid *dev_fid,
				 uint32_t            *ha_state)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(dev_fid));

	return M0_RC(spiel_device_command_send(&spl->spl_core, dev_fid,
					       M0_DEVICE_ATTACH, ha_state));
}
M0_EXPORTED(m0_spiel_device_attach_state);

int m0_spiel_device_detach(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(dev_fid));

	return M0_RC(spiel_device_command_send(&spl->spl_core, dev_fid,
					       M0_DEVICE_DETACH, NULL));
}
M0_EXPORTED(m0_spiel_device_detach);

int m0_spiel_device_format(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(dev_fid));

	return M0_RC(spiel_device_command_send(&spl->spl_core, dev_fid,
					       M0_DEVICE_FORMAT, NULL));
}
M0_EXPORTED(m0_spiel_device_format);

/****************************************************/
/*                   Processes                      */
/****************************************************/
static bool _filter_proc(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_PROCESS_TYPE;
}

static int spiel_proc_conf_obj_find(struct m0_spiel_core    *spc,
				    const struct m0_fid     *proc,
				    struct m0_conf_process **out)
{
	struct m0_conf_obj *obj;
	int                 rc;

	rc = SPIEL_CONF_OBJ_FIND(spc->spc_confc, &spc->spc_profile, proc,
				 &obj, _filter_proc,
				 M0_CONF_FILESYSTEM_NODES_FID,
				 M0_CONF_NODE_PROCESSES_FID);
	if (rc == 0)
		*out = M0_CONF_CAST(obj, m0_conf_process);
	return M0_RC(rc);
}

static int spiel_process_command_send(struct m0_spiel_core *spc,
				      const struct m0_fid  *proc_fid,
				      struct m0_fop        *fop)
{
	struct m0_conf_process *process;
	int                     rc;

	M0_ENTRY();

	M0_PRE(spc != NULL);
	M0_PRE(proc_fid != NULL);
	M0_PRE(fop != NULL);
	M0_PRE(m0_conf_fid_type(proc_fid) == &M0_CONF_PROCESS_TYPE);
	M0_PRE(spc != NULL);

	rc = spiel_proc_conf_obj_find(spc, proc_fid, &process);
	if (rc != 0)
		return M0_ERR(rc);

	m0_confc_close(&process->pc_obj);

	rc = spiel_cmd_send(spc->spc_rmachine, process->pc_endpoint, fop,
			    M0_TIME_NEVER);
	if (rc != 0)
		spiel_fop_destroy(fop);
	return M0_RC(rc);
}

static struct m0_ss_process_rep *spiel_process_reply_data(struct m0_fop *fop)
{
	return m0_ss_fop_process_rep(m0_rpc_item_to_fop(fop->f_item.ri_reply));
}

/**
 * When reply is not NULL, reply fop remains alive and gets passed to caller for
 * further reply data processing. Otherwise it immediately appears released.
 */
static int spiel_process_command_execute(struct m0_spiel_core *spc,
					 const struct m0_fid  *proc_fid,
					 int                   cmd,
					 struct m0_fop       **reply)
{
	struct m0_fop *fop;
	int            rc;

	M0_ENTRY();

	M0_PRE(spc != NULL);
	M0_PRE(proc_fid != NULL);
	M0_PRE(m0_conf_fid_type(proc_fid) == &M0_CONF_PROCESS_TYPE);

	fop = m0_ss_process_fop_create(spc->spc_rmachine, cmd, proc_fid);
	if (fop == NULL)
		return M0_ERR(-ENOMEM);
	rc = spiel_process_command_send(spc, proc_fid, fop);
	if (rc == 0) {
		rc = spiel_process_reply_data(fop)->sspr_rc;
		if (reply != NULL)
			*reply = fop;
		else
			m0_fop_put_lock(fop);
	}
	return M0_RC(rc);
}

int m0_spiel_process_stop(struct m0_spiel *spl, const struct m0_fid *proc_fid)
{
	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	return M0_RC(spiel_process_command_execute(&spl->spl_core, proc_fid,
						   M0_PROCESS_STOP, NULL));
}
M0_EXPORTED(m0_spiel_process_stop);

int m0_spiel_process_reconfig(struct m0_spiel     *spl,
			      const struct m0_fid *proc_fid)
{
	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	return M0_RC(spiel_process_command_execute(&spl->spl_core, proc_fid,
						   M0_PROCESS_RECONFIG, NULL));
}
M0_EXPORTED(m0_spiel_process_reconfig);

static int spiel_process__health(struct m0_spiel_core *spc,
				 const struct m0_fid  *proc_fid,
				 struct m0_fop       **reply)
{
	struct m0_fop *reply_fop;
	int            rc;
	int            health;

	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);
	reply_fop = NULL;
	health = M0_HEALTH_UNKNOWN;
	rc = spiel_process_command_execute(spc, proc_fid, M0_PROCESS_HEALTH,
					   &reply_fop);
	if (reply_fop != NULL) {
		M0_ASSERT(rc == 0);
		health = spiel_process_reply_data(reply_fop)->sspr_health;
		if (reply != NULL)
			*reply = reply_fop;
		else
			m0_fop_put_lock(reply_fop);
	}
	return rc < 0 ? M0_ERR(rc) : M0_RC(health);
}

int m0_spiel_process_health(struct m0_spiel     *spl,
			    const struct m0_fid *proc_fid)
{
	return M0_RC(spiel_process__health(&spl->spl_core, proc_fid, NULL));
}
M0_EXPORTED(m0_spiel_process_health);

int m0_spiel_process_quiesce(struct m0_spiel     *spl,
			     const struct m0_fid *proc_fid)
{
	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	return M0_RC(spiel_process_command_execute(&spl->spl_core, proc_fid,
						   M0_PROCESS_QUIESCE, NULL));
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

	fop = m0_ss_process_fop_create(spiel_rmachine(spl),
				       M0_PROCESS_RUNNING_LIST, proc_fid);
	if (fop == NULL)
		return M0_ERR(-ENOMEM);
	rc = spiel_process_command_send(&spl->spl_core, proc_fid, fop);
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

static int spiel_stats_item_add(struct m0_tl *tl, const struct m0_fid *fid)
{
	struct m0_fid_item *si;

	M0_ALLOC_PTR(si);
	if (si == NULL)
		return M0_ERR(-ENOMEM);
	si->i_fid = *fid;
	m0_fids_tlink_init_at(si, tl);
	return M0_RC(0);
}


static bool _filter_pool(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_POOL_TYPE;
}

/* Spiel sns context per service. */
struct spiel_sns {
	struct m0_spiel_sns_status ss_status;
	/* RPC link for the corresponding service. */
	struct m0_rpc_link         ss_rlink;
	struct m0_conf_service    *ss_service;
	struct m0_fop             *ss_fop;
	int                        ss_rc;
	bool                       ss_is_connected;
};

static int spiel_sns_cmd_send(struct m0_rpc_machine *rmachine,
			      const char            *remote_ep,
			      struct spiel_sns      *sns)
{
	struct m0_rpc_link *rlink = &sns->ss_rlink;
	m0_time_t           conn_timeout;
	int                 rc;
	struct m0_fop      *fop;
	struct m0_rpc_item *item;

	M0_ENTRY("lep=%s ep=%s", m0_rpc_machine_ep(rmachine), remote_ep);

	M0_PRE(rmachine != NULL);
	M0_PRE(remote_ep != NULL);
	M0_PRE(sns != NULL);

	fop = sns->ss_fop;
	rc = m0_rpc_link_init(rlink, rmachine, NULL, remote_ep,
			      SPIEL_MAX_RPCS_IN_FLIGHT);
	if (rc == 0) {
		conn_timeout = m0_time_from_now(SPIEL_CONN_TIMEOUT, 0);
		rc = m0_rpc_link_connect_sync(rlink, conn_timeout);
		if (rc != 0) {
			m0_rpc_link_fini(rlink);
			return M0_RC(rc);
		}
		item              = &fop->f_item;
		item->ri_ops      = NULL;
		item->ri_session  = &rlink->rlk_sess;
		item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
		item->ri_deadline = M0_TIME_IMMEDIATELY;
		m0_fop_get(fop);
		rc = m0_rpc_post(item);
	}
	sns->ss_is_connected = true;
	return M0_RC(rc);
}

static int spiel_sns_fop_fill_and_send(struct m0_spiel_core *spc,
				       struct m0_fop        *fop,
				       enum m0_sns_cm_op     op,
				       struct spiel_sns     *sns)
{
	struct trigger_fop     *treq = m0_fop_data(fop);
	struct m0_conf_service *svc = sns->ss_service;
	struct m0_conf_process *p = M0_CONF_CAST(
					m0_conf_obj_grandparent(&svc->cs_obj),
					m0_conf_process);

	M0_ENTRY("fop %p conf_service %p", fop, svc);
	treq->op = op;
	sns->ss_fop = fop;
	sns->ss_is_connected = false;
	return M0_RC(spiel_sns_cmd_send(spc->spc_rmachine, p->pc_endpoint,
					sns));
}

/** pool stats collection context */
struct _pool_cmd_ctx {
	int                   pl_rc;
	struct m0_spiel_core *pl_spc;         /**< spiel instance */
	struct m0_tl          pl_sdevs_fid;     /**< storage devices fid list */
	struct m0_tl          pl_services_fid;  /**< services fid list */
};

static int spiel_pool_device_collect(struct _pool_cmd_ctx *ctx,
				     struct m0_conf_obj *obj_diskv)
{
	struct m0_conf_objv *diskv;
	struct m0_conf_obj  *obj_disk;
	struct m0_conf_disk *disk;
	int                  rc;

	diskv = M0_CONF_CAST(obj_diskv, m0_conf_objv);
	if (diskv == NULL)
		return M0_ERR(-ENOENT);
	if (m0_conf_obj_type(diskv->cv_real) != &M0_CONF_DISK_TYPE)
		return -EINVAL; /* rackv, ctrlv objectv's are ignored. */

	rc = m0_confc_open_by_fid_sync(ctx->pl_spc->spc_confc,
				       &diskv->cv_real->co_id, &obj_disk);
	if (rc != 0)
		return M0_RC(rc);

	disk = M0_CONF_CAST(obj_disk, m0_conf_disk);
	if (disk->ck_dev != NULL)
		rc = spiel_stats_item_add(&ctx->pl_sdevs_fid,
					  &disk->ck_dev->sd_obj.co_id);

	m0_confc_close(obj_disk);
	return M0_RC(rc);
}

static bool _filter_sdev(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SDEV_TYPE;
}

static bool spiel__pool_service_has_sdev(struct _pool_cmd_ctx *ctx,
					 const struct m0_conf_obj *service)
{
	struct m0_conf_diter it;
	struct m0_conf_obj  *obj;
	int                  rc;
	bool                 found = false;

	rc = m0_conf_diter_init(&it, ctx->pl_spc->spc_confc,
				(struct m0_conf_obj*)service,
				M0_CONF_SERVICE_SDEVS_FID);

	if (rc != 0)
		goto done;

	while ((rc = m0_conf_diter_next_sync(&it, _filter_sdev))
						== M0_CONF_DIRNEXT && !found) {
		obj = m0_conf_diter_result(&it);
		if (m0_tl_find(m0_fids, si, &ctx->pl_sdevs_fid,
			      m0_fid_cmp(&si->i_fid, &obj->co_id) == 0) != NULL)
			found = true;
	}
	m0_conf_diter_fini(&it);

done:
	return found;
}

static void spiel__pool_ctx_init(struct _pool_cmd_ctx *ctx,
				 struct m0_spiel_core *spc)
{
	M0_SET0(ctx);
	ctx->pl_spc = spc;
	m0_fids_tlist_init(&ctx->pl_sdevs_fid);
	m0_fids_tlist_init(&ctx->pl_services_fid);
	M0_POST(m0_fids_tlist_invariant(&ctx->pl_sdevs_fid));
	M0_POST(m0_fids_tlist_invariant(&ctx->pl_services_fid));
}

static void spiel__pool_ctx_fini(struct _pool_cmd_ctx *ctx)
{
	struct m0_fid_item *si;

	if (!m0_fids_tlist_is_empty(&ctx->pl_sdevs_fid))
		m0_tl_teardown(m0_fids, &ctx->pl_sdevs_fid, si) {
			m0_free(si);
		}
	m0_fids_tlist_fini(&ctx->pl_sdevs_fid);

	if (!m0_fids_tlist_is_empty(&ctx->pl_services_fid))
		m0_tl_teardown(m0_fids, &ctx->pl_services_fid, si) {
			m0_free(si);
		}
	m0_fids_tlist_fini(&ctx->pl_services_fid);
}

static bool spiel__pool_service_select(const struct m0_conf_obj *item,
				       void                     *ctx)
{
	struct _pool_cmd_ctx   *pool_ctx = ctx;
	struct m0_conf_service *service;

	/* continue iterating only when no issue occurred previously */
	if (pool_ctx->pl_rc != 0)
		return false;
	/* skip all but service objects */
	if (m0_conf_obj_type(item) != &M0_CONF_SERVICE_TYPE)
		return true;

	service = M0_CONF_CAST(item, m0_conf_service);

	if (service->cs_type == M0_CST_IOS &&
	    spiel__pool_service_has_sdev(pool_ctx, item) == true)
		pool_ctx->pl_rc = spiel_stats_item_add(
						&pool_ctx->pl_services_fid,
						&item->co_id);
	return true;
}

static int spiel__pool_cmd_send(struct _pool_cmd_ctx    *ctx,
				const enum m0_sns_cm_op  cmd,
				struct spiel_sns        *sns)
{
	struct m0_fop *fop;
	M0_PRE(sns != NULL);

	return M0_RC(m0_sns_cm_trigger_fop_alloc(ctx->pl_spc->spc_rmachine,
						 cmd, &fop) ?:
		     spiel_sns_fop_fill_and_send(ctx->pl_spc, fop, cmd, sns));
}

static int spiel__pool_cmd_status_get(struct _pool_cmd_ctx    *ctx,
				      const enum m0_sns_cm_op  cmd,
				      struct spiel_sns        *sns)
{
	int                           rc;
	struct m0_fop                *fop;
	struct m0_rpc_item           *item;
	struct m0_spiel_sns_status   *status;
	struct m0_sns_status_rep_fop *reply;
	m0_time_t                     conn_timeout;

	M0_PRE(sns != NULL);

	status = &sns->ss_status;
	fop = sns->ss_fop;
	M0_ASSERT(ergo(sns->ss_is_connected, fop != NULL));
	item = &fop->f_item;
	rc = sns->ss_rc ?: m0_rpc_item_wait_for_reply(item, M0_TIME_NEVER) ?:
		m0_rpc_item_error(item);

	if (M0_IN(cmd, (SNS_REPAIR_STATUS, SNS_REBALANCE_STATUS))) {
		status->sss_fid = sns->ss_service->cs_obj.co_id;
		if (rc == 0) {
			reply = m0_fop_data(m0_rpc_item_to_fop(item->ri_reply));
			status->sss_progress = reply->ssr_progress;
			status->sss_state = reply->ssr_state;
		} else {
			status->sss_state = rc;
		}
	}
	m0_fop_put0_lock(fop);
	if (sns->ss_is_connected) {
		M0_ASSERT(sns->ss_rlink.rlk_connected);
		conn_timeout = m0_time_from_now(SPIEL_CONN_TIMEOUT, 0);
		m0_rpc_link_disconnect_sync(&sns->ss_rlink, conn_timeout);
		m0_rpc_link_fini(&sns->ss_rlink);
	}

	return M0_RC(rc);
}

static bool _filter_objv(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE;
}

static int spiel_pool__device_collection_fill(struct _pool_cmd_ctx *ctx,
					      const struct m0_fid  *pool_fid)
{
	struct m0_confc      *confc = ctx->pl_spc->spc_confc;
	struct m0_conf_obj   *pool_obj;
	struct m0_conf_obj   *obj;
	struct m0_conf_diter  it;
	int                   rc;
	M0_ENTRY();

	rc = SPIEL_CONF_OBJ_FIND(confc, &ctx->pl_spc->spc_profile, pool_fid,
				 &pool_obj, _filter_pool,
				 M0_CONF_FILESYSTEM_POOLS_FID) ?:
	     m0_conf_diter_init(&it, confc, pool_obj,
				M0_CONF_POOL_PVERS_FID,
				M0_CONF_PVER_RACKVS_FID,
				M0_CONF_RACKV_ENCLVS_FID,
				M0_CONF_ENCLV_CTRLVS_FID,
				M0_CONF_CTRLV_DISKVS_FID);
	if (rc != 0)
		goto leave;

	while ((rc = m0_conf_diter_next_sync(&it, _filter_objv))
							== M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		/* Pin obj to protect it from removal while being in use */
		m0_conf_obj_get_lock(obj);
		spiel_pool_device_collect(ctx, obj);
		m0_mutex_lock(&confc->cc_lock);
		m0_conf_obj_put(obj);
		m0_mutex_unlock(&confc->cc_lock);
	}
	m0_conf_diter_fini(&it);

leave:
	m0_confc_close(pool_obj);
	return M0_RC(rc);
}

static int spiel_pool_generic_handler(struct m0_spiel_core        *spc,
				      const struct m0_fid         *pool_fid,
				      const enum m0_sns_cm_op      cmd,
				      struct m0_spiel_sns_status **statuses)
{
	int                         rc;
	int                         service_count;
	int                         index;
	struct _pool_cmd_ctx        ctx;
	struct m0_fid_item         *si;
	bool                        cmd_status;
	struct m0_spiel_sns_status *sns_statuses = NULL;
	struct spiel_sns           *sns;

	M0_ENTRY();
	M0_PRE(pool_fid != NULL);
	M0_PRE(spc != NULL);
	M0_PRE(spc->spc_rmachine != NULL);

	if (m0_conf_fid_type(pool_fid) != &M0_CONF_POOL_TYPE)
		return M0_ERR(-EINVAL);

	spiel__pool_ctx_init(&ctx, spc);

	rc = spiel_pool__device_collection_fill(&ctx, pool_fid) ?:
		SPIEL_CONF_DIR_ITERATE(spc->spc_confc, &spc->spc_profile,
				       &ctx, spiel__pool_service_select, NULL,
				       M0_CONF_FILESYSTEM_NODES_FID,
				       M0_CONF_NODE_PROCESSES_FID,
				       M0_CONF_PROCESS_SERVICES_FID) ?:
		ctx.pl_rc;
	if (rc != 0)
		goto leave;

	cmd_status = M0_IN(cmd, (SNS_REPAIR_STATUS, SNS_REBALANCE_STATUS));
	service_count = m0_fids_tlist_length(&ctx.pl_services_fid);

	M0_ALLOC_ARR(sns, service_count);
	if (sns == NULL) {
		rc = -ENOMEM;
		goto leave;
	}
	if (cmd_status) {
		M0_ALLOC_ARR(sns_statuses, service_count);
		if (sns_statuses == NULL) {
			rc = -ENOMEM;
			m0_free(sns);
			goto leave;
		}
	}

	index = 0;
	m0_tl_for(m0_fids, &ctx.pl_services_fid, si) {
		struct m0_conf_obj *svc_obj;

		rc = m0_confc_open_by_fid_sync(spc->spc_confc, &si->i_fid,
					       &svc_obj);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "conf sync failed for service"FID_F
					"index:%d", FID_P(&si->i_fid), index);
			sns[index++].ss_rc = rc;
			continue;
		}
		if (svc_obj->co_ha_state != M0_NC_ONLINE) {
			rc = -EINVAL;
			M0_LOG(M0_ERROR, "service"FID_F"is not online index:%d",
					FID_P(&si->i_fid), index);
			m0_confc_close(svc_obj);
			sns[index++].ss_rc = rc;
			continue;
		}
		sns[index].ss_service = M0_CONF_CAST(svc_obj, m0_conf_service);
		M0_ASSERT(index < service_count);
		rc = spiel__pool_cmd_send(&ctx, cmd, &sns[index]);
		m0_confc_close(svc_obj);
		if (rc != 0) {
			sns[index++].ss_rc = rc;
			M0_LOG(M0_ERROR, "pool command send failed for service"
					FID_F"index:%d", FID_P(&si->i_fid),
					index);
			continue;
		}
		++index;
	} m0_tl_endfor;

	index = 0;
	m0_tl_for (m0_fids, &ctx.pl_services_fid, si) {
		rc = spiel__pool_cmd_status_get(&ctx, cmd, &sns[index]);
		if (cmd_status) {
			sns_statuses[index] = sns[index].ss_status;
			M0_LOG(M0_DEBUG, "service"FID_F" status=%d",
					 FID_P(&si->i_fid),
					 sns_statuses[index].sss_state);
		}
		++index;
		if (rc != 0)
			continue;
	} m0_tl_endfor;

	if (rc == 0 && cmd_status) {
		rc = index;
		*statuses = sns_statuses;
		M0_LOG(M0_DEBUG, "array addr=%p size=%d", sns_statuses, index);
	} else
		m0_free(sns_statuses);

	m0_free(sns);
leave:
	spiel__pool_ctx_fini(&ctx);
	return M0_RC(rc);
}

int m0_spiel_pool_repair_start(struct m0_spiel     *spl,
			       const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(&spl->spl_core, pool_fid,
						SNS_REPAIR, NULL));
}
M0_EXPORTED(m0_spiel_pool_repair_start);

int m0_spiel_pool_repair_continue(struct m0_spiel     *spl,
				  const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(&spl->spl_core, pool_fid,
						SNS_REPAIR, NULL));
}
M0_EXPORTED(m0_spiel_pool_repair_continue);

int m0_spiel_pool_repair_quiesce(struct m0_spiel     *spl,
				 const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(&spl->spl_core, pool_fid,
						SNS_REPAIR_QUIESCE, NULL));
}
M0_EXPORTED(m0_spiel_pool_repair_quiesce);

int m0_spiel_pool_repair_abort(struct m0_spiel     *spl,
			       const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(&spl->spl_core, pool_fid,
						SNS_REPAIR_ABORT, NULL));
}
M0_EXPORTED(m0_spiel_pool_repair_abort);

int m0_spiel_pool_repair_status(struct m0_spiel             *spl,
				const struct m0_fid         *pool_fid,
				struct m0_spiel_sns_status **statuses)
{
	int rc;

	M0_ENTRY();
	M0_PRE(statuses != NULL);
	rc = spiel_pool_generic_handler(&spl->spl_core, pool_fid,
					SNS_REPAIR_STATUS, statuses);
	return rc >= 0 ? M0_RC(rc) : M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_pool_repair_status);

int m0_spiel_pool_rebalance_start(struct m0_spiel     *spl,
				  const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(&spl->spl_core, pool_fid,
						SNS_REBALANCE, NULL));
}
M0_EXPORTED(m0_spiel_pool_rebalance_start);

int m0_spiel_pool_rebalance_continue(struct m0_spiel     *spl,
				     const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(&spl->spl_core, pool_fid,
						SNS_REBALANCE, NULL));
}
M0_EXPORTED(m0_spiel_pool_rebalance_continue);

int m0_spiel_pool_rebalance_quiesce(struct m0_spiel     *spl,
				    const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(&spl->spl_core, pool_fid,
						SNS_REBALANCE_QUIESCE, NULL));
}
M0_EXPORTED(m0_spiel_pool_rebalance_quiesce);

int m0_spiel_pool_rebalance_status(struct m0_spiel             *spl,
				   const struct m0_fid         *pool_fid,
				   struct m0_spiel_sns_status **statuses)
{
	int rc;

	M0_ENTRY();
	M0_PRE(statuses != NULL);
	rc = spiel_pool_generic_handler(&spl->spl_core, pool_fid,
					SNS_REBALANCE_STATUS, statuses);
	return rc >= 0 ? M0_RC(rc) : M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_pool_rebalance_status);

int m0_spiel_pool_rebalance_abort(struct m0_spiel     *spl,
				  const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(&spl->spl_core, pool_fid,
						SNS_REBALANCE_ABORT, NULL));
}
M0_EXPORTED(m0_spiel_pool_rebalance_abort);

/****************************************************/
/*                    Filesystem                    */
/****************************************************/

static void spiel__fs_stats_ctx_init(struct _fs_stats_ctx *fsx,
				     struct m0_spiel_core *spc,
				     const struct m0_fid  *fs_fid,
				     const struct m0_conf_obj_type *item_type)
{
	M0_PRE(fs_fid != NULL);
	M0_SET0(fsx);
	fsx->fx_spc = spc;
	fsx->fx_fid = *fs_fid;
	fsx->fx_type = item_type;
	m0_fids_tlist_init(&fsx->fx_items);
	M0_POST(m0_fids_tlist_invariant(&fsx->fx_items));
}

static void spiel__fs_stats_ctx_fini(struct _fs_stats_ctx *fsx)
{
	struct m0_fid_item *si;

	m0_tl_teardown(m0_fids, &fsx->fx_items, si) {
		m0_free(si);
	}
	m0_fids_tlist_fini(&fsx->fx_items);
}

/**
 * Callback provided to SPIEL_CONF_DIR_ITERATE. Intended for collecting fids
 * from configuration objects which type matches to the type specified in
 * collection context, i.e. process objects in this particular case. See
 * m0_spiel_filesystem_stats_fetch() where spiel__fs_stats_ctx_init() is done.
 */
static bool spiel__item_enlist(const struct m0_conf_obj *item, void *ctx)
{
	struct _fs_stats_ctx *fsx = ctx;
	struct m0_fid_item   *si;

	M0_LOG(SPIEL_LOGLVL, "arrived: " FID_F " (%s)", FID_P(&item->co_id),
	       m0_fid_type_getfid(&item->co_id)->ft_name);
	/* continue iterating only when no issue occurred previously */
	if (fsx->fx_rc != 0)
		return false;
	/* skip all but requested object types */
	if (m0_conf_obj_type(item) != fsx->fx_type)
		return true;
	M0_ALLOC_PTR(si);
	if (si == NULL) {
		fsx->fx_rc = M0_ERR(-ENOMEM);
		return false;
	}
	si->i_fid = item->co_id;
	m0_fids_tlink_init_at(si, &fsx->fx_items);
	M0_LOG(SPIEL_LOGLVL, "* booked: " FID_F " (%s)", FID_P(&item->co_id),
	       m0_fid_type_getfid(&item->co_id)->ft_name);
	return true;
}

/** Tests if filesystem object is exactly the one requested for stats. */
static bool spiel__fs_test(const struct m0_conf_obj *fs_obj, void *ctx)
{
	struct _fs_stats_ctx *fsx = ctx;
	return m0_fid_eq(&fs_obj->co_id, &fsx->fx_fid);
}

/**
 * Updates filesystem stats by list item.
 */
static void spiel__fs_stats_ctx_update(const struct m0_fid  *proc_fid,
				       struct _fs_stats_ctx *fsx)
{
	struct m0_fop            *reply_fop = NULL;
	struct m0_ss_process_rep *reply_data;
	int                       rc;

	M0_PRE(fsx->fx_rc == 0);
	rc = spiel_process__health(fsx->fx_spc, proc_fid, &reply_fop);
	M0_LOG(SPIEL_LOGLVL, "* next:  rc = %d " FID_F " (%s)",
	       rc, FID_P(proc_fid),
	       m0_fid_type_getfid(proc_fid)->ft_name);
	if (rc >= M0_HEALTH_GOOD) {
		/* some real health returned, not error code */
		reply_data = spiel_process_reply_data(reply_fop);

		if (m0_addu64_will_overflow(reply_data->sspr_free_seg,
					    fsx->fx_free_seg) ||
		    m0_addu64_will_overflow(reply_data->sspr_total_seg,
					    fsx->fx_total_seg) ||
		    m0_addu64_will_overflow(reply_data->sspr_free_disk,
					    fsx->fx_free_disk) ||
		    m0_addu64_will_overflow(reply_data->sspr_total_disk,
					    fsx->fx_total_disk))
		{
			fsx->fx_rc = M0_ERR(-EOVERFLOW);
			goto leave;
		}

		fsx->fx_free_seg   += reply_data->sspr_free_seg;
		fsx->fx_total_seg  += reply_data->sspr_total_seg;
		fsx->fx_free_disk  += reply_data->sspr_free_disk;
		fsx->fx_total_disk += reply_data->sspr_total_disk;
	} else {
		/* error occurred */
		fsx->fx_rc = M0_ERR(rc);
	}
leave:
	if (reply_fop != NULL)
		m0_fop_put_lock(reply_fop);
}

/**
 * Determines whether process with given fid should update fs stats.
 *
 * Process, in case it hosts MDS or IOS, and is M0_NC_ONLINE to the moment of
 * call, should update collected fs stats.
 */
static int spiel__proc_is_to_update_stats(const struct m0_fid *proc_fid,
					  struct m0_confc     *confc,
					  uint32_t            *svc_count,
					  bool                *update)
{
	struct m0_conf_diter    it;
	struct m0_conf_obj     *proc_obj;
	struct m0_conf_service *svc;
	int                     rc;

	M0_ENTRY("proc_fid = "FID_F, FID_P(proc_fid));
	M0_PRE(m0_conf_fid_type(proc_fid) == &M0_CONF_PROCESS_TYPE);
	M0_PRE(svc_count != NULL);
	M0_PRE(update != NULL);

	*svc_count = 0;
	*update = false;
	rc = m0_confc_open_by_fid_sync(confc, proc_fid, &proc_obj);
	if (rc != 0)
		return M0_ERR(rc);
	rc = m0_conf_diter_init(&it, confc, proc_obj,
				M0_CONF_PROCESS_SERVICES_FID);
	if (rc != 0)
		goto obj_close;
	while (m0_conf_diter_next_sync(&it, NULL) > 0) {
		svc = M0_CONF_CAST(m0_conf_diter_result(&it), m0_conf_service);
		if (M0_IN(svc->cs_type, (M0_CST_IOS, M0_CST_MDS)))
			++ *svc_count;
	}
	if (*svc_count > 0) {
		/*
		 * There is no point to update process' HA state unless
		 * the one is expected to host the required services.
		 */
		rc = m0_conf_obj_ha_update(m0_ha_session_get(), proc_fid);
		if (rc != 0)
			goto diter_fini;
		*update = proc_obj->co_ha_state == M0_NC_ONLINE;
	}
diter_fini:
	m0_conf_diter_fini(&it);
obj_close:
	m0_confc_close(proc_obj);
	return M0_RC(rc);
}

M0_INTERNAL int m0_spiel__fs_stats_fetch(struct m0_spiel_core *spc,
					 const struct m0_fid  *fs_fid,
					 struct m0_fs_stats   *stats)
{
	struct _fs_stats_ctx  fsx;
	struct m0_fid_item   *proc;
	/* count of services in the process the stats to be updated by */
	uint32_t              svc_count;
	/* total count of IOS and MDS services in the filesystem */
	uint32_t              svc_total_in_fs = 0;
	/* count of services successfully polled and replied */
	uint32_t              svc_replied = 0;
	bool                  update_stats;
	int                   rc;

	M0_ENTRY();
	M0_PRE(spc != NULL);
	M0_PRE(spc->spc_confc != NULL);
	M0_PRE(stats != NULL);
	M0_PRE(fs_fid != NULL);
	M0_PRE(m0_conf_fid_type(fs_fid) == &M0_CONF_FILESYSTEM_TYPE);
	M0_PRE(m0_conf_fid_type(&spc->spc_profile) == &M0_CONF_PROFILE_TYPE);

	spiel__fs_stats_ctx_init(&fsx, spc, fs_fid, &M0_CONF_PROCESS_TYPE);
	/* walk along the filesystem nodes and get to process level */
	rc = SPIEL_CONF_DIR_ITERATE(spc->spc_confc, &spc->spc_profile, &fsx,
				    spiel__item_enlist, spiel__fs_test,
				    M0_CONF_FILESYSTEM_NODES_FID,
				    M0_CONF_NODE_PROCESSES_FID) ?:
		fsx.fx_rc;
	if (rc != 0)
		goto end;
	/* update stats by the list of found processes */
	m0_tl_for(m0_fids, &fsx.fx_items, proc) {
		rc = spiel__proc_is_to_update_stats(&proc->i_fid, spc->spc_confc,
						    &svc_count, &update_stats);
		if (rc != 0)
			goto end;
		M0_ASSERT(!update_stats || svc_count > 0);
		svc_total_in_fs += svc_count;
		if (!update_stats)
			continue;
		/*
		 * XXX We could improve performance by sending health requests
		 * asynchronously. Oh well.
		 */
		spiel__fs_stats_ctx_update(&proc->i_fid, &fsx);
		if (fsx.fx_rc != 0) {
			if (fsx.fx_rc == -EOVERFLOW) {
				svc_replied += svc_count;
				rc = M0_ERR(-EOVERFLOW);
				goto end;
			}
			M0_LOG(SPIEL_LOGLVL, "* fsx.fx_rc = %d with "FID_F,
			       fsx.fx_rc, FID_P(&proc->i_fid));
			/* unset error code for letting further processing */
			fsx.fx_rc = 0;
		} else {
			svc_replied += svc_count;
		}
		/**
		 * @todo Need to understand if it would make sense from
		 * consumer's standpoint to interrupt stats collection here on
		 * a network error got from m0_spiel_process_health().
		 */
	} m0_tl_endfor;
	/* report stats to consumer */
	*stats = (struct m0_fs_stats) {
		.fs_free_seg = fsx.fx_free_seg,
		.fs_total_seg = fsx.fx_total_seg,
		.fs_free_disk = fsx.fx_free_disk,
		.fs_total_disk = fsx.fx_total_disk,
		.fs_svc_total = svc_total_in_fs,
		.fs_svc_replied = svc_replied,
	};
end:
	spiel__fs_stats_ctx_fini(&fsx);
	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_filesystem_stats_fetch);

int m0_spiel_filesystem_stats_fetch(struct m0_spiel     *spl,
				    const struct m0_fid *fs_fid,
				    struct m0_fs_stats  *stats)
{
	return m0_spiel__fs_stats_fetch(&spl->spl_core, fs_fid, stats);
}

int m0_spiel_confstr(struct m0_spiel *spl, char **out)
{
	char *confstr;
	int   rc;

	rc = m0_conf_cache_to_string(&spiel_confc(spl)->cc_cache, &confstr,
				     false);
	if (rc != 0)
		return M0_ERR(rc);
	*out = m0_strdup(confstr);
	if (*out == NULL)
		rc = M0_ERR(-ENOMEM);
	m0_confx_string_free(confstr);
	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_confstr);

/** @} */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
