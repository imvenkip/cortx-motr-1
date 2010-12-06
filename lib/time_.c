/* -*- C -*- */

/* Note: the filename time_.c is required because this file is also
   included in the linux_kernel/ tree, where it must not conflict
   with the local time.c there.
*/

#include "lib/cdefs.h" /* C2_EXPORTED */
#include "lib/time.h"

/**
   @addtogroup time

   Implementation of time functions on top of all c2_time defs

   @{
*/

/** Create a c2_time struct from seconds and ns
 */
void c2_time_set(struct c2_time *time, uint64_t secs, long ns)
{
	time->ts.tv_sec = secs;
	time->ts.tv_nsec = ns;
}
C2_EXPORTED(c2_time_set);

/** Flatten a c2_time structure into uint64 nanoseconds.
 This gives a limit of 585 years before we wrap. */
uint64_t c2_time_flatten(const struct c2_time *time)
{
        return ((uint64_t)time->ts.tv_sec) * ONE_BILLION + time->ts.tv_nsec;
}
C2_EXPORTED(c2_time_flatten);

/** Add t2 to t1
 */
void c2_time_add(const struct c2_time *t1, const struct c2_time *t2,
                 struct c2_time *res)
{
	uint64_t sum;

        sum = c2_time_flatten(t1) + c2_time_flatten(t2);
	res->ts.tv_sec = sum / ONE_BILLION;
	res->ts.tv_nsec = sum % ONE_BILLION;
}
C2_EXPORTED(c2_time_add);

/** Subtract t2 from t1
 */
void c2_time_sub(const struct c2_time *t1, const struct c2_time *t2,
                 struct c2_time *res)
{
	int64_t diff;

        diff = c2_time_flatten(t1) - c2_time_flatten(t2);
	res->ts.tv_sec = diff / ONE_BILLION;
	res->ts.tv_nsec = diff % ONE_BILLION;
}
C2_EXPORTED(c2_time_sub);

/** Is time a after time b?
*/
bool c2_time_after(const struct c2_time *a, const struct c2_time *b)
{
        return ((int64_t)c2_time_flatten(a) - (int64_t)c2_time_flatten(b)) > 0;
}
C2_EXPORTED(c2_time_after);

bool c2_time_after_eq(const struct c2_time *a, const struct c2_time *b)
{
        return ((int64_t)c2_time_flatten(a) - (int64_t)c2_time_flatten(b)) >= 0;
}
C2_EXPORTED(c2_time_after_eq);

/** @} end of time group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
