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

file_size=(
	50
	70
	30
	0
	40
	0
	60
	90
	10
	20
	5
	80
)

N=3
K=3
P=15
stride=32

sns_repair_test()
{
	local fail_device1=1
	local fail_device2=9
	local fail_device3=3
	local unit_size=$((stride * 1024))

	echo "Starting SNS repair testing ..."
	for ((i=0; i < ${#files[*]}; i++)) ; do
		touch_file $MERO_M0T1FS_MOUNT_DIR/${files[$i]} $stride
	done

	for ((i=0; i < ${#files[*]}; i++)) ; do
		_dd ${files[$i]} ${file_size[$i]}
	done


	for ((i=0; i < ${#files[*]}; i++)) ; do
		_md5sum ${files[$i]}
	done

	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done

	mount

####### Set Failure device
	pool_mach_set_failure $fail_device1 $fail_device2 || return $?

	echo "Device $fail_device1 and $fail_device2 failed. Do dgmode read"
	md5sum_check || return $?

	pool_mach_query $fail_device1 $fail_device2 || return $?

	sns_repair || return $?

	echo "SNS Repair done."
	md5sum_check || return $?

####### Query device state
	pool_mach_query $fail_device1 $fail_device2 || return $?

        echo "Starting SNS Re-balance.."
	sns_rebalance || return $?

	echo "SNS Rebalance done."
	md5sum_check || return $?

	pool_mach_set_failure $fail_device3 || return $?

	sns_repair || return $?

	echo "SNS Repair done."
	md5sum_check || return $?

	pool_mach_query $fail_device3 || return $?

        echo "Starting SNS Re-balance.."
	sns_rebalance || return $?

	echo "SNS Rebalance done."
	md5sum_check || return $?

	pool_mach_query $fail_device1 $fail_device2 $fail_device3

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

	sns_repair_mount $N $K $P || {
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
    echo "sns-multi: test status: SUCCESS"
fi
