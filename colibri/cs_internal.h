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
 * Original creation date: 06/28/2012
 */

#pragma once

#ifndef __COLIBRI_COLIBRI_CS_INTERNAL_H__
#define __COLIBRI_COLIBRI_CS_INTERNAL_H__

#include "colibri/colibri_setup.h"
#include "cob/cob.h"
#include "dtm/dtm.h"
#include "yaml.h"

/**
   @addtogroup colibri_setup
   @{
 */

/** Declarations private to colibri setup */

enum {
	LINUX_STOB,
	AD_STOB,
	STOBS_NR
};

enum {
	CS_MAX_EP_ADDR_LEN = 86, /* "lnet:" + C2_NET_LNET_XEP_ADDR_LEN */
	AD_BACK_STOB_ID_DEFAULT = 0x0
};
C2_BASSERT(CS_MAX_EP_ADDR_LEN >= C2_NET_LNET_XEP_ADDR_LEN);

/**
   Contains extracted network endpoint and transport from colibri endpoint.
 */
struct cs_endpoint_and_xprt {
	/**
	   colibri endpoint specified as argument.
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
	struct c2_tlink  ex_linkage;
	/**
	   Unique Colour to be assigned to each TM.
	   @see c2_net_transfer_mc::ntm_pool_colour.
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
	struct c2_stob_domain *as_dom;
	/** Back end storage object id, i.e. ad */
	struct c2_stob_id      as_id_back;
	/** Back end storage object. */
	struct c2_stob        *as_stob_back;
	uint64_t               as_magix;
	struct c2_tlink        as_linkage;
};

/**
   Structure which encapsulates stob type and
   stob domain references for linux and ad stobs respectively.
 */
struct cs_stobs {
	/** Type of storage domain to be initialise (e.g. Linux or AD) */
	const char            *s_stype;
	/** Linux storage domain. */
	struct c2_stob_domain *s_ldom;
	struct cs_stob_file    s_sfile;
	/** List of AD stobs */
	struct c2_tl           s_adoms;
	struct c2_dtx          s_tx;
};

/**
   Represents state of a request handler context.
 */
enum cs_reqh_ctx_states {
        /**
	   A request handler context is in RC_UNINTIALISED state when it is
	   allocated and added to the list of the same in struct c2_colibri.

	   @see c2_colibri::cc_reqh_ctxs
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

	/** Services running in request handler context. */
	const char                 **rc_services;

        /** Wheather to prepare storage (mkfs) attached to this context. */
        int                          rc_prepare_storage;

	/** Number of services configured in request handler context. */
	int                          rc_snr;

	/**
	    Maximum number of services allowed per request handler context.
	 */
	int                          rc_max_services;

	/** Endpoints and xprts per request handler context. */
	struct c2_tl                 rc_eps;

	/**
	    State of a request handler context, i.e. RC_INITIALISED or
	    RC_UNINTIALISED.
	 */
	enum cs_reqh_ctx_states      rc_state;

	/** Storage domain for a request handler */
	struct cs_stobs              rc_stob;

	/** Database used by the request handler */
	struct c2_dbenv              rc_db;

	/** Cob domain to be used by the request handler */
	struct c2_mdstore            rc_mdstore;

	struct c2_cob_domain_id      rc_cdom_id;

	/** File operation log for a request handler */
	struct c2_fol                rc_fol;

	/** Request handler instance to be initialised */
	struct c2_reqh               rc_reqh;

	/** Reqh context magic */
	uint64_t                     rc_magix;

	/** Linkage into reqh context list */
	struct c2_tlink              rc_linkage;

	/** Backlink to struct c2_colibri. */
	struct c2_colibri	    *rc_colibri;

	/**
	 * Minimum number of buffers in TM receive queue.
	 * Default is set to c2_colibri::cc_recv_queue_min_length
	 */
	uint32_t		     rc_recv_queue_min_length;

	/**
	 * Maximum RPC message size.
	 * Default value is set to c2_colibri::cc_max_rpc_msg_size
	 * If value of cc_max_rpc_msg_size is zero then value from
	 * c2_net_domain_get_max_buffer_size() is used.
	 */
	uint32_t		     rc_max_rpc_msg_size;
};

/**
 * Represents list of buffer pools in the colibri context.
 */
struct cs_buffer_pool {
        /** Network buffer pool object. */
        struct c2_net_buffer_pool    cs_buffer_pool;
        /** Linkage into network buffer pool list */
        struct c2_tlink              cs_bp_linkage;
        /** Magic */
        uint64_t                     cs_bp_magic;
};

/** @} endgroup colibri_setup */

/* __COLIBRI_COLIBRI_COLIBRI_SETUP_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
