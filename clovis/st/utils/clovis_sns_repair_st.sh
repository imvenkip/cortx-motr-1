#!/bin/bash

#set -x

clovis_st_util_dir=$( cd "$(dirname "$0")" ; pwd -P )
m0t1fs_dir="$clovis_st_util_dir/../../../m0t1fs/linux_kernel/st"

# Re-use as many m0t1fs system scripts as possible
. $m0t1fs_dir/common.sh
. $m0t1fs_dir/m0t1fs_common_inc.sh
. $m0t1fs_dir/m0t1fs_client_inc.sh
. $m0t1fs_dir/m0t1fs_server_inc.sh
. $m0t1fs_dir/m0t1fs_sns_common_inc.sh

. $clovis_st_util_dir/clovis_local_conf.sh
. $clovis_st_util_dir/clovis_st_inc.sh

N=3
K=2
P=14
stride=4
BLOCKSIZE=""
BLOCKCOUNT=""
OBJ_ID1="1048577"
OBJ_ID2="1048578"
# The second half is hex representations of OBJ_ID1 and OBJ_ID2.
OBJ_HID1="0:100001"
OBJ_HID2="0:100002"
PVER_1="7600000000000001:a"
PVER_2="7680000000000000:4"
clovis_pids=""
export cnt=1
# Read/Write an object via Clovis
io_conduct()
{
	operation=$1
	source=$2
	dest=$3

	local cmd_exec
	if [ $operation == "READ" ]
	then
		cmd_args="$CLOVIS_LOCAL_EP $CLOVIS_HA_EP '$CLOVIS_PROF_OPT' '$CLOVIS_PROC_FID' "true" $source"
		cmd_exec="${clovis_st_util_dir}/c0cat"
		cmd_args="$cmd_args $BLOCKSIZE $BLOCKCOUNT"
		local cmd="$cmd_exec $cmd_args > $dest &"
	else
		cmd_args="$CLOVIS_LOCAL_EP $CLOVIS_HA_EP '$CLOVIS_PROF_OPT' '$CLOVIS_PROC_FID' "false" $dest $source"
		cmd_exec="${clovis_st_util_dir}/c0cp"
		cmd_args="$cmd_args $BLOCKSIZE $BLOCKCOUNT"
		local cmd="$cmd_exec $cmd_args &"
	fi
	cwd=`pwd`
	cd $CLOVIS_TRACE_DIR

	eval $cmd
	clovis_pids[$cnt]=$!
	wait ${clovis_pids[$cnt]}
	if [ $? -ne 0 ]
	then
		echo "  Failed to run command $cmd_exec"
		cd $cwd
		return 1
	fi
	cnt=`expr $cnt + 1`
	cd $cwd
	return 0
}

CLOVIS_TEST_DIR=$SANDBOX_DIR
CLOVIS_TEST_LOGFILE=$SANDBOX_DIR/clovis_`date +"%Y-%m-%d_%T"`.log
CLOVIS_TRACE_DIR=$SANDBOX_DIR/clovis

mero_service_start()
{
	local multiple_pools=0
	mero_service start $multiple_pools $stride $N $K $P
	if [ $? -ne 0 ]
	then
		echo "Failed to start Mero Service..."
		return 1
	fi
	echo "mero service started"

	ios_eps=""
	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
	done
	return 0
}

mero_service_stop()
{
	mero_service stop
	if [ $? -ne 0 ]
	then
		echo "Failed to stop Mero Service..."
		return 1
	fi
}

error_handling()
{
	unmount_and_clean &>>$MERO_TEST_LOGFILE
	mero_service_stop
	echo "Test log file available at $CLOVIS_TEST_LOGFILE"
	echo "Clovis trace files are available at: $CLOVIS_TRACE_DIR"
	exit $1
}

main()
{

	sandbox_init

	NODE_UUID=`uuidgen`
	clovis_dgmode_sandbox="$CLOVIS_TEST_DIR/sandbox"
	src_file="$CLOVIS_TEST_DIR/clovis_source"
	dest_file="$CLOVIS_TEST_DIR/clovis_dest"
	rc=0

	dd if=/dev/urandom bs=4K count=100 of=$src_file 2> $CLOVIS_TEST_LOGFILE || {
		echo "Failed to create a source file"
		unmount_and_clean &>>$MERO_TEST_LOGFILE
		mero_service_stop
		return 1
	}
	mkdir $CLOVIS_TRACE_DIR

	mero_service_start
	#Initialise dix
	dix_init

	BLOCKSIZE="4096"
	BLOCKCOUNT="100"

	# write an object
	io_conduct "WRITE" $src_file $OBJ_ID1
	if [ $rc -ne "0" ]
	then
		echo "Healthy mode, write failed."
		error_handling $rc
	fi

	# read the written object
	io_conduct "READ" $OBJ_ID1  $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Healthy mode, read failed."
		error_handling $rc
	fi
	diff $src_file $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Healthy mode, read file differs."
		error_handling $rc
	fi
	echo "Clovis: Healthy mode IO succeeds."
	echo "Fail a disk"
	fail_device1=1
	fail_device2=9

	# fail a disk and read an object
	clovis_st_disk_state_set "failed" $fail_device1 $fail_device2 || {
		echo "Operation to mark device failure failed."
		error_handling 1
	}

	# Test degraded read
	rm -f $dest_file
	io_conduct "READ" $OBJ_ID1 $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Degraded read failed."
		error_handling $rc
	fi
	diff $src_file $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Obj read in degraded mode differs."
		error_handling $rc
	fi
	rm -f $dest_file

	clovis_st_disk_state_set "repair" $fail_device1 $fail_device2 || {
		echo "Operation to mark device repair failed."
		error_handling 1
	}

	sns_repair || {
		echo "Operation to start sns repair failed."
		error_handling 1
	}

	echo "Start concurrent io during sns repair"
	io_conduct "READ" $OBJ_ID1  $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "concurrent read during sns repair failed."
		error_handling $rc
	fi

	echo "Concurrent io during sns repair successful"

	echo "wait for sns repair"
	wait_for_sns_repair_or_rebalance "repair" || {
		echo "Failure occurred while waiting for sns repair to complete."
		error_handling 1
	}

	echo "query sns repair status"
	sns_repair_or_rebalance_status "repair" || {
		echo "SNS repair status query failure."
		error_handling 1
	}

	clovis_st_disk_state_set "repaired" $fail_device1 $fail_device2 || {
		echo "Operation to mark device repaired failed."
		error_handling 1
	}
	echo "SNS Repair done."

	echo "Read after sns repair"

	rm -f $dest_file
	io_conduct "READ" $OBJ_ID1 $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "read failure after sns repair."
		error_handling $rc
	fi
	diff $src_file $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Obj read after sns repair differs."
		error_handling $rc
	fi
	echo "Clovis: Read after repair successfull."
	rm -f $dest_file

	echo "Starting SNS Re-balance for device $fail_device1 $fail_device2"
	clovis_st_disk_state_set "rebalance" $fail_device1 $fail_device2 || {
		echo "Operation to mark device rebalance failed."
		error_handling 1
	}

	sns_rebalance || {
		echo "Operation to start sns rebalance failed."
		error_handling 1
	}

	echo "wait for sns rebalance"
	wait_for_sns_repair_or_rebalance "rebalance" || {
		echo "Failure occurred while waiting for sns rebalance to complete."
		error_handling 1
	}

	echo "query sns rebalance status"
	sns_repair_or_rebalance_status "rebalance" || {
		echo "SNS rebalance status query failure."
		error_handling 1
	}

	clovis_st_disk_state_set "online" $fail_device1 $fail_device2 || {
		echo "Operation to mark device online failed."
		error_handling 1
	}
	echo "SNS Rebalance done."

	rm -f $dest_file
	io_conduct "READ" $OBJ_ID1 $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "read failure after sns rebalance."
		error_handling $rc
	fi
	diff $src_file $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Obj read after sns rebalance differs."
		error_handling $rc
	fi
	rm -f $dest_file

	clovis_inst_cnt=`expr $cnt - 1`
	for i in `seq 1 $clovis_inst_cnt`
	do
		echo "clovis pids=${clovis_pids[$i]}" >> $CLOVIS_TEST_LOGFILE
	done

	unmount_and_clean &>> $MERO_TEST_LOGFILE
	mero_service_stop || rc=1

	if [ $rc -eq 0 ]; then
		sandbox_fini
	else
		error_handling $rc
	fi
	return $rc
}

echo "SNS Repair/Rebalance Test ... "
trap unprepare EXIT
main
report_and_exit sns-repair-rebalance $?
