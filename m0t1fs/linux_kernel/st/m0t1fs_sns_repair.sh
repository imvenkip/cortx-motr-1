#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh


sns_repair_test()
{
	local rc=0
	local fail_device=1
	local stride=20
	local unit_size=$((stride * 1024))

	echo "Starting SNS repair testing ..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $stride &>> $MERO_TEST_LOGFILE || {
		cat $MERO_TEST_LOGFILE
		return 1
	}
	dd if=/dev/zero bs=20k count=500 of=$MERO_M0T1FS_MOUNT_DIR/file1_to_repair >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
	dd if=/dev/zero bs=20k count=500 of=$MERO_M0T1FS_MOUNT_DIR/file2_to_repair >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
	dd if=/dev/zero bs=20k count=500 of=$MERO_M0T1FS_MOUNT_DIR/file3_to_repair >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
	md5sum $MERO_M0T1FS_MOUNT_DIR/file1_to_repair | tee  $MERO_M0T1FS_TEST_DIR/md5
	md5sum $MERO_M0T1FS_MOUNT_DIR/file2_to_repair | tee -a $MERO_M0T1FS_TEST_DIR/md5
	md5sum $MERO_M0T1FS_MOUNT_DIR/file3_to_repair | tee -a $MERO_M0T1FS_TEST_DIR/md5
	trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 2 -U $unit_size -F $fail_device -n 3
			-s 10240000 -s 10240000 -s 10240000 -N $NR_DATA -K $NR_PARITY -P $POOL_WIDTH
                         -S ${lnet_nid}:${EP[0]}
                         -C ${lnet_nid}:${SNS_CLI_EP}"
	echo $trigger

	if ! $trigger ; then
		echo "SNS Repair failed"
		rc=1
	else
		echo "SNS Repair done."
		md5sum -c < $MERO_M0T1FS_TEST_DIR/md5
		rc=$?
	fi

	unmount_and_clean &>> $MERO_TEST_LOGFILE

	return $rc
}

main()
{
	NODE_UUID=`uuidgen`
	mero_service start
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
