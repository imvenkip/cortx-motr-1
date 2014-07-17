pool_mach_set_failure()
{
	DEVICES=""
	STATE=""
	for i in "$@"
	do
		DEVICES="$DEVICES -I $i"
		STATE="$STATE -s 1"
	done
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N $# \
		 $DEVICES $STATE -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
	echo $poolmach
	if ! $poolmach ; then
		echo "m0poolmach failed"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
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
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -N $# \
		 $DEVICES -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
	echo $poolmach
	if ! $poolmach ; then
		echo "m0poolmach failed"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	fi

	return 0
}

sns_repair()
{
	repair_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 2 -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
	echo $repair_trigger
	if ! $repair_trigger ; then
		echo "SNS Repair failed"
		return 1
	fi

	return 0
}

sns_rebalance()
{
        rebalance_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 4 -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
        echo $rebalance_trigger
        if ! $rebalance_trigger ; then
                echo "SNS Re-balance failed"
                return 1
        fi

	return 0
}

_dd()
{
	local FILE=$1
	local COUNT=$2

	dd if=/dev/urandom bs=$unit_size count=$COUNT \
	   of=$MERO_M0T1FS_MOUNT_DIR/$FILE >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return 1
	}
}

_md5sum()
{
	local FILE=$1

	md5sum $MERO_M0T1FS_MOUNT_DIR/$FILE | \
		tee $MERO_M0T1FS_TEST_DIR/md5
}
