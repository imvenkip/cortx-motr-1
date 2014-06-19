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
file1_to_repair=0:10000
file2_to_repair=0:10001
file3_to_repair=0:10002

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
	dd if=/dev/urandom bs=$unit_size count=50 \
	   of=$MERO_M0T1FS_MOUNT_DIR/$file1_to_repair >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}


	dd if=/dev/urandom bs=$unit_size count=50 \
	   of=$MERO_M0T1FS_MOUNT_DIR/$file2_to_repair >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}

	dd if=/dev/urandom bs=$unit_size count=50 \
	   of=$MERO_M0T1FS_MOUNT_DIR/$file3_to_repair >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}

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
