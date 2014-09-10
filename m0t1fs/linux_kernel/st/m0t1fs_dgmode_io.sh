#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_sns_common_inc.sh

file_to_compare="file_to_compare"
file_in_dgmode1="file_in_dgmode1"
file_in_dgmode2="file_in_dgmode2"
file_in_dgmode3="file_in_dgmode3"

N=3
K=3
P=15
stride=32

dgio_test_module()
{
	$@ \
	   if=./sandbox/source of=./sandbox/file_to_compare >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
	$@ \
	  if=./sandbox/source  of=$MERO_M0T1FS_MOUNT_DIR/$file_to_compare >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/$file_to_compare
	return $?
}

dgio_test()
{
	local rc=0
	local fail_device1=1
	local fail_device2=9
	local fail_device3=2
	local unit_size=$((stride * 1024))

	ios_eps=""
	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done

	echo "Starting dgmode testing ..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $N $K $P   &>> $MERO_TEST_LOGFILE || {
		cat $MERO_TEST_LOGFILE
		return 1
	}
	mount

	rm -rf ./sandbox
	mkdir sandbox
	dd if=/dev/urandom bs=$unit_size count=50 of=./sandbox/source 2>&1 >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}

	echo "Creating files"
	dgio_test_module dd bs=$unit_size count=50
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi


	echo "Sending device failure"
	pool_mach_set_failure $fail_device1
	rc=$?
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "Create a file after first failure"
	touch $MERO_M0T1FS_MOUNT_DIR/$file_in_dgmode1
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "dgmode test1: IO after first failure"
	dgio_test_module dd bs=8821 count=1 seek=23 conv=notrunc
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "dgmode test 2: Read after first failure"
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/$file_to_compare
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi
	pool_mach_query $fail_device1
	rc=$?
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi


	echo "Sending device2 failure"
	pool_mach_set_failure $fail_device2
	rc=$?
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "Create a file after second failure"
	touch $MERO_M0T1FS_MOUNT_DIR/$file_in_dgmode2
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "dgmode test 3: Read after second failure"
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/$file_to_compare
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "dgmode test 4: IO after second failure"
	dgio_test_module dd bs=$unit_size count=60
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	pool_mach_query $fail_device2
	rc=$?
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "dgmode test 5: Read after second failure"
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/$file_to_compare
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "dgmode test 6: IO after second failure"
	dgio_test_module dd bs=$unit_size count=40
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi


	echo "Sending device3 failure"
	pool_mach_set_failure $fail_device3
	rc=$?
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "Create a file after third failure"
	touch $MERO_M0T1FS_MOUNT_DIR/$file_in_dgmode3
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "dgmode test 7: Read after third failure"
	diff ./sandbox/file_to_compare $MERO_M0T1FS_MOUNT_DIR/$file_to_compare
	rc=$?
	echo $rc
	if [ $rc -ne "0" ]
	then
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "dgmode test 8: IO after third failure"
	dgio_test_module dd bs=$unit_size count=10
	rc=$?
	echo $rc


	echo "unmounting and cleaning.."
	unmount_and_clean &>> $MERO_TEST_LOGFILE

	return $rc
}

main()
{
	NODE_UUID=`uuidgen`
	echo "About to start Mero service"
	mero_service start $stride $N $K $P
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	rc=0
	echo "mero service started"
	dgio_test || {
		echo "Failed: dgmode failed.."
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
    echo "degraded-mode: test status: SUCCESS"
fi
