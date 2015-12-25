#!/bin/bash

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_sns_common_inc.sh

# If DEBUG_MODE is set to 1, trace file is generated. This may be useful when
# some issue is to be debugged in developer environment.
# Note: Always keep its value 0 before pushing the code to the repository.
DEBUG_MODE=0

N=8
K=2
P=20
stride=32
bs=8192
count=150
st_dir=$M0_SRC_DIR/m0t1fs/linux_kernel/st
rcancel_sandbox="$MERO_M0T1FS_TEST_DIR/rcancel_sandbox"
source_file="$rcancel_sandbox/rcancel_source"

rcancel_mero_service_start()
{
	local multiple_pools=0
	echo "About to start Mero service"
	mero_service start $multiple_pools $stride $N $K $P
	if [ $? -ne 0 ]
	then
		echo "Failed to start Mero Service"
		return 1
	fi
	echo "mero service started"

	return 0
}

rcancel_pre()
{
	prog_file_pattern="$st_dir/m0t1fs_io_file_pattern"
	local source_abcd="$rcancel_sandbox/rcancel_abcd"

	echo "Mount debugfs and insert $MERO_CTL_MODULE.ko so as to use fault injection"
	mount -t debugfs none /sys/kernel/debug
	mount | grep debugfs
	if [ $? -ne "0" ]
	then
		echo "Failed to mount debugfs"
		return 1
	fi

	echo "insmod $mero_module_path/$MERO_CTL_MODULE.ko"
	insmod $mero_module_path/$MERO_CTL_MODULE.ko
	lsmod | grep $MERO_CTL_MODULE
	if [ $? -ne "0" ]
	then
		echo "Failed to insert module \
		      $mero_module_path/$MERO_CTL_MODULE.ko"
		return 1
	fi

	rm -rf $rcancel_sandbox
	mkdir -p $rcancel_sandbox
	echo "Creating data file $source_abcd"
	$prog_file_pattern $source_abcd 2>&1 >> $MERO_TEST_LOGFILE || {
		echo "Failed: $prog_file_pattern"
		return 1
	}

	echo "Creating source file $source_file"
	dd if=$source_abcd of=$source_file bs=$bs count=$count >> $MERO_TEST_LOGFILE 2>&1

	echo "ls -l $source_file (Reference for data files generated)"
	ls -l $source_file

	if [ $DEBUG_MODE -eq 1 ]
	then
		# TODO Resolve issue
		# - rmmod m0ctl fails with error "ERROR: Module m0ctl is in use"
		rm -f /var/log/mero/m0mero_ko.img
		rm -f /var/log/mero/m0trace.bin*
		$M0_SRC_DIR/utils/trace/m0traced -K -d
	fi
}

rcancel_post()
{
	if [ $DEBUG_MODE -eq 1 ]
	then
		pkill -9 m0traced
		# Note: trace may be read as follows
		#$M0_SRC_DIR/utils/trace/m0trace -w0 -s m0t1fs,rpc -I /var/log/mero/m0mero_ko.img -i /var/log/mero/m0trace.bin -o $MERO_TEST_LOGFILE.trace
	fi

	echo "rmmod $mero_module_path/$MERO_CTL_MODULE.ko"
	rmmod $mero_module_path/$MERO_CTL_MODULE.ko
	if [ $? -ne 0 ]; then
		echo "Failed: $MERO_CTL_MODULE.ko could not be unloaded"
		return 1
	fi
}

rcancel_session_restore()
{
	local file_for_restore=$1

	echo "Test: Enable FP for session restore"
	echo 'enable m0_reqh_mdpool_service_index_to_session rpc_session_restore oneshot' > /sys/kernel/debug/mero/finject/ctl
	echo "Write to $file_for_restore to restore session through FP"
	dd if=/dev/zero of=$file_for_restore bs=$bs count=1
}

rcancel_issue_writes_n_wait()
{
	local ww_file_base=$1
	local ww_nr_files=$2
	local ww_fault_enable=$3

	echo "Test: Creating $ww_nr_files files on m0t1fs"
	for ((i=0; i<$ww_nr_files; ++i)); do
		touch $ww_file_base$i || break
	done

	echo "Test: Writing to $ww_nr_files files on m0t1fs"
	for ((i=0; i<$ww_nr_files; ++i)); do
		echo "dd if=$source_file of=$ww_file_base$i bs=$bs count=$count &"
		dd if=$source_file of=$ww_file_base$i bs=$bs count=$count &
	done

	dd_count=$(ps -aef | grep -w "dd" | wc -l)
	echo "dd processes running : $(($dd_count-1))"
	if [ $dd_count -ne $((ww_nr_files+1)) ] && [ $ww_fault_enable -eq 1 ]
	then
		echo "Failed to issue long running $ww_nr_files dd write requests"
		return 1
	fi

	if [ $ww_fault_enable -eq 1 ]
	then
		echo "Test: Enable FP for session cancelation"
		# sleep so that enough number of fops are created
		sleep 3
		echo 'enable m0t1fs_container_id_to_session rpc_session_cancel oneshot' > /sys/kernel/debug/mero/finject/ctl
	fi

	# If the respective fault point was enabled, expect to have one rpc
	# session canceled while waiting for the dd operations to complete.
	echo "Test: Wait for dd to finish"
	wait

	# Ensure that all the dd have either failed or finished
	dd_count=$(ps -aef | grep -w "dd" | wc -l)
	echo "dd processes running : $(($dd_count-1))"
	if [ $dd_count -ne 1 ]
	then
		echo "Failed to complete $ww_nr_files dd requests"
		return 1
	fi
}

rcancel_cancel_during_write_test()
{
	local wt_file_for_restore="$MERO_M0T1FS_MOUNT_DIR/0:11111"
	local wt_file_base="$MERO_M0T1FS_MOUNT_DIR/0:2222"
	local wt_file_base_new="$MERO_M0T1FS_MOUNT_DIR/0:3333"
	local wt_nr_files=10
	local wt_enable_fp=1
	local wt_ls_rc
	local wt_cmp_rc
	local wt_rm_rc
	local wt_write_rc

	echo "touch $wt_file_for_restore before a session is canceled"
	touch $wt_file_for_restore

	# Cancel a session while running some parallel write operations
	rcancel_issue_writes_n_wait $wt_file_base $wt_nr_files $wt_enable_fp || return 1

	# Verify that some dd operations were indeed canceled.
	# It is to verify that some RPC items were indeed canceled through
	# RPC session cancelation. Some items are going to get canceled
	# while attempting to be posted after session cancelation.
	num=`grep -n "dd: " $MERO_TEST_LOGFILE | grep "writing" | grep "Operation canceled" | grep "$wt_file_base" | wc -l | cut -f1 -d' '`
	echo "dd write canceled : $num"
	if [ $num -eq 0 ]; then
		echo "Failed: No dd writing operation was canceled"
		return 1
	fi

	num=`grep -n "dd: closing" $MERO_TEST_LOGFILE | grep "Operation canceled" | grep "$wt_file_base" | wc -l | cut -f1 -d' '`
	echo "dd closing canceled : $num"

	# Test ls with canceled session
	echo "Test: ls for $wt_nr_files files before restore (Succeeds if 'Operation canceled' is not received. Shall succeed for all once we start using MD_REDUNDANCY > 1)"
	for ((i=0; i<$wt_nr_files; ++i)); do
		ls -l $wt_file_base$i
		echo "ls_rc: $?"
	done
	num=`grep -n "ls: cannot access" $MERO_TEST_LOGFILE | grep "Operation canceled" | grep "$wt_file_base" | wc -l | cut -f1 -d' '`
	echo "ls canceled : $num"

	rcancel_session_restore $wt_file_for_restore

	echo "Test: ls for $wt_nr_files files after restore (written during cancel)"
	for ((i=0; i<$wt_nr_files; ++i)); do
		ls -l $wt_file_base$i
		wt_ls_rc=$?
		echo "ls_rc: $wt_ls_rc"
		if [ $wt_ls_rc -ne 0 ]
		then
			echo "ls $wt_file_base$i after restore failed"
			return 1
		fi
	done

	echo "Test: read $wt_nr_files files after restore (written during cancel) (They receive either 'No such file or directory', 'EOF' or 'Input/output error' since some gobs/cobs may not be created)"
	for ((i=0; i<$wt_nr_files; ++i)); do
		cmp $source_file $wt_file_base$i
		echo "cmp $wt_file_base$i: rc $?"
	done

	echo "Test: rm $wt_nr_files files after restore (written during cancel) (Expected to fail for the files for which gob is not created)"
	for ((i=0; i<$wt_nr_files; ++i)); do
		rm -f $wt_file_base$i
		wt_rm_rc=$?
		echo "rm -f $wt_file_base$i: rc $wt_rm_rc"
		if [ $wt_rm_rc -ne 0 ]
		then
			echo "rm failed, $wt_file_base$i"
		fi
	done

	echo "Test: write to new $wt_nr_files files after restore"
	for ((i=0; i<$wt_nr_files; ++i)); do
		dd if=$source_file of=$wt_file_base_new$i bs=$bs count=$count >> $MERO_TEST_LOGFILE 2>&1
		wt_write_rc=$?
		echo "Write to $wt_file_base_new$i: rc $wt_write_rc"
		if [ $wt_write_rc -ne 0 ]
		then
			echo "Write $wt_file_base$i after restore failed"
			return 1
		fi
	done

	echo "Test: ls new $wt_nr_files files after restore (written after restore)"
	for ((i=0; i<$wt_nr_files; ++i)); do
		ls -l $wt_file_base_new$i
		wt_ls_rc=$?
		echo "ls_rc: $wt_ls_rc"
		if [ $wt_ls_rc -ne 0 ]
		then
			echo "ls $wt_file_base_new$i after restore failed"
			return 1
		fi
	done

	echo "Test: Read $wt_nr_files files after restore (written after restore)"
	for ((i=0; i<$wt_nr_files; ++i)); do
		cmp $source_file $wt_file_base_new$i
		wt_cmp_rc=$?
		echo "cmp $wt_file_base_new$i: rc $wt_cmp_rc"
		if [ $wt_cmp_rc -ne 0 ]
		then
			echo "Verification failed: Files differ, $source_file, $wt_file_base_new$i"
			return 1
		fi
	done

	echo "Test: rm $wt_nr_files files after restore (written after restore)"
	for ((i=0; i<$wt_nr_files; ++i)); do
		rm -f $wt_file_base_new$i
		wt_rm_rc=$?
		echo "rm -f $wt_file_base$i: rc $wt_rm_rc"
		if [ $wt_rm_rc -ne 0 ]
		then
			echo "rm failed, $wt_file_base$i"
			return 1
		fi
	done
}

rcancel_issue_read_n_wait()
{
	local rw_file_base=$1
	local rw_nr_files=$2
	local rw_fault_enable=$3

	echo "Test: Reading from $rw_nr_files files on m0t1fs"
	for ((i=0; i<$rw_nr_files; ++i)); do
		echo "dd if=$rw_file_base$i of=$rcancel_sandbox/$i bs=$bs count=$count &"
		dd if=$rw_file_base$i of=$rcancel_sandbox/$i bs=$bs count=$count >> $MERO_TEST_LOGFILE 2>&1 &
	done

	dd_count=$(ps -aef | grep -w "dd" | wc -l)
	echo "dd processes running : $(($dd_count-1))"
	if [ $dd_count -ne $((rw_nr_files+1)) ]
	then
		echo "Failed to issue long running $rw_nr_files dd read requests"
		return 1
	fi

	if [ $rw_fault_enable -eq 1 ]
	then
		echo 'enable m0t1fs_container_id_to_session rpc_session_cancel oneshot' > /sys/kernel/debug/mero/finject/ctl
	fi

	# If the respective fault point was enabled, expect to have the rpc
	# session canceled while waiting for the dd operations to complete.
	echo "Test: Wait for dd to finish"
	wait

	# Ensure that all the dd have either failed or finished
	dd_count=$(ps -aef | grep -w "dd" | wc -l)
	echo "dd processes running : $(($dd_count-1))"
	if [ $dd_count -ne 1 ]
	then
		echo "Failed to complete $rw_nr_files dd requests"
		return 1
	fi
}

rcancel_cancel_during_read_test()
{
	local rt_file_for_restore="$MERO_M0T1FS_MOUNT_DIR/0:66661"
	local rt_file_base="$MERO_M0T1FS_MOUNT_DIR/0:7777"
	local rt_nr_files=10
	local rt_enable_fp_during_write=0
	local rt_enable_fp_during_read=1
	local rt_cmp_rc
	local rt_rm_rc

	echo "touch $rt_file_for_restore before a session is canceled"
	touch $rt_file_for_restore

	# Create some data files w/o canceling session
	rcancel_issue_writes_n_wait $rt_file_base $rt_nr_files $rt_enable_fp_during_write || return 1

	# Cancel a session while running some parallel read operations
	rcancel_issue_read_n_wait $rt_file_base $rt_nr_files $rt_enable_fp_during_read || return 1

	# Verify that some dd operations were indeed canceled. It is
	# to verify that RPC session was indeed canceled.
	# Many of those read ops fail with the error "Input/output error"
	# while a few fail with the error "Operation canceled"
	num=`grep -n "dd: " $MERO_TEST_LOGFILE | grep "reading" | egrep "("Operation\ canceled"|"Input\/output\ error")" | grep "$rt_file_base" | wc -l | cut -f1 -d' '`
	echo "dd read canceled : $num"
	if [ $num -eq 0 ]; then
		echo "Failed: No dd reading operation was canceled"
		return 1
	fi

	num=`grep -n "dd: closing" $MERO_TEST_LOGFILE | grep "Operation canceled" | grep "$rt_file_base" | wc -l | cut -f1 -d' '`
	echo "dd closing canceled : $num"

	# Test ls with canceled session
	echo "Test: ls for $rt_nr_files files before restore (Succeeds if "Operation canceled" is not received. Shall succeed for all once we start using MD_REDUNDANCY > 1)"
	for ((i=0; i<$rt_nr_files; ++i)); do
		ls -l $rt_file_base$i
		echo "ls_rc: $?"
	done
	num=`grep -n "ls: cannot access" $MERO_TEST_LOGFILE | grep "Operation canceled" | grep "$rt_file_base" | wc -l | cut -f1 -d' '`
	echo "ls canceled : $num"

	rcancel_session_restore $rt_file_for_restore

	# Verify contents of files after restore to ensure that there is no
	# corruption
	echo "Test: read $rt_nr_files files after restore (written before cancel)"
	for ((i=0; i<$rt_nr_files; ++i)); do
		file_from_m0t1fs=$rt_file_base$i
		cmp $source_file $file_from_m0t1fs
		rt_cmp_rc=$?
		echo "cmp $file_from_m0t1fs: rc $rt_cmp_rc"
		if [ $rt_cmp_rc -ne 0 ]
		then
			echo "Verification failed: Files differ, $source_file, $file_from_m0t1fs"
			return 1
		fi
	done

	echo "Test: rm $rt_nr_files files after restore (written before cancel)"
	for ((i=0; i<$rt_nr_files; ++i)); do
		rm -f $rt_file_base$i
		rt_rm_rc=$?
		echo "rm -f $rt_file_base$i: rc $rt_rm_rc"
		if [ $rt_rm_rc -ne 0 ]
		then
			echo "rm failed, $rt_file_base$i"
			return 1
		fi
	done
}

rcancel_test()
{
	local mountopt="oostore,verify"

	rcancel_pre || return 1

	echo "==================================================================="
	echo "Start with session cancel testing with concurrent write IO requests"
	echo "==================================================================="

	echo "Test: rpc-service-cancel (N, K, P) = ($N, $K, $P)"
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $N $K $P $mountopt &>> $MERO_TEST_LOGFILE || return 1

	rcancel_cancel_during_write_test || {
		unmount_and_clean
		return 1
	}

	unmount_and_clean

	echo "=================================================================="
	echo "Done with session cancel testing with concurrent write IO requests"
	echo "=================================================================="

	echo "=================================================================="
	echo "Start with session cancel testing with concurrent read IO requests"
	echo "=================================================================="

	echo "Test: rpc-service-cancel (N, K, P) = ($N, $K, $P)"
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $N $K $P $mountopt &>> $MERO_TEST_LOGFILE || return 1

	rcancel_cancel_during_read_test || {
		unmount_and_clean
		return 1
	}

	unmount_and_clean

	echo "================================================================="
	echo "Done with session cancel testing with concurrent read IO requests"
	echo "================================================================="

	rcancel_post || return 1
}

main()
{
	NODE_UUID=`uuidgen`
	local rc

	echo "*********************************************************"
	echo "Starting with the RPC session cancelation testing"
	echo "*********************************************************"

	sandbox_init

	rcancel_mero_service_start || return 1

	rcancel_test 2>&1 | tee -a $MERO_TEST_LOGFILE
	rc=${PIPESTATUS[0]}
	echo "rcancel_test rc $rc"
	if [ $rc -ne "0" ]; then
		echo "Failed m0t1fs RPC Session Cancel test."
	fi

	mero_service stop
	if [ $? -ne "0" ]
	then
		echo "Failed to stop Mero Service."
		if [ $rc -eq "0" ]; then
			rc=1
		fi
	fi

	echo "*********************************************************"
	echo "Done with the RPC session cancelation testing"
	echo "*********************************************************"

	if [ $rc -eq 0 ]; then
		[ $DEBUG_MODE -eq 1 ] || sandbox_fini
	else
		echo "Test log available at $MERO_TEST_LOGFILE."
	fi
	return $rc
}

trap unprepare EXIT
main
report_and_exit rpc-session-cancel $?
