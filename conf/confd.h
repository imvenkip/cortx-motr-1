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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 19-Mar-2012
 */
#pragma once
#ifndef __MERO_CONF_CONFD_H__
#define __MERO_CONF_CONFD_H__

#include "conf/onwire.h" /* m0_conf_fetch, m0_conf_fetch_resp */
#include "conf/conf_fop.h"
#include "conf/reg.h"
#include "reqh/reqh_service.h"
#include "db/db.h"

/**
 * @page confd-fspec Configuration Service (confd)
 *
 * Configuration service (confd) is designed to work as a part of
 * user-space configuration service, driven by request handler and
 * provides a "FOP-based" interface for accessing Mero
 * configuration information stored in configuration db. Confd is run
 * within the context of a request handler.
 *
 * Confd pre-loads configuration values fetched from configuration db
 * in memory-based data cache to speed up confc requests.
 *
 * - @ref confd-fspec-data
 * - @ref confd-fspec-sub
 *   - @ref confd-fspec-sub-setup
 * - @ref confd-fspec-cli
 * - @ref confd-fspec-recipes
 *   - @ref confd-fspec-recipe1
 * - @ref confd_dfspec "Detailed Functional Specification"
 *
 * <hr> <!------------------------------------------------------------>
 * @section confd-fspec-data Data Structures
 *
 * - m0_confd --- represents configuration service instance registered
 *   in request handler, stores structures to perform caching,
 *   accesses to configuration db and handles configuration data
 *   requests.
 *
 * - m0_confd_cache -- represents an efficient, high concurrency,
 *   in-memory cache over the underlying database.
 *
 *   Members:
 * - m0_confd_cache::ca_db is a database environment to access
 *   configuration db.
 * - m0_confd_cache::ca_cache is a registry of cached configuration
 *   objects.
 *
 * Confd receives multiple configuration requests from confcs. Each
 * request is a FOP containing a "path" of a requested configuration
 * value to be retrieved from the configuration db. Confcs and confds
 * use RPC layer as a transport to send FOPs.
 *
 * The following FOPs are defined for confd (see conf/onwire.ff):
 * - m0_conf_fetch --- configuration request;
 * - m0_conf_fetch_resp --- Confd's response to m0_conf_fetch;
 * - m0_conf_update --- Update request;
 * - m0_conf_update_resp --- Confd's response to m0_conf_update;
 *
 * <hr> <!------------------------------------------------------------->
 * @section confd-fspec-sub  Subroutines
 *
 * - m0_confd_register()  - registers confd service in the system.
 * - m0_confd_unregister() - unregisters confd service.
 *
 * <!------------------------------------------------------------------>
 * @subsection confd-fspec-sub-setup Initialization and termination
 *
 * Confd is initiated and put into operation by request handler logic,
 * after mero is started. Confd service should be registered in
 * request handler with m0_confd_register() call, where it has to
 * initialise own data structures and FOPs used for communication.
 *
 * Initial configuration database is manually created prior to startup.
 * Confd assumes that:
 * - configuration db is created before confd started;
 * - the schema of configuration database conforms to the expected
 *   schema.
 *
 * The following errors may occur while using the configuration db:
 * - db is empty or is in an unrecognized format;
 * - db schema does not conform to
 *   <a href="https://docs.google.com/a/xyratex.com/document/d/1JmsVBV8B4R-FrrYyJC_kX2ibzC1F-yTHEdrm3-FLQYk/view">
 *   HLD of Mero’s configuration database schema</a>;
 * - key is not found.
 *
 * While initialization process, confd has to preload internal cache
 * of configuration objects with their configuration values. It loads
 * entire configuration db into memory-based structures. Pre-loading
 * details can be found in @ref confd-lspec.
 *
 * Initialised confd may be eventually terminated by m0_confd_unregister()
 * in which confd has to finalise own data structures and FOPs.
 *
 * After a confd instance is started it manages configuration
 * database, its own internal cache structures and incoming
 * FOP-requests.
 *
 * <!------------------------------------------------------------------>
 * @section confd-fspec-cli Command Usage
 *
 * To configure confd from console, standard options described in
 * @ref m0d in cs_help() function are used.
 *
 * <hr> <!------------------------------------------------------------->
 * @section confd-fspec-recipes  Recipes
 *
 * @subsection confd-fspec-recipe1 Typical interaction between confc and confd
 *
 * Client sends a m0_conf_fetch FOP request to confd;
 *
 * Configuration service processes confc requests in
 * m0_fom_ops::fo_tick() function of m0_conf_fetch FOP request and
 * sends m0_conf_fetch_resp FOP back.
 *
 * @see confd_dfspec
 */

/**
 * @defgroup confd_dfspec Configuration Service (confd)
 * @brief Detailed Functional Specification.
 *
 * @see @ref confd-fspec
 *
 * @{
 */

extern struct m0_reqh_service_type m0_confd_stype;
extern const struct m0_bob_type m0_confd_bob;

/** Configuration server. */
struct m0_confd {
	/** Generic service. */
	struct m0_reqh_service d_reqh;

	/** Registry of cached configuration objects. */
	struct m0_conf_reg     d_reg;

	/* struct m0_confd_cache  d_cache; */
	/* struct m0_confd_stat   d_stat; */

	/** Magic value == M0_CONFD_MAGIC. */
	uint64_t               d_magic;
};

M0_INTERNAL int m0_confd_register(void);
M0_INTERNAL void m0_confd_unregister(void);

/* /\** Configuration data accessor. *\/ */
/* struct m0_confd_cache { */
/* 	/\** */
/* 	 * Database environment pointer on m0_reqh::rh_dbenv of the */
/* 	 * request handler in which m0_confd is registered. */
/* 	 *\/ */
/* 	struct m0_dbenv   *ca_db; */
/* #if 0 /\*XXX*\/ */
/* 	/\** Registry of cached configuration objects *\/ */
/* 	struct m0_conf_map ca_cache; */
/* 	/\** Protects this structure while processing of m0_conf_fetch */
/* 	 * and m0_conf_update FOPs *\/ */
/* 	struct m0_longlock ca_rwlock; */
/* #endif */
/* }; */

/* /\** */
/*  * Configuration service statistics */
/*  * @todo To be defined. */
/*  *\/ */
/* struct m0_confd_stat {}; */

/** @} confd_dfspec */
#endif /* __MERO_CONF_CONFD_H__ */
