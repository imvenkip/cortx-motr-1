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
 * Original creation date: 02-Sep-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "lib/memory.h"
#include "lib/string.h"       /* m0_strdup */
#include "fop/fom_generic.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "ha/note.h"
#include "ha/note_fops.h"     /* m0_ha_state_fop */
#include "rpc/rpc.h"
#include "reqh/reqh.h"
#include "conf/confc.h"       /* m0_conf_cache */
#include "conf/diter.h"       /* m0_conf_diter */
#include "conf/obj_ops.h"     /* m0_conf_obj_put */
#include "conf/helpers.h"     /* m0_conf_service_open */

static void ha_state_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
	m0_free(fom);
}

static  size_t ha_state_fom_home_locality(const struct m0_fom *fom)
{
	return m0_fop_opcode(fom->fo_fop);
}

static int ha_state_set_fom_tick(struct m0_fom *fom)
{
	m0_fom_block_enter(fom);
	m0_ha_state_accept(m0_fop_data(fom->fo_fop));
	m0_fom_block_leave(fom);
	m0_rpc_reply_post(&fom->fo_fop->f_item, &fom->fo_rep_fop->f_item);
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	return M0_FSO_WAIT;
}

const struct m0_fom_ops m0_ha_state_set_fom_ops = {
	.fo_tick          = &ha_state_set_fom_tick,
	.fo_fini          = &ha_state_fom_fini,
	.fo_home_locality = &ha_state_fom_home_locality,
};

static int ha_state_set_fom_create(struct m0_fop   *fop,
				   struct m0_fom  **m,
				   struct m0_reqh  *reqh)
{
	struct m0_fom               *fom;
	struct m0_fop               *reply;
	struct m0_fop_generic_reply *rep;

	M0_PRE(fop != NULL);
	M0_PRE(m != NULL);

	M0_ALLOC_PTR(fom);
	reply = m0_fop_reply_alloc(fop, &m0_fop_generic_reply_fopt);
	if (fom == NULL || reply == NULL) {
		m0_free(fom);
		if (reply != NULL)
			m0_fop_put_lock(reply);
		return M0_ERR(-ENOMEM);
	}
	m0_fom_init(fom, &fop->f_type->ft_fom_type, &m0_ha_state_set_fom_ops,
		    fop, reply, reqh);

	rep = m0_fop_data(reply);
	rep->gr_rc = 0;
	rep->gr_msg.s_len = 0;
	*m = fom;
	return 0;
}

static void ha_state_get(struct m0_conf_cache  *cache,
			struct m0_ha_nvec      *req_fop,
			struct m0_ha_state_fop *rep_fop)
{
	struct m0_conf_obj *obj;
	struct m0_ha_nvec  *note_vec = &rep_fop->hs_note;
	int                 i;

	M0_ENTRY();
	note_vec->nv_nr = req_fop->nv_nr;
	for (i = 0; i < req_fop->nv_nr; ++i) {
		m0_conf_cache_lock(cache);
		obj = m0_conf_cache_lookup(cache, &req_fop->nv_note[i].no_id);
		if (obj != NULL) {
			note_vec->nv_note[i].no_id = obj->co_id;
			note_vec->nv_note[i].no_state = obj->co_ha_state;
		}
		m0_conf_cache_unlock(cache);
	}
	M0_LEAVE();
}

static int ha_state_get_fom_tick(struct m0_fom *fom)
{
	struct m0_ha_nvec      *req_fop;
	struct m0_ha_state_fop *rep_fop;
	struct m0_confc        *confc = &m0_fom2reqh(fom)->rh_confc;

	req_fop = m0_fop_data(fom->fo_fop);
	rep_fop = m0_fop_data(fom->fo_rep_fop);

	ha_state_get(&confc->cc_cache, req_fop, rep_fop);

        m0_rpc_reply_post(&fom->fo_fop->f_item, &fom->fo_rep_fop->f_item);
        m0_fom_phase_set(fom, M0_FOPH_FINISH);
        return M0_FSO_WAIT;
}

const struct m0_fom_ops ha_state_get_fom_ops = {
	.fo_tick          = ha_state_get_fom_tick,
	.fo_fini          = ha_state_fom_fini,
	.fo_home_locality = ha_state_fom_home_locality,
};

static int ha_state_get_fom_create(struct m0_fop   *fop,
				   struct m0_fom  **m,
				   struct m0_reqh  *reqh)
{
	struct m0_fom          *fom;
	struct m0_ha_state_fop *ha_state_fop;
	struct m0_ha_nvec      *req_fop;

	M0_PRE(fop != NULL);
	M0_PRE(m != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return M0_ERR(-ENOMEM);

	M0_ALLOC_PTR(ha_state_fop);
	if (ha_state_fop == NULL){
		m0_free(fom);
		return M0_ERR(-ENOMEM);
	}

	req_fop = m0_fop_data(fop);
	M0_ALLOC_ARR(ha_state_fop->hs_note.nv_note, req_fop->nv_nr);
	if (ha_state_fop->hs_note.nv_note == NULL){
		m0_free(ha_state_fop);
		m0_free(fom);
		return M0_ERR(-ENOMEM);
	}

	fom->fo_rep_fop = m0_fop_alloc(&m0_ha_state_get_rep_fopt, ha_state_fop,
				       m0_fop_rpc_machine(fop));
	if (fom->fo_rep_fop == NULL) {
		m0_free(ha_state_fop->hs_note.nv_note);
		m0_free(ha_state_fop);
		m0_free(fom);
		return M0_ERR(-ENOMEM);
	}

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &ha_state_get_fom_ops,
		    fop, fom->fo_rep_fop, reqh);

	*m = fom;
	return M0_RC(0);
}

static int ha_entrypoint_confd_eps_fill(struct m0_confc             *confc,
					const struct m0_fid         *profile,
					struct m0_ha_entrypoint_rep *rep_fop)
{
	struct m0_conf_service *confd_svc;
	char                   *confd_ep;
	int                     rc;

	rc = m0_conf_service_open(confc, profile, NULL, M0_CST_MGS, &confd_svc);
	if (rc != 0)
		return M0_ERR(rc);

	/*
	 * This code is executed only for testing purposes if there is no real
	 * HA service in cluster. Assume that there is always only one confd
	 * server in such case.
	 */
	confd_ep = m0_strdup(confd_svc->cs_endpoints[0]);
	M0_ALLOC_ARR(rep_fop->hbp_confd_fids.af_elems, 1);
	M0_ALLOC_ARR(rep_fop->hbp_confd_eps.ab_elems, 1);
	if (confd_ep == NULL ||
	    rep_fop->hbp_confd_fids.af_elems == NULL ||
	    rep_fop->hbp_confd_eps.ab_elems == NULL) {
		m0_free(confd_ep);
		m0_free(rep_fop->hbp_confd_fids.af_elems);
		m0_free(rep_fop->hbp_confd_eps.ab_elems);
		rc = M0_ERR(-ENOMEM);
	} else {
		rep_fop->hbp_confd_fids.af_count = 1;
		rep_fop->hbp_confd_fids.af_elems[0] = confd_svc->cs_obj.co_id;
		rep_fop->hbp_confd_eps.ab_count = 1;
		rep_fop->hbp_confd_eps.ab_elems[0] = M0_BUF_INITS(confd_ep);
	}
	m0_conf_cache_lock(&confc->cc_cache);
	m0_conf_obj_put(&confd_svc->cs_obj);
	m0_conf_cache_unlock(&confc->cc_cache);
	return M0_RC(rc);
}

static bool _online_service_filter(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE &&
	       obj->co_ha_state == M0_NC_ONLINE;
}

static int ha_entrypoint_active_rm_fill(struct m0_confc             *confc,
					const struct m0_fid         *profile,
					struct m0_ha_entrypoint_rep *rep_fop)
{
	struct m0_conf_filesystem *fs;
	struct m0_conf_obj        *obj;
	struct m0_conf_service    *s;
	struct m0_conf_diter       it;
	char                      *rm_ep = NULL;
	int                        rc;

	/*
	 * This code is executed only for testing purposes if there is no real
	 * HA service in cluster.
	 */
	rc = m0_conf_fs_get(profile, confc, &fs);
	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_NODES_FID,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	if (rc != 0) {
		m0_confc_close(&fs->cf_obj);
		return M0_ERR(rc);
	}

	while ((rc = m0_conf_diter_next_sync(&it,
					    _online_service_filter)) > 0) {
		obj = m0_conf_diter_result(&it);
		s = M0_CONF_CAST(obj, m0_conf_service);
		if (s->cs_type == M0_CST_RMS) {
			rep_fop->hbp_active_rm_fid = s->cs_obj.co_id;
			rm_ep = m0_strdup(s->cs_endpoints[0]);
			rep_fop->hbp_active_rm_ep =  M0_BUF_INITS(rm_ep);
			break;
		}
	}
	m0_conf_diter_fini(&it);
	m0_confc_close(&fs->cf_obj);
	if (rm_ep == NULL) {
		rc = M0_ERR(-EHOSTUNREACH);
		goto err;
	}
	return M0_RC(0);
err:
	return M0_ERR(rc);
}

static int ha_entrypoint_get_fom_tick(struct m0_fom *fom)
{
	struct m0_ha_entrypoint_rep *rep_fop;
	struct m0_reqh              *reqh = m0_fom_reqh(fom);
	struct m0_confc             *confc = &reqh->rh_confc;
	struct m0_fid               *profile = &reqh->rh_profile;

	rep_fop = m0_fop_data(fom->fo_rep_fop);
	rep_fop->hbp_rc =
		ha_entrypoint_confd_eps_fill(confc, profile, rep_fop) ?:
		ha_entrypoint_active_rm_fill(confc, profile, rep_fop);
	rep_fop->hbp_quorum = rep_fop->hbp_confd_fids.af_count / 2 + 1;
        m0_rpc_reply_post(&fom->fo_fop->f_item, &fom->fo_rep_fop->f_item);
        m0_fom_phase_set(fom, M0_FOPH_FINISH);
        return M0_FSO_WAIT;
}

const struct m0_fom_ops ha_entrypoint_get_fom_ops = {
	.fo_tick          = ha_entrypoint_get_fom_tick,
	.fo_fini          = ha_state_fom_fini,
	.fo_home_locality = ha_state_fom_home_locality,
};

static int ha_entrypoint_fom_create(struct m0_fop   *fop,
				    struct m0_fom  **m,
				    struct m0_reqh  *reqh)
{
	struct m0_fom               *fom;
	struct m0_ha_entrypoint_rep *reply;

	M0_PRE(fop != NULL);
	M0_PRE(m != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return M0_ERR(-ENOMEM);

	M0_ALLOC_PTR(reply);
	if (reply == NULL){
		m0_free(fom);
		return M0_ERR(-ENOMEM);
	}

	fom->fo_rep_fop = m0_fop_alloc(&m0_ha_entrypoint_rep_fopt, reply,
				       m0_fop_rpc_machine(fop));
	if (fom->fo_rep_fop == NULL) {
		m0_free(reply);
		m0_free(fom);
		return M0_ERR(-ENOMEM);
	}

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &ha_entrypoint_get_fom_ops,
		    fop, fom->fo_rep_fop, reqh);

	*m = fom;
	return M0_RC(0);
}

static const struct m0_fom_type_ops ha_get_fomt_ops = {
	.fto_create = &ha_state_get_fom_create
};

static const struct m0_fom_type_ops ha_set_fomt_ops = {
	.fto_create = &ha_state_set_fom_create
};

static const struct m0_fom_type_ops ha_entrypoint_fomt_ops = {
	.fto_create = &ha_entrypoint_fom_create
};

const struct m0_fom_type_ops *m0_ha_state_get_fom_type_ops = &ha_get_fomt_ops;
const struct m0_fom_type_ops *m0_ha_state_set_fom_type_ops = &ha_set_fomt_ops;
const struct m0_fom_type_ops *m0_ha_entrypoint_fom_type_ops =
	&ha_entrypoint_fomt_ops;

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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
