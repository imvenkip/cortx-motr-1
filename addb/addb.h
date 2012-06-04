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

#ifndef __COLIBRI_ADDB_ADDB_H__
#define __COLIBRI_ADDB_ADDB_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/assert.h"
#include "lib/adt.h"
#include "lib/list.h"

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
enum c2_addb_ev_level;
struct c2_dbenv;
struct c2_dtx;


/* these are needed earlier than they are defined */
struct c2_stob;
struct c2_table;
struct c2_net_conn;

/**
   ADDB record store type

   ADDB record can be populated and stored in various ways:
   STOB:    ADDB records will be stored in stob.
   DB:      ADDB records will be stored in database.
   NETWORK: ADDB records will be sent onto network.

   Corresponding operation will be called according to this type in
   c2_addb_add();
 */
enum c2_addb_rec_store_type {
	C2_ADDB_REC_STORE_NONE    = 0,
	C2_ADDB_REC_STORE_STOB    = 1,
	C2_ADDB_REC_STORE_DB      = 2,
	C2_ADDB_REC_STORE_NETWORK = 3
};


/**
   Common state of addb contexts.

   @see c2_addb_ctx
 */
struct c2_addb_ctx_type {
	const char *act_name;
};

/**
    Write addb records into this stob.
 */
typedef int (*c2_addb_stob_add_t)(struct c2_addb_dp *dp, struct c2_dtx *tx,
				  struct c2_stob *stob);
int c2_addb_stob_add(struct c2_addb_dp *dp, struct c2_dtx *tx,
		     struct c2_stob *stob);

/**
    Write addb records into this db.
 */
typedef int (*c2_addb_db_add_t)(struct c2_addb_dp *dp, struct c2_dbenv *dbenv,
				struct c2_table *db);
int c2_addb_db_add(struct c2_addb_dp *dp, struct c2_dbenv *dbenv,
		   struct c2_table *db);

/**
    Send addb records through this network connection.
 */
typedef int (*c2_addb_net_add_t)(struct c2_addb_dp *dp, struct c2_net_conn *);
int c2_addb_net_add(struct c2_addb_dp *dp, struct c2_net_conn *);

int c2_addb_choose_store_media(enum c2_addb_rec_store_type type, ...);



/**
   Activity in context on which addb event happens.

   This can be, for example, FOP processing or storage IO. There is also a
   "global" per address space context for events not related to any identifiable
   activity (e.g., for reception of unsolicited signals).

   There are multiple instances of struct c2_addb_ctx in the system, e.g., one
   for each FOP being processed. State common to contexts of the same "type" is
   described in c2_addb_ctx_type.

   @see c2_addb_ctx_type
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
struct c2_addb_record_header;
/**
   ADDB record (on-wire)
*/
struct c2_addb_record;

enum {
	/** addb record size alignment */
	C2_ADDB_RECORD_LEN_ALIGN = 8,
};

/**
   Pack the record header into a buffer.

   @param dp the data point
   @param header pointer to the header within the buffer
   @param size total size of the record
 */
int c2_addb_record_header_pack(struct c2_addb_dp *dp,
			       struct c2_addb_record_header *header,
			       int size);

/**
   Packing this event into a buffer.

   @param dp the data point
   @param rec the caller supplied addb record, which is long enough to fill.

   @return 0 on success. Other negative values mean error.
*/
typedef	int (*c2_addb_ev_pack_t)(struct c2_addb_dp *dp,
				 struct c2_addb_record *rec);

/**
   Get size for this event data point.

   The size is its opaque data, excluding header.
   @param dp the data point
   @return actual size is returned on success. Negative values mean error.
*/
typedef	int (*c2_addb_ev_getsize_t)(struct c2_addb_dp *dp);

struct c2_addb_ev_ops {
	c2_addb_ev_subst_t    aeo_subst;
	c2_addb_ev_pack_t     aeo_pack;
	c2_addb_ev_getsize_t  aeo_getsize;
	size_t                aeo_size;
	const char           *aeo_name;
	enum c2_addb_ev_level aeo_level;
};

/**
   Global wide Event ID.

   To avoid event ID conflict, all event ID should be defined here.
*/
enum c2_addb_event_id {
	C2_ADDB_EVENT_USUNRPC_REQ           = 0x1ULL,
	C2_ADDB_EVENT_USUNRPC_OPNOTSURPPORT = 0x2ULL,
	C2_ADDB_EVENT_OOM                   = 0x3ULL,
	C2_ADDB_EVENT_FUNC_FAIL             = 0x4ULL,

	C2_ADDB_EVENT_NET_SEND              = 0x10ULL,
	C2_ADDB_EVENT_NET_CALL              = 0x11ULL,
	C2_ADDB_EVENT_NET_QSTATS            = 0x12ULL,
	C2_ADDB_EVENT_NET_LNET_OPEN         = 0x13ULL,
	C2_ADDB_EVENT_NET_LNET_CLOSE        = 0x14ULL,
	C2_ADDB_EVENT_NET_LNET_CLEANUP      = 0x15ULL,

	C2_ADDB_EVENT_COB_MDEXISTS          = 0x21ULL,
	C2_ADDB_EVENT_COB_MDDELETE          = 0x22ULL,

	C2_ADDB_EVENT_TRACE		    = 0x30ULL,

	C2_ADDB_EVENT_LAYOUT_DECODE_FAIL    = 0x41ULL,
	C2_ADDB_EVENT_LAYOUT_ENCODE_FAIL    = 0x42ULL,
	C2_ADDB_EVENT_LAYOUT_LOOKUP_FAIL    = 0x43ULL,
	C2_ADDB_EVENT_LAYOUT_ADD_FAIL       = 0x44ULL,
	C2_ADDB_EVENT_LAYOUT_UPDATE_FAIL    = 0x45ULL,
	C2_ADDB_EVENT_LAYOUT_DELETE_FAIL    = 0x46ULL,
	C2_ADDB_EVENT_LAYOUT_TILE_CACHE_HIT = 0x47ULL
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

enum {
	ADDB_REC_HEADER_MAGIC1  = 0xADDB0123ADDB4567ULL,
	ADDB_REC_HEADER_MAGIC2  = 0xADDB89ABADDBCDEFULL,
	ADDB_REC_HEADER_VERSION = 0x000000001
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
	uint64_t    ad_rc;
	const char *ad_name;
};

void c2_addb_ctx_init(struct c2_addb_ctx *ctx, const struct c2_addb_ctx_type *t,
		      struct c2_addb_ctx *parent);
void c2_addb_ctx_fini(struct c2_addb_ctx *ctx);

/**
   Low-level interface posting a data-point to the addb.

   Use this, if type-safe interface (C2_ADDB_ADD()) is for some reason
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
   "id" should be system wide unique. Please define ID in enum c2_addb_event_id.

   Example:

   @code
   // addb event recording directory entry cache hits and misses during
   // fop processing
   //
   // This call defines const struct c2_addb_ev reqh_dirent_cache;
   C2_ADDB_EV_DEFINE(reqh_dirent_cache,
                     "sendreply",           // human-readable name
                     REQH_ADDB_DIRENT_CACHE,// unique identifier
		     C2_ADDB_FLAG);         // event type (Boolean: hit or miss)
   @endcode
 */

#define __C2_ADDB_EV_DEFINE(var, name, id, ops)				\
const struct c2_addb_ev var = {						\
	.ae_name  = (name),						\
	.ae_id    = (id),						\
	.ae_ops   = &(ops)						\
};

#define C2_ADDB_EV_DEFINE(var, name, id, ops)				\
	__C2_ADDB_EV_DEFINE(var, name, id, ops)				\
									\
	typedef typeof(__ ## ops ## _typecheck_t) __ ## var ## _typecheck_t


#define C2_ADDB_EV_DEFINE_PUBLIC(var, name, id, ops)	\
	__C2_ADDB_EV_DEFINE(var, name, id, ops)

#define C2_ADDB_EV_DECLARE(var, ops)					\
	extern const struct c2_addb_ev var;				\
									\
	typedef typeof(__ ## ops ## _typecheck_t) __ ## var ## _typecheck_t


/**
   Type-safe addb posting interface.

   Composes a data-point for a given event in a given context and a given
   location and posts it to addb.

   Event formal parameters are supplied as variadic arguments. This macro checks
   that their number and types conform to the event definition
   (C2_ADDB_EV_DEFINE(), which in turn conforms to the event operation vector
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
	__dp.ad_level = (c2_addb_level_default);		\
								\
	(void)sizeof(((__ ## ev ## _typecheck_t *)NULL)		\
		     (&__dp , ## __VA_ARGS__));			\
	if (ev.ae_ops->aeo_subst(&__dp , ## __VA_ARGS__) == 0)	\
		c2_addb_add(&__dp);				\
})

extern enum c2_addb_ev_level c2_addb_level_default;
enum c2_addb_ev_level c2_addb_choose_default_level(enum c2_addb_ev_level level);

enum c2_addb_ev_level c2_addb_choose_default_level_console(
	    enum c2_addb_ev_level level);

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
/** A call to a given function failed. */
C2_ADDB_OPS_DEFINE(C2_ADDB_FUNC_CALL, const char *fname, int rc);
/** A call to a C2 component failed. */
C2_ADDB_OPS_DEFINE(C2_ADDB_CALL, int rc);
/** An invalid value was supplied. */
C2_ADDB_OPS_DEFINE(C2_ADDB_INVAL, uint64_t val);
/** Time-stamp. */
C2_ADDB_OPS_DEFINE(C2_ADDB_STAMP);
/** Record a Boolean condition. */
C2_ADDB_OPS_DEFINE(C2_ADDB_FLAG, bool flag);
/** Record a trace event. */
C2_ADDB_OPS_DEFINE(C2_ADDB_TRACE, const char *message);

/** Events which are used throughout Colibri */

/** Report this event when memory allocation fails. */
C2_ADDB_EV_DECLARE(c2_addb_oom, C2_ADDB_STAMP);

/** Report this event when function call fails that doesn't fit into a more
    specific event. */
C2_ADDB_EV_DECLARE(c2_addb_func_fail, C2_ADDB_FUNC_CALL);

/** Report this event when a trace message has to be put into addb */
C2_ADDB_EV_DECLARE(c2_addb_trace, C2_ADDB_TRACE);

/** Global (per address space) addb context, used when no other context is
    applicable. */
extern struct c2_addb_ctx c2_addb_global_ctx;

extern struct c2_fop_type c2_addb_record_fopt; /* opcode = 14 */
extern struct c2_fop_type c2_addb_reply_fopt;
extern struct c2_fop_type_format c2_mem_buf_tfmt;
extern struct c2_fop_type_format c2_addb_record_header_tfmt;
extern struct c2_fop_type_format c2_addb_record_tfmt;
extern struct c2_fop_type_format c2_addb_reply_tfmt;

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
