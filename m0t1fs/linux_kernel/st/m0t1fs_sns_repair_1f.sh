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
	50
	70
	30
)

N=2
K=1
P=4
stride=32
src_bs=10M
src_count=2

sns_repair_test()
{
	local rc=0
	local fail_device=1
	local unit_size=$((stride * 1024))

	echo "Starting SNS repair testing ..."

	local_write $src_bs $src_count || return $?

	for ((i=0; i < ${#file[*]}; i++)) ; do
		_dd ${file[$i]} $unit_size ${file_size[$i]} || return $?
		_md5sum ${file[$i]} || return $?
	done

	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done

####### Set Failure device
	pool_mach_set_failure $fail_device || return $?

	echo "Device $fail_device failed. Do dgmode read"
	md5sum_check || return $?

	sns_repair || return $?

	echo "SNS Repair done."
	md5sum_check || return $?

####### Query device state
	pool_mach_query $fail_device || return $?

	echo "Starting SNS Re-balance.."
	sns_rebalance || return $?

	echo "SNS Re-balance done."
	echo "Verifying checksums.."
	md5sum_check || return $?

	pool_mach_query $fail_device

	return $?
}

main()
{
	local rc=0

	NODE_UUID=`uuidgen`
	mero_service start $stride $N $K $P || {
		echo "Failed to start Mero Service."
		return 1
	}

	sns_repair_mount $NR_DATA $NR_PARITY $POOL_WIDTH || {
		rc=$?
	}

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

	echo "Test log available at $MERO_TEST_LOGFILE."

	return $rc
}

trap unprepare EXIT

main

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
if [ $? -eq 0 ] ; then
    echo "sns-single: test status: SUCCESS"
fi
