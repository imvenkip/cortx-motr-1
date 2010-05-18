/* -*- C -*- */

#ifndef __COLIBRI_LIB_QUEUE_H__
#define __COLIBRI_LIB_QUEUE_H__

#include <sys/types.h>

#include "cdefs.h"

/**
   @defgroup queue Queue
   @{
 */

struct c2_queue_link;
struct c2_queue {
	struct c2_queue_link *q_head;
	struct c2_queue_link *q_tail;
};

struct c2_queue_link {
	struct c2_queue_link *ql_next;
};

extern const struct c2_queue C2_QUEUE_INIT;

void c2_queue_init(struct c2_queue *q);
void c2_queue_fini(struct c2_queue *q);
bool c2_queue_is_empty(const struct c2_queue *q);

void c2_queue_link_init (struct c2_queue_link *ql);
void c2_queue_link_fini (struct c2_queue_link *ql);
bool c2_queue_link_is_in(const struct c2_queue_link *ql);
bool c2_queue_contains  (const struct c2_queue *q, 
			 const struct c2_queue_link *ql);
size_t c2_queue_length(const struct c2_queue *q);

struct c2_queue_link *c2_queue_get(struct c2_queue *q);
void c2_queue_put(struct c2_queue *q, struct c2_queue_link *ql);

bool c2_queue_invariant(const struct c2_queue *q);

/** @} end of queue group */


/* __COLIBRI_LIB_QUEUE_H__ */
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
