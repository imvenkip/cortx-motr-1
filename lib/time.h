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


#define ONE_BILLION 1000000000

/** Get the current time.  This may or may not relate to wall time.

    @param time [OUT] current time if pointer is non-NULL
    @retval current time
*/
void c2_time_now(struct c2_time *time);

/** Flatten a c2_time structure into a uint64
    @retval time in nanoseconds
 */
uint64_t c2_time_flatten(const struct c2_time *time);

/** Create a c2_time struct from seconds and ns
 */
void c2_time_set(struct c2_time *time, uint64_t secs, long ns);

/** Add t2 to t1
 */
void c2_time_add(const struct c2_time *t1, const struct c2_time *t2,
                 struct c2_time *res);

/** Subtract t2 from t1
 */
void c2_time_sub(const struct c2_time *t1, const struct c2_time *t2,
                 struct c2_time *res);

/** Is time a after time b?
 */
bool c2_time_after(const struct c2_time *a, const struct c2_time *b);

/** Is time a after or equal to time b?
 */
bool c2_time_after_eq(const struct c2_time *a, const struct c2_time *b);


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
