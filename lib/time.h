/* -*- C -*- */

#ifndef __COLIBRI_LIB_TIME_H__
#define __COLIBRI_LIB_TIME_H__

#include <lib/types.h>

/**
   @defgroup time Generic time manipulation

   There is just one time structure in C2: struct c2_time.
   It can be used for wall time, finding an interval, adding
   times, ordering, etc.  It delivers resolution in nanoseconds,
   and has (at least) the following members:
   <sometype> tv_sec
   long       tv_nsec

   @{
*/

#ifndef __KERNEL__
#include "lib/user_space/time.h"
#else
#include "lib/linux_kernel/time.h"
#endif
/** struct c2_time is defined by headers above. */


enum {
	C2_TIME_ONE_BILLION = 1000000000ULL
};

/**
   Get the current time.  This may or may not relate to wall time.

   @param time [OUT] current time if pointer is non-NULL
   @retval current time
*/
struct c2_time *c2_time_now(struct c2_time *time);

/**
   Flatten a c2_time structure into a uint64

   @retval time in nanoseconds
 */
uint64_t c2_time_flatten(const struct c2_time *time);

/**
   Create a c2_time struct from seconds and nanosecond

   @param time [OUT] the result time.
   @param secs seconds from epoch
   @param ns nanoseconds
   @retval the result time.
 */
struct c2_time *c2_time_set(struct c2_time *time, uint64_t secs, long ns);

/**
   Add t2 to t1, store result in @res, and return that result

   @param res [OUT] the result time
   @retval the result time

   @note it is safe to use one of t1 or t2 as res.
 */
struct c2_time *c2_time_add(const struct c2_time *t1, const struct c2_time *t2,
			    struct c2_time *res);

/**
   Subtract t2 from t1, store result in @res, and return that result

   @retval the result time

   @note it is safe to use one of t1 or t2 as res.
 */
struct c2_time *c2_time_sub(const struct c2_time *t1, const struct c2_time *t2,
			    struct c2_time *res);

/**
   Is time a after time b?
 */
bool c2_time_after(const struct c2_time *a, const struct c2_time *b);

/**
   Is time a after or equal to time b?
 */
bool c2_time_after_eq(const struct c2_time *a, const struct c2_time *b);

/**
   Sleep for requested time. If interrupted, remaining time returned.

   @param req requested time to sleep
   @param rem [OUT] remaining time
   @retval 0 means success. -1 means error. remaining time will be stored
           in @rem.
*/
int c2_nanosleep(const struct c2_time *req, struct c2_time *rem);

/**
   Get "second" part from the time

   @retval second part of the time
 */
uint64_t c2_time_seconds(const struct c2_time *time);

/**
   Get "nanosecond" part from the time

   @retval nanosecond part of the time
 */
uint64_t c2_time_nanoseconds(const struct c2_time *time);


/**
   the biggest time that never reaches in system life.
*/
extern const struct c2_time C2_TIME_NEVER;

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
