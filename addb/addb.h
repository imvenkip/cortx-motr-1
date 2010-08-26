/* -*- C -*- */

#ifndef __COLIBRI_ADDB_ADDB_H__
#define __COLIBRI_ADDB_ADDB_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/assert.h"

/**
   @defgroup addb Analysis and Diagnostics Data-Base

   @{
*/

struct c2_addb_ctx_type;
struct c2_addb_ctx;
struct c2_addb_loc;
struct c2_addb_ev;
struct c2_addb_dp;
struct c2_addb_rec;

/**
   Common state of addb contexts.

   @see c2_addb_ctx
 */
struct c2_addb_ctx_type {
	const char *act_name;
};

/**
   Activity in context on which addb event happens.

   This can be, for example, FOP processing or storage IO. There is also a
   "global" per address space context for events not related to any identifiable
   activity (e.g., for reception of unsolicited signals).

   There are multiple instances of struct c2_addb_ctx in the system, e.g., one
   for each FOP being processed. State common to contexts of the same "type" is
   described in c2_addb_ctx_type.
 */
struct c2_addb_ctx {
	const struct c2_addb_ctx_type *ac_type;
	struct c2_addb_ctx            *ac_parent;
};

/**
   Part of the system where addb event happened.

   This can be a module (sns, net, fop, etc.) or a part of a module. Instances
   of c2_addb_loc are typically statically allocated.
 */
struct c2_addb_loc {
	const char *al_name;
};

typedef int (*c2_addb_ev_subst_t)(struct c2_addb_dp *dp, ...);

/** Event severity level. */
enum c2_addb_ev_level {
	AEL_TRACE,
	AEL_INFO,
	AEL_NOTE,
	AEL_WARN,
	AEL_ERROR,
	AEL_FATAL
};

struct c2_addb_ev_ops {
	c2_addb_ev_subst_t    aeo_subst;
	size_t                aeo_size;
	const char           *aeo_name;
	enum c2_addb_ev_level aeo_level;
};

/**
   Event of interest for addb.

   This can be "directory entry has been found in cache", "data read took N
   microseconds", "incoming FOP queue length is L", "memory allocation failure",
   etc.

   c2_addb_ev describes a type of event (not a particular instance of
   it). c2_addb_ev instances are typically statically allocated.

   c2_addb_ev accepts "formal parameters" (e.g., actual time it took to read
   data) and produces a "data-point". Related data-points are packed into an
   "addb record" that is used for further analysis.
 */
struct c2_addb_ev {
	const char                  *ae_name;
	uint64_t                     ae_id;
	const struct c2_addb_ev_ops *ae_ops;
	enum c2_addb_ev_level        ae_level;
};

/**
   An instance of addb event packet together with its formal parameters.
 */
struct c2_addb_dp {
	struct c2_addb_ctx       *ad_ctx;
	const struct c2_addb_loc *ad_loc;
	const struct c2_addb_ev  *ad_ev;
	enum c2_addb_ev_level     ad_level;

	/* XXX temporary */
	int         ad_rc;
	const char *ad_name;
};

void c2_addb_ctx_init(struct c2_addb_ctx *ctx, const struct c2_addb_ctx_type *t,
		      struct c2_addb_ctx *parent);
void c2_addb_ctx_fini(struct c2_addb_ctx *ctx);

/**
   Low-level interface posting a data-point to the addb.

   Use this if type-safe interface (C2_ADDB_ADD()) is for some reason
   inadequate.
 */
void c2_addb_add(struct c2_addb_dp *dp);

int  c2_addb_init(void);
void c2_addb_fini(void);

/*
 * The ugly pre-processor code below implements a type-safe interface to addb,
 * guaranteeing at compile time that formal parameters supplied for an addb
 * event match its definition. This is achieved through the mechanisms not
 * entirely unlike to C++ templates.
 */

/**
   Defines an addb event with a given name, identifier and operations vector.

   "ops" MUST be a variable name, usually introduced by C2_ADDB_OPS_DEFINE()
   macro.

   Example:

   @code
   // addb event recording directory entry cache hits and misses during 
   // fop processing
   //
   // This call defines const struct c2_addb_ev reqh_dirent_cache;
   C2_ADDB_EV_DEFINE(reqh_dirent_cache,
                     "sendreply",           // human-readable name
                     REQH_ADDB_DIRENT_CACHE,// unique identifier
		     C2_ADDB_FLAG,          // event type (Boolean: hit or miss)
		     bool flag              // prototype. Must match the 
                                            // prototype in C2_ADDB_FLAG
                                            // definition.
                    );
   @endcode
 */
#define C2_ADDB_EV_DEFINE(var, name, id, ops, ...)			\
const struct c2_addb_ev var = {						\
	.ae_name  = (name),						\
	.ae_id    = (id),						\
	.ae_ops   = &(ops)						\
};									\
									\
typedef typeof(__ ## ops ## _typecheck_t) __ ## var ## _typecheck_t

/**
   Type-safe addb posting interface.

   Composes a data-point for a given event in a given context and a given
   location and posts it to addb.

   Event formal parameters are supplied as variadic arguments. This macro checks
   that their number and types conform to the event definition
   (C2_ADDB_EV_DEFINE(), which it turns conforms to the event operation vector
   definition in C2_ADDB_OPS_DEFINE()).

   "ev" MUST be a variable name, usually introduced by C2_ADDB_EV_DEFINE().

   @code
   hit = c2_dirent_cache_lookup(pdir, name, &entry);
   C2_ADDB_ADD(&fop->f_addb_ctx, &reqh_addb_loc, &reqh_dirent_cache, hit);
   @endcode
 */
#define C2_ADDB_ADD(ctx, loc, ev, ...)				\
({								\
	struct c2_addb_dp __dp;					\
								\
	__dp.ad_ctx   = (ctx);					\
	__dp.ad_loc   = (loc);					\
	__dp.ad_ev    = &(ev);					\
	__dp.ad_level = 0;					\
								\
	(void)sizeof(((__ ## ev ## _typecheck_t *)NULL)		\
		     (&__dp , ## __VA_ARGS__));			\
	if (ev.ae_ops->aeo_subst(&__dp , ## __VA_ARGS__) == 0)	\
		c2_addb_add(&__dp);				\
})

/**
   Declare addb event operations vector with a given collection of formal
   parameter.

   @see C2_ADDB_SYSCALL, C2_ADDB_FUNC_CALL, C2_ADDB_CALL 
   @see C2_ADDB_STAMP, C2_ADDB_FLAG
 */
#define C2_ADDB_OPS_DEFINE(ops, ...)					\
extern const struct c2_addb_ev_ops ops;					\
									\
typedef int								\
__ ## ops ## _typecheck_t(struct c2_addb_dp *dp , ## __VA_ARGS__)

/** A call to an external system component failed. */
C2_ADDB_OPS_DEFINE(C2_ADDB_SYSCALL, int rc);
/** A call to an given function failed. */
C2_ADDB_OPS_DEFINE(C2_ADDB_FUNC_CALL, const char *fname, int rc);
/** A call to an C2 component failed. */
C2_ADDB_OPS_DEFINE(C2_ADDB_CALL, int rc);
/** An invalid value was supplied. */
C2_ADDB_OPS_DEFINE(C2_ADDB_INVAL, uint64_t val);
/** Time-stamp. */
C2_ADDB_OPS_DEFINE(C2_ADDB_STAMP);
/** Record a Boolean condition. */
C2_ADDB_OPS_DEFINE(C2_ADDB_FLAG, bool flag);

/** Report this event when memory allocation fails. */
extern struct c2_addb_ev c2_addb_oom;
typedef int __c2_addb_oom_typecheck_t(struct c2_addb_dp *dp);

/** Report this event when function call fails that doesn't fit into a more
    specific event. */
extern struct c2_addb_ev c2_addb_func_fail;
typedef int __c2_addb_func_fail_typecheck_t(struct c2_addb_dp *dp, 
					    const char *name, int rc);

/** Global (per address space) addb context, used when no other context is
    applicable. */
extern struct c2_addb_ctx c2_addb_global_ctx;

/** @} end of addb group */

/* __COLIBRI_ADDB_ADDB_H__ */
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
