#!/bin/sh
set -eu

# Next lines are useful for ST scripts debugging
# set -eux
# export PS4='+ ${FUNCNAME[0]:+${FUNCNAME[0]}():}line ${LINENO}: '

CWD=$(cd "$( dirname "$0")" && pwd)

source $CWD/st-config.sh
source "$ST_COMMON"

role_space()
{
	local role=$1
	[ "$role" == "$KERNEL_ROLE" ] && echo -n "kernel space"
	[ "$role" != "$KERNEL_ROLE" ] && echo -n "user space"
}

unload_all() {
	modunload
	modunload_galois
}
trap unload_all EXIT

modprobe_lnet
lctl network up > /dev/null
modload_galois
modload || exit $?

export TEST_RUN_TIME=5
echo "transfer machines endpoint prefix is $LNET_PREFIX"
for KERNEL_ROLE in "none" "client" "server"; do
	export KERNEL_ROLE
	echo -n "------ test client in $(role_space client), "
	echo "test server in $(role_space server)"
	echo "--- ping test (test message size is 4KiB)"
	sh $CWD/st-ping.sh
	echo "--- bulk test (test message size is 1MiB)"
	sh $CWD/st-bulk.sh
done

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
echo "net: test status: SUCCESS"
