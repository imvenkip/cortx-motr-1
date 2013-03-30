#! /bin/sh

#
# COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
#
# THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
# HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
# LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
# THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
# BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
# USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
# EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
#
# YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
# THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
# http://www.xyratex.com/contact
#
# Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
# Original creation date: 31-Mar-2013

#
# This script measures time spent by a process is system calls.
#
# Usage:
#
#     slugabed.sh PATH_TO_BINARY
#
# or
#
#     slugabed.sh PID
#
# The script doesn't start the process. The process can be started after or
# before the script.
#
# Script terminates when the process completes or when the script is
# interrupted. The output is a sequence of entries, like the following:
#
#     sum: 204029800 min: 11 max: 129181 avg: 4980 num: 40964
#     __lll_lock_wait+0x24 [libpthread-2.12.so]
#     _L_lock_854+0xf [libpthread-2.12.so]
#     __pthread_mutex_lock+0x37 [libpthread-2.12.so]
#     m0_mutex_lock+0x5b [libmero-0.1.0.so]
#     nlx_tm_ev_worker+0x48f [lt-ut]
#     m0_thread_trampoline+0xc1 [libmero-0.1.0.so]
#     uthread_trampoline+0x37 [libmero-0.1.0.so]
#     start_thread+0xd1 [libpthread-2.12.so]
#     __clone+0x6d [libc-2.12.so]
# 
# Each entry is the header line followed by backtrace. The header line
# summarises the time spent in a system call entered from the given backtrace:
#
#     sum: the total time in microseconds,
#     min: the minimal system call time,
#     max: the maximal system call time,
#     avg: the average time,
#     num: a number of times the kernel was entered from this backtrace.
#
# The output is sorted in the decreasing "sum" order.
#
# Caveats:
#
# The script needs kernel debugging symbols (does it?).
#
# The process should not fork.
#
# There should be a single running process matching the command line argument.
#
# Page faults are not measured (and they should,
# http://sourceware.org/systemtap/tapsets/API-vm-pagefault.html).
#
# The script introduces significant overhead. E.g., the total mero UT time
# increases by 30%.
#
#
# Sometimes the script is aborted due to excessive probe cycle count. Backtrace
# is expensive. Test newer systap versions.
#
# The "end" probe is after aborted, because it is too long. This is why the
# output is sorted by the decreasing total time.
#
#

#set -x
set -a

proc="$1"

#
# build a list of dynamic libraries
#
dlib=$(ldd $proc | while read name arrow path address ;do
    if [ -n "$address" ] ;then
	echo "-d $path"
    fi
done)
objs_opt="-d /lib64/ld-2.12.so -d /lib64/libpthread-2.12.so -d /lib64/libselinux.so.1 $dlib -d $proc"

#
# MAXTRACE=10        reduces the depth of collected backtraces to 10 to make
#                    ubacktrace() faster.
#
# STP_NO_OVERLOAD    disables aborting script on running out of cycle limit.
#
# MAXSKIPPED=1000000 some probes are skipped, because of delays, don't abort the
#                    script due to this.
#
# MAXERRORS=1000     ignores some spurious utrace errors.
#
#
sudo stap -g $objs_opt -DMAXTRACE=10 -DSTP_NO_OVERLOAD \
                       -DMAXSKIPPED=1000000 -DMAXERRORS=1000 \
    ./scripts/slugabed.stp $proc





