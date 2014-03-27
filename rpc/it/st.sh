#!/bin/bash
CWD=$(cd "$( dirname "$0")" && pwd)
SRC="$CWD/../.."

rc=0
sudo "$SRC/m0t1fs/linux_kernel/st/st" insmod
"$CWD/st" || rc=$?
sudo "$SRC/m0t1fs/linux_kernel/st/st" rmmod
[ $rc -eq 0 ] || exit $rc
