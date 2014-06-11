#!/bin/bash
CWD=$(cd "$( dirname "$0")" && pwd)
SRC="$CWD/../.."

rc=0
sudo "$SRC/m0t1fs/linux_kernel/st/st" insmod
"$CWD/st" || rc=$?
sudo "$SRC/m0t1fs/linux_kernel/st/st" rmmod
[ $rc -eq 0 ] || exit $rc

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
echo
echo "rpcping: please enable full rpcping test when resend for reply"
echo "is implemented. See rpc/it/st, MERO-244 and MERO-637 for references."
echo
echo "rpcping: test status: SUCCESS"
