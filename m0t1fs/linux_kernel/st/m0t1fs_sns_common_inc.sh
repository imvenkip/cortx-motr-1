M0_NC_UNKNOWN=1
M0_NC_ONLINE=1
M0_NC_FAILED=2
M0_NC_TRANSIENT=3
M0_NC_REPAIR=4
M0_NC_REPAIRED=5
M0_NC_REBALANCE=6

declare -A ha_states=(
	[unknown]=$M0_NC_UNKNOWN
	[online]=$M0_NC_ONLINE
	[failed]=$M0_NC_FAILED
	[offline]=$M0_NC_TRANSIENT
	[repairing]=$M0_NC_REPAIR
	[repaired]=$M0_NC_REPAIRED
	[rebalancing]=$M0_NC_REBALANCE
)

disk_state_set()
{
	local lnet_nid=`sudo lctl list_nids | head -1`
	local s_endpoint="$lnet_nid:12345:33:1"
	local c_endpoint="$lnet_nid:12345:30:*"
	local state_name=$1
	shift
	local state=${ha_states[$state_name]}

	local service_eps=(
		"$lnet_nid:${IOSEP[0]}"
		"$lnet_nid:${IOSEP[1]}"
		"$lnet_nid:${IOSEP[2]}"
		"$lnet_nid:${IOSEP[3]}"
		"$lnet_nid:${HA_EP}"
	)

	local nr=0
	local DS=""

	echo "setting device { $@ } to $state_name (HA state=$state)"

	for d in "$@"
	do
		if [ $nr -eq 0 ] ; then
			DS="(^k|1:$d, $state)"
		else
			DS="$DS, (^k|1:$d, $state)"
		fi
		nr=$((nr + 1))
	done

	for sep in "${service_eps[@]}"; do
		ha_note="$M0_SRC_DIR/console/bin/m0console     \
			-s $sep                                \
			-c $c_endpoint                         \
			-f $(opcode M0_HA_NOTE_SET_OPCODE)     \
			-v                                     \
			-d '[$nr: $DS]'"
		echo $ha_note
		eval $ha_note
	done

	rc=$?
	if [ $rc != 0 ] ; then
		echo "HA note set failed: $rc"
		unmount_and_clean &>> $MERO_TEST_LOGFILE
		return $rc
	fi

	return 0
}

disk_state_get()
{
	local lnet_nid=`sudo lctl list_nids | head -1`
	local s_endpoint="$lnet_nid:12345:33:1"
	local c_endpoint="$lnet_nid:12345:30:*"

	local service_eps=(
		"$lnet_nid:${IOSEP[0]}"
		"$lnet_nid:${IOSEP[1]}"
		"$lnet_nid:${IOSEP[2]}"
		"$lnet_nid:${IOSEP[3]}"
		"$lnet_nid:${HA_EP}"
	)

	local nr=0
	local DS=""

	echo "getting device { $@ }'s HA state"

	for d in "$@"
	do
		if [ $nr -eq 0 ] ; then
			DS="(^k|1:$d, 0)"
		else
			DS="$DS, (^k|1:$d, 0)"
		fi
		nr=$((nr + 1))
	done

	for sep in "${service_eps[@]}"; do
		ha_note="$M0_SRC_DIR/console/bin/m0console     \
			-s $sep                                \
			-c $c_endpoint                         \
			-f $(opcode M0_HA_NOTE_GET_OPCODE)     \
			-v                                     \
			-d '[$nr: $DS]'"
		echo $ha_note
		eval $ha_note
	done

	rc=$?
	if [ $rc != 0 ] ; then
		echo "HA note get failed: $rc"
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
