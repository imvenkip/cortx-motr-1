#!/usr/bin/env bash

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_sns_common_inc.sh

###################################################
# SNS repair is only supported in COPYTOOL mode,
# because ios need to hash gfid to mds. In COPYTOOL
# mode, filename is the string format of gfid.
###################################################
files=(
	0:10000
	0:10001
	0:10002
	0:10003
	0:10004
	0:10005
	0:10006
	0:10007
	0:10008
	0:10009
	0:10010
	0:10011
)

unit_size=(
	4
	8
	16
	32
	64
	128
	256
	512
	1024
	2048
	2048
	2048
)

file_size=(
	500
	700
	300
	0
	400
	0
	600
	200
	100
	60
	60
	60
)


N=3
K=3
P=15
stride=32
src_bs=10M
src_count=2

verify()
{
	echo "verifying ..."
	for ((i=0; i < ${#files[*]}; i++)) ; do
		local_read $((${unit_size[$i]} * 1024)) ${file_size[$i]} || return $?
		read_and_verify ${files[$i]} $((${unit_size[$i]} * 1024)) ${file_size[$i]} || return $?
	done

	echo "file verification sucess"
}

change_target_state()
{
	local dev_fid=$1
	local dev_state=$2
        local eplist=($3)
        local console_ep=$4
	local ha_fop="[1: ($dev_fid, $dev_state)]"

	# Generate HA event
	echo dispatch_ha_events "$ha_fop" "${eplist[*]}" "$console_ep"
	dispatch_ha_events "$ha_fop" "${eplist[*]}" "$console_ep" || {
		echo "HA Dispatch failed with: $?"
		return 1
	}
}

ha_notify_ios4_failure_or_online()
{
	# m0t1fs rpc end point, multiple mount on same
	# node have different endpoint
	local ctrl_fid="^c|1:8"
	local process_fid="^r|1:3"
	local ios4_fid="^s|1:3"
        local lnet_nid=`sudo lctl list_nids | head -1`
        local console_ep="$lnet_nid:$POOL_MACHINE_CLI_EP"
	local client_endpoint="$lnet_nid:12345:33:1"
	local eplist=()
	local failure_or_online=$1

	for (( i=0; i < ${#IOSEP[*]}; i++)) ; do
		eplist[$i]="$lnet_nid:${IOSEP[$i]}"
	done

	eplist=("${eplist[@]}" "$lnet_nid:${HA_EP}" "$client_endpoint")

	### It is expected that on failure/online HA dispatch events of some components,
	### HA dispatch events to it's descendants.
	echo "*** Mark process(m0d) on ios4 as: $failure_or_online"
	change_target_state "$process_fid" "$failure_or_online" "${eplist[*]}" "$console_ep"
	echo "*** Mark ios4 as: $failure_or_online"
	change_target_state "$ios4_fid" "$failure_or_online" "${eplist[*]}" "$console_ep"
}

kill_ios4_ioservice()
{
	echo "finding ios4 ..."
	echo pgrep -fn ${prog_exec}.+${IOSEP[3]}
	ios4_pid=`pgrep -fn ${prog_exec}.+${IOSEP[3]}`
	echo === pid of ios4: $ios4_pid ===
	kill -KILL $ios4_pid >/dev/null 2>&1
	echo "finding ios4 again..."
	ps ax | grep ${prog_exec} | grep ${IOSEP[3]}
}

start_ios4_ioservice()
{
	echo === cmd of starting ios4: $IOS4_CMD ===
	(eval $IOS4_CMD) &
}

sns_repair_test()
{
	local fail_device1=1
	local fail_device2=9
	local fail_device3=3

	local_write $src_bs $src_count || return $?

	echo "Starting SNS repair testing ..."
	for ((i=0; i < ${#files[*]}; i++)) ; do
		touch_file $MERO_M0T1FS_MOUNT_DIR/${files[$i]} ${unit_size[$i]}
		_dd ${files[$i]} $((${unit_size[$i]} * 1024)) ${file_size[$i]}
	done

	verify || return $?

	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done

	mount

	########################################################################
	# Start SNS repair/abort test while ios fails                          #
	########################################################################

	echo "Set Failure device: $fail_device1 $fail_device2"
	disk_state_set "failed" $fail_device1 $fail_device2 || return $?

	echo "Device $fail_device1 and $fail_device2 failed. Do dgmode read"
	verify || return $?

	disk_state_get $fail_device1 $fail_device2 || return $?

	echo "Start SNS repair"
	disk_state_set "repairing" $fail_device1 $fail_device2 || return $?
	sns_repair || return $?
	sleep 3

	echo "Abort SNS repair"
	sns_repair_abort

	echo "wait for sns repair"
	wait_for_sns_repair_or_rebalance "repair" || return $?
	echo "SNS repair aborted"

	echo "query sns repair status"
	sns_repair_or_rebalance_status "repair" || return $?

	echo "SNS Repair aborted."
	verify || return $?

	echo "Query device state:$fail_device1 $fail_device2"
	disk_state_get $fail_device1 $fail_device2 || return $?

	echo "Start SNS repair again ..."
	sns_repair || return $?
	sleep 2

	echo "killing ios4 ..."
	kill_ios4_ioservice
	sleep 2

	echo "ios4 failed, we have to abort SNS repair first"
	sns_repair_abort
	sleep 2

	echo "HA notifies that ios4 failed."
	ha_notify_ios4_failure_or_online $M0_NC_FAILED

	wait_for_sns_repair_or_rebalance_not_4 "repair" || return $?

	echo "query sns repair status"
	sns_repair_or_rebalance_status_not_4 "repair" || return $?

	disk_state_get $fail_device1 $fail_device2 || return $?

	echo "================================================================="
	echo "start over the failed ios4"
	start_ios4_ioservice
	sleep 5

	echo "HA notifies that ios4 online."
	ha_notify_ios4_failure_or_online $M0_NC_ONLINE

# TODO: the following sns repair seems to stuck.
#	echo "Start SNS repair again ..."
#	sns_repair || return $?

#	echo "wait for sns repair"
#	wait_for_sns_repair_or_rebalance "repair" || return $?

#	echo "query sns repair status"
#	sns_repair_or_rebalance_status "repair" || return $?

#	disk_state_set "repaired" $fail_device1 $fail_device2 || return $?
#	echo "SNS Repair done."
#	verify || return $?

#	disk_state_get $fail_device1 $fail_device2 || return $?

	return $?
}

main()
{
	local rc=0

	sandbox_init

	NODE_UUID=`uuidgen`
	local multiple_pools=0
	mero_service start $multiple_pools $stride $N $K $P || {
		echo "Failed to start Mero Service."
		return 1
	}

	sns_repair_mount || rc=$?

	if [[ $rc -eq 0 ]] && ! sns_repair_test ; then
		echo "Failed: SNS repair failed.."
		rc=1
	fi

	echo "unmounting and cleaning.."
	unmount_and_clean &>> $MERO_TEST_LOGFILE

	mero_service stop || {
		echo "Failed to stop Mero Service."
		rc=1
	}

	if [ $rc -eq 0 ]; then
		sandbox_fini
	else
		echo "Test log available at $MERO_TEST_LOGFILE."
	fi
	return $rc
}

trap unprepare EXIT
main
report_and_exit sns-repair-abort $?