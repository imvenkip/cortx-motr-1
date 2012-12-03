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
#ifndef __COLIBRI_CONF_CONFD_H__
#define __COLIBRI_CONF_CONFD_H__

#include "conf/onwire.h" /* c2_conf_fetch, c2_conf_fetch_resp */
#include "conf/conf_fop.h"
#include "reqh/reqh_service.h"
#include "db/db.h"

/**
 * @page confd-fspec Configuration Service (confd)
 *
 * Configuration service (confd) is designed to work as a part of
 * user-space configuration service, driven by request handler and
 * provides a "FOP-based" interface for accessing Colibri
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
 * - c2_confd --- represents configuration service instance registered
 *   in request handler, stores structures to perform caching,
 *   accesses to configuration db and handles configuration data
 *   requests.
 *
 * - c2_confd_cache -- represents an efficient, high concurrency,
 *   in-memory cache over the underlying database.
 *
 *   Members:
 * - c2_confd_cache::ca_db is a database environment to access
 *   configuration db.
 * - c2_confd_cache::ca_cache is a registry of cached configuration
 *   objects.
 *
 * Confd receives multiple configuration requests from confcs. Each
 * request is a FOP containing a "path" of a requested configuration
 * value to be retrieved from the configuration db. Confcs and confds
 * use RPC layer as a transport to send FOPs.
 *
 * The following FOPs are defined for confd (see conf/onwire.ff):
 * - c2_conf_fetch --- configuration request;
 * - c2_conf_fetch_resp --- Confd's response to c2_conf_fetch;
 * - c2_conf_update --- Update request;
 * - c2_conf_update_resp --- Confd's response to c2_conf_update;
 *
 * <hr> <!------------------------------------------------------------->
 * @section confd-fspec-sub  Subroutines
 *
 * - c2_confd_register()  - registers confd service in the system.
 * - c2_confd_unregister() - unregisters confd service.
 *
 * <!------------------------------------------------------------------>
 * @subsection confd-fspec-sub-setup Initialization and termination
 *
 * Confd is initiated and put into operation by request handler logic,
 * after colibri is started. Confd service should be registered in
 * request handler with c2_confd_register() call, where it has to
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
 *   HLD of Colibri’s configuration database schema</a>;
 * - key is not found.
 *
 * While initialization process, confd has to preload internal cache
 * of configuration objects with their configuration values. It loads
 * entire configuration db into memory-based structures. Pre-loading
 * details can be found in @ref confd-lspec.
 *
 * Initialised confd may be eventually terminated by c2_confd_unregister()
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
 * @ref colibri_setup in cs_help() function are used.
 *
 * <hr> <!------------------------------------------------------------->
 * @section confd-fspec-recipes  Recipes
 *
 * @subsection confd-fspec-recipe1 Typical interaction between confc and confd
 *
 * Client sends a c2_conf_fetch FOP request to confd;
 *
 * Configuration service processes confc requests in
 * c2_fom_ops::fo_tick() function of c2_conf_fetch FOP request and
 * sends c2_conf_fetch_resp FOP back.
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

extern struct c2_reqh_service_type c2_confd_stype;
extern const struct c2_bob_type c2_confd_bob;

/** Configuration server. */
struct c2_confd {
	struct c2_reqh_service d_reqh;
	/**
	 * Configuration string, used as a temporary substitution for
	 * configuration cache.
	 *
	 * @todo XXX confd should cache configuration data properly.
	 */
	const char            *d_local_conf;
	/* struct c2_confd_cache  d_cache; */
	/* struct c2_confd_stat   d_stat; */
	uint64_t               d_magic;
};

C2_INTERNAL int c2_confd_register(void);
C2_INTERNAL void c2_confd_unregister(void);

/* /\** Configuration data accessor. *\/ */
/* struct c2_confd_cache { */
/* 	/\** */
/* 	 * Database environment pointer on c2_reqh::rh_dbenv of the */
/* 	 * request handler in which c2_confd is registered. */
/* 	 *\/ */
/* 	struct c2_dbenv   *ca_db; */
/* #if 0 /\*XXX*\/ */
/* 	/\** Registry of cached configuration objects *\/ */
/* 	struct c2_conf_map ca_cache; */
/* 	/\** Protects this structure while processing of c2_conf_fetch */
/* 	 * and c2_conf_update FOPs *\/ */
/* 	struct c2_longlock ca_rwlock; */
/* #endif */
/* }; */

/* /\** */
/*  * Configuration service statistics */
/*  * @todo To be defined. */
/*  *\/ */
/* struct c2_confd_stat {}; */

/** @} confd_dfspec */
#endif /* __COLIBRI_CONF_CONFD_H__ */
