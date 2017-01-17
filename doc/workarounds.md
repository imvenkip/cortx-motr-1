List of workarounds for third-party libraries and external dependencies
=======================================================================

* `sem_timedwait(3)` from _glibc_ on _Centos_ >= 7.2

  **Problem**: `sem_timedwait(3)` returns `-ETIMEDOUT` immediately if `tv_sec` is
  greater than `gettimeofday(2) + INT_MAX`, that makes `m0_semaphore_timeddown(M0_TIME_NEVER)`
  to exit immediately instead of sleeping "forever".

  **Solution**: truncate `abs_timeout` inside `m0_semaphore_timeddown()` to
  `INT_MAX - 1`.

  **Impact**: limits sleep duration for `m0_semaphore_timeddown(M0_TIME_NEVER)`
  calls to fixed point in time which is `INT_MAX` seconds starting from the
  beginning of Unix epoch, which is approximately year 2038.

  **Source**: `lib/user_space/semaphore.c: m0_semaphore_timeddown()`

  **References**:
    - [CASTOR-1990: Different sem_timedwait() behaviour on real cluster node and EC2 node](https://jts.seagate.com/browse/CASTOR-1990)
    - [Bug 1412082 - futex_abstimed_wait() always converts abstime to relative time](https://bugzilla.redhat.com/show_bug.cgi?id=1412082)
