/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-Jan-2012
 */
#pragma once
#ifndef __MERO_CONF_CONFC_H__
#define __MERO_CONF_CONFC_H__

#include "conf/cache.h"   /* m0_conf_cache */
#include "conf/onwire.h"  /* m0_conf_fetch */
#include "lib/buf.h"      /* m0_buf, M0_BUF_INIT0 */
#include "lib/mutex.h"    /* m0_mutex */
#include "sm/sm.h"        /* m0_sm, m0_sm_ast */
#include "rpc/conn.h"     /* m0_rpc_conn */
#include "rpc/session.h"  /* m0_rpc_session */

struct m0_conf_obj;

/**
 * @page confc-fspec Configuration Client (confc)
 *
 * Configuration client library -- confc -- provides user-space and
 * kernel interfaces for accessing Mero configuration information.
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
 * - m0_confc --- an instance of configuration client.
 *   This structure contains m0_conf_cache, which represents
 *   configuration cache.  m0_confc also keeps reference to the state
 *   machine group that synchronizes state machines created by this
 *   confc.
 *
 * - m0_confc_ctx --- configuration retrieval context.
 *   This structure embodies data needed by a state machine to process
 *   configuration request.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-fspec-sub Subroutines
 *
 * - m0_confc_init() initialises configuration cache, creates a stub
 *   for the root object (m0_conf_profile).
 * - m0_confc_fini() finalises confc, destroys configuration cache.
 *
 * - m0_confc_ctx_init() initialises context object, which will be
 *   used by m0_confc_open() function.
 * - m0_confc_ctx_fini() finalises context object.
 *
 * - m0_confc_open() requests asynchronous opening of a configuration object.
 * - m0_confc_open_sync() opens configuration object synchronously.
 * - m0_confc_close() closes configuration object.
 *
 * - m0_confc_ctx_error() returns error status of an asynchronous
 *   configuration retrieval operation.
 * - m0_confc_ctx_result() is used to obtain the resulting
 *   configuration object from m0_confc_ctx.
 *
 * - m0_confc_readdir() gets next directory entry. If the entry is not
 *   cached yet, m0_confc_readdir() initiates asynchronous retrieval
 *   of configuration data.
 * - m0_confc_readdir_sync() gets next directory entry synchronously.
 *
 * <!---------------------------------------------------------------->
 * @subsection confc-fspec-sub-setup Initialization and termination
 *
 * Prior to accessing configuration, the application (aka
 * configuration consumer) should initialise configuration client by
 * calling m0_confc_init().
 *
 * A confc instance is associated with a state machine group
 * (m0_sm_group). A user managing this group is responsible for making
 * sure m0_sm_asts_run() is called when the group's channel is
 * signaled; "AST" section of @ref sm has more details on this topic.
 *
 * m0_confc_fini() terminates confc, destroying configuration cache.
 *
 * @code
 * #include "conf/confc.h"
 * #include "conf/obj.h"
 *
 * struct m0_sm_group    *grp = ...;
 * struct m0_rpc_machine *rpcm = ...;
 * struct m0_confc        confc;
 *
 * startup(const struct m0_buf *profile, ...)
 * {
 *         rc = m0_confc_init(&confc, grp, profile, "confd-ep-addr", rpcm,
 *                            NULL);
 *         ...
 * }
 *
 * ... Access configuration objects, using confc interfaces. ...
 *
 * shutdown(...)
 * {
 *         m0_confc_fini(confc);
 * }
 * @endcode
 *
 * <!---------------------------------------------------------------->
 * @subsection confc-fspec-sub-use Accessing configuration objects
 *
 * The application gets access to configuration data by opening
 * configuration objects with m0_confc_open() or m0_confc_open_sync().
 * Directory objects can be iterated over with m0_confc_readdir() or
 * m0_confc_readdir_sync().
 *
 * m0_confc_open() and m0_confc_readdir() are asynchronous functions.
 * Prior to calling them, the application should initialise a context
 * object (m0_confc_ctx_init()) and register a clink with .sm_chan
 * member of m0_confc_ctx::fc_mach. (See sm_waiter_init() in the @ref
 * confc-fspec-recipe1 "recipe #1" below.)
 *
 * m0_confc_ctx_is_completed() checks whether configuration retrieval
 * has completed, i.e., terminated or failed.
 *
 * m0_confc_ctx_error() returns the error status of an asynchronous
 * configuration retrieval operation. m0_confc_ctx_result() returns
 * the requested configuration object.
 *
 * A caller of m0_confc_open_sync() or m0_confc_readdir_sync() will be
 * blocked while confc is processing the request.
 *
 * All m0_confc_open*()ed configuration objects must be
 * m0_confc_close()ed before m0_confc_fini() is called.
 *
 * @note  Confc library pins (see @ref conf-fspec-obj-pinned) only
 *        those configuration objects that are m0_confc_open*()ed or
 *        m0_confc_readdir*()ed by the application.
 *
 * <hr> <!------------------------------------------------------------>
 * @section confc-fspec-recipes Recipes
 *
 * Configuration objects can be opened asynchronously (m0_confc_open())
 * or synchronously (m0_confc_open_sync()). Many of the examples below
 * use synchronous calls for the sake of brevity.
 *
 * @subsection confc-fspec-recipe1 Getting configuration data of the filesystem
 *
 * @code
 * #include "conf/confc.h"
 * #include "conf/obj.h"
 *
 * struct m0_confc *g_confc = ...;
 *
 * // A sample m0_confc_ctx wrapper.
 * struct sm_waiter {
 *         struct m0_confc_ctx w_ctx;
 *         struct m0_clink     w_clink;
 * };
 *
 * static void sm_waiter_init(struct sm_waiter *w, struct m0_confc *confc);
 * static void sm_waiter_fini(struct sm_waiter *w);
 *
 * // Uses asynchronous m0_confc_open() to get filesystem configuration.
 * static int filesystem_open1(struct m0_conf_filesystem **fs)
 * {
 *         struct sm_waiter w;
 *         int              rc;
 *
 *         sm_waiter_init(&w, g_confc);
 *
 *         m0_confc_open(&w.w_ctx, NULL, M0_BUF_INITS("filesystem"));
 *         while (!m0_confc_ctx_is_completed(&w.w_ctx))
 *                 m0_chan_wait(&w.w_clink);
 *
 *         rc = m0_confc_ctx_error(&w.w_ctx);
 *         if (rc == 0)
 *                 *fs = M0_CONF_CAST(m0_confc_ctx_result(&w.w_ctx),
 *                                    m0_conf_filesystem);
 *
 *         sm_waiter_fini(&w);
 *         return rc;
 * }
 *
 * // Uses synchronous m0_confc_open_sync() to get filesystem configuration.
 * static int filesystem_open2(struct m0_conf_filesystem **fs)
 * {
 *         struct m0_conf_obj *obj;
 *         int                 rc;
 *
 *         rc = m0_confc_open_sync(&obj, g_confc->cc_root,
 *                                 M0_BUF_INITS("filesystem"));
 *         if (rc == 0)
 *                 *fs = M0_CONF_CAST(obj, m0_conf_filesystem);
 *         return rc;
 * }
 *
 * // Filters out intermediate state transitions of m0_confc_ctx::fc_mach.
 * static bool sm_filter(struct m0_clink *link)
 * {
 *         return !m0_confc_ctx_is_completed(&container_of(link,
 *                                                         struct sm_waiter,
 *                                                         w_clink)->w_ctx);
 * }
 *
 * static void sm_waiter_init(struct sm_waiter *w, struct m0_confc *confc)
 * {
 *         m0_confc_ctx_init(&w->w_ctx, confc);
 *         m0_clink_init(&w->w_clink, sm_filter);
 *         m0_clink_add(&w->w_ctx.fc_mach.sm_chan, &w->w_clink);
 * }
 *
 * static void sm_waiter_fini(struct sm_waiter *w)
 * {
 *         m0_clink_del(&w->w_clink);
 *         m0_clink_fini(&w->w_clink);
 *         m0_confc_ctx_fini(&w->w_ctx);
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
 * struct m0_confc *g_confc = ...;
 *
 * static int specific_service_process(enum m0_cfg_service_type tos)
 * {
 *         struct m0_conf_obj *dir;
 *         struct m0_conf_obj *entry;
 *         int                 rc;
 *
 *         rc = m0_confc_open_sync(&dir, g_confc->cc_root,
 *                                 M0_BUF_INITS("filesystem"),
 *                                 M0_BUF_INITS("services"));
 *         if (rc != 0)
 *                 return rc;
 *
 *         for (entry = NULL; (rc = m0_confc_readdir_sync(dir, &entry)) > 0; ) {
 *                 const struct m0_conf_service *svc =
 *                         M0_CONF_CAST(entry, m0_conf_service);
 *
 *                 if (svc->cs_type == tos) {
 *                         // ... Use `svc' ...
 *                 }
 *         }
 *
 *         m0_confc_close(entry);
 *         m0_confc_close(dir);
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
 * struct m0_confc *g_confc = ...;
 *
 * static int node_devices_process(struct m0_conf_obj *node);
 *
 * // Accesses configuration data of devices that are being used by
 * // specific service on specific node.
 * static int specific_devices_process(const struct m0_buf *svc_id,
 *                                     const struct m0_buf *node_id)
 * {
 *         struct m0_conf_obj *dir;
 *         struct m0_conf_obj *svc;
 *         int                 rc;
 *
 *         rc = m0_confc_open_sync(&dir, g_confc->cc_root,
 *                                 M0_BUF_INITS("filesystem"),
 *                                 M0_BUF_INITS("services"));
 *         if (rc != 0)
 *                 return rc;
 *
 *         for (svc = NULL; (rc = m0_confc_readdir_sync(dir, &svc)) > 0; ) {
 *                 struct m0_conf_obj *node;
 *
 *                 if (!m0_buf_eq(svc->co_id, svc_id))
 *                         // This is not the service we are looking for.
 *                         continue;
 *
 *                 rc = m0_confc_open_sync(&node, svc, M0_BUF_INITS("node"));
 *                 if (rc == 0) {
 *                         if (m0_buf_eq(node->co_id, node_id))
 *                                 rc = node_devices_process(node);
 *                         m0_confc_close(node);
 *                 }
 *
 *                 if (rc != 0)
 *                         break;
 *         }
 *
 *         m0_confc_close(svc);
 *         m0_confc_close(dir);
 *         return rc;
 * }
 *
 * static int node_nics_process(struct m0_conf_obj *node);
 * static int node_sdevs_process(struct m0_conf_obj *node);
 *
 * static int node_devices_process(struct m0_conf_obj *node)
 * {
 *         return node_nics_process(node) ?: node_sdevs_process(node);
 * }
 *
 * static int node_nics_process(struct m0_conf_obj *node)
 * {
 *         struct m0_conf_obj *dir;
 *         struct m0_conf_obj *entry;
 *         int                 rc;
 *
 *         rc = m0_confc_open_sync(&dir, node, M0_BUF_INITS("nics"));
 *         if (rc != 0)
 *                 return rc;
 *
 *         for (entry = NULL; (rc = m0_confc_readdir_sync(dir, &entry)) > 0; ) {
 *                 const struct m0_conf_nic *nic =
 *                         M0_CONF_CAST(entry, m0_conf_nic);
 *                 // ... Use `nic' ...
 *         }
 *
 *         m0_confc_close(entry);
 *         m0_confc_close(dir);
 *         return rc;
 * }
 *
 * static int node_sdevs_process(struct m0_conf_obj *node)
 * {
 *         struct m0_conf_obj *dir;
 *         struct m0_conf_obj *entry;
 *         int                 rc;
 *
 *         rc = m0_confc_open_sync(&dir, node, M0_BUF_INITS("sdevs"));
 *         if (rc != 0)
 *                 return rc;
 *
 *         for (entry = NULL; (rc = m0_confc_readdir_sync(dir, &entry)) > 0; ) {
 *                 const struct m0_conf_sdev *sdev =
 *                         M0_CONF_CAST(entry, m0_conf_sdev);
 *                 // ... Use `sdev' ...
 *         }
 *
 *         m0_confc_close(entry);
 *         m0_confc_close(dir);
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
 * #include "lib/arith.h" // M0_CNT_INC
 *
 * struct sm_waiter {
 *         struct m0_confc_ctx w_ctx;
 *         struct m0_clink     w_clink;
 * };
 *
 * // sm_waiter_*() functions are defined in one of the recipes above.
 * static void sm_waiter_init(struct sm_waiter *w, struct m0_confc *confc);
 * static void sm_waiter_fini(struct sm_waiter *w);
 *
 * // Uses configuration data of every object in given directory.
 * static int dir_entries_use(struct m0_conf_obj *dir,
 *                            void (*use)(const struct m0_conf_obj *),
 *                            bool (*stop_at)(const struct m0_conf_obj *))
 * {
 *         struct sm_waiter    w;
 *         int                 rc;
 *         struct m0_conf_obj *entry = NULL;
 *
 *         sm_waiter_init(&w, m0_confc_from_obj(dir));
 *
 *         while ((rc = m0_confc_readdir(&w.w_ctx, dir, &entry)) > 0) {
 *                 if (rc == M0_CONF_DIRNEXT) {
 *                         // The entry is available immediately.
 *                         M0_ASSERT(entry != NULL);
 *
 *                         use(entry);
 *                         if (stop_at != NULL && stop_at(entry)) {
 *                                 rc = 0;
 *                                 break;
 *                         }
 *                         continue; // Note, that `entry' will be
 *                                   // closed by m0_confc_readdir().
 *                 }
 *
 *                 // Cache miss.
 *                 M0_ASSERT(rc == M0_CONF_DIRMISS);
 *                 while (!m0_confc_ctx_is_completed(&w.w_ctx))
 *                         m0_chan_wait(&w.w_clink);
 *
 *                 rc = m0_confc_ctx_error(&w.w_ctx);
 *                 if (rc != 0)
 *                         break; // error
 *
 *                 entry = m0_confc_ctx_result(&w.w_ctx);
 *                 if (entry == NULL)
 *                         break; // end of directory
 *
 *                 use(entry);
 *                 if (stop_at != NULL && stop_at(entry))
 *                         break;
 *
 *                 // Re-initialise m0_confc_ctx.
 *                 sm_waiter_fini(&w);
 *                 sm_waiter_init(&w, m0_confc_from_obj(dir));
 *         }
 *
 *         m0_confc_close(entry);
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
struct m0_confc {
	/**
	 * Serialises configuration retrieval state machines
	 * (m0_confc_ctx::fc_mach).
	 *
	 * Note, that if confc is going to be used in a FOM state
	 * function, ->cc_group should not point to the request
	 * handler's m0_fom_locality::fl_group.  Otherwise confc calls
	 * (e.g., m0_confc_ctx_{init,fini}(), m0_confc_open_sync())
	 * would deadlock, as the group lock is held when the FOM
	 * state function is invoked.
	 */
	struct m0_sm_group      *cc_group;

	/**
	 * Configuration cache.
	 *
	 * m0_conf_cache::ca_lock is being used to protect m0_confc
	 * instance, as well as the DAG of cached configuration
	 * objects, from concurrent modifications.
	 *
	 * If both group and cache locks are needed, group lock must
	 * be acquired first.
	 *
	 * @see confc-lspec-thread
	 */
	struct m0_conf_cache     cc_cache;

	/**
	 * Root of the DAG of configuration objects.
	 *
	 * ->cc_root object is never pinned, as there is no way for
	 * the application to m0_confc_open*() it.  See the note in
	 * @ref confc-fspec-sub-use.
	 */
	struct m0_conf_obj      *cc_root;

	/** Connection to confd. */
	struct m0_rpc_conn       cc_rpc_conn;

	/** RPC session with confd. */
	struct m0_rpc_session    cc_rpc_session;

	/**
	 * The number of configuration retrieval contexts associated
	 * with this m0_confc.
	 *
	 * This value is incremented by m0_confc_ctx_init() and
	 * decremented by m0_confc_ctx_fini().
	 *
	 * @see m0_confc_ctx
	 */
	uint32_t                 cc_nr_ctx;

	/** Magic number. */
	uint64_t                 cc_magic;
};

/**
 * Initialises configuration client.
 *
 * @param confc        A confc instance to be initialised.
 * @param sm_group     State machine group to be associated with this confc.
 * @param profile      Name of profile used by this confc.
 * @param confd_addr   End point address of configuration server (confd).
 * @param rpc_mach     RPC machine that will process configuration RPC items.
 * @param local_conf   Configuration string --- ASCII description of
 *                     configuration data to pre-load the cache with
 *                     (see @ref conf-fspec-preload).
 *
 * @pre  not_empty(confd_addr) || not_empty(local_conf)
 * @pre  ergo(not_empty(confd_addr), rpc_mach != NULL)
 */
M0_INTERNAL int m0_confc_init(struct m0_confc       *confc,
			      struct m0_sm_group    *sm_group,
			      const struct m0_buf   *profile,
			      const char            *confd_addr,
			      struct m0_rpc_machine *rpc_mach,
			      const char            *local_conf);

/**
 * Finalises configuration client. Destroys configuration cache,
 * freeing allocated memory.
 *
 * @pre  confc->cc_nr_ctx == 0
 * @pre  There are no opened (pinned) configuration objects.
 */
M0_INTERNAL void m0_confc_fini(struct m0_confc *confc);

/** Obtains the address of m0_confc that owns given configuration object. */
M0_INTERNAL struct m0_confc *m0_confc_from_obj(const struct m0_conf_obj *obj);

/* ------------------------------------------------------------------
 * context
 * ------------------------------------------------------------------ */

/** Maximum number of path components. */
enum { M0_CONF_PATH_MAX = 15 };

/** Configuration retrieval context. */
struct m0_confc_ctx {
	/** The confc instance this context belongs to. */
	struct m0_confc     *fc_confc;

	/** Context state machine. */
	struct m0_sm         fc_mach;

	/**
	 * Asynchronous system trap, used by the implementation to
	 * schedule a transition of ->fc_mach state machine.
	 */
	struct m0_sm_ast     fc_ast;

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
	struct m0_conf_obj  *fc_origin;

	/** Path to the object being requested by the application. */
	struct m0_buf        fc_path[M0_CONF_PATH_MAX + 1];

	/**
	 * Record of interest in `object loading completed' or
	 * `object unpinned' events.
	 */
	struct m0_clink      fc_clink;

	/**
	 * Pointer to the requested configuration object.
	 *
	 * The application should use m0_confc_ctx_result() instead of
	 * accessing this field directly.
	 */
	struct m0_conf_obj  *fc_result;

	/** RPC item to be delivered to confd. */
	struct m0_rpc_item  *fc_rpc_item;

	/** Magic number. */
	uint64_t             fc_magic;
};

/**
 * Initialises configuration retrieval context.
 * @pre  confc is initialised
 */
M0_INTERNAL void m0_confc_ctx_init(struct m0_confc_ctx *ctx,
				   struct m0_confc *confc);

M0_INTERNAL void m0_confc_ctx_fini(struct m0_confc_ctx *ctx);

/**
 * Returns true iff ctx->fc_mach has terminated or failed.
 *
 * m0_confc_ctx_is_completed() can be used to filter out intermediate
 * state transitions, signaled on ctx->fc_mach.sm_chan channel.
 *
 * @see
 *   - `Filtered wake-ups' section in @ref chan
 *   - @ref confc-fspec-recipe1
 */
M0_INTERNAL bool m0_confc_ctx_is_completed(const struct m0_confc_ctx *ctx);

/**
 * Returns error status of asynchronous configuration retrieval operation.
 *
 * @retval 0      The asynchronous configuration request has completed
 *                successfully.
 * @retval -Exxx  The request has completed unsuccessfully.
 *
 * @pre  m0_confc_ctx_is_completed(ctx)
 */
M0_INTERNAL int32_t m0_confc_ctx_error(const struct m0_confc_ctx *ctx);

/**
 * Retrieves the resulting object of a configuration request.
 *
 * m0_confc_ctx_result() should only be called once, after
 * ctx->fc_mach.sm_chan is signaled and m0_confc_ctx_error()
 * returns 0.
 *
 * m0_confc_ctx_result() sets ctx->fc_result to NULL and returns the
 * original value.
 *
 * @pre   ctx->fc_mach.sm_state == S_TERMINAL
 * @pre   ctx->fc_result != NULL
 * @post  ctx->fc_result == NULL
 */
M0_INTERNAL struct m0_conf_obj *m0_confc_ctx_result(struct m0_confc_ctx *ctx);

/* ------------------------------------------------------------------
 * open/close
 * ------------------------------------------------------------------ */

/**
 * Requests an asynchronous opening of configuration object.
 *
 * @param ctx     Fetch context.
 * @param origin  Path origin (NULL = root configuration object).
 * @param ...     Path to the requested object. Variable arguments --
 *                path components -- are m0_buf initialisers
 *                (M0_BUF_INIT(), M0_BUF_INITS()); use M0_BUF_INIT0
 *                for empty path.  The number of path components
 *                should not exceed M0_CONF_PATH_MAX.
 *
 * @pre  ctx->fc_origin == NULL && ctx->fc_path is empty
 * @pre  ctx->fc_mach.sm_state == S_INITIAL
 * @pre  ergo(origin != NULL, origin->co_cache == &ctx->fc_confc->cc_cache)
 */
#define m0_confc_open(ctx, origin, ...)                           \
	m0_confc__open((ctx), (origin), (const struct m0_buf []){ \
			__VA_ARGS__, M0_BUF_INIT0 })
M0_INTERNAL int m0_confc__open(struct m0_confc_ctx *ctx,
			       struct m0_conf_obj *origin,
			       const struct m0_buf *path);

/**
 * Opens configuration object synchronously.
 *
 * If the call succeeds, *result will point to the requested object.
 *
 * @param result  struct m0_conf_obj **
 * @param origin  Path origin (not NULL).
 * @param ...     Path to the requested object. See m0_confc_open().
 *
 * @pre   origin != NULL
 * @post  ergo(retval == 0, (*result)->co_status == M0_CS_READY)
 *
 * Example:
 * @code
 * struct m0_conf_obj *fs_obj;
 * int rc;
 *
 * rc = m0_confc_open_sync(&fs_obj, confc->cc_root, M0_BUF_INITS("filesystem"));
 * @endcode
 */
#define m0_confc_open_sync(result, origin, ...)                           \
	m0_confc__open_sync((result), (origin), (const struct m0_buf []){ \
			__VA_ARGS__, M0_BUF_INIT0 })
M0_INTERNAL int m0_confc__open_sync(struct m0_conf_obj **result,
				    struct m0_conf_obj *origin,
				    const struct m0_buf *path);

/**
 * Closes configuration object opened with m0_confc_open() or
 * m0_confc_open_sync().
 *
 * m0_confc_close(NULL) is a noop.
 *
 * @pre  ergo(obj != NULL, obj->co_nrefs > 0)
 */
M0_INTERNAL void m0_confc_close(struct m0_conf_obj *obj);

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
 * cache.  In this common case m0_confc_readdir() can fulfil the
 * request immediately. Return values M0_CONF_DIREND and M0_CONF_DIRNEXT
 * inform the caller that it may proceed without waiting for
 * ctx->fc_mach.sm_chan channel to be signaled.
 *
 * @retval M0_CONF_DIRMISS  Asynchronous retrieval of configuration has been
 *                          initiated. The caller should wait.
 * @retval M0_CONF_DIRNEXT  *pptr now points to the next directory entry.
 *                          No waiting is needed.
 * @retval M0_CONF_DIREND   End of directory is reached. No waiting is needed.
 * @retval -Exxx            Error.
 *
 * m0_confc_readdir() puts (m0_conf_obj_put()) the configuration
 * object referred to via `pptr' input parameter.
 *
 * m0_confc_readdir() pins (m0_conf_obj_get()) the resulting object
 * in case of M0_CONF_DIRNEXT.
 *
 * m0_confc_readdir() does not touch `ctx' argument if the returned
 * value is M0_CONF_DIRNEXT or M0_CONF_DIREND. `ctx' can be re-used
 * in this case.
 *
 * @see confc-fspec-recipe4
 *
 * @pre   ctx->fc_mach.sm_state == S_INITIAL
 * @pre   dir->co_type == M0_CO_DIR && dir->co_cache == &ctx->fc_confc->cc_cache
 * @post  ergo(M0_IN(retval, (M0_CONF_DIRNEXT, M0_CONF_DIREND)),
 *             ctx->fc_mach.sm_state == S_INITIAL)
 */
M0_INTERNAL int m0_confc_readdir(struct m0_confc_ctx *ctx,
				 struct m0_conf_obj  *dir,
				 struct m0_conf_obj **pptr);

/**
 * Gets next directory entry synchronously.
 *
 * @param      dir   Directory.
 * @param[in]  pptr  "Current" entry.
 * @param[out] pptr  "Next" entry.
 *
 * @retval M0_CONF_DIRNEXT  *pptr now points to the next directory entry.
 * @retval M0_CONF_DIREND   End of directory is reached.
 * @retval -Exxx            Error.
 *
 * m0_confc_readdir_sync() puts and pins configuration objects
 * similarly to m0_confc_readdir().
 *
 * @see m0_confc_readdir()
 *
 * Example:
 * @code
 * struct m0_conf_obj *entry;
 *
 * for (entry = NULL; (rc = m0_confc_readdir_sync(dir, &entry)) > 0; )
 *         use(entry);
 *
 * m0_confc_close(entry);
 * @endcode
 */
M0_INTERNAL int m0_confc_readdir_sync(struct m0_conf_obj *dir,
				      struct m0_conf_obj **pptr);

/** @} confc_dfspec */
#endif /* __MERO_CONF_CONFC_H__ */
