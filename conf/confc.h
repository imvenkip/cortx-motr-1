/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-Jan-2012
 */
#pragma once
#ifndef __COLIBRI_CONF_CONFC_H__
#define __COLIBRI_CONF_CONFC_H__

#include "conf/reg.h"     /* c2_conf_reg */
#include "conf/onwire.h"  /* c2_conf_fetch */
#include "lib/buf.h"      /* c2_buf, C2_BUF_INIT0 */
#include "sm/sm.h"        /* c2_sm, c2_sm_ast */
#include "lib/mutex.h"    /* c2_mutex */
#include "fop/fop.h"      /* c2_fop */

struct c2_conf_obj;

/**
 * @page confc-fspec Configuration Client (confc)
 *
 * Configuration client library -- confc -- provides user-space and
 * kernel interfaces for accessing Colibri configuration information.
 *
 * Confc obtains configuration data from network-accessible
 * configuration server (confd) and caches this data in memory.
 *
 * - @ref confc-fspec-data
 * - @ref confc-fspec-sub
 *   - @ref confc-fspec-sub-setup
 *   - @ref confc-fspec-sub-use
 * - @ref confc-fspec-recipes
 *   - @ref confc-fspec-recipe1
 *   - @ref confc-fspec-recipe2
 *   - @ref confc-fspec-recipe3
 *   - @ref confc-fspec-recipe4
 * - @ref confc_dfspec "Detailed Functional Specification"
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-fspec-data Data Structures
 *
 * - c2_confc --- an instance of configuration client.
 *   This structure contains configuration cache and a lock protecting
 *   the cache from concurrent writes.  c2_confc also keeps reference
 *   to the state machine group that synchronizes state machines
 *   created by this confc.
 *
 * - c2_confc_ctx --- configuration retrieval context.
 *   This structure embodies data needed by a state machine to process
 *   configuration request.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-fspec-sub Subroutines
 *
 * - c2_confc_init() initialises configuration cache, creates a stub
 *   for the root object (c2_conf_profile).
 * - c2_confc_fini() finalises confc, destroys configuration cache.
 *
 * - c2_confc_ctx_init() initialises context object, which will be
 *   used by c2_confc_open() function.
 * - c2_confc_ctx_fini() finalises context object.
 *
 * - c2_confc_open() requests asynchronous opening of a configuration object.
 * - c2_confc_open_sync() opens configuration object synchronously.
 * - c2_confc_close() closes configuration object.
 *
 * - c2_confc_ctx_error() returns error status of an asynchronous
 *   configuration retrieval operation.
 * - c2_confc_ctx_result() is used to obtain the resulting
 *   configuration object from c2_confc_ctx.
 *
 * - c2_confc_readdir() gets next directory entry. If the entry is not
 *   cached yet, c2_confc_readdir() initiates asynchronous retrieval
 *   of configuration data.
 * - c2_confc_readdir_sync() gets next directory entry synchronously.
 *
 * <!---------------------------------------------------------------->
 * @subsection confc-fspec-sub-setup Initialization and termination
 *
 * Prior to accessing configuration, the application (aka
 * configuration consumer) should initialise configuration client by
 * calling c2_confc_init().
 *
 * A confc instance is associated with a state machine group
 * (c2_sm_group). A user managing this group is responsible for making
 * sure c2_sm_asts_run() is called when the group's channel is
 * signaled; "AST" section of @ref sm has more details on this topic.
 *
 * c2_confc_fini() terminates confc, destroying configuration cache.
 *
 * @code
 * #include "conf/confc.h"
 * #include "conf/obj.h"
 *
 * struct c2_sm_group *group = ...;
 * struct c2_confc     confc;
 *
 * startup(const struct c2_buf *profile, ...)
 * {
 *         rc = c2_confc_init(&confc, "confd-endpoint", profile, group);
 *         ...
 * }
 *
 * ... Access configuration objects, using confc interfaces. ...
 *
 * shutdown(...)
 * {
 *         c2_confc_fini(confc);
 * }
 * @endcode
 *
 * <!---------------------------------------------------------------->
 * @subsection confc-fspec-sub-use Accessing configuration objects
 *
 * The application gets access to configuration data by opening
 * configuration objects with c2_confc_open() or c2_confc_open_sync().
 * Directory objects can be iterated over with c2_confc_readdir() or
 * c2_confc_readdir_sync().
 *
 * c2_confc_open() and c2_confc_readdir() are asynchronous functions.
 * Prior to calling them, the application should initialise a context
 * object (c2_confc_ctx_init()) and register a clink with .sm_chan
 * member of c2_confc_ctx::fc_mach.
 *
 * c2_confc_ctx_is_completed() checks whether configuration retrieval
 * has completed, i.e., terminated or failed.
 *
 * c2_confc_ctx_error() returns the error status of an asynchronous
 * configuration retrieval operation. c2_confc_ctx_result() returns
 * the requested configuration object.
 *
 * A caller of c2_confc_open_sync() or c2_confc_readdir_sync() will be
 * blocked while confc is processing the request.
 *
 * All c2_confc_open*()ed configuration objects must be
 * c2_confc_close()ed before c2_confc_fini() is called.
 *
 * @note  Confc library pins (see @ref conf-fspec-obj-pinned) only
 *        those configuration objects that are c2_confc_open*()ed or
 *        c2_confc_readdir*()ed by the application.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-fspec-recipes Recipes
 *
 * Configuration objects can be opened asynchronously (c2_confc_open())
 * or synchronously (c2_confc_open_sync()). Many of the examples below
 * use synchronous calls for the sake of brevity.
 *
 * @subsection confc-fspec-recipe1 Getting configuration data of the filesystem
 *
 * @code
 * #include "conf/confc.h"
 * #include "conf/obj.h"
 *
 * struct c2_confc *g_confc = ...;
 *
 * // A sample c2_confc_ctx wrapper.
 * struct sm_waiter {
 *         struct c2_confc_ctx w_ctx;
 *         struct c2_clink     w_clink;
 * };
 *
 * static void sm_waiter_init(struct sm_waiter *w, struct c2_confc *confc);
 * static void sm_waiter_fini(struct sm_waiter *w);
 *
 * // Uses asynchronous c2_confc_open() to get filesystem configuration.
 * static int filesystem_open1(struct c2_conf_filesystem **fs)
 * {
 *         struct sm_waiter w;
 *         int              rc;
 *
 *         sm_waiter_init(&w, g_confc);
 *
 *         rc = c2_confc_open(&w.w_ctx, NULL, C2_BUF_INITS("filesystem"));
 *         if (rc == 0) {
 *                 while (!c2_confc_ctx_is_completed(&w.w_ctx))
 *                         c2_chan_wait(&w.w_clink);
 *
 *                 rc = c2_confc_ctx_error(&w.w_ctx);
 *                 if (rc == 0)
 *                         *fs = C2_CONF_CAST(c2_confc_ctx_result(&w.w_ctx),
 *                                            c2_conf_filesystem);
 *         }
 *
 *         sm_waiter_fini(&w);
 *         return rc;
 * }
 *
 * // Uses synchronous c2_confc_open_sync() to get filesystem configuration.
 * static int filesystem_open2(struct c2_conf_filesystem **fs)
 * {
 *         struct c2_conf_obj *obj;
 *         int                 rc;
 *
 *         rc = c2_confc_open_sync(&obj, g_confc->cc_root,
 *                                 C2_BUF_INITS("filesystem"));
 *         if (rc == 0)
 *                 *fs = C2_CONF_CAST(obj, c2_conf_filesystem);
 *         return rc;
 * }
 *
 * // Filters out intermediate state transitions of c2_confc_ctx::fc_mach.
 * static bool sm_filter(struct c2_clink *link)
 * {
 *         return !c2_confc_ctx_is_completed(&container_of(link,
 *                                                         struct sm_waiter,
 *                                                         w_clink)->w_ctx);
 * }
 *
 * static void sm_waiter_init(struct sm_waiter *w, struct c2_confc *confc)
 * {
 *         c2_confc_ctx_init(&w->w_ctx, confc);
 *         c2_clink_init(&w->w_clink, sm_filter);
 *         c2_clink_add(&w->w_ctx.fc_mach.sm_chan, &w->w_clink);
 * }
 *
 * static void sm_waiter_fini(struct sm_waiter *w)
 * {
 *         c2_clink_del(&w->w_clink);
 *         c2_clink_fini(&w->w_clink);
 *         c2_confc_ctx_fini(&w->w_ctx);
 * }
 * @endcode
 *
 * <!---------------------------------------------------------------->
 * @subsection confc-fspec-recipe2 Service configuration
 * Getting configuration of a service of specific type.
 *
 * @code
 * #include "conf/confc.h"
 * #include "conf/obj.h"
 *
 * struct c2_confc *g_confc = ...;
 *
 * static int specific_service_process(enum c2_cfg_service_type tos)
 * {
 *         struct c2_conf_obj *dir;
 *         struct c2_conf_obj *entry;
 *         int                 rc;
 *
 *         rc = c2_confc_open_sync(&dir, g_confc->cc_root,
 *                                 C2_BUF_INITS("filesystem"),
 *                                 C2_BUF_INITS("services"));
 *         if (rc != 0)
 *                 return rc;
 *
 *         for (entry = NULL; (rc = c2_confc_readdir_sync(dir, &entry)) > 0; ) {
 *                 const struct c2_conf_service *svc =
 *                         C2_CONF_CAST(entry, c2_conf_service);
 *
 *                 if (svc->cs_type == tos) {
 *                         // ... Use `svc' ...
 *                 }
 *         }
 *
 *         c2_confc_close(entry);
 *         c2_confc_close(dir);
 *         return rc;
 * }
 * @endcode
 *
 * <!---------------------------------------------------------------->
 * @subsection confc-fspec-recipe3 List devices
 * List devices used by specific service on specific node.
 *
 * @code
 * #include "conf/confc.h"
 * #include "conf/obj.h"
 *
 * struct c2_confc *g_confc = ...;
 *
 * static int node_devices_process(struct c2_conf_obj *node);
 *
 * // Accesses configuration data of devices that are being used by
 * // specific service on specific node.
 * static int specific_devices_process(const struct c2_buf *svc_id,
 *                                     const struct c2_buf *node_id)
 * {
 *         struct c2_conf_obj *dir;
 *         struct c2_conf_obj *svc;
 *         int                 rc;
 *
 *         rc = c2_confc_open_sync(&dir, g_confc->cc_root,
 *                                 C2_BUF_INITS("filesystem"),
 *                                 C2_BUF_INITS("services"));
 *         if (rc != 0)
 *                 return rc;
 *
 *         for (svc = NULL; (rc = c2_confc_readdir_sync(dir, &svc)) > 0; ) {
 *                 struct c2_conf_obj *node;
 *
 *                 if (!c2_buf_eq(svc->co_id, svc_id))
 *                         // This is not the service we are looking for.
 *                         continue;
 *
 *                 rc = c2_confc_open_sync(&node, svc, C2_BUF_INITS("node"));
 *                 if (rc == 0) {
 *                         if (c2_buf_eq(node->co_id, node_id))
 *                                 rc = node_devices_process(node);
 *                         c2_confc_close(node);
 *                 }
 *
 *                 if (rc != 0)
 *                         break;
 *         }
 *
 *         c2_confc_close(svc);
 *         c2_confc_close(dir);
 *         return rc;
 * }
 *
 * static int node_nics_process(struct c2_conf_obj *node);
 * static int node_sdevs_process(struct c2_conf_obj *node);
 *
 * static int node_devices_process(struct c2_conf_obj *node)
 * {
 *         return node_nics_process(node) ?: node_sdevs_process(node);
 * }
 *
 * static int node_nics_process(struct c2_conf_obj *node)
 * {
 *         struct c2_conf_obj *dir;
 *         struct c2_conf_obj *entry;
 *         int                 rc;
 *
 *         rc = c2_confc_open_sync(&dir, node, C2_BUF_INITS("nics"));
 *         if (rc != 0)
 *                 return rc;
 *
 *         for (entry = NULL; (rc = c2_confc_readdir_sync(dir, &entry)) > 0; ) {
 *                 const struct c2_conf_nic *nic =
 *                         C2_CONF_CAST(entry, c2_conf_nic);
 *                 // ... Use `nic' ...
 *         }
 *
 *         c2_confc_close(entry);
 *         c2_confc_close(dir);
 *         return rc;
 * }
 *
 * static int node_sdevs_process(struct c2_conf_obj *node)
 * {
 *         struct c2_conf_obj *dir;
 *         struct c2_conf_obj *entry;
 *         int                 rc;
 *
 *         rc = c2_confc_open_sync(&dir, node, C2_BUF_INITS("sdevs"));
 *         if (rc != 0)
 *                 return rc;
 *
 *         for (entry = NULL; (rc = c2_confc_readdir_sync(dir, &entry)) > 0; ) {
 *                 const struct c2_conf_sdev *sdev =
 *                         C2_CONF_CAST(entry, c2_conf_sdev);
 *                 // ... Use `sdev' ...
 *         }
 *
 *         c2_confc_close(entry);
 *         c2_confc_close(dir);
 *         return rc;
 * }
 * @endcode
 *
 * <!---------------------------------------------------------------->
 * @subsection confc-fspec-recipe4 Iterate directory object asynchronously
 *
 * @code
 * #include "conf/confc.h"
 * #include "conf/obj.h"
 * #include "lib/arith.h" // C2_CNT_INC
 *
 * struct c2_confc *g_confc = ...;
 *
 * struct sm_waiter {
 *         struct c2_confc_ctx w_ctx;
 *         struct c2_clink     w_clink;
 * };
 *
 * // sm_waiter_*() functions are defined in one of the recipes above.
 * static void sm_waiter_init(struct sm_waiter *w, struct c2_confc *confc);
 * static void sm_waiter_fini(struct sm_waiter *w);
 *
 * // Uses configuration data of every object in given directory.
 * static int dir_entries_use(struct c2_conf_dir *dir,
 *                            void (*use)(const struct c2_conf_obj *),
 *                            bool (*stop_at)(const struct c2_conf_obj *))
 * {
 *         struct sm_waiter    w;
 *         int                 rc;
 *         struct c2_conf_obj *entry = NULL;
 *
 *         sm_waiter_init(&w, g_confc);
 *
 *         while ((rc = c2_confc_readdir(&w.w_ctx, dir, &entry)) > 0) {
 *                 if (rc == C2_CONF_DIRNEXT) {
 *                         // The entry is available immediately.
 *                         C2_ASSERT(entry != NULL);
 *                         use(entry);
 *                         continue; // Note, that `entry' will be
 *                                   // closed by c2_confc_readdir().
 *                 }
 *
 *                 // Cache miss.
 *                 C2_ASSERT(rc == C2_CONF_DIRMISS);
 *                 while (!c2_confc_ctx_is_completed(&w.w_ctx))
 *                         c2_chan_wait(&w.w_clink);
 *
 *                 rc = c2_confc_ctx_error(&w.w_ctx);
 *                 if (rc != 0)
 *                         break; // error
 *
 *                 entry = c2_confc_ctx_result(&w.w_ctx);
 *                 if (entry == NULL)
 *                         break; // end of directory
 *
 *                 use(entry);
 *                 if (stop_at != NULL && stop_at(entry))
 *                         break;
 *
 *                 // Re-initialise c2_confc_ctx.
 *                 sm_waiter_fini(&w);
 *                 sm_waiter_init(&w);
 *         }
 *
 *         c2_confc_close(entry);
 *         sm_waiter_fini(&w);
 *         return rc;
 * }
 * @endcode
 *
 * @see @ref confc_dfspec "Detailed Functional Specification"
 */

/**
 * @defgroup confc_dfspec Configuration Client (confc)
 * @brief Detailed Functional Specification.
 *
 * @see @ref conf, @ref confc-fspec "Functional Specification"
 *
 * @{
 */

/* ------------------------------------------------------------------
 * confc instance
 * ------------------------------------------------------------------ */

/** Configuration client. */
struct c2_confc {
	/** Registry of cached configuration objects. */
	struct c2_conf_reg       cc_registry;
	/**
	 * Root of the DAG of configuration objects.
	 *
	 * ->cc_root is never pinned, because there is no way for the
	 * application to open it.  See the note in @ref
	 * confc-fspec-sub-use.
	 */
	struct c2_conf_obj      *cc_root;
	/**
	 * Serialises configuration retrieval state machines
	 * (c2_confc_ctx::fc_mach).
	 *
	 * Note, that if confc is going to be used in a FOM state
	 * function, ->cc_group should not point to the request
	 * handler's c2_fom_locality::fl_group.  Otherwise confc calls
	 * (e.g., c2_confc_ctx_{init,fini}(), c2_confc_open_sync())
	 * would deadlock, as the group lock is held when the FOM
	 * state function is invoked.
	 */
	struct c2_sm_group      *cc_group;
	/**
	 * Confc lock (aka cache lock).
	 *
	 * Protects this structure and the DAG of cached configuration
	 * objects from concurrent modifications.
	 *
	 * Rationale: while ->cc_group ensures that there are no
	 * concurrent state transitions, it has no influence on the
	 * application, which may modify configuration cache by
	 * calling c2_confc_close() or c2_confc_fini().
	 *
	 * If both group and cache locks are needed, group lock must
	 * be acquired first.
	 *
	 * @see confc-lspec-thread
	 */
	struct c2_mutex          cc_lock;
#if 0 /* XXX */
	/*
	 * https://reviewboard.clusterstor.com/r/939/diff/3/?file=26037#file26037line536 :
	 *
	 * > [* defect *] c2_rpc_client_ctx is useable mostly in tests
	 * > and simplest programs like rpc-ping, because it assumes a
	 * > single connection per-rpcmachine.
	 *
	 * What should be used here then? c2_rpc_machine?
	 * TODO
	 */
	/** RPC client context that represents connection to confd. */
	struct c2_rpc_client_ctx cc_rpc;
#endif
	/**
	 * The number of configuration retrieval contexts associated
	 * with this c2_confc.
	 *
	 * This value is incremented by c2_confc_ctx_init() and
	 * decremented by c2_confc_ctx_fini().
	 *
	 * @see c2_confc_ctx
	 */
	uint32_t                 cc_nr_ctx;
	/** Magic number. */
	uint64_t                 cc_magic;
};

/**
 * Initialises configuration client.
 *
 * @param confc        A confc instance to be initialised.
 * @param conf_source  End point address of configuration server (confd).
 *                     If the value is prefixed with "local-conf:", it
 *                     is a configuration string --- ASCII description
 *                     of configuration data to pre-load the cache with
 *                     (see @ref conf-fspec-preload).
 * @param profile      Name of profile used by this confc.
 * @param sm_group     State machine group to be associated with confc
 *                     configuration cache.
 */
int c2_confc_init(struct c2_confc *confc, const char *conf_source,
		  const struct c2_buf *profile,
		  struct c2_sm_group *sm_group);

/**
 * Finalises configuration client. Destroys configuration cache,
 * freeing allocated memory.
 *
 * @pre  confc->cc_nr_ctx == 0
 * @pre  There are no opened (pinned) configuration objects.
 */
void c2_confc_fini(struct c2_confc *confc);

/* ------------------------------------------------------------------
 * context
 * ------------------------------------------------------------------ */

/** Configuration retrieval context. */
struct c2_confc_ctx {
	/** The confc instance this context belongs to. */
	struct c2_confc     *fc_confc;
	/** Context state machine. */
	struct c2_sm         fc_mach;
	/**
	 * Asynchronous system trap, used by the implementation to
	 * schedule a transition of ->fc_mach state machine.
	 */
	struct c2_sm_ast     fc_ast;
	/** Provides AST's callback with an integer value. */
	int32_t              fc_ast_datum;
	/**
	 * Origin of the requested path.
	 *
	 * ->fc_origin is not pinned unless it is opened by the
	 * application. Confc library does not take any special
	 * measures to pin this object for the duration of path
	 * traversal.  See the note in @ref confc-fspec-sub-use.
	 */
	struct c2_conf_obj  *fc_origin;
	/**
	 * Path to the object being requested by the application.
	 *
	 * It is responsibility of application's programmer to ensure
	 * validity of the path until configuration request completes.
	 */
	const struct c2_buf *fc_path;
	/** Configuration fetch request being sent to confd. */
	struct c2_conf_fetch fc_req;
	/** Request fop. */
	struct c2_fop        fc_fop;
	/**
	 * Record of interest in `object loading completed' or
	 * `object unpinned' events.
	 */
	struct c2_clink      fc_clink;
	/**
	 * Pointer to the requested configuration object.
	 *
	 * The application should use c2_confc_ctx_result() instead of
	 * accessing this field directly.
	 */
	struct c2_conf_obj  *fc_result;
	/** Magic number. */
	uint64_t             fc_magic;
};

/**
 * Initialises configuration retrieval context.
 * @pre  confc is initialised
 */
void c2_confc_ctx_init(struct c2_confc_ctx *ctx, struct c2_confc *confc);

void c2_confc_ctx_fini(struct c2_confc_ctx *ctx);

/**
 * Returns true iff ctx->fc_mach has terminated or failed.
 *
 * c2_confc_ctx_is_completed() can be used to filter out intermediate
 * state transitions, signaled on ctx->fc_mach.sm_chan channel.
 *
 * @see
 *   - `Filtered wake-ups' section in @ref chan
 *   - @ref confc-fspec-recipe1
 */
bool c2_confc_ctx_is_completed(const struct c2_confc_ctx *ctx);

/**
 * Returns error status of asynchronous configuration retrieval operation.
 *
 * @retval 0      The asynchronous configuration request has completed
 *                successfully.
 * @retval -Exxx  The request has completed unsuccessfully.
 *
 * @pre  c2_confc_ctx_is_completed(ctx)
 */
int32_t c2_confc_ctx_error(const struct c2_confc_ctx *ctx);

/**
 * Retrieves the resulting object of a configuration request.
 *
 * c2_confc_ctx_result() should only be called once, after
 * ctx->fc_mach.sm_chan is signaled and c2_confc_ctx_error()
 * returns 0.
 *
 * c2_confc_ctx_result() sets ctx->fc_result to NULL and returns the
 * original value.
 *
 * @pre   ctx->fc_mach.sm_state == S_TERMINAL
 * @pre   ctx->fc_result != NULL
 * @post  ctx->fc_result == NULL
 */
struct c2_conf_obj *c2_confc_ctx_result(struct c2_confc_ctx *ctx);

/* ------------------------------------------------------------------
 * open/close
 * ------------------------------------------------------------------ */

/**
 * Requests an asynchronous opening of configuration object.
 *
 * @param ctx     Fetch context.
 * @param origin  Path origin (NULL = root configuration object).
 * @param ...     Path to the requested object. The variable arguments
 *                are c2_buf initialisers (C2_BUF_INIT(), C2_BUF_INITS());
 *                use C2_BUF_INIT0 for empty path.
 *
 * @note  The application must keep the data, pointed to by path
 *        arguments, intact, until configuration retrieval operation
 *        completes.
 *
 * @todo XXX FIXME:
 *       c2_confc_open() constructs an array of c2_bufs, which is
 *       created on stack. If a calling thread leaves the block with
 *       c2_confc_open(), the array will be destructed, invalidating
 *       c2_confc_ctx::fc_path.  This will result in segmentation
 *       fault, if there is still configuration retrieving operation
 *       going on.
 *       .
 *       One possible solution is to make c2_confc_ctx::fc_path an
 *       array of N c2_bufs, where N is maximal possible number of
 *       path components.
 *       .
 *       See https://reviewboard.clusterstor.com/r/1067/diff/1/?file=31415#file31415line683 .
 *
 * @pre  ergo(origin != NULL, origin->co_confc == ctx->fc_confc)
 * @pre  ctx->fc_origin == NULL && ctx->fc_path == NULL
 */
#define c2_confc_open(ctx, origin, ...)                           \
	c2_confc__open((ctx), (origin), (const struct c2_buf []){ \
			__VA_ARGS__, C2_BUF_INIT0 })
int c2_confc__open(struct c2_confc_ctx *ctx, struct c2_conf_obj *origin,
		   const struct c2_buf path[]);

/**
 * Opens configuration object synchronously.
 *
 * If the call succeeds, *result will point to the requested object.
 *
 * @param result  struct c2_conf_obj **
 * @param origin  Path origin (not NULL).
 * @param ...     Path to the requested object. The variable arguments
 *                are c2_buf initialisers (C2_BUF_INIT(), C2_BUF_INITS());
 *                use C2_BUF_INIT0 for empty path.
 *
 * @pre   origin != NULL
 * @post  ergo(retval == 0, (*result)->co_status == C2_CS_READY)
 *
 * Example:
 * @code
 * struct c2_conf_obj *fs_obj;
 * int rc;
 *
 * rc = c2_confc_open_sync(&fs_obj, confc->cc_root, C2_BUF_INITS("filesystem"));
 * @endcode
 */
#define c2_confc_open_sync(result, origin, ...)                           \
	c2_confc__open_sync((result), (origin), (const struct c2_buf []){ \
			__VA_ARGS__, C2_BUF_INIT0 })
int c2_confc__open_sync(struct c2_conf_obj **result, struct c2_conf_obj *origin,
			const struct c2_buf path[]);

/**
 * Closes configuration object opened with c2_confc_open() or
 * c2_confc_open_sync().
 *
 * c2_confc_close(NULL) is a noop.
 *
 * @pre  ergo(obj != NULL, obj->co_nrefs > 0)
 */
void c2_confc_close(struct c2_conf_obj *obj);

/* ------------------------------------------------------------------
 * readdir
 * ------------------------------------------------------------------ */

/**
 * Requests asynchronous retrieval of the next directory entry.
 *
 * @param      ctx   Fetch context.
 * @param      dir   Directory.
 * @param[in]  pptr  "Current" entry.
 * @param[out] pptr  "Next" entry.
 *
 * Entries of a directory are usually present in the configuration
 * cache.  In this common case c2_confc_readdir() can fulfil the
 * request immediately. Return value C2_CONF_DIREND or C2_CONF_DIRNEXT
 * let the caller know that it can proceed without waiting for
 * ctx->fc_mach.sm_chan channel to be signaled.
 *
 * @retval C2_CONF_DIRMISS  Asynchronous retrieval of configuration has been
 *                          initiated. The caller should wait.
 * @retval C2_CONF_DIRNEXT  *pptr now points to the next directory entry.
 *                          No waiting is needed.
 * @retval C2_CONF_DIREND   End of directory is reached. No waiting is needed.
 * @retval -Exxx            Error.
 *
 * c2_confc_readdir() closes configuration object referred to via
 * `pptr' input parameter.
 *
 * c2_confc_readdir() pins the resulting object in case of
 * C2_CONF_DIRNEXT.
 *
 * c2_confc_readdir() does not touch `ctx' argument, if the returned
 * value is C2_CONF_DIRNEXT or C2_CONF_DIREND. `ctx' can be re-used
 * in this case.
 *
 * @see confc-fspec-recipe4
 *
 * @pre   ctx->fc_mach.sm_state == S_INITIAL
 * @post  ergo(C2_IN(retval, (C2_CONF_DIRNEXT, C2_CONF_DIREND)),
 *             ctx->fc_mach.sm_state == S_INITIAL)
 */
int c2_confc_readdir(struct c2_confc_ctx *ctx, struct c2_conf_obj *dir,
		     struct c2_conf_obj **pptr);

/**
 * Gets next directory entry synchronously.
 *
 * @param      dir   Directory.
 * @param[in]  pptr  "Current" entry.
 * @param[out] pptr  "Next" entry.
 *
 * @retval C2_CONF_DIRNEXT  *pptr now points to the next directory entry.
 * @retval C2_CONF_DIREND   End of directory is reached.
 * @retval -Exxx            Error.
 *
 * c2_confc_readdir_sync() closes configuration object referred to via
 * `pptr' input parameter.
 *
 * c2_confc_readdir_sync() pins the resulting object in case of
 * C2_CONF_DIRNEXT.
 *
 * Example:
 * @code
 * struct c2_conf_obj *entry;
 *
 * for (entry = NULL; (rc = c2_confc_readdir_sync(dir, &entry)) > 0; )
 *         use(entry);
 *
 * c2_confc_close(entry);
 * @endcode
 */
int c2_confc_readdir_sync(struct c2_conf_obj *dir, struct c2_conf_obj **pptr);

/** @} confc_dfspec */
#endif /* __COLIBRI_CONF_CONFC_H__ */
