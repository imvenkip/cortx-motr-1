#!/bin/sh

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh

. $M0_SRC_DIR/scripts/functions  # opcode

# Got these states from "$MERO_CORE_DIR/ha/note.h"
M0_NC_ONLINE=1
M0_NC_FAILED=2

start_stop_m0d()
{
	local dev_state=$1
	local m0d_pid

	if [ $dev_state -eq $M0_NC_FAILED ]
	then
		# Stop m0d
		echo -n Stopping m0d on controller ep $IOS_PVER2_EP ...
		m0d_pid=`pgrep -n lt-m0d `
		kill -TERM $m0d_pid >/dev/null 2>&1
		sleep 10
	else
		#restart m0d
		echo $cmd
		(eval "$IOS5_CMD") &
		while ! grep -q CTRL $MERO_M0T1FS_TEST_DIR/ios5/m0d.log;
		do
			sleep 2
		done
		# Give some time to initialise services ...
		sleep 10
	fi
}

change_controller_state()
{
	local lnet_nid=`sudo lctl list_nids | head -1`
	local s_endpoint="$lnet_nid:12345:33:1"
	local c_endpoint="$lnet_nid:12345:30:*"
	local dev_fid=$1
	local dev_state=$2
	local start_stop_m0d_flag=$3
	local ha_fop="[1: ($dev_fid, $dev_state)]"

	if [ $start_stop_m0d_flag -eq "1" ]
	then
		start_stop_m0d $dev_state
	fi

	# Generate HA event
	$M0_SRC_DIR/console/bin/m0console \
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
	local ctrl_from_pver0="^c|1:8"
	local ctrl_from_pver1="^c|10:1"
	local process_from_pver1="^r|10:1"
	local ios_from_pver1="^s|10:1"
        local disk_from_pver0="^k|1:1"
        local disk_from_pver1="^k|10:1"

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

	### Mark ctrl from pool version 0 as failed.
	change_controller_state "$ctrl_from_pver0" "$M0_NC_FAILED" "0" || {
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

	### It is expected that on failure/online HA dispatch events of some components,
	### HA dispatch events to it's descendants.
	### Mark ctrl from pool version 1 as failed.
	change_controller_state "$ctrl_from_pver1" "$M0_NC_FAILED" "1" || {
		unmount_and_clean
		return 1
	}
	### Mark process(m0d) from pool version 1 as failed.
	change_controller_state "$process_from_pver1" "$M0_NC_FAILED" "0" || {
		unmount_and_clean
		return 1
	}
	### Mark ios from pool version 1 as failed.
	change_controller_state "$ios_from_pver1" "$M0_NC_FAILED" "0" || {
		unmount_and_clean
		return 1
	}

	###  Should fail since no pool available now failed.
	touch $MERO_M0T1FS_MOUNT_DIR/$file3 && {
		unmount_and_clean
		return 1
	}

	### Mark ctrl from pool version 1 as running.
	### It reconnects endpoints from controller.
	change_controller_state "$ctrl_from_pver1" "$M0_NC_ONLINE" "1" || {
		unmount_and_clean
		return 1
	}
	### Mark process(m0d) from pool version 1 as online.
	change_controller_state "$process_from_pver1" "$M0_NC_ONLINE" "0" || {
		unmount_and_clean
		return 1
	}
	### Mark ios from pool version 1 as online.
	change_controller_state "$ios_from_pver1" "$M0_NC_ONLINE" "0" || {
		unmount_and_clean
		return 1
	}
	###  Should succeed since pool version 1 is available now.
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

	### Mark ctrl from pool version 0 as online.
	change_controller_state "$ctrl_from_pver0" "$M0_NC_ONLINE" "0" || {
		unmount_and_clean
		return 1
	}

        #Test pool version switch on disk failures.
        ### Mark disk from pool version 0 as failed.
        change_controller_state "$disk_from_pver0" "$M0_NC_FAILED" "0" || {
                unmount_and_clean
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

        ### Mark disk from pool version 1 as failed.
        change_controller_state "$disk_from_pver1" "$M0_NC_FAILED" "0" || {
                unmount_and_clean
                return 1
        }

        ###  Should fail since no pool available now failed.
        touch $MERO_M0T1FS_MOUNT_DIR/$file2 && {
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

	sandbox_init

	set -o pipefail
	m0t1fs_pool_version_assignment 2>&1 | tee -a $MERO_TEST_LOGFILE
	rc=$?

	if [ $rc -eq 0 ]; then
		sandbox_fini
	else
		echo "Test log available at $MERO_TEST_LOGFILE."
	fi
	return $rc
}

trap unprepare EXIT
main
