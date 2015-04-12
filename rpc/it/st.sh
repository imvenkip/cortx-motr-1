#!/bin/bash
CWD=$(cd "$( dirname "$0")" && pwd)
SRC="$CWD/../.."

rc=0
sudo "$SRC/conf/st" insmod
"$CWD/st" || rc=$?
sudo "$SRC/conf/st" rmmod
[ $rc -eq 0 ] || exit $rc

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
echo "rpcping: test status: SUCCESS"
