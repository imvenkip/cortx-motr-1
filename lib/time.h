/* -*- C -*- */

#ifndef __COLIBRI_LIB_TIME_H__
#define __COLIBRI_LIB_TIME_H__

#include <lib/types.h>

/**
   @defgroup time Generic time manipulation

   C2 time delivers resolution in nanoseconds. It is an unsigned 64-bit integer.
   @{
*/

#ifndef __KERNEL__
#include "lib/user_space/time.h"
#else
#include "lib/linux_kernel/time.h"
#endif

typedef uint64_t c2_time_t;

enum {
	C2_TIME_ONE_BILLION = 1000000000ULL
};

/**
   Get the current time.  This may or may not relate to wall time.

   @param time [OUT] current time if pointer is non-NULL
   @retval current time
*/
c2_time_t c2_time_now(c2_time_t *time);

/**
   Create a c2_time_t from seconds and nanosecond

   @param time [OUT] the result time.
   @param secs seconds from epoch
   @param ns nanoseconds
   @retval the result time.
 */
c2_time_t c2_time_set(c2_time_t *time, uint64_t secs, long ns);

/**
   Add t2 to t1, and return that result

   @retval the result time
 */
c2_time_t c2_time_add(const c2_time_t t1, const c2_time_t t2);

/**
   Subtract t2 from t1, and return that result

   @retval the result time
 */
c2_time_t c2_time_sub(const c2_time_t t1, const c2_time_t t2);

/**
   Is time a after time b?
 */
bool c2_time_after(const c2_time_t a, const c2_time_t b);

/**
   Is time a after or equal to time b?
 */
bool c2_time_after_eq(const c2_time_t a, const c2_time_t b);

/**
   Sleep for requested time. If interrupted, remaining time returned.

   @param req requested time to sleep
   @param rem [OUT] remaining time
   @retval 0 means success. -1 means error. remaining time will be stored
           in @rem.
*/
int c2_nanosleep(const c2_time_t req, c2_time_t *rem);

/**
   Get "second" part from the time

   @retval second part of the time
 */
uint64_t c2_time_seconds(const c2_time_t time);

/**
   Get "nanosecond" part from the time

   @retval nanosecond part of the time
 */
uint64_t c2_time_nanoseconds(const c2_time_t time);


/**
   the biggest time that never reaches in system life.
*/
extern const c2_time_t C2_TIME_NEVER;

/** @} end of time group */

/* __COLIBRI_LIB_TIME_H__ */
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
