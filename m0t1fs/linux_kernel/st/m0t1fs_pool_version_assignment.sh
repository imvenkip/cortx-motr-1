#!/usr/bin/env bash

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh

# Got these states from "$MERO_CORE_DIR/ha/note.h"
M0_NC_ONLINE=1
M0_NC_FAILED=2

multiple_pools=1

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
	local dev_fid=$1
	local dev_state=$2
	local start_stop_m0d_flag=$3
        local eplist=($4)
        local console_ep=$5
	local ha_fop="[1: ($dev_fid, $dev_state)]"

	if [ $start_stop_m0d_flag -eq "1" ]
	then
		start_stop_m0d $dev_state
	fi

	# Generate HA event
	dispatch_ha_events "$ha_fop" "${eplist[*]}" "$console_ep" || {
		unmount_and_clean $multiple_pools
		return 1
	}
}

pool_version_assignment()
{
	local file1="0:1000"
	local file2="0:2000"
	local file3="0:3000"
	local file4="0:4000"
	local file5="0:5000"
	local file6="0:6000"
	# m0t1fs rpc end point, multiple mount on same
	# node have different endpoint
	local ctrl_from_pver0="^c|1:8"
	local ctrl_from_pver1="^c|10:1"
	local process_from_pver1="^r|10:1"
	local ios_from_pver1="^s|10:1"
        local disk_from_pver0="^k|1:1"
        local disk_from_pver1="^k|10:1"
        local lnet_nid=`sudo lctl list_nids | head -1`
        local console_ep="$lnet_nid:$POOL_MACHINE_CLI_EP"
	local client_endpoint="$lnet_nid:12345:33:1"
        local eplist=()

        for (( i=0; i < ${#IOSEP[*]}; i++)) ; do
                eplist[$i]="$lnet_nid:${IOSEP[$i]}"
        done

        eplist=("${eplist[@]}" "$lnet_nid:${HA_EP}" "$client_endpoint")
        all_eps=("${eplist[@]}" "$lnet_nid:$IOS_PVER2_EP")

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR "$1"|| {
		return 1
	}

	touch $MERO_M0T1FS_MOUNT_DIR/$file1 || {
		unmount_and_clean $multiple_pools
		return 1
	}
	setfattr -n lid -v 8 $MERO_M0T1FS_MOUNT_DIR/$file1
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$file1 bs=1M count=5 || {
		unmount_and_clean $multiple_pools
		return 1
	}
	pver_0=$(getfattr -n pver $MERO_M0T1FS_MOUNT_DIR/$file1 | awk -F '=' 'NF > 1 { print $2 }')
	echo "pool version 0 id: $pver_0"

	echo "Mark ctrl from pool version 0 as failed."
	change_controller_state "$ctrl_from_pver0" "$M0_NC_FAILED" "0" "${all_eps[*]}" "$console_ep" || {
		unmount_and_clean $multiple_pools
		return 1
	}

	touch $MERO_M0T1FS_MOUNT_DIR/$file2 || {
		unmount_and_clean $multiple_pools
		return 1
	}
	setfattr -n lid -v 8 $MERO_M0T1FS_MOUNT_DIR/$file2
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$file2 bs=1M count=5 || {
		unmount_and_clean $multiple_pools
		return 1
	}

	pver_1=$(getfattr -n pver $MERO_M0T1FS_MOUNT_DIR/$file2 | awk -F '=' 'NF > 1 { print $2 }')
	echo "pool version 1 id:  $pver_1"
	if  [ "$pver_0" == "$pver_1" ]
	then
		echo "error: ids of pool version 0 and pool version 1 match...."
		unmount_and_clean $multiple_pools
		return 1
	fi

	rm -vf $MERO_M0T1FS_MOUNT_DIR/$file2 || {
		unmount_and_clean $multiple_pools
		return 1
	}

	rm -vf $MERO_M0T1FS_MOUNT_DIR/$file1 || {
		unmount_and_clean $multiple_pools
		return 1
	}

	### It is expected that on failure/online HA dispatch events of some components,
	### HA dispatch events to it's descendants.
	echo "Mark ctrl from pool version 1 as failed."
	change_controller_state "$ctrl_from_pver1" "$M0_NC_FAILED" "1" "${eplist[*]}" "$console_ep" || {
		unmount_and_clean $multiple_pools
		return 1
	}
	echo " Mark process(m0d) from pool version 1 as failed."
	change_controller_state "$process_from_pver1" "$M0_NC_FAILED" "0" "${eplist[*]}" "$console_ep" || {
		unmount_and_clean $multiple_pools
		return 1
	}
	echo "Mark ios from pool version 1 as failed."
	change_controller_state "$ios_from_pver1" "$M0_NC_FAILED" "0" "${eplist[*]}" "$console_ep" || {
		unmount_and_clean $multiple_pools
		return 1
	}

	echo "Should fail since no pool available now failed."
	touch $MERO_M0T1FS_MOUNT_DIR/$file3 && {
		unmount_and_clean $multiple_pools
		return 1
	}

	echo "Mark ctrl from pool version 1 as running."
	### It reconnects endpoints from controller.
	change_controller_state "$ctrl_from_pver1" "$M0_NC_ONLINE" "1" "${eplist[*]}" "$console_ep" || {
		unmount_and_clean $multiple_pools
		return 1
	}
	echo "Mark process(m0d) from pool version 1 as online."
	change_controller_state "$process_from_pver1" "$M0_NC_ONLINE" "0" "${all_eps[*]}" "$console_ep" || {
		unmount_and_clean $multiple_pools
		return 1
	}
	echo "Mark ios from pool version 1 as online."
	change_controller_state "$ios_from_pver1" "$M0_NC_ONLINE" "0" "${all_eps[*]}" "$console_ep" || {
		unmount_and_clean $multiple_pools
		return 1
	}
	echo "Should succeed since pool version 1 is available now."
	touch $MERO_M0T1FS_MOUNT_DIR/$file4 || {
		unmount_and_clean $multiple_pools
		return 1
	}
	setfattr -n lid -v 8 $MERO_M0T1FS_MOUNT_DIR/$file4
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$file4 bs=1M count=5 || {
		unmount_and_clean $multiple_pools
		return 1
	}
	pver=$(getfattr -n pver $MERO_M0T1FS_MOUNT_DIR/$file4 | awk -F '=' 'NF > 1 { print $2 }')
	echo "pool version id: $pver"

	if  [ "$pver" != "$pver_1" ]
	then
		echo "error: old and new pool version ids of pool version 1 do not match"
		unmount_and_clean $multiple_pools
		return 1
	fi

	rm -vf $MERO_M0T1FS_MOUNT_DIR/$file4 || {
		unmount_and_clean $multiple_pools
		return 1
	}

	echo "Mark ctrl from pool version 0 as online."
	change_controller_state "$ctrl_from_pver0" "$M0_NC_ONLINE" "0" "${all_eps[*]}" "$console_ep" || {
		unmount_and_clean $multiple_pools
		return 1
	}

        #Test pool version switch on disk failures.
        echo "Mark disk from pool version 0 as failed"
        dispatch_ha_events "[1: ("$disk_from_pver0", "$M0_NC_FAILED")]" "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }
	touch $MERO_M0T1FS_MOUNT_DIR/$file1 || {
		unmount_and_clean $multiple_pools
		return 1
	}
	setfattr -n lid -v 8 $MERO_M0T1FS_MOUNT_DIR/$file1
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$file1 bs=1M count=5 || {
		unmount_and_clean $multiple_pools
		return 1
	}
	pver_1=$(getfattr -n pver $MERO_M0T1FS_MOUNT_DIR/$file1 | awk -F '=' 'NF > 1 { print $2 }')
	echo "pool version1 id:  $pver_1"

	if  [ "$pver_0" == "$pver_1" ]
	then
		echo "ids of pool version 0 and pool version 1 match...."
		unmount_and_clean $multiple_pools
		return 1
	fi

	rm -vf $MERO_M0T1FS_MOUNT_DIR/$file1 || {
		unmount_and_clean $multiple_pools
		return 1
	}

        touch $MERO_M0T1FS_MOUNT_DIR/$file5 || {
                unmount_and_clean
                return 1
        }

        setfattr -n lid -v 8 $MERO_M0T1FS_MOUNT_DIR/$file5
        dd if=/dev/urandom of=$MERO_M0T1FS_MOUNT_DIR/$file5 bs=1M count=5 || {
                unmount_and_clean
                return 1
        }

        dd if=$MERO_M0T1FS_MOUNT_DIR/$file5 of=$MERO_M0T1FS_TEST_DIR/file5_copy bs=1M count=5 || {
                umount_and_clean
                return 1
        }

        echo "Mark disk from pool version 1 as failed."
        dispatch_ha_events "[1: ("$disk_from_pver1", "$M0_NC_FAILED")]" "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "Should fail since no pool available now failed."
        touch $MERO_M0T1FS_MOUNT_DIR/$file2 && {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "Mark disk from pool version 0 as REPAIRING"
        dispatch_ha_events "[1: ("$disk_from_pver0", "4")]"  "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "Mark disk from pool version 0 as REPAIRED"
        dispatch_ha_events "[1: ("$disk_from_pver0", "5")]" "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "Mark disk from pool version 0 as REBALANCING"
        dispatch_ha_events "[1: ("$disk_from_pver0", "6")]" "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "Mark disk from pool version 0 as ONLINE"
        dispatch_ha_events "[1: ("$disk_from_pver0", "$M0_NC_ONLINE")]" "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }

        touch $MERO_M0T1FS_MOUNT_DIR/$file6 || {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "Make io on $MERO_M0T1FS_MOUNT_DIR/$file6"
        setfattr -n lid -v 8 $MERO_M0T1FS_MOUNT_DIR/$file6
        dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$file6 bs=1M count=5 || {
                unmount_and_clean $multiple_pools
                return 1
        }

        pver_0=$(getfattr -n pver $MERO_M0T1FS_MOUNT_DIR/$file6 | awk -F '=' 'NF > 1 { print $2 }')
        echo "pool version 0 id:  $pver_0"
        if  [ "$pver_0" == "$pver_1" ]
        then
                echo "error: ids of pool version 0 and pool version 1 match...."
                nmount_and_clean
                return 1
        fi

        rm -vf $MERO_M0T1FS_MOUNT_DIR/$file6 || {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "dgmode read: Read file created on previous pool version"
        diff $MERO_M0T1FS_MOUNT_DIR/$file5 $MERO_M0T1FS_TEST_DIR/file5_copy || {
                unmount_and_clean
                return 1
        }

        echo "dgmode write: Remove file created on previous pool version"
        rm -vf $MERO_M0T1FS_MOUNT_DIR/$file5 || {
                unmount_and_clean
                return 1
        }

        rm -vf $MERO_M0T1FS_TEST_DIR/file5_copy;

        echo "Mark disk from pool version 1 as REPAIRING"
        dispatch_ha_events "[1: ("$disk_from_pver1", "4")]" "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "Mark disk from pool version 1 as REPAIRED"
        dispatch_ha_events "[1: ("$disk_from_pver1", "5")]" "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "Mark disk from pool version 1 as REBALANCING"
        dispatch_ha_events "[1: ("$disk_from_pver1", "6")]" "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }

        echo "Mark disk from pool version 1 as ONLINE"
        dispatch_ha_events "[1: ("$disk_from_pver1", "$M0_NC_ONLINE")]" "${all_eps[*]}" $console_ep || {
                unmount_and_clean $multiple_pools
                return 1
        }

	unmount_and_clean $multiple_pools
	return 0
}

m0t1fs_pool_version_assignment()
{
	NODE_UUID=`uuidgen`

	local unit=16 # Kbytes
	local N=2
	local K=1
	local P=4
	mero_service start "$multiple_pools" $unit $N $K $P
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
		echo "Failed pool_version_assignment system tests: rc=$rc"
		return $rc
	fi
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
report_and_exit pool-version-assignment $?
