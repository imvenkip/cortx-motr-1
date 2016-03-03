#!/usr/bin/env bash
M0_SRC_DIR=`readlink -f $0`
M0_SRC_DIR=${M0_SRC_DIR%/*/*/*}

. $M0_SRC_DIR/spiel/st/m0t1fs_spiel_sns_common_inc.sh


spiel_sns_repair_test()
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

	mount | grep m0t1fs

	#######################################################################
	#  Now starting SPIEL sns repair/rebalance abort/continue testing     #
	#######################################################################

	echo "Set Failure device: $fail_device1 $fail_device2"
	disk_state_set "failed" $fail_device1 $fail_device2 || return $?

	echo "Device $fail_device1 and $fail_device2 failed. Do dgmode read"
	verify || return $?

	disk_state_get $fail_device1 $fail_device2 || return $?

	echo "Start SNS repair"
	disk_state_set "repairing" $fail_device1 $fail_device2 || return $?
	spiel_sns_repair_start
	sleep 2

	echo "Abort SNS repair"
	spiel_sns_repair_abort

	echo "wait for sns repair"
	spiel_wait_for_sns_repair || return $?

	verify || return $?

	echo "start SNS repair again ..."
	spiel_sns_repair_start
	sleep 3

	echo "wait for the second sns repair"
	spiel_wait_for_sns_repair || return $?

	disk_state_set "repaired" $fail_device1 $fail_device2 || return $?
	echo "SNS Repair done."
	verify || return $?

	disk_state_get $fail_device1 $fail_device2 || return $?

	disk_state_set "rebalancing" $fail_device1 $fail_device2 || return $?
	echo "Starting SNS Re-balance.."
	spiel_sns_rebalance_start
	sleep 2

	echo "wait for sns rebalance"
	spiel_wait_for_sns_rebalance || return $?
	disk_state_set "online" $fail_device1 $fail_device2 || return $?
	echo "SNS Rebalance done."

	verify || return $?

	disk_state_get $fail_device1 $fail_device2 || return $?

	#######################################################################
	#  End                                                                #
	#######################################################################

	return 0
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

	spiel_prepare

	if [[ $rc -eq 0 ]] && ! spiel_sns_repair_test ; then
		echo "Failed: SNS repair failed.."
		rc=1
	fi

	spiel_cleanup

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
report_and_exit spiel-sns-repair $?
