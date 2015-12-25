#!/bin/bash

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
	50
	70
	30
	0
	40
	0
	60
	20
	10
	6
	6
	6
)


N=3
K=3
P=15
stride=32
src_bs=10M
src_count=2

verify()
{
	for ((i=0; i < ${#files[*]}; i++)) ; do
		local_read $((${unit_size[$i]} * 1024)) ${file_size[$i]} || return $?
		read_and_verify ${files[$i]} $((${unit_size[$i]} * 1024)) ${file_size[$i]} || return $?
	done

	echo "file verification sucess"
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

####### Set Failure device
	pool_mach_set_state "failed" $fail_device1 $fail_device2 || return $?

	pool_mach_query $fail_device1 $fail_device2 || return $?

	echo "Device $fail_device1 and $fail_device2 failed. Do dgmode read"
	verify || return $?

	pool_mach_set_state "repairing" $fail_device1 $fail_device2 || return $?
	sns_repair || return $?

	echo "wait for sns repair"
	wait_for_sns_repair_or_rebalance "repair" || return $?

	echo "query sns repair status"
	sns_repair_or_rebalance_status "repair" || return $?

	pool_mach_set_state "repaired" $fail_device1 $fail_device2 || return $?
	echo "SNS Repair done."

####### Query device state
	pool_mach_query $fail_device1 $fail_device2 || return $?

	verify || return $?

        echo "Starting SNS Re-balance.."
	pool_mach_set_state "rebalancing" $fail_device1 $fail_device2 || return $?
	sns_rebalance || return $?

	echo "wait for sns rebalance"
	wait_for_sns_repair_or_rebalance "rebalance" || return $?

	echo "query sns repair status"
	sns_repair_or_rebalance_status "rebalance" || return $?
	pool_mach_set_state "online" $fail_device1 $fail_device2 || return $?

	echo "SNS Rebalance done."
	pool_mach_query $fail_device1 $fail_device2 || return $?

	verify || return $?

	pool_mach_set_state "failed" $fail_device3 || return $?

	pool_mach_set_state "repairing" $fail_device3 || return $?
	sns_repair || return $?

	echo "wait for sns repair"
	wait_for_sns_repair_or_rebalance "repair" || return $?

	echo "query sns repair status"
	sns_repair_or_rebalance_status "repair" || return $?
	pool_mach_set_state "repaired" $fail_device3 || return $?

	pool_mach_query $fail_device3 || return $?

	echo "SNS Repair done."
	verify || return $?

        echo "Starting SNS Re-balance.."
	pool_mach_set_state "rebalancing" $fail_device3 || return $?
	sns_rebalance || return $?

	echo "wait for sns rebalance"
	wait_for_sns_repair_or_rebalance "rebalance" || return $?

	echo "query sns repair status"
	sns_repair_or_rebalance_status "rebalance" || return $?
	pool_mach_set_state "online" $fail_device3 || return $?

	echo "SNS Rebalance done."
	pool_mach_query $fail_device1 $fail_device2 $fail_device3

	verify || return $?

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

	sns_repair_mount $N $K $P || rc=$?

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

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
if [ $? -eq 0 ] ; then
    echo "sns-multi: test status: SUCCESS"
fi
