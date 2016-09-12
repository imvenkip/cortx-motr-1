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

#pragma once

#ifndef __MERO_HA_ENTRYPOINT_FOPS_H__
#define __MERO_HA_ENTRYPOINT_FOPS_H__

/**
 * @defgroup ha
 *
 * @{
 */

#include "xcode/xcode_attr.h"   /* M0_XCA_RECORD */

#include "lib/types.h"          /* int32_t */
#include "lib/buf.h"            /* m0_buf */
#include "fid/fid.h"            /* m0_fid */
#include "ha/link_fops.h"       /* m0_ha_link_parameters */

/*
 * The following includes are needed only for entrypoint_fops_xc.h compilation.
 */
#include "lib/buf_xc.h"         /* m0_buf_xc */
#include "lib/types_xc.h"       /* m0_uint128_xc */
#include "fid/fid_xc.h"         /* m0_fid_xc */
#include "ha/link_fops_xc.h"    /* m0_ha_link_parameters_xc */

/**
 * Cluster entry point contains information necessary to access cluster
 * configuration. This information is maintained by HA subsystem.
 */
struct m0_ha_entrypoint_rep_fop {
	/** Negative if accessing cluster configuration is impossible. */
	int32_t                  hbp_rc;
	/**
	 * Minimum number of confd servers agreed upon current configuration
	 * version in cluster. Client shouldn't access configuration if this
	 * quorum value is not reached.
	 */
	uint32_t                 hbp_quorum;
	/**
	 * Fids of confd services replicating configuration database. The same
	 * Fids should be present in configuration database tree.
	 */
	struct m0_fid_arr        hbp_confd_fids;
	/** RPC endpoints of confd services. */
	struct m0_bufs           hbp_confd_eps;
	/**
	 * Fid of RM service maintaining read/write access to configuration
	 * database. The same fid should be present in configuration database
	 * tree.
	 */
	struct m0_fid            hbp_active_rm_fid;
	/**
	 * RPC endpoint of RM service.
	 */
	struct m0_buf            hbp_active_rm_ep;

	/* link parameters */
	struct m0_ha_link_params hbp_link_params;
	uint64_t                 hbp_link_do_reconnect;
} M0_XCA_RECORD;

struct m0_ha_entrypoint_req_fop {
	int32_t                  erf_first_request;
	uint64_t                 erf_generation;
	struct m0_fid            erf_process_fid;
	struct m0_fid            erf_profile_fid;
	struct m0_ha_link_params erf_link_params;
	struct m0_buf            erf_git_rev_id;
} M0_XCA_RECORD;

struct m0_ha_entrypoint_req {
	/**
	 * It's the first request from this m0 instance
	 * m0_ha is responsible for this field.
	 */
	bool                      heq_first_request;
	uint64_t                  heq_generation;
	char                     *heq_rpc_endpoint;
	struct m0_fid             heq_process_fid;
	struct m0_fid             heq_profile_fid;
	struct m0_ha_link_params  heq_link_params;
	/* m0_build_info::bi_git_rev_id */
	const char               *heq_git_rev_id;
};

struct m0_ha_entrypoint_rep {
	int                        hae_rc;
	uint32_t                   hae_quorum;
	struct m0_fid_arr          hae_confd_fids;
	const char               **hae_confd_eps;
	struct m0_fid              hae_active_rm_fid;
	char                      *hae_active_rm_ep;

	struct m0_ha_link_params   hae_link_params;
	bool                       hae_link_do_reconnect;
};

extern struct m0_fop_type m0_ha_entrypoint_req_fopt;
extern struct m0_fop_type m0_ha_entrypoint_rep_fopt;

M0_INTERNAL void m0_ha_entrypoint_fops_init(void);
M0_INTERNAL void m0_ha_entrypoint_fops_fini(void);


M0_INTERNAL int
m0_ha_entrypoint_req2fop(const struct m0_ha_entrypoint_req *req,
                         struct m0_ha_entrypoint_req_fop   *req_fop);
M0_INTERNAL int
m0_ha_entrypoint_fop2req(const struct m0_ha_entrypoint_req_fop *req_fop,
                         const char                            *rpc_endpoint,
                         struct m0_ha_entrypoint_req           *req);

M0_INTERNAL int
m0_ha_entrypoint_fop2rep(const struct m0_ha_entrypoint_rep_fop *rep_fop,
                         struct m0_ha_entrypoint_rep           *rep);
M0_INTERNAL int
m0_ha_entrypoint_rep2fop(const struct m0_ha_entrypoint_rep *rep,
                         struct m0_ha_entrypoint_rep_fop   *rep_fop);

M0_INTERNAL void m0_ha_entrypoint_rep_free(struct m0_ha_entrypoint_rep *rep);
M0_INTERNAL void m0_ha_entrypoint_req_free(struct m0_ha_entrypoint_req *req);
M0_INTERNAL int  m0_ha_entrypoint_rep_copy(struct m0_ha_entrypoint_rep *to,
					   struct m0_ha_entrypoint_rep *from);

/** @} end of ha group */
#endif /* __MERO_HA_ENTRYPOINT_FOPS_H__ */

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
