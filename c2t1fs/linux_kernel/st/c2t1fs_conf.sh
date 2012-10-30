#!/bin/sh

. `dirname $0`/common.sh
. `dirname $0`/c2t1fs_common_inc.sh
. `dirname $0`/c2t1fs_client_inc.sh
. `dirname $0`/c2t1fs_server_inc.sh

local_conf_test()
{
	pool_width=$1
	data_units=$2
	parity_units=$3
	local_conf=$4

	mount_c2t1fs $COLIBRI_C2T1FS_MOUNT_DIR 4 $local_conf \
	    &>> $COLIBRI_TEST_LOGFILE || return 1

	unmount_and_clean &>> $COLIBRI_TEST_LOGFILE
	return 0
}

error() { echo "$@" >&2; exit 1; }

main()
{
	modprobe lnet
	lctl network up &>> /dev/null
	lnet_nid=`lctl list_nids | head -1`
	export COLIBRI_IOSERVICE_ENDPOINT="$lnet_nid:12345:34:1"
	export COLIBRI_C2T1FS_ENDPOINT="$lnet_nid:12345:34:6"

	colibri_service start
	[ $? -ne "0" ] && error 'Failed to start Colibri Service.'

	sleep 3

	local_conf_test $POOL_WIDTH 1 1 $1
	[ $? -ne "0" ] && error 'Failed: Local configuration test failed.'

	colibri_service stop
	[ $? -ne "0" ] && error 'Failed to stop Colibri Service.'

	return 0
}

trap unprepare EXIT

main $1
