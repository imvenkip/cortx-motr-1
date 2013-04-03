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
 * @todo separate client information from server node information
 */
struct m0_mgmt_conf {
	/** The (remote) node name */
	char        *mnc_name;
	/** The (remote) node UUID */
	char        *mnc_uuid;
	/** String endpoint of (remote) m0d */
	char        *mnc_m0d_ep;
	/** String endpoint of client, always for local node */
	char        *mnc_client_ep;
	/** The client node UUID */
	char        *mnc_client_uuid;
	/** The client "var" directory, eg /var/mero */
	char        *mnc_var;
	/** Max RPC message size */
	m0_bcount_t  mnc_max_rpc_msg;
	/** Minimum recv queue length */
	uint32_t     mnc_recv_queue_min_length;
	/** List of services on (remote) node */
	struct m0_tl mnc_svc;
};

M0_TL_DESCR_DECLARE(m0_mgmt_conf, M0_EXTERN);
M0_TL_DECLARE(m0_mgmt_conf, M0_INTERNAL, struct m0_mgmt_svc_conf);

/**
 * Initialize a m0_mgmt_conf object using information in the given
 * genders file.
 * @param conf Object to initialize.
 * @param genders Path to genders file, defaults to /etc/mero/genders.
 * @param nodename Node whose configuration is desired, NULL for localhost.
 * @retval -ENOENT Genders files does not exist
 * @retval -ENODATA No data for this node found in genders
 * @retval -EINVAL Node information is incomplete (e.g. missing node UUID)
 * @note additional errors can be returned.
 */
M0_INTERNAL int m0_mgmt_conf_init(struct m0_mgmt_conf *conf,
				  const char *genders,
				  const char *nodename);

M0_INTERNAL void m0_mgmt_conf_fini(struct m0_mgmt_conf *conf);

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
