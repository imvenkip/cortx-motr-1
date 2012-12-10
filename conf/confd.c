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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "lib/memory.h"    /* M0_ALLOC_PTR_ADDB */
#include "lib/errno.h"     /* ENOMEM */
#include "lib/misc.h"      /* strdup */
#include "mero/magic.h" /* M0_CONFD_MAGIC */
#include "conf/confd.h"

/**
 * @page confd-lspec-page confd Internals
 * - @ref confd-depends
 * - @ref confd-highlights
 * - @ref confd-lspec
 *   - @ref confd-lspec-state
 *   - @ref confd-lspec-long-lock
 *   - @ref confd-lspec-thread
 *   - @ref confd-lspec-numa
 * - @ref confd-conformance
 * - @ref confd-ut
 * - @ref confd-O
 * - @ref confd_dlspec "Detailed Logical Specification"
 *
 * @section confd-depends Dependencies
 *
 * Confd depends on the following subsystems:
 * - @ref rpc_service <!-- rpc/service.h -->
 * - @ref db  <!-- db/db.h -->
 * - @ref fom <!-- fop/fom.h -->
 * - @ref fop <!-- fop/fop.h -->
 * - @ref reqh <!-- reqh/reqh.h -->
 * - @ref m0d <!-- mero/setup.h -->
 * - m0_reqh_service_type_register()  <!--reqh/reqh_service.h -->
 * - m0_reqh_service_type_unregister() <!--reqh/reqh_service.h -->
 * - m0_addb_ctx_init() <!-- addb/addb.h -->
 * - m0_addb_ctx_fini() <!-- addb/addb.h -->
 * - @ref m0_long_lock_API <!-- fop/fom_long_lock.h -->
 *
 * Most important functions, confd depends on, are listed above:
 * - RPC layer:
 *   - m0_rpc_reply_post() used to send FOP-based reply to Confc;
 *   - M0_RPC_SERVER_CTX_DEFINE() used to create rpc server context.
 *     (XXX I'm not sure confd may use a definition from ut/ directory. --vvv)
 * - DB layer:
 *   - m0_db_pair_setup() and m0_table_lookup() used to access
 *     configuration values stored in db.
 * - FOP, FOM, REQH:
 *   - m0_fom_block_at();
 *   - m0_fom_block_leave();
 *   - m0_fom_block_enter();
 *   - m0_long_read_lock();
 *   - m0_long_write_lock().
 * - Mero setup:
 *   - m0_cs_setup_env() configures Mero to use confd's environment.
 *
 * <hr> <!------------------------------------------------------------->
 * @section confd-highlights Design Highlights
 *
 * - User-space implementation.
 * - Provides a "FOP-based" interface for confc to access configuration
 *   information.
 * - Relies on request handler threading model and is driven by
 *   reqh. Request processing is based on FOM execution.
 * - Maintains its own configuration cache, implementation of which is
 *   common to confd and confc.
 * - Several confd state machines (FOMs) processing requests from
 *   configuration consumers can work with configuration cache
 *   concurrently.
 *
 * <hr> <!------------------------------------------------------------->
 * @section confd-lspec Logical Specification
 *
 * Confd service initialization is performed by request handler. To
 * allocate Confd service and its internal structures in memory
 * m0_confd_service_locate() is used.
 *
 * Confd service type is registered in `subsystem' data structure of
 * "mero/init.c", the following lines are added:
 * @code
 * struct init_fini_call subsystem[] = {
 *      ...
 *	{ &m0_confd_register, &m0_confd_unregister, "confd" },
 *      ...
 * };
 * @endcode
 *
 * Configuration cache pre-loading procedure traverses all tables of
 * configuration db. Since relations between neighbour levels only are
 * possible, tables of higher "levels of DAG" are processed first.
 * The following code example presents pre-loading in details:
 *
 * @code
 * conf_cache_preload (...)
 * {
 *    for each record in the "profiles" table do
 *      ... allocate and fill struct m0_conf_profile from p_prof
 *    endfor
 *
 *    for table in "file_systems", "services",
 *              "nodes", "nics", "storage_devices",
 *	        in the order specified, do
 *       for each record in the table, do
 *         ... allocate and fill struct m0_conf_obj ...
 *         ... create DAG struct m0_conf_relation to appropriate conf object ...
 *       endfor
 *    end for
 * }
 * @endcode
 *
 * FOP format, FOP operation vector, FOP type, and RPC item type have
 * to be defined for each FOP.  The following structures are defined
 * for m0_conf_fetch FOP:
 * - struct m0_fop_type_format m0_conf_fetch_tfmt --- defines format
 *   registered in *.ff used in confd;
 * - struct m0_fop_type m0_conf_fetch_fopt --- defines FOP type;
 * - struct m0_fop_type_ops m0_conf_fetch_ops --- defines FOP
 *   operation vector;
 * - struct m0_rpc_item_type m0_rpc_item_type_fetch --- defines RPC
 *   item type.
 *
 * m0_fom_fetch_state() - called by reqh to handle incoming
 * confc requests. Implementation of this function processes all
 * FOP-FOM specific and m0_conf_fetch_resp phases:
 * @code
 * static int m0_fom_fetch_state(struct m0_fom *fom)
 *  {
 *       checks if FOM should transition into a generic/standard
 *       phase or FOP specific phase.
 *
 *       if (fom->fo_phase < FOPH_NR) {
 *               result = m0_fom_state_generic(fom);
 *       } else {
 *		... process m0_conf_fetch_resp phase transitions ...
 *	 }
 *  }
 * @endcode
 *
 * Request handler triggers user-defined functions to create FOMs for
 * processed FOPs. Service has to register FOM-initialization functions
 * for each FOP treated as a request:
 *   - m0_conf_fetch;
 *   - m0_conf_update;
 *
 * To do so, the appropriate structures and functions have to be
 * defined. For example the following used by m0_conf_fetch FOP:
 *
 * @code
 * static const struct m0_fom_type_ops fom_fetch_type_ops = {
 *       .fto_create = fetch_fom_create
 * };
 *
 * struct m0_fom_type m0_fom_fetch_mopt = {
 *       .ft_ops = &fom_fetch_type_ops
 * };
 *
 * static int fetch_fom_create(struct m0_fop *fop, struct m0_fom **m)
 * {
 *    1) allocate fom;
 *    2) m0_fom_init(fom, &m0_fom_ping_mopt, &fom_fetch_type_ops, fop, NULL);
 *    3) *m = fom;
 * }
 * @endcode
 *
 * The implementation of m0_fom_fetch_state() needs the following
 * functions to be defined:
 *
 * - fetch_check_request(), update_check_request() - check incoming
 *     request and validates requested path of configuration objects.
 *
 * - fetch_next_state(), update_next_state() - transit FOM phases
 *   depending on the current phase and on the state of configuration
 *   objects.
 *
 * - obj_serialize() - serializes given object to FOP.
 *
 * - fetch_failure_handle(), update_failure_handle() - handle occurred
 *   errors.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confd-lspec-state State Specification
 *
 * Confd as a whole is not a state machine, phase processing is
 * implemented on basis of FOM of m0_conf_fetch, m0_conf_update FOPs.
 * After corresponding FOM went through a list of FOM specific phases
 * it transited into F_INITIAL phase.
 *
 * The number of state machine instances correspond to the number of
 * FOPs being processed in confd.
 *
 * m0_conf_fetch FOM state transition diagram:
 * @dot
 *  digraph conf_fetch_phase {
 *      node [fontsize=9];
 *      edge [fontsize=9];
 *      F_INITIAL [style=filled, fillcolor=lightgrey];
 *      F_SERIALISE;
 *      F_TERMINATE [style=filled, fillcolor=lightgrey];
 *      F_FAILURE [style=filled, fillcolor=lightgrey];
 *
 *      F_INITIAL -> F_SERIALISE [label=
 *      "m0_long_read_lock(m0_confd::d_cache::ca_rwlock)"];
 *
 *      F_SERIALISE -> F_TERMINATE [label = "success"];
 *      F_SERIALISE -> F_FAILURE [label = "failure"];
 *  }
 * @enddot
 *
 * - F_INITIAL
 *   In this phase, incoming FOM/FOP-related structures are being
 *   initialized and FOP-processing preconditions are being
 *   checked. Then, an attempt is made to obtain a read lock
 *   m0_confd::d_cache::ca_rwlock. When it's obtained then
 *   m0_long_lock logic transits FOM back into F_SERIALISE.
 *
 * - F_SERIALISE:
 *   Current design assumes that data is pre-loaded into configuration
 *   cache. In F_SERIALISE phase, m0_confd::d_cache::ca_rwlock lock has
 *   been already obtained as a read lock.
 *   m0_conf_fetch_resp FOP is being prepared for sending by looking
 *   up requested path in configuration cache and unlocking
 *   m0_confd::d_cache::ca_rwlock.  After that, m0_conf_fetch_resp FOP
 *   is sent with m0_rpc_reply_post().  fetch_next_state() transits FOM into
 *   F_TERMINATE. If incoming request consists of a path which is not
 *   in configuration cache, then the m0_conf_fetch FOM is
 *   transitioned to the F_FAILURE phase.
 *
 * - F_TERMINATE:
 *   In this phase, statistics values are being updated in
 *   m0_confd::d_stat. m0_confd::d_cache::ca_rwlock has to be
 *   unlocked.
 *
 * - F_FAILURE:
 *   In this phase, statistics values are being updated in
 *   m0_confd::d_stat, ADDB records are being added.
 *   m0_conf_fetch_resp FOP with an empty configuration objects
 *   sequence and negative error code is sent with m0_rpc_reply_post().
 *   m0_confd::d_cache::ca_rwlock has to be unlocked.
 *
 *  @note m0_conf_stat FOM has a similar state diagram as
 *  m0_conf_fetch FOM does and hence is not illustrated here.
 *
 *  m0_conf_update FOM state transition diagram:
 * @dot
 *  digraph conf_update_phase {
 *      node [fontsize=9];
 *      edge [fontsize=9];
 *      U_INITIAL [style=filled, fillcolor=lightgrey];
 *      U_UPDATE;
 *      U_TERMINATE [style=filled, fillcolor=lightgrey];
 *      U_FAILURE [style=filled, fillcolor=lightgrey];
 *
 *      U_INITIAL -> U_UPDATE [label=
 *      "m0_long_write_lock(m0_confd::d_cache::ca_rwlock)"];
 *
 *      U_UPDATE -> U_TERMINATE [label = "success"];
 *      U_UPDATE -> U_FAILURE [label = "failure"];
 *  }
 * @enddot
 *
 * - U_INITIAL:
 *   In this phase, incoming FOM/FOP-related structures are being
 *   initialized and FOP-processing preconditions are being
 *   checked. Then, an attempt is made to obtain a write lock
 *   m0_confd::d_cache::ca_rwlock. When it's obtained then
 *   m0_long_lock logic transits FOM back into U_UPDATE.
 *
 * - U_UPDATE:
 *   In current phase, m0_confd::d_cache::ca_rwlock lock has been
 *   already obtained as a write lock. Then, configuration cache has
 *   to be updated and m0_confd::d_cache::ca_rwlock lock should be
 *   unlocked.  After that, m0_conf_update_resp FOP is sent with
 *   m0_rpc_reply_post(). update_next_state() transits FOM into
 *   U_TERMINATE.  If incoming request consists of a path which is not
 *   in configuration cache than the m0_conf_fetch FOM is transitioned
 *   to the U_FAILURE phase
 *
 * - U_TERMINATE:
 *   In this phase, statistics values are being updated in
 *   m0_confd::d_stat. m0_confd::d_cache::ca_rwlock has to be
 *   unlocked.
 *
 * - U_FAILURE:
 *   In this phase, statistics values are being updated in
 *   m0_confd::d_stat, ADDB records are being added.
 *   m0_conf_update_resp FOP with an empty configuration objects
 *   sequence and negative error code is sent with m0_rpc_reply_post().
 *   m0_confd::d_cache::ca_rwlock has to be unlocked.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confd-lspec-long-lock Locking model
 *
 * Confd relies on a locking primitive integrated with FOM signaling
 * mechanism. The following interfaces are used:
 *
 * @code
 * bool m0_long_{read,write}_lock(struct m0_longlock *lock,
 *                                struct m0_fom *fom, int next_phase);
 * void m0_long_{read,write}_unlock(struct m0_longlock *lock);
 * bool m0_long_is_{read,write}_locked(struct m0_longlock *lock);
 * @endcode
 *
 * m0_long_{read,write}_lock() returns true iff the lock is
 * obtained. If the lock is not obtained (i.e. the return value is
 * false), the subroutine would have arranged to awaken the FOM at the
 * appropriate time to retry the acquisition of the lock.  It is
 * expected that the invoker will return M0_FSO_AGAIN from the state
 * function in this case.
 *
 * m0_long_is_{read,write}_locked() returns true iff the lock has been
 * obtained.
 *
 * The following code example shows how to perform a transition from
 * F_INITIAL to F_SERIALISE and obtain a lock:
 * @code
 * static int fom_fetch_state(struct m0_fom *fom)
 * {
 *      //...
 *      struct m0_long_lock_link *link;
 * 	if (fom->fo_phase == F_INITIAL) {
 * 		// Initialise things.
 *		// ...
 *		// Retreive long lock link from derived FOM object: link = ...;
 *		// and acquire the lock
 *		return M0_FOM_LONG_LOCK_RETURN(m0_long_read_lock(lock,
 *								 link,
 *								 F_SERIALISE));
 *	}
 *	//...
 * }
 * @endcode
 * @see fom-longlock <!-- @todo fom-longlock has to be defined in future -->
 *
 * <hr> <!------------------------------------------------------------>
 * @section confd-lspec-thread Threading and Concurrency Model
 *
 * Confd creates no threads of its own but instead is driven by the
 * request handler. All threading and concurrency is being performed
 * on the Request Handler side, registered in the system.  Incoming
 * FOPs handling, phase transitions, outgoing FOPs serialization,
 * error handling is done in callbacks called by reqh-component.
 *
 * Configuration service relies on rehq component threading model and
 * should not acquire any locks or be in any waiting states, except
 * listed below. Request processing should be performed in an
 * asynchronous-like manner. Only synchronous calls to configuration
 * DB are allowed which should be bracketed with m0_fom_block_{enter,leave}().
 *
 * Multiple concurrently executing FOMs share the same configuration
 * cache and db environment of confd, so access to them is
 * synchronized with the specialized m0_longlock read/write lock
 * designed for use in FOMs: the FOM does not busy-wait, but gets
 * blocked until lock acquisition can be retried. Simplistic
 * synchronization of the database and in-memory cache through means
 * of this read/writer lock (m0_confd::d_cache::ca_lock) is
 * sufficient, as the workload of confd is predominantly read-only.
 *
 * @subsection confd-lspec-numa NUMA Optimizations
 *
 * Multiple confd instances can run in the system, but no more than
 * one per request handler. Each confd has its own data-base back-end
 * and its own pre-loaded copy of data-base in memory.
 *
 * <hr> <!------------------------------------------------------------->
 * @section confd-conformance Conformance
 *
 * - @b i.conf.confd.user
 *   Confd is implemented in user space.
 * - @b i.conf.cache.data-model
 *   Configuration information is organized as outlined in section 4.1
 *   of the HLD. The same data structures are used for confc and
 *   confd.  Configuration structures are kept in memory.
 * - @b i.conf.cache.unique-objects
 *   A registry of cached objects (m0_conf_cache::cc_registry) is used
 *   to achieve uniqueness of configuration object identities.
 *
 * <hr> <!------------------------------------------------------------->
 * @section confd-ut Unit Tests
 *
 * @test obj_serialize() will be tested.
 * @test {fetch,update}_next_state() will be tested.
 *
 * @test Load predefined configuration object from configuration db.
 * Check its predefined value.
 *
 * @test Load predefined configuration directory from db. Check theirs
 * predefined values.
 *
 * @test Fetch non-existent configuration object from configuration db.
 *
 * <hr> <!------------------------------------------------------------->
 * @section confd-O Analysis
 *
 * Size of configuration cache, can be evaluated according to a number
 * of configuration objects in configuration db and is proportional to
 * the size of the database file
 *
 * Configuration request FOP (m0_conf_fetch) is executed in
 * approximately constant time (measured in disk I/O) because the
 * entire configuration db is cached in-memory and rarely would be
 * blocked by an update.
 *
 * @see confd_dlspec
 */

/**
 * @defgroup confd_dlspec confd Internals
 *
 * @see @ref conf, @ref confd-lspec "Logical Specification of confd"
 *
 * @{
 */

const struct m0_bob_type m0_confd_bob = {
	.bt_name         = "m0_confd",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_confd, d_magic),
	.bt_magix        = M0_CONFD_MAGIC
};

static const struct m0_addb_loc confd_addb_loc = {
	.al_name = "confd"
};
static const struct m0_addb_ctx_type confd_addb_ctx_type = {
	.act_name = "confd"
};
static struct m0_addb_ctx confd_addb_ctx;

static int confd_allocate(struct m0_reqh_service_type *stype,
			  struct m0_reqh_service **service);

static const struct m0_reqh_service_type_ops confd_stype_ops = {
	.rsto_service_allocate = confd_allocate
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_confd_stype, &confd_stype_ops, "confd");

M0_INTERNAL int m0_confd_register(void)
{
	return m0_reqh_service_type_register(&m0_confd_stype);
}

M0_INTERNAL void m0_confd_unregister(void)
{
	m0_reqh_service_type_unregister(&m0_confd_stype);
}

static int confd_start(struct m0_reqh_service *service);
static void confd_stop(struct m0_reqh_service *service);
static void confd_fini(struct m0_reqh_service *service);

static const struct m0_reqh_service_ops confd_ops = {
	.rso_start = confd_start,
	.rso_stop  = confd_stop,
	.rso_fini  = confd_fini
};

/** Allocates and initialises confd service. */
static int confd_allocate(struct m0_reqh_service_type *stype,
			  struct m0_reqh_service **service)
{
	struct m0_confd *confd;

	M0_ENTRY();

	m0_addb_ctx_init(&confd_addb_ctx, &confd_addb_ctx_type,
			 &m0_addb_global_ctx);

	M0_ALLOC_PTR_ADDB(confd, &confd_addb_ctx, &confd_addb_loc);
	if (confd == NULL)
		M0_RETURN(-ENOMEM);

#if 1 /* XXX FIXME */
	/* XXX Temporary kludge for "conf-net" demo.
	 * This configuration string is equal to the one in conf/ut/confc.c. */
	confd->d_local_conf = strdup(
"[6: (\"prof\", {1| (\"fs\")}),\n"
"    (\"fs\", {2| ((11, 22),\n"
"                [3: \"par1\", \"par2\", \"par3\"],\n"
"                [3: \"svc-0\", \"svc-1\", \"svc-2\"])}),\n"
"    (\"svc-0\", {3| (1, [1: \"addr0\"], \"node-0\")}),\n"
"    (\"svc-1\", {3| (3, [3: \"addr1\", \"addr2\", \"addr3\"], \"node-1\")}),\n"
"    (\"svc-2\", {3| (2, [0], \"node-1\")}),\n"
"    (\"node-0\", {4| (8000, 2, 3, 2, 0, [2: \"nic-0\", \"nic-1\"],\n"
"                    [1: \"sdev-0\"])})]\n");
#endif
	if (confd->d_local_conf == NULL) {
		m0_free(confd);
		M0_RETURN(-ENOMEM);
	}
	m0_bob_init(&m0_confd_bob, confd);

	*service = &confd->d_reqh;
	(*service)->rs_type = stype;
	(*service)->rs_ops = &confd_ops;

	M0_RETURN(0);
}

/** Finalises and deallocates confd service. */
static void confd_fini(struct m0_reqh_service *service)
{
	struct m0_confd *confd;

	M0_ENTRY();
	M0_PRE(service != NULL);

	confd = bob_of(service, struct m0_confd, d_reqh, &m0_confd_bob);

	m0_bob_fini(&m0_confd_bob, confd);
	m0_free((void *)confd->d_local_conf);
	m0_free(confd);
	m0_addb_ctx_fini(&confd_addb_ctx);

	M0_LEAVE();
}

/**
 * Starts confd service.
 *
 * - Initialises configuration cache if necessary: first start will
 *   create the cache, subsequent starts won't.
 *
 * - Loads configuration cache from the configuration database.
 */
static int confd_start(struct m0_reqh_service *service)
{
	M0_ENTRY();

	/* XXX TODO: mount local storage, ... ? */

	M0_RETURN(0);
}

static void confd_stop(struct m0_reqh_service *service)
{
	M0_ENTRY();

	/* XXX TODO: unmount local storage, ... ? */

	M0_LEAVE();
}

/* /\** */
/*  * m0_conf_fetch FOM phases. */
/*  *\/ */
/* enum m0_confd_fetch_status { */
/* 	F_INITIAL = FOPH_NR + 1, */
/* 	F_SERIALISE, */
/* 	F_TERMINATE, */
/* 	F_FAILURE */
/* }; */

/* /\** */
/*  * m0_conf_update FOM pahses. */
/*  *\/ */
/* enum m0_confd_update_status { */
/* 	U_INITIAL = FOPH_NR + 1, */
/* 	U_UPDATE, */
/* 	U_TERMINATE, */
/* 	U_FAILURE */
/* }; */

/* /\** */
/*  * Serialises given path into FOP-package. */
/*  * */
/*  * @param confd	configuration service instance. */
/*  * @param path path to the object/directory requested by confc. */
/*  * @param fout FOP, prepared to be sent as a reply with m0_rpc_reply_post(). */
/*  * */
/*  * @pre for_each(obj in path) obj.co_status == M0_CS_READY. */
/*  * @pre out is not initialized. */
/*  *\/ */
/* static int obj_serialize(struct m0_confd *confd, struct m0_conf_pathcomp *path, */
/* 			 struct m0_fop *fout) */
/* { */
/* } */

/* /\** */
/*  * Transits fetch FOM into the next phase. */
/*  * */
/*  * @param confd	configuration service instance. */
/*  * @param st current pahse of incoming FOP-request processing. */
/*  *\/ */
/* static int fetch_next_state(struct m0_confd *confd, int st) */
/* { */
/* } */

/* /\** */
/*  * Transits update FOM into the next state. */
/*  * */
/*  * @param confd	configuration service instance. */
/*  * @param st current phase of incoming FOP-request processing. */
/*  *\/ */
/* static int update_next_state(struct m0_confd *confd, int st) */
/* { */
/* } */

/* /\** */
/*  * Called when confd transits to F_FAILURE */
/*  * */
/*  * @param confd	configuration service instance. */
/*  *\/ */
/* static void fetch_failure_handle(struct m0_confd *confd) */
/* { */
/* } */

/* /\** */
/*  * Called when confd transits to U_FAILURE */
/*  * */
/*  * @param confd	configuration service instance. */
/*  *\/ */
/* static void update_failure_handle(struct m0_confd *confd) */
/* { */
/* } */

#undef M0_TRACE_SUBSYSTEM

/** @} confd_dlspec */
