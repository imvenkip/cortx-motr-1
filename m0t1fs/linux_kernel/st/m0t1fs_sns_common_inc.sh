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
	eval $poolmach
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	return 0
}

# XXX MERO-703: Start
# Take out these functions once MERO-699 and MERO-701 are fixed
pool_mach_set_repairing()
{
	DEVICES=""
	STATE=""
	for i in "$@"
	do
		DEVICES="$DEVICES -I $i"
		STATE="$STATE -s 3"
	done
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N $# \
		 $DEVICES $STATE -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
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

pool_mach_set_repaired()
{
	DEVICES=""
	STATE=""
	for i in "$@"
	do
		DEVICES="$DEVICES -I $i"
		STATE="$STATE -s 4"
	done
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N $# \
		 $DEVICES $STATE -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
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

pool_mach_set_rebalancing()
{
	DEVICES=""
	STATE=""
	for i in "$@"
	do
		DEVICES="$DEVICES -I $i"
		STATE="$STATE -s 5"
	done
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N $# \
		 $DEVICES $STATE -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
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

pool_mach_set_rebalanced()
{
	DEVICES=""
	STATE=""
	for i in "$@"
	do
		DEVICES="$DEVICES -I $i"
		STATE="$STATE -s 0"
	done
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Set -T device -N $# \
		 $DEVICES $STATE -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
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
# XXX MERO-703: End

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
	eval $poolmach
	rc=$?
	if [ $rc != 0 ] ; then
		echo "m0poolmach failed: $rc"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
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
		tee -a $MERO_M0T1FS_TEST_DIR/md5
}
