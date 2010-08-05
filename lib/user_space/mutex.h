/* -*- C -*- */

#ifndef __COLIBRI_LIB_USER_SPACE_MUTEX_H__
#define __COLIBRI_LIB_USER_SPACE_MUTEX_H__

#include <pthread.h>

#include "lib/cdefs.h"

/**
   @addtogroup mutex 

   <b>User space mutex.</b>
   @{
*/

struct c2_mutex {
	/* POSIX mutex. */
	pthread_mutex_t m_impl;
	pthread_t       m_owner;
};

/** @} end of mutex group */

/* __COLIBRI_LIB_USER_SPACE_MUTEX_H__ */
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
