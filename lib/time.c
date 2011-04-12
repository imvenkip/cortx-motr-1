/* -*- C -*- */

#include "lib/cdefs.h" /* C2_EXPORTED */
#include "lib/assert.h" /* C2_PRE */
#include "lib/time.h"

/**
   @addtogroup time

   Implementation of time functions on top of all c2_time defs

   @{
*/

/**
   Create a c2_time struct from seconds and ns
 */
struct c2_time *c2_time_set(struct c2_time *time, uint64_t secs, long ns)
{
	uint64_t nanos = secs * C2_TIME_ONE_BILLION + ns;
	time->ts.tv_sec = nanos / C2_TIME_ONE_BILLION;
	time->ts.tv_nsec = nanos % C2_TIME_ONE_BILLION;
	return time;
}
C2_EXPORTED(c2_time_set);

/**
   Flatten a c2_time structure into uint64 nanoseconds.

   This gives a limit of 585 years before we wrap.
*/
uint64_t c2_time_flatten(const struct c2_time *time)
{
        return ((uint64_t)time->ts.tv_sec) * C2_TIME_ONE_BILLION + time->ts.tv_nsec;
}
C2_EXPORTED(c2_time_flatten);

/**
   Add t2 to t1
 */
struct c2_time *c2_time_add(const struct c2_time *t1, const struct c2_time *t2,
                            struct c2_time *res)
{
	uint64_t sum;

	C2_PRE(c2_time_after_eq(&C2_TIME_NEVER, t1));
	C2_PRE(c2_time_after_eq(&C2_TIME_NEVER, t2));

	if (c2_time_flatten(t1) == c2_time_flatten(&C2_TIME_NEVER) ||
	    c2_time_flatten(t2) == c2_time_flatten(&C2_TIME_NEVER)) {
		*res = C2_TIME_NEVER;
	} else {
		sum = c2_time_flatten(t1) + c2_time_flatten(t2);
		res->ts.tv_sec = sum / C2_TIME_ONE_BILLION;
		res->ts.tv_nsec = sum % C2_TIME_ONE_BILLION;
	}
	C2_POST(c2_time_after_eq(res, t1));
	C2_POST(c2_time_after_eq(res, t2));
	return res;
}
C2_EXPORTED(c2_time_add);

/**
   Subtract t2 from t1
 */
struct c2_time *c2_time_sub(const struct c2_time *t1, const struct c2_time *t2,
			    struct c2_time *res)
{
	int64_t diff;

	C2_PRE(c2_time_after_eq(&C2_TIME_NEVER, t1));
	C2_PRE(c2_time_after   (&C2_TIME_NEVER, t2));
	C2_PRE(c2_time_after_eq(t1, t2));

	if (c2_time_flatten(t1) == c2_time_flatten(&C2_TIME_NEVER)) {
		*res = C2_TIME_NEVER;
	} else {
		diff = c2_time_flatten(t1) - c2_time_flatten(t2);
		res->ts.tv_sec = diff / C2_TIME_ONE_BILLION;
		res->ts.tv_nsec = diff % C2_TIME_ONE_BILLION;
	}
	C2_POST(c2_time_after_eq(t1, res));
	return res;
}
C2_EXPORTED(c2_time_sub);

/**
   Is time a after time b?
*/
bool c2_time_after(const struct c2_time *a, const struct c2_time *b)
{
	return c2_time_flatten(a) > c2_time_flatten(b);
}
C2_EXPORTED(c2_time_after);

/**
   Is time a after or equal to time b?
*/
bool c2_time_after_eq(const struct c2_time *a, const struct c2_time *b)
{
	return c2_time_flatten(a) >= c2_time_flatten(b);
}
C2_EXPORTED(c2_time_after_eq);

/**
   Get "second" part from the time

   @retval second part of the time
 */
uint64_t c2_time_seconds(const struct c2_time *time)
{
	return time->ts.tv_sec;
}
C2_EXPORTED(c2_time_seconds);

/**
   Get "nanosecond" part from the time

   @retval nanosecond part of the time
 */
uint64_t c2_time_nanoseconds(const struct c2_time *time)
{

        return time->ts.tv_nsec;
}
C2_EXPORTED(c2_time_nanoseconds);


const struct c2_time C2_TIME_NEVER = {
	.ts = {
		.tv_sec  = ~0ULL / C2_TIME_ONE_BILLION - 1,
		.tv_nsec = C2_TIME_ONE_BILLION - 1,
	}
};

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
