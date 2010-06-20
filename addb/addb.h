/* -*- C -*- */

#ifndef __COLIBRI_ADDB_ADDB_H__
#define __COLIBRI_ADDB_ADDB_H__

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

   This can be, for example, FOP processing, storage IO, etc. There is also a
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

void c2_addb_ctx_init(struct c2_addb_ctx *ctx, const struct c2_addb_ctx_type *t,
		      struct c2_addb_ctx *parent);
void c2_addb_ctx_fini(struct c2_addb_ctx *ctx);

/**
   Part of the system where addb event happened.

   This can be a module (sns, net, fop, etc.) or a part of a module. Instances
   of c2_addb_loc are typically statically allocated.
 */
struct c2_addb_loc {
	const char *al_name;
};

typedef int (*c2_addb_ev_subst_t)(struct c2_addb_dp *dp, ...);

struct c2_addb_ev_ops {
	c2_addb_ev_subst_t aeo_subst;
	size_t             aeo_size;
	const char        *aeo_name;
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
};

struct c2_addb_dp {
	struct c2_addb_ctx       *ad_ctx;
	const struct c2_addb_loc *ad_loc;
	const struct c2_addb_ev  *ad_ev;

	int ad_rc;
};

void c2_addb_add(struct c2_addb_dp *dp);

int  c2_addb_init(void);
void c2_addb_fini(void);


#define C2_ADDB_EV_DEFINE(var, name, id, ops, ...)			\
const struct c2_addb_ev var = {						\
	.ae_name  = (name),						\
	.ae_id    = (id),						\
	.ae_ops   = &(ops)						\
};									\
									\
typedef typeof(__ ## ops ## _typecheck_t) __ ## var ## _typecheck_t;


#define C2_ADDB_ADD(ctx, loc, ev, ...)				\
({								\
	struct c2_addb_dp __dp;					\
								\
	__dp.ad_ctx = (ctx);					\
	__dp.ad_loc = (loc);					\
	__dp.ad_ev  = &(ev);					\
								\
	(void)sizeof(((__ ## ev ## _typecheck_t *)NULL)		\
		     (&__dp , ## __VA_ARGS__));			\
	if (ev.ae_ops->aeo_subst(&__dp , ## __VA_ARGS__) == 0)	\
		c2_addb_add(&__dp);				\
})

#define C2_ADDB_OPS_DEFINE(ops, ...)					\
extern const struct c2_addb_ev_ops ops;					\
									\
typedef int								\
__ ## ops ## _typecheck_t(struct c2_addb_dp *dp , ## __VA_ARGS__)

/** A call to an external system component failed. */
C2_ADDB_OPS_DEFINE(C2_ADDB_SYSCALL, int rc);
/** A call to an C2 component failed. */
C2_ADDB_OPS_DEFINE(C2_ADDB_CALL, int rc);
/** Time-stamp. */
C2_ADDB_OPS_DEFINE(C2_ADDB_STAMP);
/** Record a Boolean condition. */
C2_ADDB_OPS_DEFINE(C2_ADDB_FLAG, bool flag);

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
