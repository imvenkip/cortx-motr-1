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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 11-Feb-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/finject.h"       /* M0_FI_ENABLED */
#include "lib/memory.h"        /* m0_alloc, M0_ALLOC_ARR */
#include "lib/string.h"        /* m0_strdup, m0_strings_dup */
#include "lib/tlist.h"
#include "conf/cache.h"
#include "conf/flip_fop.h"
#include "conf/load_fop.h"
#include "conf/obj.h"
#include "conf/obj_ops.h"
#include "conf/objs/common.h"
#include "conf/onwire.h"       /* arr_fid */
#include "conf/onwire_xc.h"    /* m0_confx_xc */
#include "conf/preload.h"      /* m0_confx_free, m0_confx_to_string */
#include "rpc/link.h"
#include "rpc/rpclib.h"        /* m0_rpc_client_connect */
#include "spiel/spiel.h"
#include "spiel/conf_mgmt.h"

/**
 * @addtogroup spiel-api-fspec-intr
 * @{
 */

M0_TL_DESCR_DECLARE(rpcbulk, M0_EXTERN);

/* tlists and tlist APIs referred from rpc layer. */
static const struct arr_fid EMPTY_ARR_FID_PTR = { .af_count = 0,
						  .af_elems = NULL };

struct spiel_conf_param {
	const struct m0_fid            *scp_fid;
	const struct m0_conf_obj_type  *scp_type;
	struct m0_conf_obj            **scp_obj;
};

#define SPIEL_CONF_CHECK(cache, ...)                                     \
	spiel_conf_parameter_check(cache, (struct spiel_conf_param []) { \
						__VA_ARGS__,             \
						{ NULL, NULL, NULL }, });

static int spiel_root_add(struct m0_spiel_tx *tx)
{
	int                  rc;
	struct m0_conf_obj  *obj;
	struct m0_conf_root *root;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);
	rc = m0_conf_obj_find(&tx->spt_cache, &M0_CONF_ROOT_FID, &obj);
	if (obj != NULL && obj->co_status != M0_CS_MISSING)
		rc = M0_ERR(-EEXIST);
	if (rc == 0) {
		root = M0_CONF_CAST(obj, m0_conf_root);
		root->rt_verno = tx->spt_version;
		rc = dir_new(&tx->spt_cache,
			     &root->rt_obj.co_id,
			     &M0_CONF_ROOT_PROFILES_FID,
			     &M0_CONF_ROOT_TYPE,
			     &EMPTY_ARR_FID_PTR,
			     &root->rt_profiles);
		obj->co_status = M0_CS_READY;
	}
	m0_mutex_unlock(&tx->spt_lock);

	return M0_RC(rc);
}

/**
 * Overrides configuration version number originally set during
 * m0_spiel_tx_open() and stored in root object.
 */
static int spiel_root_ver_update(struct m0_spiel_tx *tx, uint64_t verno)
{
	int                  rc;
	struct m0_conf_obj  *obj;
	struct m0_conf_root *root;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);
	rc = m0_conf_obj_find(&tx->spt_cache, &M0_CONF_ROOT_FID, &obj);
	if (obj == NULL || obj->co_status == M0_CS_MISSING)
		rc = M0_ERR(-ENOENT);
	if (rc == 0) {
		root = M0_CONF_CAST(obj, m0_conf_root);
		root->rt_verno = verno;
		tx->spt_version = verno;
	}
	m0_mutex_unlock(&tx->spt_lock);

	return M0_RC(rc);
}

void m0_spiel_tx_open(struct m0_spiel    *spiel,
		      struct m0_spiel_tx *tx)
{
	M0_ENTRY();

	tx->spt_spiel = spiel;
	tx->spt_buffer = NULL;
	m0_mutex_init(&tx->spt_lock);
	m0_conf_cache_init(&tx->spt_cache, &tx->spt_lock);

	tx->spt_version = m0_rconfc_ver_max_read(&spiel->spl_rconfc);
	M0_ASSERT(tx->spt_version != M0_CONF_VER_UNKNOWN);
	++tx->spt_version;

	spiel_root_add(tx);

	M0_LEAVE();
}
M0_EXPORTED(m0_spiel_tx_open);

/**
 *  Check cache for completeness:
 *  each element has state M0_CS_READY and
 *  has real parent (if last need by obj type)
 */
static int spiel_cache_check(struct m0_spiel_tx *tx)
{
	struct m0_conf_obj   *obj;
	struct m0_conf_obj   *obj_parent;
	struct m0_conf_cache *cache = &tx->spt_cache;

	M0_ENTRY();

	m0_tl_for(m0_conf_cache, &cache->ca_registry, obj) {
		if (m0_conf_obj_type(obj) != &M0_CONF_DIR_TYPE) {
			/* Check status*/
			if (obj->co_status != M0_CS_READY)
				return M0_ERR(-EBUSY);

			if (m0_conf_obj_type(obj) != &M0_CONF_ROOT_TYPE) {
				/* Check parent */
				if (obj->co_parent == NULL)
					return M0_ERR(-ENOENT);
				/* Check parent in cache */
				obj_parent = m0_conf_cache_lookup(cache,
							&obj->co_parent->co_id);
				if (obj_parent == NULL ||
				    obj_parent != obj->co_parent)
					return M0_ERR(-ENOENT);
			}
		}
	} m0_tl_endfor;

	return M0_RC(0);
}

/**
 * Frees Spiel context without completing the transaction.
 */
void m0_spiel_tx_close(struct m0_spiel_tx *tx)
{
	M0_ENTRY();

	/*
	 * Directories and cache objects are mixed in conf cache ca_registry
	 * list, but finalization of conf object will fail if it still
	 * presents in some directory. So delete all directories first.
	 */
	m0_conf_cache_dir_clean(&tx->spt_cache);

	m0_conf_cache_fini(&tx->spt_cache);
	m0_mutex_fini(&tx->spt_lock);

	M0_LEAVE();
}
M0_EXPORTED(m0_spiel_tx_close);

static bool spiel_load_cmd_invariant(struct m0_spiel_load_command *cmd)
{
	return _0C(cmd != NULL) &&
	       _0C(cmd->slc_load_fop.f_type == &m0_fop_conf_load_fopt);
}

/**
 * Finalizes a FOP for Spiel Load command.
 * @pre spiel_load_cmd_invariant(spiel_cmd)
 */
static void spiel_load_fop_fini(struct m0_spiel_load_command *spiel_cmd)
{
	M0_PRE(spiel_load_cmd_invariant(spiel_cmd));
	m0_rpc_bulk_fini(&spiel_cmd->slc_rbulk);
	m0_fop_fini(&spiel_cmd->slc_load_fop);
}

/**
 * Release a FOP for Spiel Load command.
 * @pre spiel_load_cmd_invariant(spiel_cmd)
 */
static void spiel_load_fop_release(struct m0_ref *ref)
{
	struct m0_spiel_load_command *spiel_cmd;
	struct m0_fop                *fop;

	fop = container_of(ref, struct m0_fop, f_ref);
	spiel_cmd = container_of(fop, struct m0_spiel_load_command,
				 slc_load_fop);
	spiel_load_fop_fini(spiel_cmd);
}

/**
 * Initializes a FOP for Spiel Load command.
 * @pre spiel_cmd != NULL.
 * @pre tx != NULL.
 * @post spiel_load_cmd_invariant(spiel_cmd)
 */
static int spiel_load_fop_init(struct m0_spiel_load_command *spiel_cmd,
			       struct m0_spiel_tx           *tx)
{
	int                      rc;
	struct m0_fop_conf_load *conf_fop;

	M0_PRE(spiel_cmd != NULL);
	M0_PRE(tx != NULL);

	m0_fop_init(&spiel_cmd->slc_load_fop, &m0_fop_conf_load_fopt, NULL,
		    spiel_load_fop_release);
	rc = m0_fop_data_alloc(&spiel_cmd->slc_load_fop);

	if (rc == 0) {
		/* Fill Spiel Conf FOP specific data*/
		conf_fop = m0_conf_fop_to_load_fop(&spiel_cmd->slc_load_fop);
		conf_fop->clf_version = tx->spt_version;
		conf_fop->clf_tx_id = (uint64_t)tx;
		m0_rpc_bulk_init(&spiel_cmd->slc_rbulk);
		M0_POST(spiel_load_cmd_invariant(spiel_cmd));
	}

	return M0_RC(rc);
}

static int spiel_load_fop_create(struct m0_spiel_tx           *tx,
				 struct m0_spiel_load_command *spiel_cmd)
{
	int                     rc;
	struct m0_rpc_bulk_buf *rbuf;
	struct m0_net_domain   *nd;
	int                     i;
	int                     segs_nr;
	m0_bcount_t             seg_size;
	m0_bcount_t             size = strlen(tx->spt_buffer)+1;


	rc = spiel_load_fop_init(spiel_cmd, tx);
	if (rc != 0) {
		return M0_ERR(rc);
	}

	/* Fill RPC Bulk part of Spiel FOM */
	nd = tx->spt_spiel->spl_rmachine->rm_tm.ntm_dom;
	seg_size = m0_net_domain_get_max_buffer_segment_size(nd);
	/*
	 * Calculate number of segments for given data size.
	 * Segments number is rounded up.
	 */
	segs_nr = (size + seg_size - 1) / seg_size;
	rc = m0_rpc_bulk_buf_add(&spiel_cmd->slc_rbulk, segs_nr, nd,
				 NULL, &rbuf);
	for (i = 0; i < segs_nr; ++i) {
		m0_rpc_bulk_buf_databuf_add(rbuf,
				    tx->spt_buffer + i*seg_size,
				    min_check(size, seg_size), i, nd);
		size -= seg_size;
	}
	rbuf->bb_nbuf->nb_qtype = M0_NET_QT_PASSIVE_BULK_SEND;

	return rc;
}

static int spiel_load_fop_send(struct m0_spiel_tx           *tx,
			       struct m0_spiel_load_command *spiel_cmd)
{
	int                          rc;
	struct m0_fop_conf_load     *load_fop;
	struct m0_fop               *rep;
	struct m0_fop_conf_load_rep *load_rep;

	rc = spiel_load_fop_create(tx, spiel_cmd);
	if (rc != 0)
		return rc;

	load_fop = m0_conf_fop_to_load_fop(&spiel_cmd->slc_load_fop);
	rc = m0_rpc_bulk_store(&spiel_cmd->slc_rbulk,
			       &spiel_cmd->slc_connect,
			       &load_fop->clf_desc,
			       &m0_rpc__buf_bulk_cb);
	if (rc != 0)
		return rc;

	rc = m0_rpc_post_sync(&spiel_cmd->slc_load_fop,
			      &spiel_cmd->slc_session,
			      NULL,
			      0/*M0_TIME_NEVER*/);
	rep = rc == 0 ? m0_rpc_item_to_fop(
				spiel_cmd->slc_load_fop.f_item.ri_reply)
			: NULL;
	load_rep = rep != NULL ? m0_conf_fop_to_load_fop_rep(rep) : NULL;

	if (load_rep != NULL) {
		rc = load_rep->clfr_rc;
		spiel_cmd->slc_version = load_rep->clfr_version;
	} else {
		rc = M0_ERR(-ENOENT);
	}

	m0_fop_put_lock(&spiel_cmd->slc_load_fop);

	return rc;
}

static void spiel_flip_fop_release(struct m0_ref *ref)
{
	struct m0_fop *fop;
	fop = container_of(ref, struct m0_fop, f_ref);
	m0_fop_fini(fop);
}

static int spiel_flip_fop_send(struct m0_spiel_tx           *tx,
			       struct m0_spiel_load_command *spiel_cmd)
{
	int                          rc;
	struct m0_fop_conf_flip     *flip_fop;
	struct m0_fop               *rep;
	struct m0_fop_conf_flip_rep *flip_rep;

	M0_PRE(spiel_cmd != NULL);

	M0_ENTRY();

	m0_fop_init(&spiel_cmd->slc_flip_fop, &m0_fop_conf_flip_fopt, NULL,
		    spiel_flip_fop_release);

	rc = m0_fop_data_alloc(&spiel_cmd->slc_flip_fop);

	if (rc == 0) {
		flip_fop = m0_conf_fop_to_flip_fop(&spiel_cmd->slc_flip_fop);
		flip_fop->cff_prev_version = spiel_cmd->slc_version;
		flip_fop->cff_next_version = tx->spt_version;
		flip_fop->cff_tx_id = (uint64_t)tx;
	}

	if (rc == 0)
		rc = m0_rpc_post_sync(&spiel_cmd->slc_flip_fop,
				      &spiel_cmd->slc_session,
				      NULL, 0);

	rep = rc == 0 ? m0_rpc_item_to_fop(
				spiel_cmd->slc_flip_fop.f_item.ri_reply)
			: NULL;

	flip_rep = rep != NULL ? m0_conf_fop_to_flip_fop_rep(rep) : NULL;
	rc = flip_rep != NULL ? flip_rep->cffr_rc : M0_ERR(-ENOENT);
	m0_fop_put_lock(&spiel_cmd->slc_flip_fop);

	return M0_RC(rc);
}

int m0_spiel_tx_commit_forced(struct m0_spiel_tx *tx, bool forced,
			      uint64_t ver_forced, uint32_t *rquorum)
{
	int                           rc;
	struct m0_spiel_load_command *spiel_cmd   = NULL;
	uint32_t                      confd_count = 0;
	uint32_t                      quorum      = 0;
	uint32_t                      idx;

	enum {
		MAX_RPCS_IN_FLIGHT = 2
	};

	M0_ENTRY();

	/*
	 * in case ver_forced value is other than M0_CONF_VER_UNKNOWN, override
	 * transaction version number with ver_forced, otherwise leave the one
	 * intact
	 */
	if (ver_forced != M0_CONF_VER_UNKNOWN) {
		rc = spiel_root_ver_update(tx, ver_forced);
		M0_ASSERT(rc == 0);
	}

	rc = spiel_cache_check(tx) ?:
	     m0_conf_cache_to_string(&tx->spt_cache, &tx->spt_buffer);
	if (rc != 0)
		goto tx_fini;

	for (confd_count = 0; tx->spt_spiel->spl_confd_eps[confd_count] != NULL;
	     ++confd_count)
		; /* collect the number of confd addresses */

	M0_ALLOC_ARR(spiel_cmd, confd_count);
	if (spiel_cmd == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto tx_fini;
	}

	for (idx = 0; idx < confd_count; ++idx) {
		spiel_cmd[idx].slc_status =
			m0_rpc_client_connect(&spiel_cmd[idx].slc_connect,
					      &spiel_cmd->slc_session,
					      tx->spt_spiel->spl_rmachine,
					      tx->spt_spiel->spl_confd_eps[idx],
					      MAX_RPCS_IN_FLIGHT) ?:
			spiel_load_fop_send(tx, &spiel_cmd[idx]);
	}

	quorum = m0_count(idx, confd_count, (spiel_cmd[idx].slc_status == 0));
	/*
	 * Unless forced transaction committing requested, make sure the quorum
	 * at least reached the value specified at m0_spiel_start_quorum(), or
	 * better went beyond it.
	 */
	if (!forced && quorum < tx->spt_spiel->spl_rconfc.rc_quorum) {
		rc = M0_ERR(-ENOENT);
		goto tx_fini;
	}

	quorum = 0;
	for (idx = 0; idx < confd_count; ++idx) {
		if (spiel_cmd[idx].slc_status == 0) {
			rc = spiel_flip_fop_send(tx, &spiel_cmd[idx]);
			if (rc == 0)
				++quorum;
		}
	}
	rc = !forced && quorum < tx->spt_spiel->spl_rconfc.rc_quorum ?
		M0_ERR(-ENOENT) : 0;

tx_fini:
	if (tx->spt_buffer != NULL) {
		m0_free_aligned(tx->spt_buffer,
				strlen(tx->spt_buffer) + 1,
				PAGE_SHIFT);
	}

	if (spiel_cmd != NULL) {
		for (idx = 0; idx < confd_count; ++idx) {
			struct m0_rpc_session *ss = &spiel_cmd[idx].slc_session;
			struct m0_rpc_conn    *sc = &spiel_cmd[idx].slc_connect;

			if (!M0_IN(ss->s_sm.sm_state,
				   (M0_RPC_SESSION_FAILED,
				    M0_RPC_SESSION_INITIALISED)))
				m0_rpc_session_destroy(ss, M0_TIME_NEVER);
			if (!M0_IN(sc->c_sm.sm_state,
				   (M0_RPC_CONN_FAILED,
				    M0_RPC_CONN_INITIALISED)))
			    m0_rpc_conn_destroy(sc, M0_TIME_NEVER);
		}
		m0_free(spiel_cmd);
	}

	/* report resultant quorum value reached during committing */
	if (rquorum != NULL)
		*rquorum = quorum;

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_tx_commit_forced);

int m0_spiel_tx_commit(struct m0_spiel_tx *tx)
{
	return m0_spiel_tx_commit_forced(tx, false, M0_CONF_VER_UNKNOWN, NULL);
}
M0_EXPORTED(m0_spiel_tx_commit);

/**
 * Create m0_conf_dir objects for m0_conf_filesystem
 */
static int spiel_filesystem_dirs_create(struct m0_conf_cache      *cache,
					struct m0_conf_filesystem *fs)
{
	int rc;

	M0_ENTRY();
	M0_PRE(fs->cf_nodes == NULL);
	M0_PRE(fs->cf_pools == NULL);
	M0_PRE(fs->cf_racks == NULL);

	rc = dir_new(cache, &fs->cf_obj.co_id, &M0_CONF_FILESYSTEM_NODES_FID,
		     &M0_CONF_NODE_TYPE, &EMPTY_ARR_FID_PTR, &fs->cf_nodes) ?:
	     dir_new(cache, &fs->cf_obj.co_id, &M0_CONF_FILESYSTEM_POOLS_FID,
		     &M0_CONF_POOL_TYPE, &EMPTY_ARR_FID_PTR, &fs->cf_pools) ?:
	     dir_new(cache, &fs->cf_obj.co_id, &M0_CONF_FILESYSTEM_RACKS_FID,
		     &M0_CONF_RACK_TYPE, &EMPTY_ARR_FID_PTR, &fs->cf_racks);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for m0_conf_node
 */
static int spiel_node_dirs_create(struct m0_conf_cache *cache,
				  struct m0_conf_node  *node)
{
	int rc;

	M0_ENTRY();
	M0_PRE(node->cn_processes == NULL);

	rc = dir_new(cache, &node->cn_obj.co_id, &M0_CONF_NODE_PROCESSES_FID,
		     &M0_CONF_PROCESS_TYPE, &EMPTY_ARR_FID_PTR,
		     &node->cn_processes);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for m0_conf_process
 */
static int spiel_process_dirs_create(struct m0_conf_cache   *cache,
				     struct m0_conf_process *process)
{
	int rc;

	M0_ENTRY();
	M0_PRE(process->pc_services == NULL);

	rc = dir_new(cache, &process->pc_obj.co_id,
		     &M0_CONF_PROCESS_SERVICES_FID, &M0_CONF_SERVICE_TYPE,
		     &EMPTY_ARR_FID_PTR, &process->pc_services);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for m0_conf_service
 */
static int spiel_service_dirs_create(struct m0_conf_cache   *cache,
				     struct m0_conf_service *service)
{
	int rc;

	M0_ENTRY();
	M0_PRE(service->cs_sdevs == NULL);

	rc = dir_new(cache, &service->cs_obj.co_id, &M0_CONF_SERVICE_SDEVS_FID,
		     &M0_CONF_SDEV_TYPE, &EMPTY_ARR_FID_PTR,
		     &service->cs_sdevs);

	return M0_RC(rc);
}


/**
 * Create m0_conf_dir objects for m0_conf_pool
 */
static int spiel_pool_dirs_create(struct m0_conf_cache *cache,
				  struct m0_conf_pool  *pool)
{
	int rc;

	M0_ENTRY();
	M0_PRE(pool->pl_pvers == NULL);

	rc = dir_new(cache, &pool->pl_obj.co_id, &M0_CONF_POOL_PVERS_FID,
		     &M0_CONF_PVER_TYPE, &EMPTY_ARR_FID_PTR, &pool->pl_pvers);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for m0_conf_rack
 */
static int spiel_rack_dirs_create(struct m0_conf_cache *cache,
				  struct m0_conf_rack  *rack)
{
	int rc;

	M0_ENTRY();
	M0_PRE(rack->cr_encls == NULL);

	rc = dir_new(cache, &rack->cr_obj.co_id, &M0_CONF_RACK_ENCLS_FID,
		     &M0_CONF_ENCLOSURE_TYPE, &EMPTY_ARR_FID_PTR,
		     &rack->cr_encls);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for m0_conf_enclosure
 */
static int spiel_enclosure_dirs_create(struct m0_conf_cache     *cache,
				       struct m0_conf_enclosure *enclosure)
{
	int rc;

	M0_ENTRY();
	M0_PRE(enclosure->ce_ctrls == NULL);

	rc = dir_new(cache, &enclosure->ce_obj.co_id,
		     &M0_CONF_ENCLOSURE_CTRLS_FID, &M0_CONF_CONTROLLER_TYPE,
		     &EMPTY_ARR_FID_PTR, &enclosure->ce_ctrls);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for m0_conf_controller
 */
static int spiel_controller_dirs_create(struct m0_conf_cache      *cache,
					struct m0_conf_controller *controller)
{
	int rc;

	M0_ENTRY();
	M0_PRE(controller->cc_disks == NULL);

	rc = dir_new(cache, &controller->cc_obj.co_id,
		     &M0_CONF_CONTROLLER_DISKS_FID, &M0_CONF_DISK_TYPE,
		     &EMPTY_ARR_FID_PTR, &controller->cc_disks);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for m0_conf_pver
 */
static int spiel_pver_dirs_create(struct m0_conf_cache *cache,
				  struct m0_conf_pver  *pver)
{
	int rc;

	M0_ENTRY();
	M0_PRE(pver->pv_rackvs == NULL);

	rc = dir_new(cache, &pver->pv_obj.co_id, &M0_CONF_PVER_RACKVS_FID,
		     &M0_CONF_OBJV_TYPE, &EMPTY_ARR_FID_PTR, &pver->pv_rackvs);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for rack version
 */
static int spiel_rackv_dirs_create(struct m0_conf_cache *cache,
				   struct m0_conf_objv  *objv)
{
	int rc;

	M0_ENTRY();
	M0_PRE(objv->cv_children == NULL);

	rc = dir_new(cache, &objv->cv_obj.co_id, &M0_CONF_RACKV_ENCLVS_FID,
		     &M0_CONF_OBJV_TYPE, &EMPTY_ARR_FID_PTR,
		     &objv->cv_children);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for enclosure version
 */
static int spiel_enclosurev_dirs_create(struct m0_conf_cache *cache,
					struct m0_conf_objv  *objv)
{
	int rc;

	M0_ENTRY();
	M0_PRE(objv->cv_children == NULL);

	rc = dir_new(cache, &objv->cv_obj.co_id, &M0_CONF_ENCLV_CTRLVS_FID,
		     &M0_CONF_OBJV_TYPE, &EMPTY_ARR_FID_PTR,
		     &objv->cv_children);

	return M0_RC(rc);
}

/**
 * Create m0_conf_dir objects for controller version
 */
static int spiel_controllerv_dirs_create(struct m0_conf_cache *cache,
					struct m0_conf_objv   *objv)
{
	int rc;

	M0_ENTRY();
	M0_PRE(objv->cv_children == NULL);

	rc = dir_new(cache, &objv->cv_obj.co_id, &M0_CONF_CTRLV_DISKVS_FID,
		     &M0_CONF_OBJV_TYPE, &EMPTY_ARR_FID_PTR,
		     &objv->cv_children);

	return M0_RC(rc);
}


static int spiel_conf_parameter_check(struct m0_conf_cache    *cache,
				      struct spiel_conf_param *parameters)
{
	int                      rc;
	struct spiel_conf_param *param = parameters;

	M0_PRE(cache != NULL);
	M0_PRE(parameters != NULL);

	while(param->scp_fid != NULL) {
		if (m0_conf_fid_type(param->scp_fid) != param->scp_type)
			return M0_ERR(-EINVAL);
		rc = m0_conf_obj_find(cache, param->scp_fid, param->scp_obj);
		if (rc != 0)
			return M0_ERR(rc);
		++param;
	}

	if ((*parameters->scp_obj)->co_status != M0_CS_MISSING)
		return M0_ERR(-EEXIST);

	return M0_RC(0);
}

int m0_spiel_profile_add(struct m0_spiel_tx *tx, const struct m0_fid *fid)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_root *root;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_PROFILE_TYPE, &obj},
			      {&M0_CONF_ROOT_FID, &M0_CONF_ROOT_TYPE,
			       &obj_parent});
	if (rc != 0)
		goto fail;

	root = M0_CONF_CAST(obj_parent, m0_conf_root);
	m0_conf_dir_tlist_add_tail(&root->rt_profiles->cd_items, obj);

	child_adopt(&root->rt_profiles->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	if (!m0_conf_obj_invariant(obj)) {
		rc = M0_ERR(-EINVAL);
		goto fail;
	}

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_profile_add);

int m0_spiel_filesystem_add(struct m0_spiel_tx   *tx,
			    const struct m0_fid  *fid,
			    const struct m0_fid  *parent,
			    unsigned              redundancy,
			    const struct m0_fid  *rootfid,
			    const struct m0_fid  *mdpool,
			    const char          **fs_params)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_filesystem *fs;
	struct m0_conf_obj        *obj_parent;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_FILESYSTEM_TYPE, &obj},
			      {parent, &M0_CONF_PROFILE_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	fs = M0_CONF_CAST(obj, m0_conf_filesystem);
	fs->cf_params = m0_strings_dup(fs_params);
	if (fs->cf_params == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto fail;
	}
	fs->cf_rootfid = *rootfid;
	fs->cf_mdpool = *mdpool;
	fs->cf_redundancy = redundancy;

	if (fs->cf_racks == NULL)
		rc = spiel_filesystem_dirs_create(&tx->spt_cache, fs);
	if (rc != 0)
		goto fail;

	M0_CONF_CAST(obj_parent, m0_conf_profile)->cp_filesystem = fs;

	child_adopt(obj_parent, obj);
	obj->co_status = M0_CS_READY;

	if (!m0_conf_obj_invariant(obj)) {
		rc = M0_ERR(-EINVAL);
		goto fail;
	}
	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_filesystem_add);


int m0_spiel_node_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent,
		      uint32_t             memsize,
		      uint32_t             nr_cpu,
		      uint64_t             last_state,
		      uint64_t             flags,
		      struct m0_fid       *pool_fid)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_node       *node;
	struct m0_conf_obj        *pool;
	struct m0_conf_filesystem *fs;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_NODE_TYPE, &obj },
			      {pool_fid, &M0_CONF_POOL_TYPE, &pool},
			      {parent, &M0_CONF_FILESYSTEM_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	node = M0_CONF_CAST(obj, m0_conf_node);
	node->cn_memsize = memsize;
	node->cn_nr_cpu = nr_cpu;
	node->cn_last_state = last_state;
	node->cn_flags = flags;
	node->cn_pool = M0_CONF_CAST(pool, m0_conf_pool);

	if (node->cn_processes == NULL)
		rc = spiel_node_dirs_create(&tx->spt_cache, node);
	if (rc != 0)
		goto fail;

	fs = M0_CONF_CAST(obj_parent, m0_conf_filesystem);
	if (fs->cf_nodes == NULL)
		rc = spiel_filesystem_dirs_create(&tx->spt_cache, fs);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&fs->cf_nodes->cd_items, obj);

	child_adopt(&fs->cf_nodes->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	if (!m0_conf_obj_invariant(obj)) {
		rc = M0_ERR(-EINVAL);
		goto fail;
	}

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_node_add);

int m0_spiel_process_add(struct m0_spiel_tx  *tx,
			 const struct m0_fid *fid,
			 const struct m0_fid *parent,
			 struct m0_bitmap    *cores,
			 uint64_t             memlimit_as,
			 uint64_t             memlimit_rss,
			 uint64_t             memlimit_stack,
			 uint64_t             memlimit_memlock)
{
	int                     rc;
	struct m0_conf_obj     *obj = NULL;
	struct m0_conf_process *process;
	struct m0_conf_obj     *obj_parent;
	struct m0_conf_node    *node;

	M0_PRE(tx != NULL);
	M0_PRE(fid != NULL);
	M0_PRE(parent != NULL);
	M0_PRE(cores != NULL);
	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_PROCESS_TYPE, &obj },
			      {parent, &M0_CONF_NODE_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	process = M0_CONF_CAST(obj, m0_conf_process);

	rc = m0_bitmap_init(&process->pc_cores, cores->b_nr);
	if (rc != 0)
		goto fail;
	m0_bitmap_copy(&process->pc_cores, cores);
	process->pc_memlimit_as      = memlimit_as;
	process->pc_memlimit_rss     = memlimit_rss;
	process->pc_memlimit_stack   = memlimit_stack;
	process->pc_memlimit_memlock = memlimit_memlock;

	if (process->pc_services == NULL)
		rc = spiel_process_dirs_create(&tx->spt_cache, process);
	if (rc != 0)
		goto fail;

	node = M0_CONF_CAST(obj_parent, m0_conf_node);
	if (node->cn_processes == NULL)
		rc = spiel_node_dirs_create(&tx->spt_cache, node);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&node->cn_processes->cd_items, obj);

	child_adopt(&node->cn_processes->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	if (!m0_conf_obj_invariant(obj)) {
		rc = M0_ERR(-EINVAL);
		goto fail;
	}

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_process_add);

static int spiel_service_info_copy(struct m0_conf_service             *service,
				   const struct m0_spiel_service_info *info)
{
	if (!M0_CONF_SVC_TYPE_IS_VALID(info->svi_type))
		return M0_ERR(-EINVAL);

	if (info->svi_type == M0_CST_MGS && info->svi_u.confdb_path == NULL)
		return M0_ERR(-EINVAL);

	/* TODO: Check what parameters are used by which service types */
	memcpy(&service->cs_u, &info->svi_u, sizeof(service->cs_u));

	if (info->svi_type == M0_CST_MGS) {
		service->cs_u.confdb_path = m0_strdup(info->svi_u.confdb_path);
		if (service->cs_u.confdb_path == NULL)
			return M0_ERR(-EINVAL);
	}

	return M0_RC(0);
}

int m0_spiel_service_add(struct m0_spiel_tx                 *tx,
			 const struct m0_fid                *fid,
			 const struct m0_fid                *parent,
			 const struct m0_spiel_service_info *service_info)
{
	int                     rc;
	struct m0_conf_obj     *obj = NULL;
	struct m0_conf_service *service;
	struct m0_conf_obj     *obj_parent;
	struct m0_conf_process *process;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_SERVICE_TYPE, &obj },
			      {parent, &M0_CONF_PROCESS_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	service = M0_CONF_CAST(obj, m0_conf_service);
	service->cs_type = service_info->svi_type;

	service->cs_endpoints = m0_strings_dup(service_info->svi_endpoints);
	if (service->cs_endpoints == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto fail;
	}

	/* Copy Different service-specific parameters */
	rc = spiel_service_info_copy(service, service_info);
	if (rc != 0)
		goto fail;

	if (service->cs_sdevs == NULL)
		rc = spiel_service_dirs_create(&tx->spt_cache, service);
	if (rc != 0)
		goto fail;

	process = M0_CONF_CAST(obj_parent, m0_conf_process);
	if (process->pc_services == NULL)
		rc = spiel_process_dirs_create(&tx->spt_cache, process);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&process->pc_services->cd_items, obj);

	child_adopt(&process->pc_services->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	if (!m0_conf_obj_invariant(obj)) {
		rc = M0_ERR(-EINVAL);
		goto fail;
	}

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);

	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_service_add);

int m0_spiel_device_add(struct m0_spiel_tx                        *tx,
			const struct m0_fid                       *fid,
			const struct m0_fid                       *svc_parent,
			const struct m0_fid                       *disk_parent,
			enum m0_cfg_storage_device_interface_type  iface,
			enum m0_cfg_storage_device_media_type      media,
			uint32_t                                   bsize,
			uint64_t                                   size,
			uint64_t                                   last_state,
			uint64_t                                   flags,
			const char                                *filename)
{
	int                     rc;
	struct m0_conf_obj     *obj = NULL;
	struct m0_conf_sdev    *device;
	struct m0_conf_obj     *svc_obj;
	struct m0_conf_obj     *disk_obj;
	struct m0_conf_service *service;
	struct m0_conf_disk    *disk;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_SDEV_TYPE, &obj },
			      {svc_parent, &M0_CONF_SERVICE_TYPE, &svc_obj},
			      {disk_parent, &M0_CONF_DISK_TYPE, &disk_obj});
	if (rc != 0)
		goto fail;

	device = M0_CONF_CAST(obj, m0_conf_sdev);
	device->sd_iface = iface;
	device->sd_media = media;
	device->sd_bsize = bsize;
	device->sd_size = size;
	device->sd_last_state = last_state;
	device->sd_flags = flags;
	device->sd_filename = m0_strdup(filename);
	if (device->sd_filename == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto fail;
	}

	service = M0_CONF_CAST(svc_obj, m0_conf_service);
	if (service->cs_sdevs == NULL)
		rc = spiel_service_dirs_create(&tx->spt_cache, service);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&service->cs_sdevs->cd_items, obj);

	disk = M0_CONF_CAST(disk_obj, m0_conf_disk);

	child_adopt(&service->cs_sdevs->cd_obj, obj);
	if (disk->ck_dev != NULL) {
		rc = M0_ERR(-EINVAL);
		goto fail;
	}
	disk->ck_dev = device;
	obj->co_status = M0_CS_READY;

	if (!m0_conf_obj_invariant(obj)) {
		rc = M0_ERR(-EINVAL);
		goto fail;
	}

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_device_add);

int m0_spiel_pool_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent,
		      uint32_t order)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_pool       *pool;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_filesystem *fs;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_POOL_TYPE, &obj },
			      {parent, &M0_CONF_FILESYSTEM_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	pool = M0_CONF_CAST(obj, m0_conf_pool);
	pool->pl_order = order;

	if (pool->pl_pvers == NULL)
		rc = spiel_pool_dirs_create(&tx->spt_cache, pool);
	if (rc != 0)
		goto fail;

	fs = M0_CONF_CAST(obj_parent, m0_conf_filesystem);
	if (fs->cf_pools == NULL)
		rc = spiel_filesystem_dirs_create(&tx->spt_cache, fs);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&fs->cf_pools->cd_items, obj);

	child_adopt(&fs->cf_pools->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	if (!m0_conf_obj_invariant(obj)) {
		rc = M0_ERR(-EINVAL);
		goto fail;
	}

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_pool_add);

int m0_spiel_rack_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent)
{
	int                       rc;
	struct m0_conf_obj       *obj = NULL;
	struct m0_conf_rack       *rack;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_filesystem *fs;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_RACK_TYPE, &obj },
			      {parent, &M0_CONF_FILESYSTEM_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	rack = M0_CONF_CAST(obj, m0_conf_rack);
	if(rack->cr_encls == NULL)
		rc = spiel_rack_dirs_create(&tx->spt_cache, rack);
	if (rc != 0)
		goto fail;

	fs = M0_CONF_CAST(obj_parent, m0_conf_filesystem);
	if (fs->cf_racks == NULL)
		rc = spiel_filesystem_dirs_create(&tx->spt_cache, fs);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&fs->cf_racks->cd_items, obj);

	child_adopt(&fs->cf_racks->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_rack_add);

int m0_spiel_enclosure_add(struct m0_spiel_tx  *tx,
			   const struct m0_fid *fid,
			   const struct m0_fid *parent)
{
	int                       rc;
	struct m0_conf_obj       *obj = NULL;
	struct m0_conf_enclosure *enclosure;
	struct m0_conf_obj       *obj_parent;
	struct m0_conf_rack      *rack;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_ENCLOSURE_TYPE, &obj },
			      {parent, &M0_CONF_RACK_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	enclosure = M0_CONF_CAST(obj, m0_conf_enclosure);

	if (enclosure->ce_ctrls == NULL)
		rc = spiel_enclosure_dirs_create(&tx->spt_cache, enclosure);
	if (rc != 0)
		goto fail;

	rack = M0_CONF_CAST(obj_parent, m0_conf_rack);
	if (rack->cr_encls == NULL)
		rc = spiel_rack_dirs_create(&tx->spt_cache, rack);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&rack->cr_encls->cd_items, obj);

	child_adopt(&rack->cr_encls->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	if (!m0_conf_obj_invariant(obj)) {
		rc = M0_ERR(-EINVAL);
		goto fail;
	}

	m0_mutex_unlock(&tx->spt_lock);

	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_enclosure_add);


int m0_spiel_controller_add(struct m0_spiel_tx  *tx,
			    const struct m0_fid *fid,
			    const struct m0_fid *parent,
			    const struct m0_fid *node)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_obj        *node_obj;
	struct m0_conf_controller *controller;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_enclosure  *enclosure;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_CONTROLLER_TYPE, &obj },
			      {parent, &M0_CONF_ENCLOSURE_TYPE, &obj_parent},
			      {node, &M0_CONF_NODE_TYPE, &node_obj});
	if (rc != 0)
		goto fail;

	controller = M0_CONF_CAST(obj, m0_conf_controller);
	controller->cc_node = M0_CONF_CAST(node_obj, m0_conf_node);
	if (controller->cc_disks == NULL)
		rc = spiel_controller_dirs_create(&tx->spt_cache, controller);
	if (rc != 0)
		goto fail;

	enclosure = M0_CONF_CAST(obj_parent, m0_conf_enclosure);
	if (enclosure->ce_ctrls == NULL)
		rc = spiel_enclosure_dirs_create(&tx->spt_cache, enclosure);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&enclosure->ce_ctrls->cd_items, obj);

	child_adopt(&enclosure->ce_ctrls->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_controller_add);

int m0_spiel_disk_add(struct m0_spiel_tx  *tx,
		      const struct m0_fid *fid,
		      const struct m0_fid *parent)
{
	int                        rc;
	struct m0_conf_obj        *obj = NULL;
	struct m0_conf_obj        *obj_parent;
	struct m0_conf_controller *controller;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_DISK_TYPE, &obj },
			      {parent, &M0_CONF_CONTROLLER_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	controller = M0_CONF_CAST(obj_parent, m0_conf_controller);
	if (controller->cc_disks == NULL)
		rc = spiel_controller_dirs_create(&tx->spt_cache, controller);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&controller->cc_disks->cd_items, obj);

	child_adopt(&controller->cc_disks->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_disk_add);

int m0_spiel_pool_version_add(struct m0_spiel_tx     *tx,
			      const struct m0_fid    *fid,
			      const struct m0_fid    *parent,
			      uint32_t               *nr_failures,
			      uint32_t                nr_failures_cnt,
			      struct m0_pdclust_attr *attrs)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_pver *pver = NULL;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_pool *pool;

	M0_ENTRY();

	if (nr_failures_cnt == 0)
		return M0_ERR(-EINVAL);

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_PVER_TYPE, &obj },
			      {parent, &M0_CONF_POOL_TYPE, &obj_parent});
	if (rc != 0)
		goto fail;

	pver = M0_CONF_CAST(obj, m0_conf_pver);

	pver->pv_nr_failures_nr = nr_failures_cnt;
	M0_ALLOC_ARR(pver->pv_nr_failures, nr_failures_cnt);
	if (pver->pv_nr_failures == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto fail;
	}
	memcpy(pver->pv_nr_failures, nr_failures,
	       nr_failures_cnt * sizeof *nr_failures);
	pver->pv_attr = *attrs;

	if (pver->pv_rackvs == NULL)
		rc = spiel_pver_dirs_create(&tx->spt_cache, pver);
	if (rc != 0)
		goto fail;

	pool = M0_CONF_CAST(obj_parent, m0_conf_pool);
	if (pool->pl_pvers == NULL)
		rc = spiel_pool_dirs_create(&tx->spt_cache, pool);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&pool->pl_pvers->cd_items, obj);

	child_adopt(&pool->pl_pvers->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (pver != NULL)
		m0_free(pver->pv_nr_failures);
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_pool_version_add);

int m0_spiel_rack_v_add(struct m0_spiel_tx  *tx,
			const struct m0_fid *fid,
			const struct m0_fid *parent,
			const struct m0_fid *real)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_objv *objv;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_pver *pver;
	struct m0_conf_obj  *real_obj;

	M0_ENTRY();

	real_obj = m0_conf_cache_lookup(&tx->spt_cache, real);
	if(real_obj == NULL)
		return M0_RC(-ENOENT);

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_OBJV_TYPE, &obj },
			      {parent, &M0_CONF_PVER_TYPE, &obj_parent},
			      {real, &M0_CONF_RACK_TYPE, &real_obj});
	if (rc != 0)
		goto fail;

	objv = M0_CONF_CAST(obj, m0_conf_objv);
	objv->cv_real = real_obj;

	if (objv->cv_children == NULL)
		rc = spiel_rackv_dirs_create(&tx->spt_cache, objv);
	if (rc != 0)
		goto fail;

	pver = M0_CONF_CAST(obj_parent, m0_conf_pver);
	if (pver->pv_rackvs == NULL)
		rc = spiel_pver_dirs_create(&tx->spt_cache, pver);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&pver->pv_rackvs->cd_items, obj);

	child_adopt(&pver->pv_rackvs->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_rack_v_add);

int m0_spiel_enclosure_v_add(struct m0_spiel_tx  *tx,
			     const struct m0_fid *fid,
			     const struct m0_fid *parent,
			     const struct m0_fid *real)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_objv *objv;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_objv *objv_parent;
	struct m0_conf_obj  *real_obj;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_OBJV_TYPE, &obj },
			      {parent, &M0_CONF_OBJV_TYPE, &obj_parent},
			      {real, &M0_CONF_ENCLOSURE_TYPE, &real_obj});
	if (rc != 0)
		goto fail;

	objv = M0_CONF_CAST(obj, m0_conf_objv);
	objv->cv_real = real_obj;

	if (objv->cv_children == NULL)
		rc = spiel_enclosurev_dirs_create(&tx->spt_cache, objv);
	if (rc != 0)
		goto fail;

	objv_parent = M0_CONF_CAST(obj_parent, m0_conf_objv);
	if (objv_parent->cv_children == NULL)
		rc = spiel_rackv_dirs_create(&tx->spt_cache, objv_parent);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&objv_parent->cv_children->cd_items, obj);

	child_adopt(&objv_parent->cv_children->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);

	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_enclosure_v_add);

int m0_spiel_controller_v_add(struct m0_spiel_tx  *tx,
			      const struct m0_fid *fid,
			      const struct m0_fid *parent,
			      const struct m0_fid *real)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_objv *objv;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_objv *objv_parent;
	struct m0_conf_obj  *real_obj;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_OBJV_TYPE, &obj },
			      {parent, &M0_CONF_OBJV_TYPE, &obj_parent},
			      {real, &M0_CONF_CONTROLLER_TYPE, &real_obj});
	if (rc != 0)
		goto fail;

	objv = M0_CONF_CAST(obj, m0_conf_objv);
	objv->cv_real = real_obj;

	if (objv->cv_children == NULL)
		rc = spiel_controllerv_dirs_create(&tx->spt_cache, objv);
	if (rc != 0)
		goto fail;

	objv_parent = M0_CONF_CAST(obj_parent, m0_conf_objv);
	if (objv_parent->cv_children == NULL)
		rc = spiel_enclosurev_dirs_create(&tx->spt_cache, objv_parent);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&objv_parent->cv_children->cd_items, obj);

	child_adopt(&objv_parent->cv_children->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_controller_v_add);

int m0_spiel_disk_v_add(struct m0_spiel_tx  *tx,
			const struct m0_fid *fid,
			const struct m0_fid *parent,
			const struct m0_fid *real)
{
	int                  rc;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_objv *objv;
	struct m0_conf_obj  *obj_parent;
	struct m0_conf_objv *objv_parent;
	struct m0_conf_obj  *real_obj;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);

	rc = SPIEL_CONF_CHECK(&tx->spt_cache,
			      {fid, &M0_CONF_OBJV_TYPE, &obj },
			      {parent, &M0_CONF_OBJV_TYPE, &obj_parent},
			      {real, &M0_CONF_DISK_TYPE, &real_obj});
	if (rc != 0)
		goto fail;

	objv = M0_CONF_CAST(obj, m0_conf_objv);
	objv->cv_real = real_obj;

	objv_parent = M0_CONF_CAST(obj_parent, m0_conf_objv);
	if (objv_parent->cv_children == NULL)
		rc = spiel_controllerv_dirs_create(&tx->spt_cache, objv_parent);
	if (rc != 0)
		goto fail;

	m0_conf_dir_tlist_add_tail(&objv_parent->cv_children->cd_items, obj);

	child_adopt(&objv_parent->cv_children->cd_obj, obj);
	obj->co_status = M0_CS_READY;

	m0_mutex_unlock(&tx->spt_lock);
	return M0_RC(rc);

fail:
	if (obj != NULL && rc != -EEXIST)
		m0_conf_cache_del(&tx->spt_cache, obj);
	m0_mutex_unlock(&tx->spt_lock);
	return M0_ERR(rc);
}
M0_EXPORTED(m0_spiel_disk_v_add);

int m0_spiel_pool_version_done(struct m0_spiel_tx  *tx,
			       const struct m0_fid *fid)
{
	M0_ENTRY();

	/* TODO */

	M0_LEAVE();

	return 0;
}
M0_EXPORTED(m0_spiel_pool_version_done);


int m0_spiel_element_del(struct m0_spiel_tx *tx, const struct m0_fid *fid)
{
	int                 rc = 0;
	struct m0_conf_obj *obj;

	M0_ENTRY();

	m0_mutex_lock(&tx->spt_lock);
	obj = m0_conf_cache_lookup(&tx->spt_cache, fid);
	if (obj != NULL) {
		m0_conf_dir_tlist_del(obj);
		m0_conf_cache_del(&tx->spt_cache, obj);
	}
	m0_mutex_unlock(&tx->spt_lock);

	return M0_RC(rc);
}
M0_EXPORTED(m0_spiel_element_del);

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
