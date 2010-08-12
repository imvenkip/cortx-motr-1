/* -*- C -*- */

#ifndef __COLIBRI_LIB_TRACE_H__
#define __COLIBRI_LIB_TRACE_H__

#include "lib/types.h"

/**
   @defgroup trace Tracing.

   See doc/logging-and-tracing

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
