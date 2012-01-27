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

#include "lib/types.h"

/**
   @defgroup trace Tracing.

   See doc/logging-and-tracing.

   Fast and light-weight tracing facility.

   @{
 */

struct c2_trace_rec_header;
struct c2_trace_descr;

struct c2_trace_rec_header {
	uint64_t                     thr_magic;
	uint64_t                     thr_no;
	uint64_t                     trh_timestamp;
	const struct c2_trace_descr *trh_descr;
};

struct c2_trace_descr {
	const char *td_fmt;
	const char *td_func;
	const char *td_file;
	int         td_line;
	int         td_size;
	int         td_nr;
	const int  *td_offset;
	const int  *td_sizeof;
};

__attribute__ ((format (printf, 1, 2))) static inline void
printf_check(const char *fmt, ...)
{}

void *c2_trace_allot(const struct c2_trace_descr *td);
int   c2_trace_parse(void);

/**
   This is a low-level entry point into tracing sub-system.

   Don't call this directly, use C2_LOG* macros instead.

   Add a fixed-size trace entry into the trace buffer.

   A typical examples of usage are

   @code
   C2_TRACE_POINT(1, { uint32_t nr_calls; }, { 0 }, "Calls: %u", calls++);
   @endcode

   and

   @code
   C2_TRACE_POINT({ uint64_t fop_opcode; uint16_t got_lock; },
                  { 0, 8 }, "Opcode: %llx, lock: %u",
                  fop->f_opcode, c2_mutex_is_locked(&queue_lock));
   @endcode

   The DECL parameter is a C definition of a trace entry format. The variadic
   arguments must match the number and types of fields in the format.
 */
#define C2_TRACE_POINT(NR, DECL, OFFSET, SIZEOF, FMT, ...)		\
({									\
	struct t_body DECL;						\
	static const int _offset[NR] = OFFSET;				\
	static const int _sizeof[NR] = SIZEOF;				\
	static const struct c2_trace_descr td  = {			\
                .td_fmt    = (FMT),					\
		.td_func   = __func__,					\
		.td_file   = __FILE__,					\
		.td_line   = __LINE__,					\
		.td_size   = sizeof(struct t_body),			\
		.td_nr     = (NR),					\
		.td_offset = _offset,					\
		.td_sizeof = _sizeof					\
	};								\
	printf_check(FMT , ## __VA_ARGS__);				\
	*(struct t_body *)c2_trace_allot(&td) = 			\
                                (const struct t_body){ __VA_ARGS__ };	\
})

enum {
	C2_TRACE_ARGC_MAX = 9
};

#define __T_T(a, v) typeof(a) v
#define __T_O(v) offsetof(struct t_body, v)
#define __T_S(a) sizeof(a)
#define __T_P(...) __VA_ARGS__

#define C2_LOG0(fmt)     C2_TRACE_POINT(0, { ; }, {}, {}, fmt)

#define C2_LOG1(fmt, a0)			\
C2_TRACE_POINT(1,				\
	       { __T_T(a0, v0); },			\
	       { __T_O(v0) },			\
	       { __T_S(a0) },			\
	       fmt, a0)


#define C2_LOG2(fmt, a0, a1)			\
C2_TRACE_POINT(2,				\
	       { __T_T(a0, v0); __T_T(a1, v1); },	\
	       __T_P({ __T_O(v0), __T_O(v1) }),		\
	       __T_P({ __T_S(a0), __T_S(a1) }),		\
	       fmt, a0, a1)

#define C2_LOG3(fmt, a0, a1, a2)			\
C2_TRACE_POINT(3,					\
	       { __T_T(a0, v0); __T_T(a1, v1); __T_T(a2, v2); },	\
	       __T_P({ __T_O(v0), __T_O(v1), __T_O(v2) }),		\
	       __T_P({ __T_S(a0), __T_S(a1), __T_S(a2) }),		\
	       fmt, a0, a1, a2)

#define C2_LOG4(fmt, a0, a1, a2, a3)					\
C2_TRACE_POINT(4,							\
	       { __T_T(a0, v0); __T_T(a1, v1); __T_T(a2, v2); __T_T(a3, v3); },	\
	       __T_P({ __T_O(v0), __T_O(v1), __T_O(v2), __T_O(v3) }),	\
	       __T_P({ __T_S(a0), __T_S(a1), __T_S(a2), __T_S(a3) }),	\
	       fmt, a0, a1, a2, a3)

#define C2_LOG5(fmt, a0, a1, a2, a3, a4)				\
C2_TRACE_POINT(5,							\
	       { __T_T(a0, v0); __T_T(a1, v1); __T_T(a2, v2); __T_T(a3, v3); __T_T(a4, v4); },	\
	       __T_P({ __T_O(v0), __T_O(v1), __T_O(v2), __T_O(v3), __T_O(v4) }),		\
	       __T_P({ __T_S(a0), __T_S(a1), __T_S(a2), __T_S(a3), __T_S(a4) }),		\
	       fmt, a0, a1, a2, a3, a4)

#define C2_LOG6(fmt, a0, a1, a2, a3, a4, a5)				\
C2_TRACE_POINT(6,							\
	       { __T_T(a0, v0); __T_T(a1, v1); __T_T(a2, v2); __T_T(a3, v3); __T_T(a4, v4); \
		 __T_T(a5, v5); },						\
	       __T_P({ __T_O(v0), __T_O(v1), __T_O(v2), __T_O(v3), __T_O(v4), __T_O(v5) }),	\
	       __T_P({ __T_S(a0), __T_S(a1), __T_S(a2), __T_S(a3), __T_S(a4), __T_S(a5) }),	\
	       fmt, a0, a1, a2, a3, a4, a5)

#define C2_LOG7(fmt, a0, a1, a2, a3, a4, a5, a6)			\
C2_TRACE_POINT(7,							\
	       { __T_T(a0, v0); __T_T(a1, v1); __T_T(a2, v2); __T_T(a3, v3); __T_T(a4, v4); \
		 __T_T(a5, v5); __T_T(a6, v6); },				\
	       __T_P({ __T_O(v0), __T_O(v1), __T_O(v2), __T_O(v3), __T_O(v4), __T_O(v5), __T_O(v6) }), \
	       __T_P({ __T_S(a0), __T_S(a1), __T_S(a2), __T_S(a3), __T_S(a4), __T_S(a5), __T_S(a6) }), \
	       fmt, a0, a1, a2, a3, a4, a5, a6)

#define C2_LOG8(fmt, a0, a1, a2, a3, a4, a5, a6, a7)			\
C2_TRACE_POINT(8,							\
	       { __T_T(a0, v0); __T_T(a1, v1); __T_T(a2, v2); __T_T(a3, v3); __T_T(a4, v4); \
		 __T_T(a5, v5); __T_T(a6, v6); __T_T(a7, v7); },			\
	       __T_P({ __T_O(v0), __T_O(v1), __T_O(v2), __T_O(v3), __T_O(v4), __T_O(v5), __T_O(v6), \
				       __T_O(v7) }),			\
	       __T_P({ __T_S(a0), __T_S(a1), __T_S(a2), __T_S(a3), __T_S(a4), __T_S(a5), __T_S(a6), \
				       __T_S(a7) }),			\
	       fmt, a0, a1, a2, a3, a4, a5, a6, a7)

#define C2_LOG9(fmt, a0, a1, a2, a3, a4, a5, a6, a7, a8)		\
C2_TRACE_POINT(9,							\
	       { __T_T(a0, v0); __T_T(a1, v1); __T_T(a2, v2); __T_T(a3, v3); __T_T(a4, v4); \
		 __T_T(a5, v5); __T_T(a6, v6); __T_T(a7, v7); __T_T(a8, v8); },	\
	       __T_P({ __T_O(v0), __T_O(v1), __T_O(v2), __T_O(v3), __T_O(v4), __T_O(v5), __T_O(v6), \
				       __T_O(v7), __T_O(v8) }),		\
	       __T_P({ __T_S(a0), __T_S(a1), __T_S(a2), __T_S(a3), __T_S(a4), __T_S(a5), __T_S(a6), \
				       __T_S(a7), __T_S(a8) }),		\
	       fmt, a0, a1, a2, a3, a4, a5, a6, a7, a8)

int  c2_trace_init(void);
void c2_trace_fini(void);

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
