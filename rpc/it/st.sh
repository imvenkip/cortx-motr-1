#!/bin/bash
set -eu

if [ -z "${SRC:-}" ]; then
    SRC="$(readlink -f $0)"
    SRC="${SRC%/*/*/*}"
fi
[ -n "${SUDO:-}" ] || SUDO='sudo -E'

rc=0
$SUDO "$SRC/conf/st" insmod
"$SRC/rpc/it/st" || rc=$?
$SUDO "$SRC/conf/st" rmmod
[ $rc -eq 0 ] || exit $rc

# This message is used by Jenkins as a test success criteria;
# it should appear in STDOUT.
echo 'rpcping: test status: SUCCESS'
