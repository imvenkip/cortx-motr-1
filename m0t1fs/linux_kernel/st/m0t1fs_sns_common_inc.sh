declare -A states=(
	[online]=0
	[failed]=1
	[offline]=2
	[repairing]=3
	[repaired]=4
	[rebalancing]=5
)

pool_mach_set_state()
{
	DEVICES=""
	STATE=""
	state_name=$1
	shift

	state=${states[$state_name]}

	echo "setting device { $@ } to $state_name ($state)"

	for i in "$@"
	do
		DEVICES="$DEVICES -I $i"
		STATE="$STATE -s $state"
	done
	poolmach="$M0_SRC_DIR/pool/m0poolmach -O Set -T device -N $# \
		 $DEVICES $STATE -C ${lnet_nid}:${POOL_MACHINE_CLI_EP} $ios_eps"
	echo $poolmach
	eval $poolmach
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	return 0
}

pool_mach_query()
{
	DEVICES=""
	for i in "$@"
	do
		DEVICES="$DEVICES -I $i"
	done
	poolmach="$M0_SRC_DIR/pool/m0poolmach -O Query -T device -N $# \
		 $DEVICES -C ${lnet_nid}:${POOL_MACHINE_CLI_EP} $ios_eps"
	echo $poolmach
	eval $poolmach
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	return 0
}

sns_repair_mount()
{
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR "oostore,verify" || {
		echo "mount failed"
		return 1
	}
}

sns_repair()
{
	local rc=0

	repair_trigger="$M0_SRC_DIR/sns/cm/st/m0repair -O 2 -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
	echo $repair_trigger
	eval $repair_trigger
	rc=$?
	if [ $rc != 0 ]; then
		echo "SNS Repair failed"
	fi

	return $rc
}

sns_rebalance()
{
	local rc=0

        rebalance_trigger="$M0_SRC_DIR/sns/cm/st/m0repair -O 4 -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
        echo $rebalance_trigger
	eval $rebalance_trigger
	rc=$?
        if [ $rc != 0 ] ; then
                echo "SNS Re-balance failed"
        fi

	return $rc
}

sns_repair_quiesce()
{
	local rc=0

	repair_quiesce_trigger="$M0_SRC_DIR/sns/cm/st/m0repair -O 8 -C ${lnet_nid}:${SNS_QUIESCE_CLI_EP} $ios_eps"
	echo $repair_quiesce_trigger
	eval $repair_quiesce_trigger
	rc=$?
	if [ $rc != 0 ]; then
		echo "SNS Repair quiesce failed"
	fi

	return $rc
}

sns_rebalance_quiesce()
{
	local rc=0

	rebalance_quiesce_trigger="$M0_SRC_DIR/sns/cm/st/m0repair -O 16 -C ${lnet_nid}:${SNS_QUIESCE_CLI_EP} $ios_eps"
	echo $rebalance_quiesce_trigger
	eval $rebalance_quiesce_trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "SNS Re-balance quiesce failed"
	fi

	return $rc
}

sns_repair_abort()
{
	local rc=0

	repair_abort_trigger="$M0_SRC_DIR/sns/cm/st/m0repair -O 128 -C ${lnet_nid}:${SNS_QUIESCE_CLI_EP} $ios_eps"
	echo $repair_abort_trigger
	eval $repair_abort_trigger
	rc=$?
	if [ $rc != 0 ]; then
		echo "SNS Repair abort failed"
	fi

	return $rc
}

sns_repair_or_rebalance_status()
{
	local rc=0
	local op=32
	[ "$1" == "repair" ] && op=32
	[ "$1" == "rebalance" ] && op=64

	repair_status="$M0_SRC_DIR/sns/cm/st/m0repair -O $op -C ${lnet_nid}:${SNS_QUIESCE_CLI_EP} $ios_eps"
	echo $repair_status
	eval $repair_status
	rc=$?
	if [ $rc != 0 ]; then
		echo "SNS Repair status query failed"
	fi

	return $rc
}

wait_for_sns_repair_or_rebalance()
{
	local rc=0
	local op=32
	[ "$1" == "repair" ] && op=32
	[ "$1" == "rebalance" ] && op=64
	while true ; do
		sleep 5
		repair_status="$M0_SRC_DIR/sns/cm/st/m0repair -O $op -C ${lnet_nid}:${SNS_QUIESCE_CLI_EP} $ios_eps"
		echo $repair_status
		status=`eval $repair_status`
		rc=$?
		if [ $rc != 0 ]; then
			echo "SNS Repair status query failed"
			return $rc
		fi

		echo $status | grep status=2 && continue #sns repair is active, continue waiting
		break;
	done
	return 0
}

_dd()
{
	local FILE=$1
	local BS=$2
	local COUNT=$3

	dd if=$MERO_M0T1FS_TEST_DIR/srcfile bs=$BS count=$COUNT \
	   of=$MERO_M0T1FS_MOUNT_DIR/$FILE &>> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
}

local_write()
{
	local BS=$1
	local COUNT=$2

	dd if=/dev/urandom bs=$BS count=$COUNT \
		of=$MERO_M0T1FS_TEST_DIR/srcfile &>> $MERO_TEST_LOGFILE || {
			echo "local write failed"
			unmount_and_clean &>> $MERO_TEST_LOGFILE
			return 1
	}
}

local_read()
{
	local BS=$1
	local COUNT=$2

	dd if=$MERO_M0T1FS_TEST_DIR/srcfile of=$MERO_M0T1FS_TEST_DIR/file-$BS-$COUNT \
		bs=$BS count=$COUNT &>> $MERO_TEST_LOGFILE || {
                        echo "local read failed"
                        unmount_and_clean &>> $MERO_TEST_LOGFILE
                        return 1
        }
}

read_and_verify()
{
	local FILE=$1
	local BS=$2
	local COUNT=$3

	dd if=$MERO_M0T1FS_MOUNT_DIR/$FILE of=$MERO_M0T1FS_TEST_DIR/$FILE \
		bs=$BS count=$COUNT &>> $MERO_TEST_LOGFILE || {
                        echo "m0t1fs read failed"
                        unmount_and_clean &>> $MERO_TEST_LOGFILE
                        return 1
        }

	diff $MERO_M0T1FS_TEST_DIR/file-$BS-$COUNT $MERO_M0T1FS_TEST_DIR/$FILE &>> $MERO_TEST_LOGFILE || {
		echo "files differ"
		unmount_and_clean &>>$MERO_TEST_LOGFILE
		return 1
	}
	rm -f $FILE
}

_md5sum()
{
	local FILE=$1

	md5sum $MERO_M0T1FS_MOUNT_DIR/$FILE | \
		tee -a $MERO_M0T1FS_TEST_DIR/md5
}

md5sum_check()
{
	local rc

	md5sum -c < $MERO_M0T1FS_TEST_DIR/md5
	rc=$?
	if [ $rc != 0 ] ; then
		echo "md5 sum does not match: $rc"
	fi
	return $rc
}
