#!/bin/bash
set -eu

SANDBOX_DIR=${SANDBOX_DIR:-/var/mero/sandbox.rpc-st}

M0_SRC_DIR="$(readlink -f $0)"
M0_SRC_DIR="${M0_SRC_DIR%/*/*/*}"
[ -n "${SUDO:-}" ] || SUDO='sudo -E'

. "$M0_SRC_DIR/scripts/functions"  # sandbox_init

rc=0
sandbox_init
$SUDO "$M0_SRC_DIR/conf/st" insmod
"$M0_SRC_DIR/rpc/it/st" || rc=$?
$SUDO "$M0_SRC_DIR/conf/st" rmmod
[ $rc -eq 0 ] || exit $rc
sandbox_fini

# This message is used by Jenkins as a test success criteria;
# it should appear in STDOUT.
echo 'rpcping: test status: SUCCESS'
