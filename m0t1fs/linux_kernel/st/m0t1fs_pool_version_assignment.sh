#!/bin/sh

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh

M0_CORE_DIR=`readlink -f $0`
M0_CORE_DIR=${M0_CORE_DIR%/*/*/*/*}

. $M0_CORE_DIR/scripts/functions

send_ha_device_event()
{
	local lnet_nid=`sudo lctl list_nids | head -1`
	local s_endpoint="$lnet_nid:12345:33:1"
	local c_endpoint="$lnet_nid:12345:30:*"
	local dev_fid=$1
	local dev_state=$2
	local ha_fop="[1: ($dev_fid, $dev_state)]"

	$M0_CORE_DIR/console/bin/m0console \
                -f $(opcode M0_HA_NOTE_SET_OPCODE) \
                -s $s_endpoint -c $c_endpoint \
                -d "$ha_fop" || true
}

pool_version_assignment()
{
	local mode=$1
	local file1="0:1000"
	local file2="0:2000"
	local file3="0:3000"
	local file4="0:4000"
	# m0t1fs rpc end point, multiple mount on same
	# node have different endpoint
	local rack_from_pver0="^a|1:6"
	local encl_from_pver1="^e|10:1"
	# Got these states from "$MERO_CORE_DIR/ha/note.h"
	local M0_NC_ONLINE=1
	local M0_NC_FAILED=2

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH "$1"|| {
		return 1
	}

	touch $MERO_M0T1FS_MOUNT_DIR/$file1 || {
		unmount_and_clean
		return 1
	}
	setfattr -n lid -v 8 $MERO_M0T1FS_MOUNT_DIR/$file1
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$file1 bs=1M count=5 || {
		unmount_and_clean
		return 1
	}
	rm -vf $MERO_M0T1FS_MOUNT_DIR/$file1 || {
		unmount_and_clean
		return 1
	}

	### Mark rack from pool version 0 as failed.
	send_ha_device_event "$rack_from_pver0" "$M0_NC_FAILED" || {
		unmount_and_clean
		return 1
	}

	touch $MERO_M0T1FS_MOUNT_DIR/$file2 || {
		unmount_and_clean
		return 1
	}
	setfattr -n lid -v 8 $MERO_M0T1FS_MOUNT_DIR/$file2
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$file2 bs=1M count=5 || {
		unmount_and_clean
		return 1
	}
	rm -vf $MERO_M0T1FS_MOUNT_DIR/$file2 || {
		unmount_and_clean
		return 1
	}

	### Mark encl from pool version 1 as failed.
	send_ha_device_event "$encl_from_pver1" "$M0_NC_FAILED" || {
		unmount_and_clean
		return 1
	}

	###  Should fail since no pool available now failed.
	touch $MERO_M0T1FS_MOUNT_DIR/$file3 && {
		unmount_and_clean
		return 1
	}

	### Mark rack from pool version 0 as running.
	send_ha_device_event "$rack_from_pver0" "$M0_NC_ONLINE" || {
		unmount_and_clean
		return 1
	}

	###  Should succeed since pool version 0 is available now.
	touch $MERO_M0T1FS_MOUNT_DIR/$file4 || {
		unmount_and_clean
		return 1
	}
	setfattr -n lid -v 8 $MERO_M0T1FS_MOUNT_DIR/$file4
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$file4 bs=1M count=5 || {
		unmount_and_clean
		return 1
	}
	rm -vf $MERO_M0T1FS_MOUNT_DIR/$file4 || {
		unmount_and_clean
		return 1
	}

	unmount_and_clean
	return 0
}

m0t1fs_pool_version_assignment()
{
	NODE_UUID=`uuidgen`
	local multiple_pools=1
	mero_service start "$multiple_pools"
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	pool_version_assignment && pool_version_assignment "oostore"
	rc=$?

	mero_service stop
	if [ $? -ne "0" ]
	then
		echo "Failed to stop Mero Service."
		return 1
	fi

	if [ $rc -ne "0" ]
	then
		echo "Failed pool_version_assignment system tests."
		return 1
	fi

	echo "System tests status: SUCCESS."
}

main()
{
	local rc
	echo "System tests start:"
	echo "Test log will be stored in $MERO_TEST_LOGFILE."

	set -o pipefail
	m0t1fs_pool_version_assignment 2>&1 | tee -a $MERO_TEST_LOGFILE
	rc=$?

	echo "Test log available at $MERO_TEST_LOGFILE."

	return $rc
}

trap unprepare EXIT
main
