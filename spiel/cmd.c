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
#include "lib/time.h"         /* M0_TIME_IMMEDIATELY, m0_time_from_now */
#include "conf/helpers.h"     /* service_name_dup */
#include "conf/obj.h"
#include "conf/obj_ops.h"     /* M0_CONF_DIRNEXT, m0_conf_obj_get */
#include "conf/confc.h"
#include "conf/diter.h"
#include "fid/fid_list.h"
#include "fop/fop.h"
#include "rpc/rpc_machine.h"
#include "rpc/link.h"
#include "rpc/rpclib.h"         /* m0_rpc_post_sync */
#include "sss/device_fops.h"
#include "sns/cm/cm.h"          /* m0_sns_cm_op */
#include "sns/cm/trigger_fop.h"
#include "sss/ss_fops.h"
#include "sss/process_fops.h"
#include "spiel/spiel.h"
#include "spiel/cmd_internal.h"

/**
 * @addtogroup spiel-api-fspec-intr
 * @{
 */

M0_TL_DESCR_DEFINE(spiel_string, "list of endpoints",
		   static, struct spiel_string_entry, sse_link, sse_magic,
		   M0_STATS_MAGIC, M0_STATS_HEAD_MAGIC);
M0_TL_DEFINE(spiel_string, static, struct spiel_string_entry);


static bool _filter_svc(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE;
}

static bool _filter_controller(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_CONTROLLER_TYPE;
}

static int spiel_node_process_endpoint_add(struct m0_spiel    *spl,
				           struct m0_conf_obj *node,
				           struct m0_tl       *list)
{
	struct spiel_string_entry *entry;
	struct m0_confc           *confc = &spl->spl_rconfc.rc_confc;
	struct m0_conf_diter       it;
	struct m0_conf_process    *p;
	struct m0_conf_service    *svc;
	int                        rc;

	M0_ENTRY("conf_node: %p", &node);

	rc = m0_conf_diter_init(&it, confc, node,
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
static int spiel_endpoints_for_device_generic(struct m0_spiel     *spl,
					      const struct m0_fid *device_fid,
					      struct m0_tl        *list)
{
	struct m0_confc           *confc = &spl->spl_rconfc.rc_confc;
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
				spl->spl_profile,
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
				spiel_node_process_endpoint_add(spl, node_obj,
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

	rc = m0_rpc_link_init(rlink, rmachine, remote_ep,
			      SPIEL_MAX_RPCS_IN_FLIGHT);
	if (rc == 0) {
		conn_timeout = m0_time_from_now(SPIEL_CONN_TIMEOUT, 0);
		rc = m0_rpc_link_connect_sync(rlink, conn_timeout) ?:
		     m0_rpc_post_with_timeout_sync(cmd_fop,
						    &rlink->rlk_sess,
						    NULL,
						    M0_TIME_IMMEDIATELY,
						    timeout);

		/* disconnect should be called even if connect failed */
		conn_timeout = m0_time_from_now(SPIEL_CONN_TIMEOUT, 0);
		m0_rpc_link_disconnect_sync(rlink, conn_timeout);
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

static bool _all_stop(const struct m0_conf_obj *obj M0_UNUSED)
{
	return true;
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
	       (m0_conf_diter_next_sync(&it, _all_stop)) == M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		/* Pin obj to protect it from removal while being in use */
		m0_mutex_lock(&confc->cc_lock);
		m0_conf_obj_get(obj);
		m0_mutex_unlock(&confc->cc_lock);
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
		rc = spiel_cmd_send(spl->spl_rmachine, ss_ep, fop,
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
	     spiel_sss_reply_data(fop)->ssr_rc;

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

	return M0_RC(spiel_svc_generic_handler(spl, svc_fid, M0_SERVICE_HEALTH));
}
M0_EXPORTED(m0_spiel_service_health);

int m0_spiel_service_quiesce(struct m0_spiel     *spl,
		             const struct m0_fid *svc_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(svc_fid));

	return M0_RC(spiel_svc_generic_handler(spl, svc_fid, M0_SERVICE_QUIESCE));
}
M0_EXPORTED(m0_spiel_service_quiesce);


/****************************************************/
/*                     Devices                      */
/****************************************************/

static int spiel_device_command_fop_send(struct m0_spiel     *spl,
					 const char          *endpoint,
					 const struct m0_fid *dev_fid,
					 int                  cmd)
{
	struct m0_fop                *fop;
	struct m0_sss_device_fop_rep *rep;
	int                           rc;

	fop  = m0_sss_device_fop_create(spl->spl_rmachine, cmd, dev_fid);
	if (fop == NULL)
		return M0_RC(-ENOMEM);

	rc = spiel_cmd_send(spl->spl_rmachine, endpoint, fop,
			    cmd == M0_DEVICE_FORMAT ?
				   SPIEL_DEVICE_FORMAT_TIMEOUT :
				   M0_TIME_NEVER);
	if (rc == 0) {
		rep = m0_sss_fop_to_dev_rep(
				m0_rpc_item_to_fop(fop->f_item.ri_reply));
		rc = rep->ssdp_rc;
		m0_fop_put_lock(fop);
	} else {
		m0_fop_fini(fop);
		m0_free(fop);
	}
	return M0_RC(rc);
}

/**
 * Search all processes that have IO services, fetch their endpoints and send
 * command FOPs.
 */
static int spiel_device_command_send(struct m0_spiel     *spl,
				     const struct m0_fid *dev_fid,
				     int                  cmd)
{
	struct m0_tl               endpoints;
	struct spiel_string_entry *ep;
	int                        rc;

	M0_ENTRY();

	if (m0_conf_fid_type(dev_fid) != &M0_CONF_DISK_TYPE)
		return M0_ERR(-EINVAL);

	spiel_string_tlist_init(&endpoints);

	rc = spiel_endpoints_for_device_generic(spl, dev_fid, &endpoints);
	if (rc == 0 && spiel_string_tlist_is_empty(&endpoints))
		rc = M0_ERR(-ENOENT);

	if (rc == 0)
		m0_tl_teardown(spiel_string, &endpoints, ep) {
			rc = rc ?: spiel_device_command_fop_send(spl,
					ep->sse_string, dev_fid, cmd);
			m0_free((char *)ep->sse_string);
			m0_free(ep);
		}

	spiel_string_tlist_fini(&endpoints);

	return M0_RC(rc);
}

int m0_spiel_device_attach(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(dev_fid));

	return M0_RC(spiel_device_command_send(spl, dev_fid, M0_DEVICE_ATTACH));
}
M0_EXPORTED(m0_spiel_device_attach);

int m0_spiel_device_detach(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(dev_fid));

	return M0_RC(spiel_device_command_send(spl, dev_fid, M0_DEVICE_DETACH));
}
M0_EXPORTED(m0_spiel_device_detach);

int m0_spiel_device_format(struct m0_spiel *spl, const struct m0_fid *dev_fid)
{
	M0_ENTRY("spl %p svc_fid "FID_F, spl, FID_P(dev_fid));

	return M0_RC(spiel_device_command_send(spl, dev_fid, M0_DEVICE_FORMAT));
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

	m0_confc_close(&process->pc_obj);

	if (rc == 0)
		rc = spiel_cmd_send(spl->spl_rmachine, process->pc_endpoint,
				    fop, M0_TIME_NEVER);
	if (rc != 0) {
		m0_fop_fini(fop);
		m0_free(fop);
	}

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
static int spiel_process_command_execute(struct m0_spiel     *spl,
					 const struct m0_fid *proc_fid,
					 int                  cmd,
					 struct m0_fop      **reply)
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

	return M0_RC(spiel_process_command_execute(spl, proc_fid,
						   M0_PROCESS_STOP, NULL));
}
M0_EXPORTED(m0_spiel_process_stop);

int m0_spiel_process_reconfig(struct m0_spiel     *spl,
			      const struct m0_fid *proc_fid)
{
	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	return M0_RC(spiel_process_command_execute(spl, proc_fid,
						   M0_PROCESS_RECONFIG, NULL));
}
M0_EXPORTED(m0_spiel_process_reconfig);

static int spiel_process__health(struct m0_spiel      *spl,
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
	rc = spiel_process_command_execute(spl, proc_fid,
					   M0_PROCESS_HEALTH,
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
	return M0_RC(spiel_process__health(spl, proc_fid, NULL));
}
M0_EXPORTED(m0_spiel_process_health);

int m0_spiel_process_quiesce(struct m0_spiel     *spl,
			     const struct m0_fid *proc_fid)
{
	M0_ENTRY();

	if (m0_conf_fid_type(proc_fid) != &M0_CONF_PROCESS_TYPE)
		return M0_ERR(-EINVAL);

	return M0_RC(spiel_process_command_execute(spl, proc_fid,
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

static int spiel_sns_fop_fill_and_send(struct m0_spiel        *spl,
				       struct m0_fop          *fop,
				       struct m0_conf_service *svc,
				       enum m0_sns_cm_op       op)
{
	struct trigger_fop *treq = m0_fop_data(fop);
	struct m0_conf_process *p = M0_CONF_CAST(
					m0_conf_obj_grandparent(&svc->cs_obj),
					m0_conf_process);

	M0_ENTRY("fop %p conf_service %p", fop, svc);
	treq->op = op;
	return M0_RC(spiel_cmd_send(spl->spl_rmachine, p->pc_endpoint,
				    fop, M0_TIME_NEVER));
}

static int spiel_sns_status_reply(int                           rc,
				  const struct m0_fop          *fop,
				  const struct m0_conf_service *svc,
				  struct m0_spiel_sns_status   *status)
{
	struct m0_sns_status_rep_fop *reply;

	M0_ENTRY("fop %p", fop);
	M0_PRE(status != NULL);
	status->sss_fid = svc->cs_obj.co_id;
	if (rc == 0) {
		reply = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
		status->sss_state = reply->ssr_state;
		status->sss_progress = reply->ssr_progress;
	} else {
		status->sss_state = rc;
	}
	return M0_RC(0);
}

/** pool stats collection context */
struct _pool_cmd_ctx {
	int              pl_rc;
	struct m0_spiel *pl_spl;           /*< spiel instance           */
	struct m0_confc *pl_confc;         /*< confc instance           */
	struct m0_tl     pl_sdevs_fid;     /*< storage devices fid list */
	struct m0_tl     pl_services_fid;  /*< services fid list        */
};

static int spiel_pool_device_collect(struct _pool_cmd_ctx *ctx,
				     struct m0_conf_objv  *diskv)
{
	struct m0_conf_obj  *obj_disk;
	struct m0_conf_disk *disk;
	int                  rc;


	rc = m0_confc_open_by_fid_sync(ctx->pl_confc,
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

	rc = m0_conf_diter_init(&it, ctx->pl_confc,
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
				 struct m0_spiel      *spl)
{
	M0_SET0(ctx);
	ctx->pl_spl = spl;
	ctx->pl_confc = &spl->spl_rconfc.rc_confc;
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
	if (m0_conf_fid_type(&item->co_id) != &M0_CONF_SERVICE_TYPE)
		return true;

	service = M0_CONF_CAST(item, m0_conf_service);

	if (service->cs_type == M0_CST_IOS &&
	    spiel__pool_service_has_sdev(pool_ctx, item) == true)
		pool_ctx->pl_rc = spiel_stats_item_add(
						&pool_ctx->pl_services_fid,
						&item->co_id);
	return true;
}

static int spiel__pool_cmd_send(struct _pool_cmd_ctx        *ctx,
				 struct m0_fid              *service_fid,
				 const enum m0_sns_cm_op     cmd,
				 int                         index,
				 struct m0_spiel_sns_status *statuses)
{
	struct m0_conf_obj     *svc_obj;
	struct m0_conf_service *svc;
	struct m0_fop          *fop = NULL;
	int                     rc;

	rc = m0_confc_open_by_fid_sync(ctx->pl_confc, service_fid, &svc_obj);
	if(rc != 0)
		return M0_ERR(rc);

	svc = M0_CONF_CAST(svc_obj, m0_conf_service);
	rc = m0_sns_cm_trigger_fop_alloc(ctx->pl_spl->spl_rmachine, cmd, &fop)?:
	     spiel_sns_fop_fill_and_send(ctx->pl_spl, fop, svc, cmd);
	if (M0_IN(cmd, (SNS_REPAIR_STATUS, SNS_REBALANCE_STATUS)))
		rc = spiel_sns_status_reply(rc, fop, svc, &statuses[index]);

	m0_confc_close(svc_obj);

	return M0_RC(rc);
}

static bool _filter_objv(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE;
}

static int spiel_pool__device_collection_fill(struct _pool_cmd_ctx *ctx,
					      const struct m0_fid  *pool_fid)
{
	struct m0_confc      *confc = &ctx->pl_spl->spl_rconfc.rc_confc;
	struct m0_conf_obj   *pool_obj = NULL;
	struct m0_conf_obj   *obj;
	struct m0_conf_objv  *objv;
	struct m0_conf_diter  it;
	int                   rc;
	M0_ENTRY();

	rc = SPIEL_CONF_OBJ_FIND(confc, &ctx->pl_spl->spl_profile, pool_fid,
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
		objv = M0_CONF_CAST(obj, m0_conf_objv);
		if (objv == NULL ||
		    m0_conf_obj_type(objv->cv_real) != &M0_CONF_DISK_TYPE)
			continue;
		/* Pin obj to protect it from removal while being in use */
		m0_mutex_lock(&confc->cc_lock);
		m0_conf_obj_get(obj);
		m0_mutex_unlock(&confc->cc_lock);
		rc = spiel_pool_device_collect(ctx, objv);
		M0_ASSERT(rc == 0);
		m0_mutex_lock(&confc->cc_lock);
		m0_conf_obj_put(obj);
		m0_mutex_unlock(&confc->cc_lock);
	}
	m0_conf_diter_fini(&it);

leave:
	m0_confc_close(pool_obj);
	return M0_RC(rc);
}

static int spiel_pool_generic_handler(struct m0_spiel             *spl,
				      const struct m0_fid         *pool_fid,
				      const enum m0_sns_cm_op      cmd,
				      struct m0_spiel_sns_status **statuses)
{
	int                   rc;
	int                   service_count;
	int                   index;
	struct _pool_cmd_ctx  ctx;
	struct m0_fid_item   *si;
	bool                  cmd_status;

	M0_ENTRY();
	M0_PRE(pool_fid != NULL);

	if (m0_conf_fid_type(pool_fid) != &M0_CONF_POOL_TYPE)
		return M0_ERR(-EINVAL);

	spiel__pool_ctx_init(&ctx, spl);

	rc = spiel_pool__device_collection_fill(&ctx, pool_fid) ?:
	     SPIEL_CONF_DIR_ITERATE(ctx.pl_confc, &spl->spl_profile, &ctx,
				    spiel__pool_service_select, NULL,
				    M0_CONF_FILESYSTEM_NODES_FID,
				    M0_CONF_NODE_PROCESSES_FID,
				    M0_CONF_PROCESS_SERVICES_FID);
	if (rc != 0)
		goto leave;
	if (ctx.pl_rc != 0) {
		rc = ctx.pl_rc;
		goto leave;
	}

	cmd_status = M0_IN(cmd, (SNS_REPAIR_STATUS, SNS_REBALANCE_STATUS));
	service_count = m0_fids_tlist_length(&ctx.pl_services_fid);

	if (cmd_status) {
		M0_ALLOC_ARR(*statuses, service_count);
		if (*statuses == NULL) {
			rc = -ENOMEM;
			goto leave;
		}
	}

	index = 0;
	m0_tl_for(m0_fids, &ctx.pl_services_fid, si) {
		M0_ASSERT(index < service_count);
		rc = spiel__pool_cmd_send(&ctx, &si->i_fid, cmd, index,
					  cmd_status ? *statuses : NULL);
		++index;
		if (rc != 0)
			break;
	} m0_tl_endfor;

	if (rc == 0 && cmd_status)
		rc = index;

leave:
	spiel__pool_ctx_fini(&ctx);
	return M0_RC(rc);
}

int m0_spiel_pool_repair_start(struct m0_spiel     *spl,
			       const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(spl, pool_fid, SNS_REPAIR,
						NULL));
}
M0_EXPORTED(m0_spiel_pool_repair_start);

int m0_spiel_pool_repair_continue(struct m0_spiel     *spl,
				  const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(spl, pool_fid,
						SNS_REPAIR, NULL));
}
M0_EXPORTED(m0_spiel_pool_repair_continue);

int m0_spiel_pool_repair_quiesce(struct m0_spiel     *spl,
				 const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(spl, pool_fid,
						SNS_REPAIR_QUIESCE, NULL));
}
M0_EXPORTED(m0_spiel_pool_repair_quiesce);

int m0_spiel_pool_repair_status(struct m0_spiel             *spl,
				const struct m0_fid         *pool_fid,
				struct m0_spiel_sns_status **statuses)
{
	int rc;

	M0_ENTRY();
	M0_PRE(statuses != NULL);
	rc = spiel_pool_generic_handler(spl, pool_fid, SNS_REPAIR_STATUS,
					statuses);
	return rc >= 0 ? M0_RC(rc) : M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_pool_repair_status);

int m0_spiel_pool_rebalance_start(struct m0_spiel     *spl,
				  const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(spl, pool_fid, SNS_REBALANCE,
						NULL));
}
M0_EXPORTED(m0_spiel_pool_rebalance_start);

int m0_spiel_pool_rebalance_continue(struct m0_spiel     *spl,
				     const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(spl, pool_fid,
						SNS_REBALANCE, NULL));
}
M0_EXPORTED(m0_spiel_pool_rebalance_continue);

int m0_spiel_pool_rebalance_quiesce(struct m0_spiel     *spl,
				    const struct m0_fid *pool_fid)
{
	M0_ENTRY();
	return M0_RC(spiel_pool_generic_handler(spl, pool_fid,
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
	rc = spiel_pool_generic_handler(spl, pool_fid, SNS_REPAIR_STATUS,
					statuses);
	return rc >= 0 ? M0_RC(rc) : M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_pool_rebalance_status);

/****************************************************/
/*                    Filesystem                    */
/****************************************************/

static void spiel__fs_stats_ctx_init(struct _fs_stats_ctx *fsx,
			      struct m0_spiel *spl,
			      const struct m0_fid *fs_fid,
			      const struct m0_conf_obj_type *item_type)
{
	M0_PRE(fs_fid != NULL);
	M0_SET0(fsx);
	fsx->fx_spl = spl;
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
	if (m0_conf_fid_type(&item->co_id) != fsx->fx_type)
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
static void spiel__fs_stats_ctx_update(struct m0_fid_item   *si,
				       struct _fs_stats_ctx *fsx)
{
	struct m0_fop            *reply_fop = NULL;
	struct m0_ss_process_rep *reply_data;
	int                       rc;

	M0_PRE(fsx->fx_rc == 0);
	rc = spiel_process__health(fsx->fx_spl, &si->i_fid, &reply_fop);
	M0_LOG(SPIEL_LOGLVL, "* next:  rc = %d " FID_F " (%s)",
	       rc, FID_P(&si->i_fid),
	       m0_fid_type_getfid(&si->i_fid)->ft_name);
	if (rc >= M0_HEALTH_GOOD) {
		/* some real health returned, not error code */
		reply_data = spiel_process_reply_data(reply_fop);
		if (m0_addu64_will_overflow(reply_data->sspr_free,
					    fsx->fx_free)) {
			fsx->fx_rc = M0_ERR(-EOVERFLOW);
			goto leave;
		}
		fsx->fx_free  += reply_data->sspr_free;
		if (m0_addu64_will_overflow(reply_data->sspr_total,
					    fsx->fx_total)) {
			fsx->fx_rc = M0_ERR(-EOVERFLOW);
			goto leave;
		}
		fsx->fx_total += reply_data->sspr_total;
	} else {
		/* error occurred */
		fsx->fx_rc = M0_ERR(rc);
	}
leave:
	if (reply_fop != NULL)
		m0_fop_put_lock(reply_fop);
}

int m0_spiel_filesystem_stats_fetch(struct m0_spiel     *spl,
				    const struct m0_fid *fs_fid,
				    struct m0_fs_stats  *stats)
{
	struct m0_confc      *confc;
	struct m0_fid        *profile;
	struct _fs_stats_ctx  fsx;
	struct m0_fid_item    *si;
	int                   rc;

	M0_ENTRY();
	M0_PRE(spl != NULL);
	M0_PRE(stats != NULL);
	M0_PRE(fs_fid != NULL);
	M0_PRE(m0_conf_fid_type(fs_fid) == &M0_CONF_FILESYSTEM_TYPE);
	M0_PRE(m0_conf_fid_type(&spl->spl_profile) == &M0_CONF_PROFILE_TYPE);

	confc   = &spl->spl_rconfc.rc_confc;
	profile = &spl->spl_profile;

	spiel__fs_stats_ctx_init(&fsx, spl, fs_fid, &M0_CONF_PROCESS_TYPE);
	/* walk along the filesystem nodes and get to process level */
	rc = SPIEL_CONF_DIR_ITERATE(confc, profile, &fsx,
				    spiel__item_enlist, spiel__fs_test,
				    M0_CONF_FILESYSTEM_NODES_FID,
				    M0_CONF_NODE_PROCESSES_FID);
	if (rc != 0)
		goto leave;
	/* see if there were issues during conf iteration */
	if (fsx.fx_rc != 0) {
		rc = fsx.fx_rc;
		goto leave;
	}
	/* update stats by the list of found items */
	m0_tl_for(m0_fids, &fsx.fx_items, si) {
		spiel__fs_stats_ctx_update(si, &fsx);
		if (fsx.fx_rc != 0) {
			if (fsx.fx_rc == -EOVERFLOW) {
				rc = fsx.fx_rc;
				goto leave;
			}
			M0_LOG(SPIEL_LOGLVL, "* fsx.fx_rc = %d with "FID_F,
			       fsx.fx_rc, FID_P(&si->i_fid));
			/* unset error code for letting further processing */
			fsx.fx_rc = 0;
		}
		/**
		 * @todo Need to understand if it would make sense from
		 * consumer's standpoint to interrupt stats collection here on
		 * a network error got from m0_spiel_process_health().
		 */
	} m0_tl_endfor;
	/* report stats to consumer */
	*stats = (struct m0_fs_stats) {
		.fs_free = fsx.fx_free,
		.fs_total = fsx.fx_total,
	};

	spiel__fs_stats_ctx_fini(&fsx);
	return M0_RC(rc);

leave:
	spiel__fs_stats_ctx_fini(&fsx);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_filesystem_stats_fetch);

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
