#!/bin/bash

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_sns_common_inc.sh
. `dirname $0`/m0t1fs_io_config_params.sh

N=3
K=3
P=15
stride=32
random_source_dd_count=650  # dd count used during random source file creation
dd_count=0                 # Counter for the number of times dd is invoked
st_dir=$MERO_CORE_ROOT/m0t1fs/linux_kernel/st
blk_size=$((stride * 1024))
cnt=0

fmio_truncation_module()
{
	j=$((RANDOM%9))
	i=$((10-$j))
	let seek=$i\-1
	echo "Test a call to fopen(..,O_TRUNC)"
	fmio_files_write dd bs=$blk_size count=$i
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "File truncation failed while count=$i"
		return $rc
	fi
	echo "Test a call to ftruncate(fd, 0)"
	fmio_files_write dd bs=$blk_size count=100 seek=0
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "File truncation failed for count=100 and seek=0"
		return $rc
	fi
	echo "Ensure that call to ftruncate(fd, <non-zero-size>) returns an error."
	fmio_files_write dd bs=$blk_size count=50 seek=$seek
	rc=$?
	if [ $rc -ne "1" ]
	then
		echo "File truncation failed for count=50 and seek=$seek."
		return $rc
	fi
	echo "Ensure that same data is present in both files"
	fmio_files_write dd bs=$blk_size count=$i
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "File truncation failed."
		return $rc
	fi
	return 0
}

valid_count_get()
{
	j=$((RANDOM%9))
	i=$((10-$j))
	bs=$1
	input_file_size=`expr $i \* $bs`
	echo $input_file_size
	echo $ABCD_SOURCE_SIZE
	while [ $input_file_size -gt $ABCD_SOURCE_SIZE ]
	do
		j=$((RANDOM%9))
		i=$((10-$j))
		input_file_size=`expr $i \* $bs`
	done
	return $i
}

fmio_large_file_truncation_module()
{
	bs=1048576
	valid_count_get $bs
	fmio_files_write dd bs=$bs count=$cnt
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "File truncation failed with bs=$bs and count=$cnt"
		return $rc
	fi
	bs=2097152
	valid_count_get $bs
	fmio_files_write dd bs=$bs count=$cnt
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "File truncation failed with bs=$bs and count=$cnt"
		return $rc
	fi
	return $rc
}


fmio_source_files_create()
{
	if [ $pattern == $ABCD ] || [ $pattern == $ALTERNATE ]
	then
		$prog_file_pattern $source_abcd 2>&1 >> $MERO_TEST_LOGFILE || {
			echo "Failed: m0t1fs_io_file_pattern..."
			return 1
		}
	fi

	if [ $pattern == $RANDOM1 ] || [ $pattern == $ALTERNATE ]
	then
		dd if=/dev/urandom bs=$block_size count=$random_source_dd_count of=$source_random 2>&1 >> $MERO_TEST_LOGFILE || {
			echo "Failed: dd..."
			return 1
		}
	fi

	return 0
}

# pre: value of bs shall be necessarily specified in terms of bytes
fmio_files_write()
{
	dd_count=`expr $dd_count + 1`

	# Verify that 'the size of the file to be written' is not larger than
	# 'the ABCD source file size'
	bs=$(echo $2 | cut -d= -f2)

	if [ $bs -lt 0 ]
	then
		echo "Specify bs in terms of bytes"
		return 1
	fi

	count=$(echo $3 | cut -d= -f2)
	input_file_size=$(($bs * $count))
	if [ $input_file_size -gt $ABCD_SOURCE_SIZE ]
	then
		echo "input_file_size ($input_file_size) is greater than ABCD_SOURCE_SIZE ($ABCD_SOURCE_SIZE)"
		return 1
	fi

	# Verify that 'the size of the file to be written' is not larger than
	# 'the RANDOM source file size'
	if [ $input_file_size -gt $random_source_size ]
	then
		echo "input_file_size ($input_file_size) is greater than random_source_size ($random_source_size)"
		return 1
	fi

	# Select source file from the sandbox, according to the configured
	# pattern
	if [ $pattern == $ABCD ]
	then
		source_sandbox=$source_abcd
	elif [ $pattern == $RANDOM1 ]
	then
		source_sandbox=$source_random
	elif [ $pattern == $ALTERNATE ]
	then
		if [ `expr $dd_count % 2` == 0 ]
		then
			echo "dd_count $dd_count (even), pattern to use $ABCD"
			source_sandbox=$source_abcd
		else
			echo "dd_count $dd_count (odd), pattern to use $RANDOM1"
			source_sandbox=$source_random
		fi
	else
		echo "Error: Invalid pattern $pattern"
		return 1
	fi

	if [ $single_file_test -ne 1 ]
	then
		file_to_compare_sandbox="$fmio_sandbox/0:1000$dd_count"
		file_to_compare_m0t1fs="$MERO_M0T1FS_MOUNT_DIR/0:1000$dd_count"
	fi

	echo -e "Write to the files from sandbox and m0t1fs (dd_count #$dd_count):"
	echo -e "\t - $file_to_compare_sandbox \n\t - $file_to_compare_m0t1fs"

	$@ \
	   if=$source_sandbox of=$file_to_compare_sandbox >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd..."
		return 1
	}
	$@ \
	   if=$source_sandbox of=$file_to_compare_m0t1fs >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd..."
		return 1
	}

	if [ $debug_level != $DEBUG_LEVEL_OFF ]
	then
		echo "od -A d -c $file_to_compare_sandbox | tail"
		od -A d -c $file_to_compare_sandbox | tail
		echo "od -A d -c $file_to_compare_m0t1fs | tail"
		od -A d -c $file_to_compare_m0t1fs | tail
	fi

	if [ $debug_level == $DEBUG_LEVEL_3 ]
	then
		echo "stob_read after dd execution (dd_count #$dd_count)"
		fmio_stob_read_full
	fi

	echo "Compare (read) the files from sandbox and m0t1fs"
	fmio_files_compare
	rc=$?

	return $rc
}

fmio_files_compare()
{
	cmp $file_to_compare_sandbox $file_to_compare_m0t1fs
	rc=$?
	if [ $rc -ne 0 ]
	then
		echo "Files differ..."
		echo -e "\tparity group number may be calculated as:"
		echo -e "\t\tpg_no = differing_offset / (unit_size * N)\n"
		echo "od -A d -c $file_to_compare_sandbox | tail"
		od -A d -c $file_to_compare_sandbox | tail
		echo "od -A d -c $file_to_compare_m0t1fs | tail"
		od -A d -c $file_to_compare_m0t1fs | tail

		if [ $debug_level == $DEBUG_LEVEL_2 ] ||
		   [ $debug_level == $DEBUG_LEVEL_3 ]
		then
			echo "stob_read after data discrepancy is encountered (dd_count #$dd_count)"
			fmio_stob_read_full
		fi
	fi

	echo "cmp output (dd_count #$dd_count): $rc"
	if [ $debug_level == $DEBUG_LEVEL_INTERACTIVE ]
	then
		fmio_if_to_continue_check
	fi

	return $rc
}

fmio_pool_mach_set_failure()
{
	if [ $debug_level != $DEBUG_LEVEL_STTEST ]
	then
		pool_mach_set_failure $1 || {
			echo "Failed: pool_mach_set_failure..."
			return 1
		}
		pool_mach_query $fail_devices
	fi
	return 0
}

fmio_sns_repair()
{
	if [ $debug_level != $DEBUG_LEVEL_STTEST ]
	then
		sns_repair || {
			echo "Failed: SNS repair..."
			return 1
		}
		pool_mach_query $fail_devices
	fi

	if [ $debug_level == $DEBUG_LEVEL_3 ]
	then
		echo "stob_read after repair (dd_count #$dd_count)"
		fmio_stob_read_full
	fi

	return 0
}

fmio_sns_rebalance()
{
	if [ $debug_level != $DEBUG_LEVEL_STTEST ]
	then
		sns_rebalance || {
			echo "Failed: SNS rebalance..."
			return 1
		}
		pool_mach_query $fail_devices
	fi
	return 0
}

fmio_repair_n_rebalance()
{
	echo "Performing repair and rebalance to mark the devices back online"
	fmio_sns_repair || {
		echo "Failed: sns repair..."
		return 1
	}

	echo "Performing rebalance"
	fmio_sns_rebalance || {
		echo "Failed: sns rebalance..."
		return 1
	}
}

fmio_stob_read_full()
{
	if [ $P -gt 15 ]
	then
		echo "stob reading supported with P <= 15 only..."
		return 0 # Not returning error intentionally
	fi

	if [ $file_kind != $SINGLE_FILE ]
	then
		echo "stob reading supported with $SINGLE_FILE kind only..."
		return 0 # Not returning error intentionally
	fi

	str1="00000000"
	fid="0010000"
	for (( i=1; i <= $P; ++i ))
	do
		if [ $i -le 4 ]
		then
			ios="ios1"
		elif [ $i -le 8 ]
		then
			ios="ios2"
		elif [ $i -le 12 ]
		then
			ios="ios3"
		else
			ios="ios4"
		fi
		str2=$(printf "%x" $i)
		stobid="$str1$str2$fid"

		echo "od -A d -c $MERO_M0T1FS_TEST_DIR/$ios/stobs/o/$stobid"
		od -A d -c $MERO_M0T1FS_TEST_DIR/$ios/stobs/o/$stobid
		# Note: During development, the above can be quickly modified
		# using 'sed' to read specific lines of interest from the stob
		# output.
		# e.g. by adding '| sed -n '50, 64p'' to the above line
	done
}

fmio_if_to_continue_check()
{
	while true; do
		read -p "Do you wish to continue with the ST script (dd_count #$dd_count) ?" yn
		case $yn in
			[Yy]* )
				echo -e "\n\tUser input $yn, will continue...\n";
				break;;
			[Nn]* )
				echo -e "\n\tUser input $yn, Will unmount, stop service and exit...";
				fmio_m0t1fs_unmount
				fmio_mero_service_stop
				exit;;
			* )
				echo "Please answer yes or no";;
		esac
	done
}

fmio_pre()
{
	fail_device1=1
	fail_device2=9
	fail_device3=2
	fail_devices="$fail_device1 $fail_device2 $fail_device3"
	block_size=$((stride * 1024))
	random_source_size=$(($block_size * $random_source_dd_count))

	prog_file_pattern="$st_dir/m0t1fs_io_file_pattern"
	source_abcd="$fmio_sandbox/source_abcd"
	source_random="$fmio_sandbox/source_random"
	file_to_create1="$MERO_M0T1FS_MOUNT_DIR/0:11111"
	file_to_create2="$MERO_M0T1FS_MOUNT_DIR/0:11112"
	file_to_create3="$MERO_M0T1FS_MOUNT_DIR/0:11113"

	rm -rf $fmio_sandbox
	mkdir $fmio_sandbox
	if [ $debug_level == $DEBUG_LEVEL_STTEST ]
	then
		mkdir $fmio_sandbox/tmp
	fi

	echo "Creating source files"
	fmio_source_files_create || {
		echo "Failed: source file creation..."
		return 1
	}

	return 0
}

fmio_io_test()
{
	if [ $failed_dev_test -eq 1 ]
	then
		test_name="failed_dev"
		step="failure"
	else
		test_name="repaired_dev"
		step="repair"
	fi

	echo "All the devices are online at this point"
	if [ $debug_level != $DEBUG_LEVEL_STTEST ]
	then
		pool_mach_query $fail_devices
	fi

	echo "Creating files initially"
	fmio_files_write dd bs=$block_size count=50 || {
		echo "Failed: file creation..."
		return 1
	}

	echo "Truncating files"
	fmio_truncation_module
	if [ $? -ne "0" ]
	then
		echo "File truncation failed."
		return 1
	fi

	fmio_large_file_truncation_module
	if [ $? -ne "0" ]
	then
		echo "Large file truncation failed."
		return 1
	fi

	echo "Sending device1 failure"
	fmio_pool_mach_set_failure $fail_device1 || {
		return 1
	}

	echo "File truncation post first device failure"
	fmio_truncation_module
	if [ $? -ne "0" ]
	then
		echo "File truncation failed after first failure."
		return 1
	fi

	fmio_large_file_truncation_module
	if [ $? -ne "0" ]
	then
		echo "Large file truncation failed after the first failure."
		return 1
	fi



	if [ $failed_dev_test -ne 1 ]
	then
		echo "Repairing after device1 failure"
		fmio_sns_repair || {
			return 1
		}
	fi

	echo -e "\n*** $test_name test 1: Read after first $step ***"
	fmio_files_compare || {
		echo "Failed: read after first $step..."
		return 1
	}

	# XXX Enable once MERO-705, MERO-706 are fixed
	#echo "Create a file after first $step: $file_to_create1"
	#touch $file_to_create1
	#rc=$?
	#if [ $rc -ne 0 ]
	#then
	#	echo "Failed: create after first $step, rc $rc..."
	#	return 1
	#fi

	echo -e "\n*** $test_name test 2: IO and read after first $step ***"
	fmio_files_write dd bs=8821 count=5 seek=23 conv=notrunc || {
		echo "Failed: IO or read after first $step..."
		return 1
	}

	echo "Sending device2 failure"
	fmio_pool_mach_set_failure $fail_device2 || {
		return 1
	}

	if [ $failed_dev_test -ne 1 ]
	then
		echo "Repairing after device2 failure"
		fmio_sns_repair || {
			return 1
		}
	fi

	# XXX Enable once MERO-705, MERO-706 are fixed
	#echo "Create a file after second $step: $file_to_create2"
	#touch $file_to_create2
	#rc=$?
	#if [ $rc -ne 0 ]
	#then
	#	echo "Failed: create after second $step, rc $rc..."
	#	return 1
	#fi

	echo -e "\n*** $test_name test 3: Read after second $step ***"
	fmio_files_compare || {
		echo "Failed: read after second $step..."
		return 1
	}

	echo -e "\n*** $test_name test 4: IO and read after second $step ***"
	fmio_files_write dd bs=$block_size count=60 || {
		echo "Failed: IO or read after second $step..."
		return 1
	}

	echo -e "\n*** $test_name test 5: Another IO after second $step *** (truncate test)"
	fmio_files_write dd bs=$block_size count=40 || {
		echo "Failed: Another IO or read after second $step..."
		return 1
	}

	echo "Sending device3 failure"
	fmio_pool_mach_set_failure $fail_device3 || {
		return 1
	}

	if [ $failed_dev_test -ne 1 ]
	then
		echo "Truncation after two repairs and one failure"
		fmio_truncation_module
		if [ $? -ne "0" ]
		then
			echo "File truncation failed."
			return 1
		fi

		fmio_large_file_truncation_module
		if [ $? -ne "0" ]
		then
			echo "Large file truncation failed."
			return 1
		fi
		echo "Repairing after device3 failure"
		fmio_sns_repair || {
		return 1
		}
	fi

	echo -e "\n*** $test_name test 6: Read after third $step ***"
	fmio_files_compare || {
		echo "Failed: read after third $step..."
		return 1
	}

	# XXX Enable once MERO-705, MERO-706 are fixed
	#echo "Create a file after third $step: $file_to_create3"
	#touch $file_to_create3
	#rc=$?
	#if [ $rc -ne 0 ]
	#then
	#	echo "Failed: create after third $step, rc $rc..."
	#	return 1
	#fi

	echo -e "\n*** $test_name test 7: IO and read after third $step ***"
	fmio_files_write dd bs=$block_size count=50 || {
		echo "Failed: IO or read after third $step..."
		return 1
	}

	echo -e "\n*** $test_name test 8: Another IO and read after third $step ***"
	fmio_files_write dd bs=$block_size count=10 || {
		echo "Failed: IO or read after third $step..."
		return 1
	}

	return 0
}

fmio_failed_dev_test()
{
	if [ $failed_dev_test -eq 0 ]
	then
		return 1
	fi

	echo "Starting failed device IO testing"
	fmio_io_test || {
		echo "Failed: fmio_io_test for failed device..."
		return 1
	}

	echo -e "failed device IO test succeeded"
	return 0
}

fmio_repaired_dev_test()
{
	if [ $failed_dev_test -eq 1 ]
	then
		return 1
	fi

	echo "Starting repaired device IO testing"
	fmio_io_test || {
		echo "Failed: fmio_io_test for repaired device..."
		return 1
	}

	echo -e "repaired device IO test succeeded"
	return 0
}

fmio_mero_service_start()
{
	if [ $debug_level != $DEBUG_LEVEL_STTEST ]
	then
		echo "About to start Mero service"
		mero_service start $stride $N $K $P
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
	fi
	return 0
}

fmio_mero_service_stop()
{
	if [ $debug_level != $DEBUG_LEVEL_STTEST ]
	then
		echo "About to stop Mero service"
		mero_service stop
		if [ $? -ne 0 ]
		then
			echo "Failed to stop Mero Service..."
			return 1
		fi
	fi
	return 0
}

fmio_m0t1fs_mount()
{
	# XXX MERO-704: Perform testing with non-oostore mode once MERO-678 is
	# fixed
	local mountopt="oostore,verify"

	if [ $debug_level != $DEBUG_LEVEL_STTEST ]
	then
		mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $N $K $P $mountopt &>> $MERO_TEST_LOGFILE || {
			cat $MERO_TEST_LOGFILE
			return 1
		}
		mount
	fi
	return 0
}

fmio_m0t1fs_unmount()
{
	if [ $debug_level != $DEBUG_LEVEL_STTEST ]
	then
		echo "unmounting and cleaning.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE
	fi
	return 0
}

failure_modes_test()
{
	if [ $single_file_test -eq 1 ]
	then
		file_to_compare_sandbox="$fmio_sandbox/0:10000"
		file_to_compare_m0t1fs="$MERO_M0T1FS_MOUNT_DIR/0:10000"
		str="single file"
	else
		str="separate file"
	fi

	if [ $failure_mode == $FAILED_DEVICES ] || [ $failure_mode == $BOTH_DEVICES ]
	then
		echo "--------------------------------------------------------"
		echo "Start with the failed device IO testing ($str)"
		echo "--------------------------------------------------------"
		failed_dev_test=1

		echo "ls -l $MERO_M0T1FS_MOUNT_DIR"
		ls -l $MERO_M0T1FS_MOUNT_DIR

		# Perform failed device IO test by 'marking some devices as
		# failed' in between
		fmio_failed_dev_test || {
			echo "Failed: failed device IO test..."
			return 1
		}

		echo "--------------------------------------------------------"
		echo "Done with the failed device IO testing ($str)"
		echo "--------------------------------------------------------"

		echo -e "Mark the devices online again before the next test"
		fmio_repair_n_rebalance || {
			return 1
		}
	fi

	if [ $failure_mode == $REPAIRED_DEVICES ] || [ $failure_mode == $BOTH_DEVICES ]
	then
		echo "--------------------------------------------------------"
		echo "Starting with the repaired device IO testing ($str)"
		echo "--------------------------------------------------------"

		# XXX MERO-638: Take out the following condition once
		# MERO-638 is fixed
#		if [ $single_file_test == 1 ]
#		then
#			echo -e "single file kind can not be tested with repaired device until MERO-638 is fixed...\nHence, exiting from repaired device testing..."
#			echo "-------------------------------------------------"
#			echo "Done with the repaired device IO testing ($str)"
#			echo "-------------------------------------------------"
#			return 0
#		fi

		failed_dev_test=0

		echo "ls -l $MERO_M0T1FS_MOUNT_DIR"
		ls -l $MERO_M0T1FS_MOUNT_DIR

		# Perform repaired device IO test by 'marking some devices as
		# failed followed by performing repair' in between
		fmio_repaired_dev_test || {
			echo "Failed: repaired device IO test..."
			return 1
		}

		echo "--------------------------------------------------------"
		echo "Done with the repaired device IO testing ($str)"
		echo "--------------------------------------------------------"

		echo -e "Mark the devices online again before the next test"
		fmio_sns_rebalance || {
			echo "Failed: sns rebalance..."
			return 1
		}
	fi
	return 0
}

main()
{
	NODE_UUID=`uuidgen`
	fmio_sandbox="$st_dir/sandbox"
	rc=0

	echo "*********************************************************"
	echo "Starting with the failure modes IO testing"
	echo "*********************************************************"

	# Override this variable so as to use linux stob, for debugging
	if [ $debug_level != $DEBUG_LEVEL_OFF ]
	then
		MERO_STOB_DOMAIN="linux"
	fi

	# Override these variables so as to test the ST framework without
	# involving mero service and m0t1fs
	if [ $debug_level == $DEBUG_LEVEL_STTEST ]
	then
		MERO_TEST_LOGFILE="$fmio_sandbox/log"
		MERO_M0T1FS_MOUNT_DIR="$fmio_sandbox/tmp"
	fi

	# Display the configuration information
	echo -e "fmio_sandbox \t\t $fmio_sandbox"
	echo -e "MERO_M0T1FS_MOUNT_DIR \t $MERO_M0T1FS_MOUNT_DIR"
	echo -e "failure_mode \t\t $failure_mode"
	echo -e "file_kind \t\t $file_kind"
	echo -e "(data) pattern \t\t $pattern"
	echo -e "debug_level \t\t $debug_level"
	echo -e "MERO_STOB_DOMAIN \t $MERO_STOB_DOMAIN"

	echo -e "\nPreprocessing for failure modes IO testing"
	fmio_pre || {
		return 1
	}

	fmio_mero_service_start || {
		return 1
	}

	fmio_m0t1fs_mount || {
		fmio_mero_service_stop
		return 1
	}
	echo -e "Done with preprocessing for failure modes IO testing\n"

	if [ $file_kind == $SINGLE_FILE ] || [ $file_kind == $BOTH_FILE_KINDS ]
	then
		echo "========================================================"
		echo "Start with the single file IO testing"
		echo "========================================================"
		single_file_test=1
		failure_modes_test || {
			fmio_m0t1fs_unmount
			fmio_mero_service_stop
			return 1
		}
		echo "========================================================"
		echo "Done with the single file IO testing"
		echo "========================================================"
	fi

	if [ $file_kind == $SEPARATE_FILE ] || [ $file_kind == $BOTH_FILE_KINDS ]
	then
		echo "========================================================"
		echo "Start with the separate file IO testing"
		echo "========================================================"
		single_file_test=0
		failure_modes_test || {
			fmio_m0t1fs_unmount
			fmio_mero_service_stop
			return 1
		}
		echo "========================================================"
		echo "Done with the separate file IO testing"
		echo "========================================================"
	fi

	fmio_m0t1fs_unmount || {
		rc=1
	}

	fmio_mero_service_stop || {
		rc=1
	}

	echo "********************************************************"
	echo "Done with the failure modes IO testing"
	echo "********************************************************"

	echo "Test log available at $MERO_TEST_LOGFILE."
	return $rc
}

trap unprepare EXIT
main

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
if [ $? -eq 0 ] ; then
    echo "degraded-mode: test status: SUCCESS"
fi
