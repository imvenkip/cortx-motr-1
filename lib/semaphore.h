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
 * Original creation date: 03/11/2011
 */

#ifndef __COLIBRI_LIB_SEMAPHORE_H__
#define __COLIBRI_LIB_SEMAPHORE_H__

#include "lib/types.h"
#include "lib/time.h"

/**
   @defgroup semaphore Dijkstra semaphore

   Wait on a c2_semaphore is non-interruptable: signals won't preempt it. Use
   semaphores to wait for events that are expected to arrive in a "short time".

   @see http://en.wikipedia.org/wiki/Semaphore_(programming)

   @{
 */

#ifndef __KERNEL__
#include "lib/user_space/semaphore.h"
#else
#include "lib/linux_kernel/semaphore.h"
#endif

/* struct c2_semaphore is defined by headers above. */

int  c2_semaphore_init(struct c2_semaphore *semaphore, unsigned value);
void c2_semaphore_fini(struct c2_semaphore *semaphore);

/**
   Downs the semaphore (P-operation).
 */
void c2_semaphore_down(struct c2_semaphore *semaphore);

/**
   Ups the semaphore (V-operation).
 */
void c2_semaphore_up(struct c2_semaphore *semaphore);

/**
   Tries to down a semaphore without blocking.

   Returns true iff the P-operation succeeded without blocking.
 */
bool c2_semaphore_trydown(struct c2_semaphore *semaphore);


/**
   Returns the number of times a P-operation could be executed without blocking.

   @note the return value might, generally, be invalid by the time
   c2_semaphore_value() returns.

   @note that the parameter is not const. This is because of POSIX
   sem_getvalue() prototype.
 */
unsigned c2_semaphore_value(struct c2_semaphore *semaphore);

/**
   Downs the semaphore, blocking for not longer than the (absolute) timeout
   given.

   @param abs_timeout absolute time since Epoch (00:00:00, 1 January 1970)
   @return true if P-operation succeed immediately or before timeout;
   @return false otherwise.

 */
bool c2_semaphore_timeddown(struct c2_semaphore *semaphore,
			    const c2_time_t abs_timeout);

/** @} end of semaphore group */

/* __COLIBRI_LIB_SEMAPHORE_H__ */
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
