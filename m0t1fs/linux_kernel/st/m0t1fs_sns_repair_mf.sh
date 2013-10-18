#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh


sns_repair_test()
{
	local rc=0
	local fail_device1=1
	local fail_device2=3
	local N=3
	local K=3
	local P=9
	local stride=4
	local unit_size=$((stride * 1024))

	echo "Starting SNS repair testing ..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $stride $N $K $P &>> $MERO_TEST_LOGFILE || {
		cat $MERO_TEST_LOGFILE
		return 1
	}

	dd if=/dev/urandom bs=$unit_size count=9 \
	   of=$MERO_M0T1FS_MOUNT_DIR/file1_to_repair >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}

	dd if=/dev/urandom bs=$unit_size count=9 \
	   of=$MERO_M0T1FS_MOUNT_DIR/file2_to_repair >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}

	dd if=/dev/urandom bs=$unit_size count=9 \
	   of=$MERO_M0T1FS_MOUNT_DIR/file3_to_repair >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}

	for ((i=1; i < ${#EP[*]}; i++)) ; do
		IOSEP="$IOSEP -S ${lnet_nid}:${EP[$i]}"
	done

####### Set Failure device
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -I $fail_device1 -s 1
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $poolmach

	if ! $poolmach ; then
		echo "m0poolmach failed"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	fi

	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -I $fail_device2 -s 1
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $poolmach

	if ! $poolmach ; then
		echo "m0poolmach failed"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	fi

	repair_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 2 -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $repair_trigger

	if ! $repair_trigger ; then
		echo "SNS Repair failed"
		rc=1
	fi

####### Query device state
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -I $fail_device1
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $poolmach

	if ! $poolmach ; then
		echo "m0poolmach failed"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	fi

	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -I $fail_device2
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
	echo $poolmach

	if ! $poolmach ; then
		echo "m0poolmach failed"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	fi

        echo "Starting SNS Re-balance.."
        rebalance_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 4 -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
        echo $rebalance_trigger

        if ! $rebalance_trigger ; then
                echo "SNS Re-balance failed"
                return 1
        fi

        poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -I $fail_device1
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
        echo $poolmach
        if ! $poolmach ; then
                echo "m0poolmach failed"
                unmount_and_clean &>> $MERO_TEST_LOGFILE
                return 1
        fi

        poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -I $fail_device2
                         -C ${lnet_nid}:${SNS_CLI_EP} $IOSEP"
        echo $poolmach
        if ! $poolmach ; then
                echo "m0poolmach failed"
                unmount_and_clean &>> $MERO_TEST_LOGFILE
                return 1
        fi

	echo "unmounting and cleaning.."
	unmount_and_clean &>> $MERO_TEST_LOGFILE

	return $rc
}

main()
{
	NODE_UUID=`uuidgen`
	mero_service start 9
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
