/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 18-May-2016
 */


/**
 * @addtogroup ha
 *
 * TODO handle memory allocation errors;
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "ha/entrypoint_fops.h"
#include "ha/entrypoint_fops_xc.h"

#include "lib/memory.h"         /* M0_ALLOC_ARR */
#include "lib/string.h"         /* memcpy */

#include "fop/fop.h"            /* M0_FOP_TYPE_INIT */
#include "rpc/rpc_opcodes.h"    /* M0_HA_ENTRYPOINT_REQ_OPCODE */
#include "rpc/item.h"           /* M0_RPC_ITEM_TYPE_REQUEST */

#include "ha/entrypoint.h"      /* m0_ha_entrypoint_service_type */

struct m0_fop_type m0_ha_entrypoint_req_fopt;
struct m0_fop_type m0_ha_entrypoint_rep_fopt;

M0_INTERNAL void m0_ha_entrypoint_fops_init(void)
{
	M0_FOP_TYPE_INIT(&m0_ha_entrypoint_req_fopt,
			 .name      = "HA Cluster Entry Point Get Req",
			 .opcode    = M0_HA_ENTRYPOINT_REQ_OPCODE,
			 .xt        = m0_ha_entrypoint_req_fop_xc,
			 .fom_ops   = &m0_ha_entrypoint_fom_type_ops,
			 .sm        = &m0_ha_entrypoint_server_fom_states_conf,
			 .svc_type  = &m0_ha_entrypoint_service_type,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST);
	M0_FOP_TYPE_INIT(&m0_ha_entrypoint_rep_fopt,
			 .name      = "HA Cluster Entry Point Get Reply",
			 .opcode    = M0_HA_ENTRYPOINT_REP_OPCODE,
			 .xt        = m0_ha_entrypoint_rep_fop_xc,
			 .svc_type  = &m0_ha_entrypoint_service_type,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
}

M0_INTERNAL void m0_ha_entrypoint_fops_fini(void)
{
	m0_fop_type_fini(&m0_ha_entrypoint_rep_fopt);
	m0_fop_type_fini(&m0_ha_entrypoint_req_fopt);
}

M0_INTERNAL int
m0_ha_entrypoint_req2fop(const struct m0_ha_entrypoint_req *req,
                         struct m0_ha_entrypoint_req_fop   *req_fop)
{
	*req_fop = (struct m0_ha_entrypoint_req_fop){
		.erf_first_request   = req->heq_first_request ? 1 : 0,
		.erf_link_id_request = req->heq_link_id_request ? 1 : 0,
		.erf_link_id_local   = req->heq_link_id_local,
		.erf_link_id_remote  = req->heq_link_id_remote,
		.erf_link_tag_even   = req->heq_link_tag_even ? 1 : 0,
	};
	return 0;
}

M0_INTERNAL int
m0_ha_entrypoint_fop2req(const struct m0_ha_entrypoint_req_fop *req_fop,
                         const char                            *rpc_endpoint,
                         struct m0_ha_entrypoint_req           *req)
{
	char *rpc_endpoint_dup = m0_strdup(rpc_endpoint);

	if (rpc_endpoint_dup == NULL)
		return M0_ERR(-ENOMEM);
	*req = (struct m0_ha_entrypoint_req){
		.heq_first_request   = req_fop->erf_first_request != 0,
		.heq_rpc_endpoint    = rpc_endpoint_dup,
		.heq_link_id_request = req_fop->erf_link_id_request != 0,
		.heq_link_id_local   = req_fop->erf_link_id_local,
		.heq_link_id_remote  = req_fop->erf_link_id_remote,
		.heq_link_tag_even   = req_fop->erf_link_tag_even != 0,
	};
	return M0_RC(0);
}

M0_INTERNAL int
m0_ha_entrypoint_fop2rep(const struct m0_ha_entrypoint_rep_fop *rep_fop,
                         struct m0_ha_entrypoint_rep           *rep)
{
	uint32_t i;
	int      rc;

	*rep = (struct m0_ha_entrypoint_rep){
		.hae_rc             = rep_fop->hbp_rc,
		.hae_quorum         = rep_fop->hbp_quorum,
		.hae_confd_fids = {
			.af_count = rep_fop->hbp_confd_fids.af_count,
		},
		.hae_active_rm_fid  = rep_fop->hbp_active_rm_fid,
		.hae_active_rm_ep   = m0_buf_strdup(&rep_fop->hbp_active_rm_ep),
		.hae_link_id_local  = rep_fop->hbp_link_id_local,
		.hae_link_id_remote = rep_fop->hbp_link_id_remote,
		.hae_link_tag_even  = rep_fop->hbp_link_tag_even != 0,
	};
	M0_ASSERT(rep->hae_active_rm_ep != NULL);
	rc = m0_bufs_to_strings(&rep->hae_confd_eps, &rep_fop->hbp_confd_eps);
	M0_ASSERT(rc == 0);
	M0_ALLOC_ARR(rep->hae_confd_fids.af_elems,
		     rep->hae_confd_fids.af_count);
	M0_ASSERT(rep->hae_confd_fids.af_elems != NULL);
	for (i = 0; i < rep->hae_confd_fids.af_count; ++i) {
		rep->hae_confd_fids.af_elems[i] =
			rep_fop->hbp_confd_fids.af_elems[i];
	}
	return M0_RC(0);
}

M0_INTERNAL int
m0_ha_entrypoint_rep2fop(const struct m0_ha_entrypoint_rep *rep,
                         struct m0_ha_entrypoint_rep_fop   *rep_fop)
{
	uint32_t  i;
	char     *rm_ep;
	int       rc;

	rm_ep = rep->hae_active_rm_ep == NULL ?
		m0_strdup("") : m0_strdup(rep->hae_active_rm_ep);
	*rep_fop = (struct m0_ha_entrypoint_rep_fop){
		.hbp_rc            = rep->hae_rc,
		.hbp_quorum        = rep->hae_quorum,
		.hbp_confd_fids = {
			.af_count = rep->hae_confd_fids.af_count,
		},
		.hbp_confd_eps = {
			.ab_count = rep->hae_confd_fids.af_count,
		},
		.hbp_active_rm_fid = rep->hae_active_rm_fid,
		.hbp_active_rm_ep  = M0_BUF_INITS(rm_ep),
		.hbp_link_id_local  = rep->hae_link_id_local,
		.hbp_link_id_remote = rep->hae_link_id_remote,
		.hbp_link_tag_even  = rep->hae_link_tag_even ? 1 : 0,
	};
	M0_ALLOC_ARR(rep_fop->hbp_confd_eps.ab_elems,
		     rep_fop->hbp_confd_eps.ab_count);
	M0_ASSERT(rep_fop->hbp_confd_eps.ab_elems != NULL);
	for (i = 0; i < rep_fop->hbp_confd_eps.ab_count; ++i) {
		rc = m0_buf_copy(&rep_fop->hbp_confd_eps.ab_elems[i],
				 &M0_BUF_INITS((char *)rep->hae_confd_eps[i]));
		M0_ASSERT(rc == 0);
	}
	M0_ALLOC_ARR(rep_fop->hbp_confd_fids.af_elems,
		     rep_fop->hbp_confd_fids.af_count);
	M0_ASSERT(rep_fop->hbp_confd_fids.af_elems != NULL);
	for (i = 0; i < rep_fop->hbp_confd_fids.af_count; ++i) {
		rep_fop->hbp_confd_fids.af_elems[i] =
			rep->hae_confd_fids.af_elems[i];
	}
	return 0;
}

M0_INTERNAL void m0_ha_entrypoint_rep_free(struct m0_ha_entrypoint_rep *rep)
{
	m0_free0(&rep->hae_confd_fids.af_elems);
	m0_strings_free(rep->hae_confd_eps);
	m0_free0(&rep->hae_active_rm_ep);
}

M0_INTERNAL void m0_ha_entrypoint_req_free(struct m0_ha_entrypoint_req *req)
{
	m0_free(req->heq_rpc_endpoint);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of ha group */

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
