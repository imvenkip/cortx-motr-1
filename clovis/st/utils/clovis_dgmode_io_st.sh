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
		cmd_args="$CLOVIS_LOCAL_EP $CLOVIS_HA_EP '$CLOVIS_PROF_OPT' '$CLOVIS_PROC_FID' /tmp $source"
		cmd_exec="${clovis_st_util_dir}/c0cat"
		cmd_args="$cmd_args $BLOCKSIZE $BLOCKCOUNT"
		local cmd="$cmd_exec $cmd_args > $dest &"
	else
		cmd_args="$CLOVIS_LOCAL_EP $CLOVIS_HA_EP '$CLOVIS_PROF_OPT' '$CLOVIS_PROC_FID' /tmp $dest $source"
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


# Dgmode IO

CLOVIS_TEST_DIR=$SANDBOX_DIR
CLOVIS_TEST_LOGFILE=$SANDBOX_DIR/clovis_`date +"%Y-%m-%d_%T"`.log
CLOVIS_TRACE_DIR=$SANDBOX_DIR/clovis

mero_service_start()
{
	local multiple_pools=0
	mero_service start $multiple_pools
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

	#mount m0t1fs as well. This helps in two ways:
	# 1) Currently clovis does not have a utility to check attributes of an
	#    object. Hence checking of attributes is done by fetching them via
	#    m0t1fs.
	# 2) A method to send HA notifications assumes presence of m0t1fs. One
	#    way to circumvent this is by assigning same end-point to clovis,
	#    but creating a clovis instance concurrently with HA notifications
	#    is hard. Another way is to re-write the method to send HA
	#    notifications by excluding m0t1fs end-point. We have differed these
	#    changes in current patch.
	local mountopt="oostore,verify"
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $mountopt || return 1
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
	fail_device=1

	# fail a disk and read an object
	disk_state_set "failed" $fail_device || {
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

	# Test write, when a disk is failed
	io_conduct "WRITE" $src_file $OBJ_ID2
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Degraded write failed."
		error_handling $rc
	fi
	echo "Check pver of the first object"
	output=`getfattr -n pver $MERO_M0T1FS_MOUNT_DIR/"$OBJ_HID1"`
	echo $output
	if [[ $output != *"$PVER_1"* ]]
	then
		echo "getattr failed on $OBJ_HID1."
		error_handling 1
	fi
	echo "Check pver of the second object, created post device failure."
	output=`getfattr -n pver $MERO_M0T1FS_MOUNT_DIR/"$OBJ_HID2"`
	echo $output
	if [[ $output != *"$PVER_2"* ]]
	then
		echo "getattr failed on $OBJ_HID2"
		error_handling 1
	fi
	rm -f $dest_file

	# Read a file from new pool version.
	io_conduct "READ" $OBJ_ID2 $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Reading a file from a new pool version failed."
		error_handling $rc
	fi
	diff $src_file $dest_file
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "File read from a new pool version differs."
		error_handling $rc
	fi
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

echo "DGMODE IO Test ... "
trap unprepare EXIT
main
report_and_exit degraded-mode-IO $?
