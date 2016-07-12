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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 24-Jun-2016
 */

#pragma once

#ifndef __MERO_DIX_CLIENT_H__
#define __MERO_DIX_CLIENT_H__

/**
 * @addtogroup dix
 *
 * @{
 *
 * Distributed index client provides an interface to access and modify
 * distributed indices. It uses CAS client internally and extends its
 * functionality by providing indices distribution.
 *
 * Please refer to HLD of the distributed indexing for high-level overview.
 *
 * Dependencies
 * ------------
 * - CAS client to make requests to CAS service.
 * - @ref m0_pools_common to locate RPC connections to CAS services in a pool,
 *   where distributed index is stored.
 * - Mero layout functionality to determine destination CAS services for CAS
 *   requests.
 *
 * Index meta-data
 * ---------------
 * There are three meta-indices that are used by client internally and can be
 * manipulated through appropriate interfaces externally:
 * - Root index. Top-level index to find all other indices. Its pool version
 *   normally is stored in cluster configuration as one of filesystem
 *   parameters. Other layout parameters are hard-coded. Contains exactly two
 *   records for now with layouts of 'layout' and 'layout-descr' meta-indices.
 *   Please note, that pool version for root index should contain only storage
 *   devices controlled by CAS services, no IOS storage devices are allowed.
 *
 * - Layout index. Holds layouts of "normal" indices. Layouts are stored in the
 *   form of index layout descriptor or index layout identifier.
 *
 * - Layout-descr index. Maps index layout identifiers to full-fledged
 *   index layout descriptors.
 *   User is responsible for index layout identifiers allocation and
 *   populates layout-descr index explicitly with m0_dix_ldescr_put().
 *   Similarly, m0_dix_ldescr_del() deletes mapping between layout identifier
 *   and layout descriptor.
 *
 * Meta-data is global for the file system and normally is created during
 * cluster provisioning via m0_dix_meta_create(). Meta-data can be destroyed via
 * m0_dix_meta_destroy(). Also user is able to check whether meta-data is
 * accessible and is correct via m0_dix_meta_check(). Distributed index
 * meta-data is mandatory and should always be present in the filesystem.
 *
 * Initialisation and start
 * ------------------------
 * Client is initialised with m0_dix_cli_init(). The main argument is a pool
 * version fid of the root index. All subsequent operations use this fid to
 * locate indices in the cluster.
 *
 * In order to make DIX requests through the client user should start the
 * client. Client start procedure is executed through m0_dix_cli_start() or
 * m0_dix_cli_start_sync(). The start procedure involves reading root index to
 * retrieve layouts of 'layout' and 'layout-descr' meta-indeces.
 *
 * There is a special client mode called "bootstap" mode. In that mode the only
 * request allowed is creating meta-indices in cluster (m0_dix_meta_create()).
 * A client can be moved to this mode just after initialisation using
 * m0_dix_cli_bootstrap() call. It's useful at cluster provisioning state,
 * because client can't start successfully without meta-indices being created in
 * mero file system. After meta indices creation is done, client can be started
 * as usual or finalised.
 *
 * References:
 * - HLD of the distributed indexing
 * https://docs.google.com/document/d/1WpENdsq5YXCCoDcBbNe6juVY85163-HUpvIzXrmKwdM/edit
 */

#include "dix/layout.h" /* m0_dix_ldesc */
#include "dix/meta.h"   /* m0_dix_meta_req */

/* Import */
struct m0_pools_common;
struct m0_layout_domain;

struct m0_dix_cli {
	struct m0_sm             dx_sm;
	struct m0_clink          dx_clink;
	/** Meta-request to initialise meta-indices during startup. */
	struct m0_dix_meta_req   dx_mreq;
	struct m0_pools_common  *dx_pc;
	struct m0_layout_domain *dx_ldom;
	/** Pool version of the root index. */
	struct m0_pool_version  *dx_pver;
	struct m0_sm_ast         dx_ast;
	struct m0_dix_ldesc      dx_root;
	struct m0_dix_ldesc      dx_layout;
	struct m0_dix_ldesc      dx_ldescr;
};

M0_INTERNAL int m0_dix_cli_init(struct m0_dix_cli       *cli,
				struct m0_sm_group      *sm_group,
				struct m0_pools_common  *pc,
			        struct m0_layout_domain *ldom,
				const struct m0_fid     *pver);

M0_INTERNAL void m0_dix_cli_lock(struct m0_dix_cli *cli);

M0_INTERNAL bool m0_dix_cli_is_locked(const struct m0_dix_cli *cli);

M0_INTERNAL void m0_dix_cli_unlock(struct m0_dix_cli *cli);

M0_INTERNAL void m0_dix_cli_start(struct m0_dix_cli *cli);

M0_INTERNAL int  m0_dix_cli_start_sync(struct m0_dix_cli *cli);

M0_INTERNAL void m0_dix_cli_bootstrap(struct m0_dix_cli *cli);

M0_INTERNAL void m0_dix_cli_bootstrap_lock(struct m0_dix_cli *cli);

M0_INTERNAL void m0_dix_cli_stop(struct m0_dix_cli *cli);

M0_INTERNAL void m0_dix_cli_stop_lock(struct m0_dix_cli *cli);

M0_INTERNAL void m0_dix_cli_fini(struct m0_dix_cli *cli);

M0_INTERNAL void m0_dix_cli_fini_lock(struct m0_dix_cli *cli);

/** @} end of dix group */

#endif /* __MERO_DIX_CLIENT_H__ */

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
