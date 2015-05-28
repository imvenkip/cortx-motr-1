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
# XXX MERO-703: End

pool_mach_query()
{
	DEVICES=""
	for i in "$@"
	do
		DEVICES="$DEVICES -I $i"
	done
	poolmach="$MERO_CORE_ROOT/pool/m0poolmach -O Query -T device -N $# \
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
	N=$1
	K=$2
	P=$3

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $N $K $P "oostore,verify" &>> $MERO_TEST_LOGFILE || {
		cat $MERO_TEST_LOGFILE
		echo "mount failed"
		return 1
	}
	mount
}

sns_repair()
{
	local rc=0

	repair_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 2 -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
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

        rebalance_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 4 -C ${lnet_nid}:${SNS_CLI_EP} $ios_eps"
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

	repair_quiesce_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 8 -C ${lnet_nid}:${SNS_QUIESCE_CLI_EP} $ios_eps"
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

	rebalance_quiesce_trigger="$MERO_CORE_ROOT/sns/cm/st/m0repair -O 16 -C ${lnet_nid}:${SNS_QUIESCE_CLI_EP} $ios_eps"
	echo $rebalance_quiesce_trigger
	eval $rebalance_quiesce_trigger
	rc=$?
	if [ $rc != 0 ] ; then
		echo "SNS Re-balance quiesce failed"
	fi

	return $rc
}

wait4snsrepair()
{
	echo "**** Wait for sns repair to complete ****"
	while [ "`ps ax | grep -v grep | grep m0repair`" ];
		do echo -n .; sleep 5; done
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
