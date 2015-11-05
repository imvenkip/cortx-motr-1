#!/bin/sh

#set -x

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
file=(
	0:10000
	0:10001
	0:10002
)

file_size=(
	500
	700
	300
)

N=3
K=3
P=15
stride=32
src_bs=10M
src_count=20

sns_repair_rebalance_quiesce_test()
{
	local rc=0
	local fail_device1=1
	local fail_device9=9
	local unit_size=$((stride * 1024))

	echo "Starting SNS repair/rebalance quiesce testing ..."

	local_write $src_bs $src_count || return $?

	for ((i=0; i < ${#file[*]}; i++)) ; do
		_dd ${file[$i]} $unit_size ${file_size[$i]} || return $?
		_md5sum ${file[$i]} || return $?
	done

	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done

	echo "****** Start failure after quiesce test ******"
	pool_mach_set_failure $fail_device1 || return $?

	echo "Device $fail_device1 failed. Do dgmode read"
	md5sum_check || return $?

	echo "SNS repair, and this will be quiesecd"
	sns_repair
	sleep 5

	# sending out QUIESCE cmd
	sns_repair_quiesce

	echo "wait for sns repair"
	wait_for_sns_repair_or_rebalance "repair" || return $?

	echo "Query device state"
	pool_mach_query $fail_device1 || return $?

	pool_mach_set_failure $fail_device9 || return $?

	echo "Query device state"
	pool_mach_query $fail_device1 $fail_device9 || return $?

	echo "Continue SNS repair..."
	sns_repair || return $?

	echo "wait for sns repair"
	wait_for_sns_repair_or_rebalance "repair" || return $?

	echo "SNS Repair done."
	md5sum_check || return $?

	echo "Query device state"
	pool_mach_query $fail_device1 $fail_device9 || return $?

	echo "Starting SNS Re-balance, and this will be quiesced"
	sns_rebalance
	sleep 5

	# sending out QUIESCE cmd
	sns_rebalance_quiesce

	echo "wait for sns rebalance"
	wait_for_sns_repair_or_rebalance "rebalance" || return $?

	echo "Continue SNS Re-balance..."
	sns_rebalance || return $?

	echo "wait for sns rebalance"
	wait_for_sns_repair_or_rebalance "rebalance" || return $?

	echo "SNS Re-balance done."
####### Query device state
	pool_mach_query $fail_device1 $fail_device9 || return $?

	echo "Verifying checksums.."
	md5sum_check || return $?

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

	sns_repair_mount $NR_DATA $NR_PARITY $POOL_WIDTH || {
		rc=$?
	}

	if [[ $rc -eq 0 ]] && ! sns_repair_rebalance_quiesce_test ; then
		echo "Failed: SNS repair/rebalance quiesce failed.."
		rc=1
	fi

	echo "unmounting and cleaning.."
	unmount_and_clean &>> $MERO_TEST_LOGFILE

	mero_service stop || {
		echo "Failed to stop Mero Service."
		rc=1
	}

	echo "Test log available at $MERO_TEST_LOGFILE."
	[ $rc -ne 0 ] || sandbox_fini
	return $rc
}

trap unprepare EXIT
main

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
if [ $? -eq 0 ] ; then
    echo "sns-repair-rebalance-quiesce: test status: SUCCESS"
fi
