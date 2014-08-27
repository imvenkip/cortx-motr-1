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

N=2
K=1
P=4
stride=32

sns_repair_test()
{
	local rc=0
	local fail_device=1
	local unit_size=$((stride * 1024))

	echo "Starting SNS repair testing ..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH "copytool" &>> $MERO_TEST_LOGFILE || {
		cat $MERO_TEST_LOGFILE
		return 1
	}

	_dd $file1 50
	_dd $file2 70
	_dd $file3 30

	_md5sum $file1
	_md5sum $file2
	_md5sum $file3

	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done

	mount

####### Set Failure device
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N 1 \
		 -I $fail_device -s 1
                         -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
	echo $poolmach
	eval $poolmach
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	repair_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 2 -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
	echo $repair_trigger
	eval $repair_trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "SNS Repair failed: $rc"
	else
		echo "SNS Repair done."
		md5sum -c < $MERO_M0T1FS_TEST_DIR/md5
		rc=$?
	fi

####### Query device state
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -N 1 \
		 -I $fail_device
                         -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
	echo $poolmach
	eval $poolmach
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	echo "Starting SNS Re-balance.."
	rebalance_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 4 -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
	echo $rebalance_trigger
	eval $rebalance_trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "SNS Re-balance failed: $rc"
	else
		echo "SNS Re-balance done."
		echo "Verifying checksums.."
		md5sum -c < $MERO_M0T1FS_TEST_DIR/md5
		rc=$?
	fi

	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -N 1 \
		 -I $fail_device
			 -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
	echo $poolmach
	eval $poolmach
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
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
    echo "sns-single: test status: SUCCESS"
fi
