/* -*- C -*- */

#include "lib/cdefs.h" /* C2_EXPORTED */
#include "lib/assert.h" /* C2_PRE */
#include "lib/time.h"

/**
   @addtogroup time

   Implementation of time functions on top of all c2_time_t defs

   @{
*/


/**
   Create a c2_time_t from seconds and ns
 */
c2_time_t c2_time_set(c2_time_t *time, uint64_t secs, long ns)
{
	*time = secs * C2_TIME_ONE_BILLION + ns;
	return *time;
}
C2_EXPORTED(c2_time_set);

/**
   Add t2 to t1
 */
c2_time_t c2_time_add(const c2_time_t t1, const c2_time_t t2)
{
	c2_time_t res;

	C2_PRE(c2_time_after_eq(C2_TIME_NEVER, t1));
	C2_PRE(c2_time_after_eq(C2_TIME_NEVER, t2));

	if (t1 == C2_TIME_NEVER || t2 == C2_TIME_NEVER) {
		res = C2_TIME_NEVER;
	} else {
		res = t1 + t2;
	}
	C2_POST(c2_time_after_eq(res, t1));
	C2_POST(c2_time_after_eq(res, t2));
	return res;
}
C2_EXPORTED(c2_time_add);

/**
   Subtract t2 from t1
 */
c2_time_t c2_time_sub(const c2_time_t t1, const c2_time_t t2)
{
	c2_time_t res;
	C2_PRE(c2_time_after_eq(C2_TIME_NEVER, t1));
	C2_PRE(c2_time_after   (C2_TIME_NEVER, t2));
	C2_PRE(c2_time_after_eq(t1, t2));

	if (t1 == C2_TIME_NEVER) {
		res = C2_TIME_NEVER;
	} else {
		res = t1 - t2;
	}
	C2_POST(c2_time_after_eq(t1, res));
	return res;
}
C2_EXPORTED(c2_time_sub);

/**
   Is time a after time b?
*/
bool c2_time_after(const c2_time_t a, const c2_time_t b)
{
	return a > b;
}
C2_EXPORTED(c2_time_after);

/**
   Is time a after or equal to time b?
*/
bool c2_time_after_eq(const c2_time_t a, const c2_time_t b)
{
	return a >= b;
}
C2_EXPORTED(c2_time_after_eq);

/**
   Get "second" part from the time

   @retval second part of the time
 */
uint64_t c2_time_seconds(const c2_time_t time)
{
	return time / C2_TIME_ONE_BILLION;
}
C2_EXPORTED(c2_time_seconds);

/**
   Get "nanosecond" part from the time

   @retval nanosecond part of the time
 */
uint64_t c2_time_nanoseconds(const c2_time_t time)
{

        return time % C2_TIME_ONE_BILLION;
}
C2_EXPORTED(c2_time_nanoseconds);


const c2_time_t C2_TIME_NEVER = ~0ULL;

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
