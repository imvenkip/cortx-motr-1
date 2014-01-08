#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_sns_common_inc.sh


dgio_test_module()
{
	$@ \
	   if=./sandbox/source of=./sandbox/file_to_compare >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
	$@ \
	  if=./sandbox/source  of=$MERO_M0T1FS_MOUNT_DIR/file_to_compare >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/file_to_compare
	echo $?
}

dgio_test()
{
	local rc=0
	local fail_device1=1
	local fail_device2=9
	local fail_device3=2
	local N=3
	local K=3
	local P=9
	local stride=32
	local unit_size=$((stride * 1024))

	echo "Starting SNS repair testing ..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $stride $N $K $P &>> $MERO_TEST_LOGFILE || {
		cat $MERO_TEST_LOGFILE
		return 1
	}
	rm -rf ./sandbox
	mkdir sandbox
	dd if=/dev/urandom bs=$unit_size count=50 \
	of=./sandbox/source >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
	echo "Creating files"
	dgio_test_module dd bs=$unit_size count=50

	for ((i=1; i < ${#EP[*]}; i++)) ; do
		IOSEP="$IOSEP -S ${lnet_nid}:${EP[$i]}"
	done

	pool_mach_set_failure $fail_device1
	if [ $? -ne "0" ]
	then
		return $?
	fi
	echo "dgmode test1: IO after first failure"
	dgio_test_module dd bs=$unit_size count=20 conv=notrunc
	sns_repair
	if [ $? -ne "0" ]
	then
		return $?
	fi
	echo "dgmode test 2: Read after repair"
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/file_to_compare
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
	echo "dgmode test 3: Read after second failure"
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/file_to_compare
	echo $?

	echo "dgmode test 4: IO after second failure"
	dgio_test_module dd bs=$unit_size count=60
	sns_repair
	if [ $? -ne "0" ]
	then
		return $?
	fi

	pool_mach_query $fail_device2
	if [ $? -ne "0" ]
	then
		return $?
	fi

	echo "dgmode test 5: Read after second repair"
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/file_to_compare
	echo $?

	echo "dgmode test 6: IO after second repair"
	dgio_test_module dd bs=$unit_size count=40

	pool_mach_set_failure $fail_device3
	if [ $? -ne "0" ]
	then
		return $?
	fi
	echo "dgmode test 7: Read after third failure"
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/file_to_compare
	echo $?

	echo "dgmode test 8: IO after third failure"
	dgio_test_module dd bs=$unit_size count=10

	echo "unmounting and cleaning.."
	unmount_and_clean &>> $MERO_TEST_LOGFILE

	return $rc
}

main()
{
	NODE_UUID=`uuidgen`
	echo "About to start Mero service"
	mero_service start 9
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	rc=0
	echo "mero service started"
	dgio_test || {
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
