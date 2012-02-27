/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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

#ifndef __COLIBRI_LIB_TRACE_H__
#define __COLIBRI_LIB_TRACE_H__

#include <stdarg.h>

#ifdef HAVE_CONFIG_H
#  include <config.h> /* ENABLE_DEBUG */
#endif

#include "lib/types.h"
#include "lib/arith.h"

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

       - always-on tracing used to postmortem analysis of a Colibri
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

   Users produce trace records by calling C2_LOG() macro like

   @code
   C2_LOG("Cached value found: %llx, attempt: %i", foo->f_val, i);
   @endcode

   These records are placed in a shared cyclic buffer. The buffer can be
   "parsed", producing for each record formatted output together with additional
   information:

       - file, line and function (__FILE__, __LINE__ and __func__) for C2_LOG()
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
   record descriptor (c2_trace_descr) is created, which contains all the static
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
   extensions. For a list of arguments A0, A1, ..., An, one of C2_LOG{n}()
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

   In addition, C2_LOG{n}() macro produces 2 integer arrays:

@code
       { offsetof(struct t_body, v0), ..., offsetof(struct t_body, vn) };
       { sizeof(a0), ..., sizeof(an) };
@endcode

   These arrays are used during parsing to extract the arguments from the
   buffer.

   @{
 */

/**
   C2_LOG(fmt, ...) is the main user interface for the tracing. It accepts
   the arguments in printf(3) format for the numbers, but there are some
   tricks for string arguments.

   String arguments should be specified like this:

   @code
   C2_LOG("%s", (char *)"foo");
   @endcode

   i.e. explicitly typecast to the pointer. It is because typeof("foo")
   is not the same as typeof((char*)"foo").

   @note The number of arguments after fmt is limited to 9!

   C2_LOG() counts the number of arguments and calls correspondent C2_LOGx().
 */
#define C2_LOG(...) \
	C2_CAT(C2_LOG, C2_COUNT_PARAMS(__VA_ARGS__))(__VA_ARGS__)

int  c2_trace_init(void);
void c2_trace_fini(void);

/*
 * Below is the internal implementation stuff.
 */

#ifdef ENABLE_DEBUG
#  define C2_TRACE_IMMEDIATE_DEBUG (1)
#else
#  define C2_TRACE_IMMEDIATE_DEBUG (0)
#endif

/** Magic number to locate the record */
enum {
	C2_TRACE_MAGIC = 0xc0de1eafacc01adeULL,
};

/** Default buffer size, the real buffer size is at c2_logbufsize */
enum {
	C2_TRACE_BUFSIZE  = 1 << (10 + 12) /* 4MB */
};

extern void      *c2_logbuf;      /**< Trace buffer pointer */
extern uint32_t   c2_logbufsize;  /**< The real buffer size */

/** The bitmask of what should be printed immediately to console */
extern unsigned long c2_trace_immediate_mask;
/** The subsystem bitmask definishions */
enum c2_trace_subsystem {
	C2_TRACE_SUBSYS_OTHER = (1 <<  0),
	C2_TRACE_SUBSYS_UT    = (1 <<  1),
};

/**
 * Record header structure
 *
 * - magic number to locate the record in buffer
 * - stack pointer - useful to distinguish between threads
 * - global record number
 * - timestamp
 * - pointer to record description in the program file
 */
struct c2_trace_rec_header {
	uint64_t                     trh_magic;
	uint64_t                     trh_sp; /**< stack pointer */
	uint64_t                     trh_no; /**< record # */
	uint64_t                     trh_timestamp;
	const struct c2_trace_descr *trh_descr;
};

struct c2_trace_descr {
	const char *td_fmt;
	const char *td_func;
	const char *td_file;
	uint64_t    td_subsys;
	int         td_line;
	int         td_size;
	int         td_nr;
	const int  *td_offset;
	const int  *td_sizeof;
};

void c2_trace_allot(const struct c2_trace_descr *td, const void *data);
void c2_trace_record_print(const struct c2_trace_rec_header *trh, const void *buf);
void c2_console_printf(const char *fmt, ...);
void c2_console_vprintf(const char *fmt, va_list ap);

/*
 * The code below abuses C preprocessor badly. Looking at it might be damaging
 * to your eyes and sanity.
 */

/**
 * This is a low-level entry point into tracing sub-system.
 *
 * Don't call this directly, use C2_LOG() macros instead.
 *
 * Add a fixed-size trace entry into the trace buffer.
 *
 * @param NR the number of arguments
 * @param DECL C definition of a trace entry format
 * @param OFFSET the set of offsets of each argument
 * @param SIZEOF the set of sizes of each argument
 * @param FMT the printf-like format string
 * @note The variadic arguments must match the number
 *       and types of fields in the format.
 */
#define C2_TRACE_POINT(NR, DECL, OFFSET, SIZEOF, FMT, ...)		\
({									\
	struct t_body DECL;						\
	static const int _offset[NR] = OFFSET;				\
	static const int _sizeof[NR] = SIZEOF;				\
	static const struct c2_trace_descr td = {			\
                .td_fmt    = (FMT),					\
		.td_func   = __func__,					\
		.td_file   = __FILE__,					\
		.td_line   = __LINE__,					\
		.td_subsys = C2_TRACE_SUBSYSTEM,			\
		.td_size   = sizeof(struct t_body),			\
		.td_nr     = (NR),					\
		.td_offset = _offset,					\
		.td_sizeof = _sizeof					\
	};								\
	printf_check(FMT , ## __VA_ARGS__);				\
	c2_trace_allot(&td, &(const struct t_body){ __VA_ARGS__ });	\
})

#ifndef C2_TRACE_SUBSYSTEM
#  define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_OTHER
#endif

enum {
	C2_TRACE_ARGC_MAX = 9
};

/*
 *  Helpers for C2_LOG{n}().
 */
#define LOG_TYPEOF(a, v) typeof(a) v
#define LOG_OFFSETOF(v) offsetof(struct t_body, v)
#define LOG_SIZEOF(a) sizeof(a)

#define LOG_CHECK(a)							\
C2_CASSERT(!C2_HAS_TYPE(a, const char []) &&				\
	   (sizeof(a) == 1 || sizeof(a) == 2 || sizeof(a) == 4 ||	\
	    sizeof(a) == 8))

/**
 * LOG_GROUP() is used to pass { x0, ..., xn } as a single argument to
 * C2_TRACE_POINT().
 */
#define LOG_GROUP(...) __VA_ARGS__

#define C2_LOG0(fmt)     C2_TRACE_POINT(0, { ; }, {}, {}, fmt)

#define C2_LOG1(fmt, a0)						\
({ C2_TRACE_POINT(1,							\
   { LOG_TYPEOF(a0, v0); },						\
   { LOG_OFFSETOF(v0) },						\
   { LOG_SIZEOF(a0) },							\
   fmt, a0);								\
   LOG_CHECK(a0); })

#define C2_LOG2(fmt, a0, a1)						\
({ C2_TRACE_POINT(2,							\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); },				\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1) }),			\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1) }),			\
   fmt, a0, a1);							\
   LOG_CHECK(a0); LOG_CHECK(a1); })

#define C2_LOG3(fmt, a0, a1, a2)					\
({ C2_TRACE_POINT(3,							\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2); },	\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2) }),	\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2) }),	\
   fmt, a0, a1, a2);							\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); })

#define C2_LOG4(fmt, a0, a1, a2, a3)					\
({ C2_TRACE_POINT(4,							\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); },						\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3) }),					\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3) }),					\
   fmt, a0, a1, a2, a3);						\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3); })

#define C2_LOG5(fmt, a0, a1, a2, a3, a4)				\
({ C2_TRACE_POINT(5,							\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); },				\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4) }),			\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4) }),			\
   fmt, a0, a1, a2, a3, a4);						\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); })

#define C2_LOG6(fmt, a0, a1, a2, a3, a4, a5)				\
({ C2_TRACE_POINT(6,							\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); LOG_TYPEOF(a5, v5); },	\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4), LOG_OFFSETOF(v5) }),	\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4), LOG_SIZEOF(a5) }),	\
   fmt, a0, a1, a2, a3, a4, a5);					\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); LOG_CHECK(a5); })

#define C2_LOG7(fmt, a0, a1, a2, a3, a4, a5, a6)			\
({ C2_TRACE_POINT(7,							\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); LOG_TYPEOF(a5, v5);	\
     LOG_TYPEOF(a6, v6); },						\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4),			\
               LOG_OFFSETOF(v5), LOG_OFFSETOF(v6) }),			\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4),				\
               LOG_SIZEOF(a5), LOG_SIZEOF(a6) }),			\
   fmt, a0, a1, a2, a3, a4, a5, a6);					\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); LOG_CHECK(a5); LOG_CHECK(a6); })

#define C2_LOG8(fmt, a0, a1, a2, a3, a4, a5, a6, a7)			\
({ C2_TRACE_POINT(8,							\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); LOG_TYPEOF(a5, v5);	\
     LOG_TYPEOF(a6, v6); LOG_TYPEOF(a7, v7); },				\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4), LOG_OFFSETOF(v5),	\
               LOG_OFFSETOF(v6), LOG_OFFSETOF(v7) }),			\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4), LOG_SIZEOF(a5),		\
               LOG_SIZEOF(a6), LOG_SIZEOF(a7) }),			\
   fmt, a0, a1, a2, a3, a4, a5, a6, a7);				\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); LOG_CHECK(a5); LOG_CHECK(a6); LOG_CHECK(a7); })

#define C2_LOG9(fmt, a0, a1, a2, a3, a4, a5, a6, a7, a8)		\
({ C2_TRACE_POINT(9,							\
   { LOG_TYPEOF(a0, v0); LOG_TYPEOF(a1, v1); LOG_TYPEOF(a2, v2);	\
     LOG_TYPEOF(a3, v3); LOG_TYPEOF(a4, v4); LOG_TYPEOF(a5, v5);	\
     LOG_TYPEOF(a6, v6); LOG_TYPEOF(a7, v7); LOG_TYPEOF(a8, v8); },	\
   LOG_GROUP({ LOG_OFFSETOF(v0), LOG_OFFSETOF(v1), LOG_OFFSETOF(v2),	\
               LOG_OFFSETOF(v3), LOG_OFFSETOF(v4), LOG_OFFSETOF(v5),	\
               LOG_OFFSETOF(v6), LOG_OFFSETOF(v7), LOG_OFFSETOF(v8) }),	\
   LOG_GROUP({ LOG_SIZEOF(a0), LOG_SIZEOF(a1), LOG_SIZEOF(a2),		\
               LOG_SIZEOF(a3), LOG_SIZEOF(a4), LOG_SIZEOF(a5),		\
               LOG_SIZEOF(a6), LOG_SIZEOF(a7), LOG_SIZEOF(a8) }),	\
   fmt, a0, a1, a2, a3, a4, a5, a6, a7, a8);				\
   LOG_CHECK(a0); LOG_CHECK(a1); LOG_CHECK(a2); LOG_CHECK(a3);		\
   LOG_CHECK(a4); LOG_CHECK(a5); LOG_CHECK(a6); LOG_CHECK(a7); LOG_CHECK(a8); })

/** @} end of trace group */

/* __COLIBRI_LIB_TRACE_H__ */
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
