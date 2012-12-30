/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 28-Jun-2012
 */

#pragma once

#ifndef __MERO_SETUP_INTERNAL_H__
#define __MERO_SETUP_INTERNAL_H__

#include "mero/setup.h"
#include "mdstore/mdstore.h"  /* m0_mdstore */
#include "fol/fol.h"          /* m0_fol */
#include "net/lnet/lnet.h"    /* M0_NET_LNET_XEP_ADDR_LEN */
#include "reqh/reqh.h"        /* m0_reqh */
#include "yaml.h"             /* yaml_document_t */

/**
   @addtogroup m0d
   @{
 */

/** Declarations private to mero setup */

enum {
	LINUX_STOB,
	AD_STOB,
	STOBS_NR
};

enum {
	CS_MAX_EP_ADDR_LEN = 86, /* "lnet:" + M0_NET_LNET_XEP_ADDR_LEN */
	AD_BACK_STOB_ID_DEFAULT = 0x0
};
M0_BASSERT(CS_MAX_EP_ADDR_LEN >= M0_NET_LNET_XEP_ADDR_LEN);

/**
   Contains extracted network endpoint and transport from mero endpoint.
 */
struct cs_endpoint_and_xprt {
	/**
	   mero endpoint specified as argument.
	   Used for ADDB purpose.
	 */
	const char      *ex_cep;
	/**
	   4-tuple network layer endpoint address.
	   e.g. 172.18.50.40@o2ib1:12345:34:1
	 */
	const char      *ex_endpoint;
	/** Supported network transport. */
	const char      *ex_xprt;
	/**
	   Scratch buffer for endpoint and transport extraction.
	 */
	char            *ex_scrbuf;
	uint64_t         ex_magix;
	/** Linkage into reqh context endpoint list, cs_reqh_context::rc_eps */
	struct m0_tlink  ex_linkage;
	/**
	   Unique Colour to be assigned to each TM.
	   @see m0_net_transfer_mc::ntm_pool_colour.
	 */
	uint32_t	 ex_tm_colour;
};

/**
 * Represent devices configuration file in form of yaml document.
 * @note This is temporary implementation in-order to configure device as
 *       a stob. This may change when confc implementation lands into master.
 */
struct cs_stob_file {
	bool              sf_is_initialised;
	yaml_document_t   sf_document;
};

struct cs_ad_stob {
	/** Allocation data storage domain.*/
	struct m0_stob_domain *as_dom;
	/** Back end storage object id, i.e. ad */
	struct m0_stob_id      as_id_back;
	/** Back end storage object. */
	struct m0_stob        *as_stob_back;
	uint64_t               as_magix;
	struct m0_tlink        as_linkage;
};

/**
   Structure which encapsulates stob type and
   stob domain references for linux and ad stobs respectively.
 */
struct cs_stobs {
	/** Type of storage domain to be initialise (e.g. Linux or AD) */
	const char            *s_stype;
	/** Linux storage domain. */
	struct m0_stob_domain *s_ldom;
	struct cs_stob_file    s_sfile;
	/** List of AD stobs */
	struct m0_tl           s_adoms;
	struct m0_dtx          s_tx;
};

/**
   Represents state of a request handler context.
 */
enum cs_reqh_ctx_states {
	/**
	   A request handler context is in RC_UNINTIALISED state when it is
	   allocated and added to the list of the same in struct m0_mero.

	   @see m0_mero::cc_reqh_ctxs
	 */
	RC_UNINITIALISED,
	/**
	   A request handler context is in RC_INITIALISED state once the
	   request handler (embedded inside the context) is successfully
	   initialised.

	   @see cs_reqh_context::rc_reqh
	 */
	RC_INITIALISED
};

/**
   Represents a request handler environment.
   It contains configuration information about the various global entities
   to be configured and their corresponding instances that are needed to be
   initialised before the request handler is started, which by itself is
   contained in the same structure.
 */
struct cs_reqh_context {
	/** Storage path for request handler context. */
	const char                  *rc_stpath;

	/** Path to device configuration file. */
	const char                  *rc_dfilepath;

	/** Type of storage to be initialised. */
	const char                  *rc_stype;

	/** Database environment path for request handler context. */
	const char                  *rc_dbpath;

	/** Whether to prepare storage (mkfs) attached to this context. */
	int                          rc_prepare_storage;

	/** Services running in request handler context. */
	const char                 **rc_services;

	/** Number of services configured in request handler context. */
	uint32_t                     rc_nr_services;

	/** Maximum number of services allowed per request handler context. */
	int                          rc_max_services;

	/** Endpoints and xprts per request handler context. */
	struct m0_tl                 rc_eps;

	/**
	    State of a request handler context, i.e. RC_INITIALISED or
	    RC_UNINTIALISED.
	 */
	enum cs_reqh_ctx_states      rc_state;

	/** Storage domain for a request handler */
	struct cs_stobs              rc_stob;

	/** Database used by the request handler */
	struct m0_dbenv              rc_db;

	/** Path to the configuration database to be used by confd service. */
	const char                  *rc_confdb;

	/** Cob domain to be used by the request handler */
	struct m0_mdstore            rc_mdstore;

	struct m0_cob_domain_id      rc_cdom_id;

	/** File operation log for a request handler */
	struct m0_fol                rc_fol;

	/** Request handler instance to be initialised */
	struct m0_reqh               rc_reqh;

	/** Reqh context magic */
	uint64_t                     rc_magix;

	/** Linkage into reqh context list */
	struct m0_tlink              rc_linkage;

	/** Backlink to struct m0_mero. */
	struct m0_mero              *rc_mero;

	/**
	 * Minimum number of buffers in TM receive queue.
	 * Default is set to m0_mero::cc_recv_queue_min_length
	 */
	uint32_t                     rc_recv_queue_min_length;

	/**
	 * Maximum RPC message size.
	 * Default value is set to m0_mero::cc_max_rpc_msg_size
	 * If value of cc_max_rpc_msg_size is zero then value from
	 * m0_net_domain_get_max_buffer_size() is used.
	 */
	uint32_t                     rc_max_rpc_msg_size;
};

/**
 * Represents list of buffer pools in the mero context.
 */
struct cs_buffer_pool {
	/** Network buffer pool object. */
	struct m0_net_buffer_pool cs_buffer_pool;
	/** Linkage into network buffer pool list */
	struct m0_tlink           cs_bp_linkage;
	/** Magic */
	uint64_t                  cs_bp_magic;
};

/**
 * Obtains configuration data from confd and converts it into options,
 * understood by _args_parse().
 *
 * @param[out] args   Arguments to be filled.
 * @param confd_addr  Endpoint address of confd service.
 * @param profile     The name of configuration profile.
 */
M0_INTERNAL int cs_conf_to_args(struct cs_args *args, const char *confd_addr,
				const char *profile);

/** @} endgroup m0d */
#endif /* __MERO_SETUP_INTERNAL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
