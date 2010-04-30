/* -*- C -*- */

#ifndef __COLIBRI_LIB_QUEUE_H__
#define __COLIBRI_LIB_QUEUE_H__

#include "lib/cdefs.h"

/**
 queue link
 */
struct c2_queue_link {
	/**
	 * pointer to next in queue
	 */
	struct c2_queue_link *next;
};


/**
 queue
 */
struct c2_queue {
	/**
	 * pointer to first element in queue
	 */
	struct c2_queue_link *head;
};

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
