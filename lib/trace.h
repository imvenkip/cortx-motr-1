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
	const char *td_func;
	const char *td_file;
	int         td_line;
	int         td_size;
	const char *td_decl;
};

void *c2_trace_allot(const struct c2_trace_descr *td);
int   c2_trace_parse(void);

/**
   Add a fixed-size trace entry into the trace buffer.

   A typical examples of usage are

   @code
   C2_TRACE_POINT({ uint32_t nr_calls; }, calls++);
   @endcode

   and

   @code
   C2_TRACE_POINT({ uint64_t fop_opcode; uint16_t got_lock; }, 
                  fop->f_opcode, c2_mutex_is_locked(&queue_lock));
   @endcode

   The first argument, DECL is a C definition of a trace entry format. The
   remaining arguments must match the number and types of fields in the format.
 */
#define C2_TRACE_POINT(DECL, ...)					\
({									\
	struct t_body DECL;						\
									\
	static const struct c2_trace_descr td  = {			\
		.td_func = __func__,					\
		.td_file = __FILE__,					\
		.td_line = __LINE__,					\
		.td_size = sizeof(struct t_body),			\
		.td_decl = #DECL					\
	};								\
	*(struct t_body *)c2_trace_allot(&td) = 			\
                                (const struct t_body){ __VA_ARGS__ };	\
})

#ifdef __KERNEL__
#   define __TRACE(format, args ...) printk(format, ## args)
#else
#   include <stdio.h>
#   define __TRACE(format, args ...) fprintf(stderr, format, ## args)
#endif

/** No trace messages at all */
#define C2_TRACE_LEVEL_DISABLED     0

/** Do not trace function entry and exit points */
#define C2_TRACE_LEVEL_NO_START_END 1

/** Enable all trace points */
#define C2_TRACE_LEVEL_ALL          2

/** Select tracing level. Set to one of C2_TRACE_LEVEL_* */
#define C2_TRACE_LEVEL              C2_TRACE_LEVEL_ALL

/* Enable C2_TRACE() for all trace levels except C2_TRACE_LEVEL_DISABLED */

#if C2_TRACE_LEVEL > C2_TRACE_LEVEL_DISABLED

#   define C2_TRACE(format, args ...)  \
        __TRACE("colibri: %s[%d]: " format, __FUNCTION__, __LINE__, ## args)

#else /* C2_TRACE_LEVEL <= C2_TRACE_LEVEL_DISABLED */

#   define C2_TRACE(format, args ...)

#endif

/* Enable C2_TRACE_START() and C2_TRACE_END() for only C2_TRACE_LEVEL_ALL */
#if C2_TRACE_LEVEL >= C2_TRACE_LEVEL_ALL

#   define C2_TRACE_START()   C2_TRACE("Start\n")
#   define C2_TRACE_END(rc)   C2_TRACE("End (0x%lx)\n", (unsigned long)(rc))

#else /* C2_TRACE_LEVEL < C2_TRACE_LEVEL_ALL */

#   define C2_TRACE_START()
#   define C2_TRACE_END(rc)

#endif

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
