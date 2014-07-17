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
file1=0:10000
file2=0:10001
file3=0:10002

N=3
K=3
P=9
stride=32

sns_repair_test()
{
	local rc=0
	local fail_device1=1
	local fail_device2=3
	local fail_device3=9
	local unit_size=$((stride * 1024))

	echo "Starting SNS repair testing ..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $N $K $P "copytool" &>> $MERO_TEST_LOGFILE || {
		cat $MERO_TEST_LOGFILE
		return 1
	}

	for f in $file1 $file2 $file3; do
		touch_file $MERO_M0T1FS_MOUNT_DIR/$f $stride
	done

	_dd $file1 50
	_dd $file2 70
	_dd $file3 30

	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done

	mount

####### Set Failure device
	pool_mach_set_failure $fail_device1
	if [ $? -ne "0" ]
	then
		return $?
	fi

	sns_repair
	if [ $? -ne "0" ]
	then
		return $?
	fi
####### Query device state

	pool_mach_query $fail_device1
	if [ $? -ne "0" ]
	then
		return $?
	fi

	pool_mach_set_failure $fail_device2
	if [ $? -ne "0" ]
	then
		return $?
	fi

	sns_repair
	if [ $? -ne "0" ]
	then
		return $?
	fi
####### Query device state

	pool_mach_query $fail_device2
	if [ $? -ne "0" ]
	then
		return $?
	fi
	pool_mach_set_failure $fail_device3
	if [ $? -ne "0" ]
	then
		return $?
	fi

	sns_repair
	if [ $? -ne "0" ]
	then
		return $?
	fi

	pool_mach_query $fail_device3
	if [ $? -ne "0" ]
	then
		return $?
	fi

        echo "Starting SNS Re-balance.."
	sns_rebalance
	if [ $? -ne "0" ]
	then
		return $?
	fi
	pool_mach_query $fail_device1 $fail_device2 $fail_device3
	if [ $? -ne "0" ]
	then
		return $?
	fi

	echo "unmounting and cleaning.."
	unmount_and_clean &>> $MERO_TEST_LOGFILE

	return $rc
}

main()
{
	NODE_UUID=`uuidgen`
	mero_service start $stride $N $K $P
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	rc=0
	sns_repair_test || {
		echo "Failed: SNS repair failed.."
		rc=1
	}

	mero_service stop
	if [ $? -ne "0" ]
	then
		echo "Failed to stop Mero Service."
		return 1
	fi

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
