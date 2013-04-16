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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 5-Mar-2013
 */
#pragma once
#ifndef __MERO_MGMT_MGMT_H__
#define __MERO_MGMT_MGMT_H__

#include "lib/tlist.h"

struct m0_reqh_service;

/**
   @defgroup mgmt Management Interfaces
   This module provides interfaces to manage Mero.

   @see @ref MGMT-DLD "Management Detailed Design"

   @{
 */

/** The name of the management service service-type */
#define M0_MGMT_SVC_TYPE_NAME "mgmt"

/**
   Management module initializer.
 */
M0_INTERNAL int m0_mgmt_init(void);

/**
   Management module finalizer.
 */
M0_INTERNAL void m0_mgmt_fini(void);

/**
   Allocate a management service.
 */
M0_INTERNAL int m0_mgmt_service_allocate(struct m0_reqh_service **service);

/**
 * Properties of single mero servers.
 * @todo use conf objects if possible once they are extended
 */
struct m0_mgmt_svc_conf {
	uint64_t        msc_magic;
	/** Service name */
	char           *msc_name;
	/** Service UUID */
	char           *msc_uuid;
	/** Service options/arguments */
	char          **msc_argv;
	int             msc_argc;
	struct m0_tlink msc_link;
};

/**
 * Properties of single mero server node.
 * @todo use conf objects if possible once they are extended
 */
struct m0_mgmt_node_conf {
	/** The (remote) node UUID */
	char        *mnc_uuid;
	/** String endpoint of node m0d */
	char        *mnc_m0d_ep;
	/** The node "var" directory, eg /var/mero */
	char        *mnc_var;
	/** Max RPC message size */
	m0_bcount_t  mnc_max_rpc_msg;
	/** Minimum recv queue length */
	uint32_t     mnc_recvq_min_len;
	/** List of services on (remote) node */
	struct m0_tl mnc_svc;
};

/**
 * Properties of mero client on current node.
 */
struct m0_mgmt_client_conf {
	/** String endpoint of mgmt client, always for local node */
	char        *mcc_mgmt_ep;
	/** The client node UUID */
	char        *mcc_uuid;
	/** Max RPC message size */
	m0_bcount_t  mcc_max_rpc_msg;
	/** Minimum recv queue length */
	uint32_t     mcc_recvq_min_len;
};

/**
 * Endpoints of mero service type.
 */
struct m0_mgmt_service_ep_conf {
	int    mse_ep_nr;
	/** String end points for this service type */
	char **mse_ep;
};

struct m0_mgmt_conf_private;

/**
 * Management Configuration information.
 */
struct m0_mgmt_conf {
	struct m0_mgmt_conf_private *mc_private;
};

M0_TL_DESCR_DECLARE(m0_mgmt_conf, M0_EXTERN);
M0_TL_DECLARE(m0_mgmt_conf, M0_INTERNAL, struct m0_mgmt_svc_conf);

/**
 * Initialize a m0_mgmt_conf object.
 * @param conf Object to initialize.
 * @param genders Path to genders file, defaults to /etc/mero/genders.
 * @retval -ENOENT Genders files does not exist.
 * @note additional errors can be returned.
 */
M0_INTERNAL int m0_mgmt_conf_init(struct m0_mgmt_conf *conf,
				  const char *genders);

M0_INTERNAL void m0_mgmt_conf_fini(struct m0_mgmt_conf *conf);

/**
 * Query genders for information about a specific server node.
 * @param conf Configuration object.
 * @param nodename Node whose configuration is desired, NULL for localhost.
 * @param node On success, node information is returned here.  It must
 * be released using m0_mgmt_node_free().
 * @retval -EINVAL Node information is incomplete (e.g. missing node UUID).
 * @note additional errors can be returned.
 */
M0_INTERNAL int m0_mgmt_node_get(struct m0_mgmt_conf *conf,
				 const char *nodename,
				 struct m0_mgmt_node_conf *node);

M0_INTERNAL void m0_mgmt_node_free(struct m0_mgmt_node_conf *node);

/**
 * Query genders for information relevant to a client on the current node.
 * @param conf Configuration object.
 * @param client On success, client information is returned here.  It must
 * be released using m0_mgmt_client_free().
 * @retval -EINVAL Client information is incomplete (e.g. missing node UUID).
 * @note additional errors can be returned.
 */
M0_INTERNAL int m0_mgmt_client_get(struct m0_mgmt_conf *conf,
				   struct m0_mgmt_client_conf *client);

M0_INTERNAL void m0_mgmt_client_free(struct m0_mgmt_client_conf *client);

/**
 * Query genders for information about a specific server type.
 * Returns all of the configured endpoints for services of this type.
 * The caller must use HA or some other mechanism to determine the active
 * instance for service types such as confd.
 * @param conf Configuration object.
 * @param service_type Service type whose endpoints are desired.
 * @param svc On success, service information is returned here.  It must
 * be released using m0_mgmt_service_free().
 * @retval -ENOENT No data for this service type found in genders.
 * @retval -EINVAL Genders information is incomplete (e.g. missing node UUID).
 * @note additional errors can be returned.
 */
M0_INTERNAL int m0_mgmt_service_ep_get(struct m0_mgmt_conf *conf,
				       const char *service_type,
				       struct m0_mgmt_service_ep_conf *svc);

M0_INTERNAL void m0_mgmt_service_ep_free(struct m0_mgmt_service_ep_conf *svc);

/** @} end mgmt group */

#endif /* __MERO_MGMT_MGMT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
