/* -*- C -*- */

#ifndef __COLIBRI_LIB_USER_SPACE_TIME_H__
#define __COLIBRI_LIB_USER_SPACE_TIME_H__

#include <sys/time.h>
#include_next <time.h>

/**
   @addtogroup time

   <b>User space time.</b>
   @{
*/

struct c2_time {
        struct timespec ts;
};

/** @} end of time group */

/* __COLIBRI_LIB_USER_SPACE_TIME_H__ */
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
