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
 * Original author: Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation date: 12-Mar-2013
 */
#pragma once
#ifndef __MERO_MGMT_CTL_H__
#define __MERO_MGMT_CTL_H__

/**
   @defgroup mgmt_ctl_pvt Management Client Private
   @ingroup mgmt_pvt
   @{
 */

enum {
	M0_MC_RPC_TIMEOUT = M0_MKTIME(30, 0),
};

/**
 * Defines the global m0ctl context.  A global context is initialized by
 * the main m0ctl program and passed to operation functions.
 * @todo other members to be added as required by implementation.
 */
struct m0_mgmt_ctl_ctx {
	/** Path to genders file to use */
	const char              *mcc_genders;
	/** RPC timeout */
	m0_time_t                mcc_timeout;
	/** Output in YAML when true */
	bool                     mcc_yaml;
	/** Configuration of the node, read from the genders file */
	struct m0_mgmt_conf      mcc_conf;
	/** name of temporary directory */
	char                     mcc_tmpdir[24];
	/** The ADDB context is a child of the proc context. */
	struct m0_addb_ctx       mcc_addb_ctx;
	/** RPC client structure */
	struct m0_rpc_client_ctx mcc_client;
	/* m0_rpc_client_ctx related arguments */
	struct m0_net_domain     mcc_net_dom;
	struct m0_dbenv          mcc_dbenv;
	struct m0_cob_domain     mcc_cob_dom;
	char                     mcc_dbname[24];
};

/**
 * Denotes an operation of m0ctl.  Each such operation is is denoted by an
 * instance of this structure.
 * @todo other members to be added as required by implementation.
 */
struct m0_mgmt_ctl_op {
	/** The operation, eg "query-all" */
	const char *cto_op;

	/**
	 * The main function of the operation.  Called after global option
	 * processing completes.  The argc and argv are adjusted so that argv[0]
	 * of cto_main refers to the operation argument with value cto_op
	 * rather than the original argv[0] of the global main() function.
	 */
	int       (*cto_main)(int argc, char *argv[],
			      struct m0_mgmt_ctl_ctx *ctx);
};

/** @} end mgmt_ctl_pvt group */
#endif /* __MERO_MGMT_CTL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
