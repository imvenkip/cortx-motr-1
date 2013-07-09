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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/12/2010
 */

#pragma once

#ifndef __MERO_LIB_TRACE_H__
#define __MERO_LIB_TRACE_H__

#include <stdarg.h>

#include "lib/types.h"
#include "lib/arith.h"
#include "lib/cdefs.h"   /* M0_HAS_TYPE */
#include "mero/magic.h"  /* M0_TRACE_DESCR_MAGIC */

#ifndef __KERNEL__
#include "lib/user_space/trace.h"
#endif

/**
   @defgroup trace Tracing

   See doc/logging-and-tracing.

   <b>Fast and light-weight tracing facility</b>

   The purpose of tracing module is to provide an interface usable for the
   following purposes:

       - temporary tracing to investigate and hunt down bugs and

       - always-on tracing used to postmortem analysis of a Mero
         installation.

   Always-on mode must be non-intrusive and light-weight, otherwise users would
   tend to disable it. On the other hand, the interface should be convenient to
   use (i.e., to place trace points and to analyze a trace) otherwise
   programmers would tend to ignore it. These conflicting requirements lead to
   a implementation subtler than one might expect.

   Specifically, the tracing module should conform to the following
   requirements:

       - minimal synchronization between threads;

       - minimal amount of data-copying and, more generally, minimal processor
         cost of producing a trace record;

       - minimal instruction cache foot-print of tracing calls;

       - printf-like interface.

   <b>Usage</b>

   Users produce trace records by calling M0_LOG() macro like

   @code
   M0_LOG(M0_INFO, "Cached value found: %llx, attempt: %i", foo->f_val, i);
   @endcode

   These records are placed in a shared cyclic buffer. The buffer can be
   "parsed", producing for each record formatted output together with additional
   information:

       - file, line and function (__FILE__, __LINE__ and __func__) for M0_LOG()
         call,

       - processor-dependent time-stamp.

   Parsing occurs in the following situations:

       - @todo synchronously when a record is produced, or

       - @todo asynchronously by a background thread, or

       - after the process (including a kernel) that generated records
         crashed. To this end the cyclic buffer is backed up by a memory mapped
         file.

   <b>Implementation</b>

   To minimize processor cost of tracing, the implementation avoids run-time
   interpretation of format string. Instead a static (in C language sense)
   record descriptor (m0_trace_descr) is created, which contains all the static
   information about the trace point: file, line, function, format string. The
   record, placed in the cyclic buffer, contains only time-stamp, arguments
   (foo->f_val, i in the example above) and the pointer to the
   descriptor. Substitution of arguments into the format string happens during
   record parsing. This approach poses two problems:

       - how to copy arguments (which are variable in number and size) to the
         cyclic buffer and

       - how to estimate the number of bytes that have to be allocated in the
         buffer for this copying.

   Both problems are solved by means of ugly preprocessor tricks and gcc
   extensions. For a list of arguments A0, A1, ..., An, one of M0_LOG{n}()
   macros defines a C type declaration
@code
       struct t_body { typeof(A0) v0; typeof(A1) v1; ... typeof(An) vn; };
@endcode

   This declaration is used to

       - find how much space in the cyclic buffer is needed to store the
         arguments: sizeof(struct t_body);

       - to copy all the arguments into allocated space:
@code
        *(struct t_body *)space = (struct t_body){ A0, ..., An }
@endcode
         This uses C99 struct literal syntax.

   In addition, M0_LOG{n}() macro produces 2 integer arrays:

@code
       { offsetof(struct t_body, v0), ..., offsetof(struct t_body, vn) };
       { sizeof(a0), ..., sizeof(an) };
@endcode

   These arrays are used during parsing to extract the arguments from the
   buffer.

   @{
 */

/**
   M0_LOG(level, fmt, ...) is the main user interface for the tracing. It
   accepts the arguments in printf(3) format for the numbers, but there are some
   tricks for string arguments.

   String arguments should be specified like this:

   @code
   M0_LOG(M0_DEBUG, "%s", (char *)"foo");
   @endcode

   i.e. explicitly typecast to the pointer. It is because typeof("foo")
   is not the same as typeof((char*)"foo").

   @note The number of arguments after fmt is limited to 9

   M0_LOG() counts the number of arguments and calls correspondent M0_LOGx().
 */
#define M0_LOG(level, ...) \
	M0_CAT(M0_LOG, M0_COUNT_PARAMS(__VA_ARGS__))(level, __VA_ARGS__)

#define M0_ENTRY(...) M0_LOG(M0_CALL, "> " __VA_ARGS__)
#define M0_LEAVE(...) M0_LOG(M0_CALL, "< " __VA_ARGS__)

#define M0_RETURN(rc)                                    \
do {                                                     \
	typeof(rc) __rc = (rc);                          \
	(__rc == 0) ? M0_LOG(M0_CALL, "< rc=%d", __rc) : \
		M0_LOG(M0_NOTICE, "< rc=%d", __rc);      \
	return __rc;                                     \
} while (0)

#define M0_RETERR(rc, fmt, ...)                                  \
do {                                                             \
	typeof(rc) __rc = (rc);                                  \
	M0_ASSERT(__rc != 0);                                    \
	M0_LOG(M0_ERROR, "<! rc=%d " fmt, __rc, ## __VA_ARGS__); \
	return __rc;                                             \
} while (0)

M0_INTERNAL int m0_trace_init(void);
M0_INTERNAL void m0_trace_fini(void);

/**
 * The subsystems definitions.
 *
 * @note: Such kind of definition (via defines) allow to keep enum
 *        and string array in sync.
 *
 * Please keep the lower list sorted.
 */
#define M0_TRACE_SUBSYSTEMS      \
  M0_TRACE_SUBSYS(OTHER,      0) \
  M0_TRACE_SUBSYS(LIB,        1) \
  M0_TRACE_SUBSYS(UT,         2) \
				 \
  M0_TRACE_SUBSYS(ADDB,       3) \
  M0_TRACE_SUBSYS(BALLOC,     4) \
  M0_TRACE_SUBSYS(BE,         5) \
  M0_TRACE_SUBSYS(CM,         6) \
  M0_TRACE_SUBSYS(COB,        7) \
  M0_TRACE_SUBSYS(CONF,       8) \
  M0_TRACE_SUBSYS(FOP,        9) \
  M0_TRACE_SUBSYS(FORMATION, 10) \
  M0_TRACE_SUBSYS(IOSERVICE, 11) \
  M0_TRACE_SUBSYS(LAYOUT,    12) \
  M0_TRACE_SUBSYS(LNET,      13) \
  M0_TRACE_SUBSYS(M0D,       14) \
  M0_TRACE_SUBSYS(M0T1FS,    15) \
  M0_TRACE_SUBSYS(MEMORY,    16) \
  M0_TRACE_SUBSYS(MGMT,      17) \
  M0_TRACE_SUBSYS(NET,       18) \
  M0_TRACE_SUBSYS(POOL,      19) \
  M0_TRACE_SUBSYS(RM,        20) \
  M0_TRACE_SUBSYS(RPC,       21) \
  M0_TRACE_SUBSYS(SM,        22) \
  M0_TRACE_SUBSYS(SNS,       23) \
  M0_TRACE_SUBSYS(SNSCM,     24) \
  M0_TRACE_SUBSYS(STOB,      25)

#define M0_TRACE_SUBSYS(name, value) M0_TRACE_SUBSYS_ ## name = (1 << value),
/** The subsystem bitmask definitions */
enum m0_trace_subsystem {
	M0_TRACE_SUBSYSTEMS
};

/**
 * The subsystems bitmask of what should be printed immediately
 * to the console.
 */
extern unsigned long m0_trace_immediate_mask;

/**
 * Controls whether to display additional trace point info, like
 * timestamp, subsystem, file, func, etc.
 *
 * Acceptable values are elements of enum m0_trace_print_context.
 */
extern unsigned int m0_trace_print_context;

/**
 * Controls which M0_LOG() messages are displayed on console when
 * M0_TRACE_IMMEDIATE_DEBUG is enabled.
 *
 * If log level of trace point is less or equal to m0_trace_level, then it's
 * displayed, otherwise not.
 *
 * By default m0_trace_level is set to M0_WARN. Also, @see documentation of
 * M0_CALL log level, it has special meaning.
 */
extern unsigned int m0_trace_level;

/*
 * Below is the internal implementation stuff.
 */

#ifdef ENABLE_IMMEDIATE_TRACE
#  define M0_TRACE_IMMEDIATE_DEBUG (1)
#else
#  define M0_TRACE_IMMEDIATE_DEBUG (0)
#endif

enum {
	/** Default buffer size, the real buffer size is at m0_logbufsize */
	M0_TRACE_BUFSIZE   = 1 << (10 + 12), /* 4MB */
	/** Alignment for trace records in trace buffer */
	M0_TRACE_REC_ALIGN = 8, /* word size on x86_64 */
};

extern void      *m0_logbuf;      /**< Trace buffer pointer */
extern uint32_t   m0_logbufsize;  /**< The real buffer size */


/**
 * Record header structure
 *
 * - magic number to locate the record in buffer
 * - stack pointer - useful to distinguish between threads
 * - global record number
 * - timestamp
 * - pointer to record description in the program file
 */
struct m0_trace_rec_header {
	uint64_t                     trh_magic;
	uint64_t                     trh_sp; /**< stack pointer */
	uint64_t                     trh_no; /**< record # */
	uint64_t                     trh_timestamp;
	const struct m0_trace_descr *trh_descr;
	uint32_t                     trh_string_data_size;
	pid_t                        trh_pid; /**< current PID */
};

/**
 * Trace levels. To be used as a first argument of M0_LOG().
 */
enum m0_trace_level {
	/**
	 * special level, which represents an invalid trace level,
	 * should _not_ be used directly with M0_LOG()
	 */
	M0_NONE   = 0,

	/** system is unusable and not able to perform it's basic function */
	M0_FATAL  = 1 << 0,
	/**
	 * local error condition, i.e.: resource unavailable, no connectivity,
	 * incorrect value, etc.
	 */
	M0_ERROR  = 1 << 1,
	/**
	 * warning condition, i.e.: condition which requires some special
	 * treatment, something which not happens normally, corner case, etc.
	 */
	M0_WARN   = 1 << 2,
	/** normal but significant condition */
	M0_NOTICE = 1 << 3,
	/** some useful information about current system's state */
	M0_INFO   = 1 << 4,
	/**
	 * lower-level, detailed information to aid debugging and analysis of
	 * incorrect behavior
	 */
	M0_DEBUG  = 1 << 5,

	/**
	 * special level, used only with M0_ENTRY() and M0_LEAVE() to trace
	 * function calls, should _not_ be used directly with M0_LOG();
	 */
	M0_CALL   = 1 << 6,
};

enum m0_trace_print_context {
	M0_TRACE_PCTX_NONE  = 0,
	M0_TRACE_PCTX_FUNC  = 1,
	M0_TRACE_PCTX_SHORT = 2,
	M0_TRACE_PCTX_FULL  = 3,

	M0_TRACE_PCTX_INVALID
};

struct m0_trace_descr {
	uint64_t             td_magic;
	const char          *td_fmt;
	const char          *td_func;
	const char          *td_file;
	uint64_t             td_subsys;
	int                  td_line;
	int                  td_size;
	int                  td_nr;
	const int           *td_offset;
	const int           *td_sizeof;
	const bool          *td_isstr;
	enum m0_trace_level  td_level;
};

M0_INTERNAL void m0_trace_allot(const struct m0_trace_descr *td,
				const void *data);
M0_INTERNAL void m0_trace_record_print(const struct m0_trace_rec_header *trh,
				       const void *buf);

M0_INTERNAL void m0_trace_print_subsystems(void);

__attribute__ ((format (printf, 1, 2)))
M0_INTERNAL void m0_console_printf(const char *fmt, ...);
M0_INTERNAL void m0_console_vprintf(const char *fmt, va_list ap);

/*
 * The code below abuses C preprocessor badly. Looking at it might be damaging
 * to your eyes and sanity.
 */

/**
 * This is a low-level entry point into tracing sub-system.
 *
 * Don't call this directly, use M0_LOG() macros instead.
 *
 * Add a fixed-size trace entry into the trace buffer.
 *
 * @param LEVEL a log level
 * @param NR the number of arguments
 * @param DECL C definition of a trace entry format
 * @param OFFSET the set of offsets of each argument
 * @param SIZEOF the set of sizes of each argument
 * @param FMT the printf-like format string
 * @note The variadic arguments must match the number
 *       and types of fields in the format.
 */
#define M0_TRACE_POINT(LEVEL, NR, DECL, OFFSET, SIZEOF, ISSTR, FMT, ...)\
({									\
	struct t_body DECL;						\
	static const int  _offset[NR] = OFFSET;				\
	static const int  _sizeof[NR] = SIZEOF;				\
	static const bool _isstr[NR]  = ISSTR;				\
	static const struct m0_trace_descr __trace_descr = {		\
		.td_magic  = M0_TRACE_DESCR_MAGIC,			\
		.td_level  = (LEVEL),					\
		.td_fmt    = (FMT),					\
		.td_func   = __func__,					\
		.td_file   = __FILE__,					\
		.td_line   = __LINE__,					\
		.td_subsys = M0_TRACE_SUBSYSTEM,			\
		.td_size   = sizeof(struct t_body),			\
		.td_nr     = (NR),					\
		.td_offset = _offset,					\
		.td_sizeof = _sizeof,					\
		.td_isstr  = _isstr,					\
	};								\
	printf_check(FMT , ## __VA_ARGS__);				\
	m0_trace_allot(&__trace_descr, &(const struct t_body){ __VA_ARGS__ });\
})

#ifndef M0_TRACE_SUBSYSTEM
#  define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_OTHER
#endif

enum {
	M0_TRACE_ARGC_MAX = 9
};

/*
 *  Helpers for M0_LOG{n}().
 */
#define LOG_TYPEOF(a, v) typeof(a) v
#define LOG_OFFSETOF(v) offsetof(struct t_body, v)
#define LOG_SIZEOF(a) sizeof(a)
#define LOG_IS_STR_ARG(a)			    \
		M0_HAS_TYPE((a), char*)        ?:   \
		M0_HAS_TYPE((a), const char*)  ?:   \
		M0_HAS_TYPE((a), char[])       ?:   \
		M0_HAS_TYPE((a), const char[]) ?: false


#define LOG_CHECK(a)							\
M0_CASSERT(!M0_HAS_TYPE(a, const char []) &&				\
	   (sizeof(a) == 1 || sizeof(a) == 2 || sizeof(a) == 4 ||	\
	    sizeof(a) == 8))

/**
 * LOG_GROUP() is used to pass { x0, ..., xn } as a single argument to
 * M0_TRACE_POINT().
 */
#define LOG_GROUP(...) __VA_ARGS__

#define M0_LOG0(level, fmt)     M0_TRACE_POINT(level, 0, { ; }, {}, {}, {}, fmt)

#define M0_LOG1(level, fmt, a0)						\
({ M0_TRACE_POINT(level, 1,						\
   { LOG_TYPEOF(a0, v0); },						\
   { LOG_OFFSETOF(v0) },						\
   { LOG_SIZEOF(a0) },							\
   { LOG_IS_STR_ARG(a0) },						\
   fmt, a0);								\
   LOG_CHECK(a0); })

#define M0_LOG2(level, fmt, a0, a1)					\
({ M0_TRACE_POINT(level, 2,						\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); },				\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1) }),			\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1) }),			\
   LOG_GROUP({ LOG_IS_STR_ARG(a0), LOG_IS_STR_ARG(a1) }),		\
   fmt, a0, a1);							\
   LOG_CHECK(a0); LOG_CHECK(a1); })

#define M0_LOG3(level, fmt, a0, a1, a2)					\
({ M0_TRACE_POINT(level, 3,						\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2); },	\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2) }),	\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2) }),	\
   LOG_GROUP({ LOG_IS_STR_ARG(a0), LOG_IS_STR_ARG(a1),			\
               LOG_IS_STR_ARG(a2) }),					\
   fmt, a0, a1, a2);							\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); })

#define M0_LOG4(level, fmt, a0, a1, a2, a3)				\
({ M0_TRACE_POINT(level, 4,						\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); },						\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3) }),					\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3) }),					\
   LOG_GROUP({ LOG_IS_STR_ARG(a0), LOG_IS_STR_ARG(a1),			\
               LOG_IS_STR_ARG(a2), LOG_IS_STR_ARG(a3)  }),		\
   fmt, a0, a1, a2, a3);						\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3); })

#define M0_LOG5(level, fmt, a0, a1, a2, a3, a4)				\
({ M0_TRACE_POINT(level, 5,						\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); },				\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4) }),			\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4) }),			\
   LOG_GROUP({ LOG_IS_STR_ARG(a0), LOG_IS_STR_ARG(a1),			\
               LOG_IS_STR_ARG(a2), LOG_IS_STR_ARG(a3),			\
               LOG_IS_STR_ARG(a4) }),					\
   fmt, a0, a1, a2, a3, a4);						\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); })

#define M0_LOG6(level, fmt, a0, a1, a2, a3, a4, a5)			\
({ M0_TRACE_POINT(level, 6,						\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); LOG_TYPEOF(a5, v5); },	\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4), LOG_OFFSETOF(v5) }),	\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4), LOG_SIZEOF(a5) }),	\
   LOG_GROUP({ LOG_IS_STR_ARG(a0), LOG_IS_STR_ARG(a1),			\
               LOG_IS_STR_ARG(a2), LOG_IS_STR_ARG(a3),			\
               LOG_IS_STR_ARG(a4), LOG_IS_STR_ARG(a5) }),		\
   fmt, a0, a1, a2, a3, a4, a5);					\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); LOG_CHECK(a5); })

#define M0_LOG7(level, fmt, a0, a1, a2, a3, a4, a5, a6)			\
({ M0_TRACE_POINT(level, 7,						\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); LOG_TYPEOF(a5, v5);	\
     LOG_TYPEOF(a6, v6); },						\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4),			\
               LOG_OFFSETOF(v5), LOG_OFFSETOF(v6) }),			\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4),				\
               LOG_SIZEOF(a5), LOG_SIZEOF(a6) }),			\
   LOG_GROUP({ LOG_IS_STR_ARG(a0), LOG_IS_STR_ARG(a1),			\
               LOG_IS_STR_ARG(a2), LOG_IS_STR_ARG(a3),			\
               LOG_IS_STR_ARG(a4), LOG_IS_STR_ARG(a5),			\
               LOG_IS_STR_ARG(a6) }),					\
   fmt, a0, a1, a2, a3, a4, a5, a6);					\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); LOG_CHECK(a5); LOG_CHECK(a6); })

#define M0_LOG8(level, fmt, a0, a1, a2, a3, a4, a5, a6, a7)		\
({ M0_TRACE_POINT(level, 8,						\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); LOG_TYPEOF(a5, v5);	\
     LOG_TYPEOF(a6, v6); LOG_TYPEOF(a7, v7); },				\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4), LOG_OFFSETOF(v5),	\
               LOG_OFFSETOF(v6), LOG_OFFSETOF(v7) }),			\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4), LOG_SIZEOF(a5),		\
               LOG_SIZEOF(a6), LOG_SIZEOF(a7) }),			\
   LOG_GROUP({ LOG_IS_STR_ARG(a0), LOG_IS_STR_ARG(a1),			\
               LOG_IS_STR_ARG(a2), LOG_IS_STR_ARG(a3),			\
               LOG_IS_STR_ARG(a4), LOG_IS_STR_ARG(a5),			\
               LOG_IS_STR_ARG(a6), LOG_IS_STR_ARG(a7) }),		\
   fmt, a0, a1, a2, a3, a4, a5, a6, a7);				\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); LOG_CHECK(a5); LOG_CHECK(a6); LOG_CHECK(a7); })

#define M0_LOG9(level, fmt, a0, a1, a2, a3, a4, a5, a6, a7, a8)		\
({ M0_TRACE_POINT(level, 9,						\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); LOG_TYPEOF(a5, v5);	\
     LOG_TYPEOF(a6, v6); LOG_TYPEOF(a7, v7); LOG_TYPEOF(a8, v8); },	\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4), LOG_OFFSETOF(v5),	\
               LOG_OFFSETOF(v6), LOG_OFFSETOF(v7), LOG_OFFSETOF(v8) }),	\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4), LOG_SIZEOF(a5),		\
               LOG_SIZEOF(a6), LOG_SIZEOF(a7), LOG_SIZEOF(a8) }),	\
   LOG_GROUP({ LOG_IS_STR_ARG(a0), LOG_IS_STR_ARG(a1),			\
               LOG_IS_STR_ARG(a2), LOG_IS_STR_ARG(a3),			\
               LOG_IS_STR_ARG(a4), LOG_IS_STR_ARG(a5),			\
               LOG_IS_STR_ARG(a6), LOG_IS_STR_ARG(a7),			\
               LOG_IS_STR_ARG(a8) }),					\
   fmt, a0, a1, a2, a3, a4, a5, a6, a7, a8);				\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); LOG_CHECK(a5); LOG_CHECK(a6); LOG_CHECK(a7); LOG_CHECK(a8); })

/** @} end of trace group */

/* __MERO_LIB_TRACE_H__ */
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
