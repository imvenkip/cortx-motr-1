/* -*- C -*- */

#ifndef __COLIBRI_LIB_USER_SPACE_SEMAPHORE_H__
#define __COLIBRI_LIB_USER_SPACE_SEMAPHORE_H__

#include_next <semaphore.h>

/**
   @addtogroup semaphore

   <b>User space semaphore.</b>
   @{
*/

struct c2_semaphore {
	/* POSIX semaphore. */
	sem_t s_sem;
};

/** @} end of semaphore group */

/* __COLIBRI_LIB_USER_SPACE_SEMAPHORE_H__ */
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
