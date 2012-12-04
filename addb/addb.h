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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 06/19/2010
 */

#pragma once

#ifndef __MERO_ADDB_ADDB_H__
#define __MERO_ADDB_ADDB_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/assert.h"
#include "lib/adt.h"
#include "lib/list.h"

/**
   @defgroup addb Analysis and Diagnostics Data-Base

   @{
*/

struct m0_addb_ctx_type;
struct m0_addb_ctx;
struct m0_addb_loc;
struct m0_addb_ev;
struct m0_addb_dp;
struct m0_addb_rec;
enum m0_addb_ev_level;
struct m0_dbenv;
struct m0_dtx;


/* these are needed earlier than they are defined */
struct m0_stob;
struct m0_table;
struct m0_net_conn;

/**
   ADDB record store type

   ADDB record can be populated and stored in various ways:
   STOB:    ADDB records will be stored in stob.
   DB:      ADDB records will be stored in database.
   NETWORK: ADDB records will be sent onto network.

   Corresponding operation will be called according to this type in
   m0_addb_add();
 */
enum m0_addb_rec_store_type {
	M0_ADDB_REC_STORE_NONE    = 0,
	M0_ADDB_REC_STORE_STOB    = 1,
	M0_ADDB_REC_STORE_DB      = 2,
	M0_ADDB_REC_STORE_NETWORK = 3
};


/**
   Common state of addb contexts.

   @see m0_addb_ctx
 */
struct m0_addb_ctx_type {
	const char *act_name;
};

/**
    Write addb records into this stob.
 */
typedef int (*m0_addb_stob_add_t)(struct m0_addb_dp *dp, struct m0_dtx *tx,
				  struct m0_stob *stob);
M0_INTERNAL int m0_addb_stob_add(struct m0_addb_dp *dp, struct m0_dtx *tx,
				 struct m0_stob *stob);

/**
    Write addb records into this db.
 */
typedef int (*m0_addb_db_add_t)(struct m0_addb_dp *dp, struct m0_dbenv *dbenv,
				struct m0_table *db);
M0_INTERNAL int m0_addb_db_add(struct m0_addb_dp *dp, struct m0_dbenv *dbenv,
			       struct m0_table *db);

/**
    Send addb records through this network connection.
 */
typedef int (*m0_addb_net_add_t)(struct m0_addb_dp *dp, struct m0_net_conn *);
M0_INTERNAL int m0_addb_net_add(struct m0_addb_dp *dp, struct m0_net_conn *);

M0_INTERNAL int
m0_addb_choose_store_media(enum m0_addb_rec_store_type type, ...);



/**
   Activity in context on which addb event happens.

   This can be, for example, FOP processing or storage IO. There is also a
   "global" per address space context for events not related to any identifiable
   activity (e.g., for reception of unsolicited signals).

   There are multiple instances of struct m0_addb_ctx in the system, e.g., one
   for each FOP being processed. State common to contexts of the same "type" is
   described in m0_addb_ctx_type.

   @see m0_addb_ctx_type
 */
struct m0_addb_ctx {
	const struct m0_addb_ctx_type *ac_type;
	struct m0_addb_ctx            *ac_parent;
};

/**
   Part of the system where addb event happened.

   This can be a module (sns, net, fop, etc.) or a part of a module. Instances
   of m0_addb_loc are typically statically allocated.
 */
struct m0_addb_loc {
	const char *al_name;
};

typedef int (*m0_addb_ev_subst_t)(struct m0_addb_dp *dp, ...);

/** Event severity level. */
enum m0_addb_ev_level {
	AEL_NONE = 0,
	AEL_TRACE,
	AEL_INFO,
	AEL_NOTE,
	AEL_WARN,
	AEL_ERROR,
	AEL_FATAL,
	AEL_MAX = AEL_FATAL
};

/**
   ADDB record header (on-disk & on-wire)

   This header is always followed by actual addb record body, in memory or
   on disk. Magic is to check validity. The @arh_len is the total record length,
   including header and opaque body. Event ID can be used to identify the event
   type. Event ID should be unique in system wide. There should be a mechanism
   to keep the uniqueness of the event id.

   @note the record length should keep 64bit aligned.
*/
struct m0_addb_record_header;
/**
   ADDB record (on-wire)
*/
struct m0_addb_record;

enum {
	/** addb record size alignment */
	M0_ADDB_RECORD_LEN_ALIGN = 8,
};

/**
   Pack the record header into a buffer.

   @param dp the data point
   @param header pointer to the header within the buffer
   @param size total size of the record
 */
M0_INTERNAL int m0_addb_record_header_pack(struct m0_addb_dp *dp,
					   struct m0_addb_record_header *header,
					   int size);

/**
   Packing this event into a buffer.

   @param dp the data point
   @param rec the caller supplied addb record, which is long enough to fill.

   @return 0 on success. Other negative values mean error.
*/
typedef	int (*m0_addb_ev_pack_t)(struct m0_addb_dp *dp,
				 struct m0_addb_record *rec);

/**
   Get size for this event data point.

   The size is its opaque data, excluding header.
   @param dp the data point
   @return actual size is returned on success. Negative values mean error.
*/
typedef	int (*m0_addb_ev_getsize_t)(struct m0_addb_dp *dp);

struct m0_addb_ev_ops {
	m0_addb_ev_subst_t    aeo_subst;
	m0_addb_ev_pack_t     aeo_pack;
	m0_addb_ev_getsize_t  aeo_getsize;
	size_t                aeo_size;
	const char           *aeo_name;
	enum m0_addb_ev_level aeo_level;
};

/**
   Global wide Event ID.

   To avoid event ID conflict, all event ID should be defined here.
*/
enum m0_addb_event_id {
	M0_ADDB_EVENT_OOM                   = 0x1ULL,
	M0_ADDB_EVENT_FUNC_FAIL             = 0x2ULL,

	M0_ADDB_EVENT_NET_SEND              = 0x10ULL,
	M0_ADDB_EVENT_NET_CALL              = 0x11ULL,
	M0_ADDB_EVENT_NET_QSTATS            = 0x12ULL,
	M0_ADDB_EVENT_NET_LNET_OPEN         = 0x13ULL,
	M0_ADDB_EVENT_NET_LNET_CLOSE        = 0x14ULL,
	M0_ADDB_EVENT_NET_LNET_CLEANUP      = 0x15ULL,

	M0_ADDB_EVENT_COB_MDEXISTS          = 0x21ULL,
	M0_ADDB_EVENT_COB_MDDELETE          = 0x22ULL,

	M0_ADDB_EVENT_TRACE		    = 0x30ULL,

	M0_ADDB_EVENT_LAYOUT_DECODE_FAIL    = 0x41ULL,
	M0_ADDB_EVENT_LAYOUT_ENCODE_FAIL    = 0x42ULL,
	M0_ADDB_EVENT_LAYOUT_LOOKUP_FAIL    = 0x43ULL,
	M0_ADDB_EVENT_LAYOUT_ADD_FAIL       = 0x44ULL,
	M0_ADDB_EVENT_LAYOUT_UPDATE_FAIL    = 0x45ULL,
	M0_ADDB_EVENT_LAYOUT_DELETE_FAIL    = 0x46ULL,
	M0_ADDB_EVENT_LAYOUT_TILE_CACHE_HIT = 0x47ULL
};

/**
   Event of interest for addb.

   This can be "directory entry has been found in cache", "data read took N
   microseconds", "incoming FOP queue length is L", "memory allocation failure",
   etc.

   m0_addb_ev describes a type of event (not a particular instance of
   it). m0_addb_ev instances are typically statically allocated.

   m0_addb_ev accepts "formal parameters" (e.g., actual time it took to read
   data) and produces a "data-point". Related data-points are packed into an
   "addb record" that is used for further analysis.
 */
struct m0_addb_ev {
	const char                  *ae_name;
	uint64_t                     ae_id;
	const struct m0_addb_ev_ops *ae_ops;
	enum m0_addb_ev_level        ae_level;
};

enum {
	ADDB_REC_HEADER_VERSION = 0x000000001
};

/**
   An instance of addb event packet together with its formal parameters.
 */
struct m0_addb_dp {
	struct m0_addb_ctx       *ad_ctx;
	const struct m0_addb_loc *ad_loc;
	const struct m0_addb_ev  *ad_ev;
	enum m0_addb_ev_level     ad_level;

	/* XXX temporary */
	uint64_t    ad_rc;
	const char *ad_name;
};

M0_INTERNAL void m0_addb_ctx_init(struct m0_addb_ctx *ctx,
				  const struct m0_addb_ctx_type *t,
				  struct m0_addb_ctx *parent);
M0_INTERNAL void m0_addb_ctx_fini(struct m0_addb_ctx *ctx);

/**
   Low-level interface posting a data-point to the addb.

   Use this, if type-safe interface (M0_ADDB_ADD()) is for some reason
   inadequate.
 */
M0_INTERNAL void m0_addb_add(struct m0_addb_dp *dp);

M0_INTERNAL int m0_addb_init(void);
M0_INTERNAL void m0_addb_fini(void);

/*
 * The ugly pre-processor code below implements a type-safe interface to addb,
 * guaranteeing at compile time that formal parameters supplied for an addb
 * event match its definition. This is achieved through the mechanisms not
 * entirely unlike to C++ templates.
 */

/**
   Defines an addb event with a given name, identifier and operations vector.

   "ops" MUST be a variable name, usually introduced by M0_ADDB_OPS_DEFINE()
   macro.
   "id" should be system wide unique. Please define ID in enum m0_addb_event_id.

   Example:

   @code
   // addb event recording directory entry cache hits and misses during
   // fop processing
   //
   // This call defines const struct m0_addb_ev reqh_dirent_cache;
   M0_ADDB_EV_DEFINE(reqh_dirent_cache,
                     "sendreply",           // human-readable name
                     REQH_ADDB_DIRENT_CACHE,// unique identifier
		     M0_ADDB_FLAG);         // event type (Boolean: hit or miss)
   @endcode
 */

#define __M0_ADDB_EV_DEFINE(var, name, id, ops)				\
const struct m0_addb_ev var = {						\
	.ae_name  = (name),						\
	.ae_id    = (id),						\
	.ae_ops   = &(ops)						\
};

#define M0_ADDB_EV_DEFINE(var, name, id, ops)				\
	__M0_ADDB_EV_DEFINE(var, name, id, ops)				\
									\
	typedef typeof(__ ## ops ## _typecheck_t) __ ## var ## _typecheck_t


#define M0_ADDB_EV_DEFINE_PUBLIC(var, name, id, ops)	\
	__M0_ADDB_EV_DEFINE(var, name, id, ops)

#define M0_ADDB_EV_DECLARE(var, ops)					\
	extern const struct m0_addb_ev var;				\
									\
	typedef typeof(__ ## ops ## _typecheck_t) __ ## var ## _typecheck_t


/**
   Type-safe addb posting interface.

   Composes a data-point for a given event in a given context and a given
   location and posts it to addb.

   Event formal parameters are supplied as variadic arguments. This macro checks
   that their number and types conform to the event definition
   (M0_ADDB_EV_DEFINE(), which in turn conforms to the event operation vector
   definition in M0_ADDB_OPS_DEFINE()).

   "ev" MUST be a variable name, usually introduced by M0_ADDB_EV_DEFINE().

   @code
   hit = m0_dirent_cache_lookup(pdir, name, &entry);
   M0_ADDB_ADD(&fop->f_addb_ctx, &reqh_addb_loc, &reqh_dirent_cache, hit);
   @endcode
 */
#define M0_ADDB_ADD(ctx, loc, ev, ...)				\
({								\
	struct m0_addb_dp __dp;					\
								\
	__dp.ad_ctx   = (ctx);					\
	__dp.ad_loc   = (loc);					\
	__dp.ad_ev    = &(ev);					\
	__dp.ad_level = (m0_addb_level_default);		\
								\
	(void)sizeof(((__ ## ev ## _typecheck_t *)NULL)		\
		     (&__dp , ## __VA_ARGS__));			\
	if (ev.ae_ops->aeo_subst(&__dp , ## __VA_ARGS__) == 0)	\
		m0_addb_add(&__dp);				\
})

extern enum m0_addb_ev_level m0_addb_level_default;
enum m0_addb_ev_level m0_addb_choose_default_level(enum m0_addb_ev_level level);


M0_INTERNAL enum m0_addb_ev_level
m0_addb_choose_default_level_console(enum m0_addb_ev_level level);

/**
   Declare addb event operations vector with a given collection of formal
   parameter.

   @see M0_ADDB_SYSCALL, M0_ADDB_FUNC_CALL, M0_ADDB_CALL
   @see M0_ADDB_STAMP, M0_ADDB_FLAG
 */
#define M0_ADDB_OPS_DEFINE(ops, ...)					\
extern const struct m0_addb_ev_ops ops;					\
									\
typedef int								\
__ ## ops ## _typecheck_t(struct m0_addb_dp *dp , ## __VA_ARGS__)

/** A call to an external system component failed. */
M0_ADDB_OPS_DEFINE(M0_ADDB_SYSCALL, int rc);
/** A call to a given function failed. */
M0_ADDB_OPS_DEFINE(M0_ADDB_FUNC_CALL, const char *fname, int rc);
/** A call to a M0 component failed. */
M0_ADDB_OPS_DEFINE(M0_ADDB_CALL, int rc);
/** An invalid value was supplied. */
M0_ADDB_OPS_DEFINE(M0_ADDB_INVAL, uint64_t val);
/** Time-stamp. */
M0_ADDB_OPS_DEFINE(M0_ADDB_STAMP);
/** Record a Boolean condition. */
M0_ADDB_OPS_DEFINE(M0_ADDB_FLAG, bool flag);
/** Record a trace event. */
M0_ADDB_OPS_DEFINE(M0_ADDB_TRACE, const char *message);

/** Events which are used throughout Mero */

/** Report this event when memory allocation fails. */
M0_ADDB_EV_DECLARE(m0_addb_oom, M0_ADDB_STAMP);

/** Report this event when function call fails that doesn't fit into a more
    specific event. */
M0_ADDB_EV_DECLARE(m0_addb_func_fail, M0_ADDB_FUNC_CALL);

/** Report this event when a trace message has to be put into addb */
M0_ADDB_EV_DECLARE(m0_addb_trace, M0_ADDB_TRACE);

/** Global (per address space) addb context, used when no other context is
    applicable. */
extern struct m0_addb_ctx m0_addb_global_ctx;

extern struct m0_fop_type m0_addb_record_fopt;
extern struct m0_fop_type m0_addb_reply_fopt;

M0_INTERNAL int m0_addb_fop_init(void);
M0_INTERNAL void m0_addb_fop_fini(void);

/** @} end of addb group */

/* __MERO_ADDB_ADDB_H__ */
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
