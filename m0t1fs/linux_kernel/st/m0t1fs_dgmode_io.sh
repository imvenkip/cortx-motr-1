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
io_count=0          # Counter for the number of times dd is invoked
random_dd_count=60  # value of count used during random source file creation

dgio_source_files_create()
{
	if [ "$pattern" == "$ABCD" ] || [ "$pattern" == "$MIXED" ]
	then
		$prog_file_pattern $source_abcd 2>&1 >> $MERO_TEST_LOGFILE || {
			echo "Failed: m0t1fs_io_file_pattern failed"
			return 1
		}
	fi

	if [ "$pattern" == "$RANDOM1" ] || [ "$pattern" == "$MIXED" ]
	then
		dd if=/dev/urandom bs=$unit_size count=$random_dd_count of=$source_random 2>&1 >> $MERO_TEST_LOGFILE || {
			echo "Failed: dd failed.."
			return 1
		}
	fi
	return 0
}

dgio_files_write()
{
	io_count=`expr $io_count + 1`

	# Verify that 'the size of the file to be written' is not larger than
	# 'the ABCD source file size'
	bs=$(echo $2 | cut -d= -f2)
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
	if [ "$pattern" == "$ABCD" ]
	then
		source_sandbox=$source_abcd
	elif [ "$pattern" == "$RANDOM1" ]
	then
		source_sandbox=$source_random
	elif [ "$pattern" == "MIXED" ]
	then
		if [ `expr $io_count % 2` == 0 ]
		then
			echo "io_count $io_count (even), pattern to use $ABCD"
			source_sandbox=$source_abcd
		else
			echo "io_count $io_count (odd), pattern to use $RANDOM1"
			source_sandbox=$source_random
		fi
	else
		echo "Error: Invalid pattern $pattern"
		return 1
	fi

	echo "Write to the files from sandbox and m0t1fs"
	$@ \
	   if=$source_sandbox of=$file_to_compare_sandbox >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		return 1
	}
	$@ \
	   if=$source_sandbox of=$file_to_compare_m0t1fs >> $MERO_TEST_LOGFILE || {
		echo "Failed: dd failed.."
		return 1
	}

	if [ $debug_level -gt $DEBUG_LEVEL_OFF ]
	then
		echo "od -A d -c $file_to_compare_sandbox | tail"
		od -A d -c $file_to_compare_sandbox | tail
		echo "od -A d -c $file_to_compare_m0t1fs | tail"
		od -A d -c $file_to_compare_m0t1fs | tail
	fi

	if [ $debug_level -eq $DEBUG_LEVEL_3 ]
	then
		echo "stob_read after dd execution, io_count #$io_count"
		dgio_stob_read_full
	fi

	echo "Compare (read) the files from sandbox and m0t1fs"
	dgio_files_compare
	rc=$?

	if [ $debug_level -eq $DEBUG_LEVEL_USER_INPUT ]
	then
		if_to_continue_check
	fi

	return $rc
}

dgio_files_compare()
{
	cmp $file_to_compare_sandbox $file_to_compare_m0t1fs
	rc=$?
	echo "cmp output: $rc"
	if [ $rc -ne "0" ]
	then
		echo "Files differ..."
		echo "od -A d -c $file_to_compare_sandbox | tail"
		od -A d -c $file_to_compare_sandbox | tail
		echo "od -A d -c $file_to_compare_m0t1fs | tail"
		od -A d -c $file_to_compare_m0t1fs | tail

		if [ $debug_level -eq $DEBUG_LEVEL_2 ] ||
		   [ $debug_level -eq $DEBUG_LEVEL_3 ]
		then
			echo "stob_read after data discrepancy is encountered, io_count #$io_count"
			dgio_stob_read_full
		fi
	fi
	return $rc
}

dgio_pool_mach_set_failure()
{
	rc=0
	if [ $debug_level -ne $DEBUG_LEVEL_TEST ]
	then
		pool_mach_set_failure $1
		rc=$?
		if [ $rc -ne "0" ]
		then
			return $rc
		fi

		pool_mach_query $1
		rc=$?
		if [ $rc -ne "0" ]
		then
			return $rc
		fi
	else
		return $rc
	fi
}

dgio_stob_read_full()
{
	if [ $P -gt 15 ]
	then
		echo "stob reading not yet supported with P > 15..."
		return 0
	fi

	str1="00000000"
	str3="0010000"
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
		stobid="$str1$str2$str3"
		od -A d -c $MERO_M0T1FS_TEST_DIR/$ios/stobs/o/$stobid
	done
}

if_to_continue_check()
{
	while true; do
		read -p "Do you wish to continue with the ST script? (io_count #$io_count)" yn
		case $yn in
			[Yy]* )
				echo "User input $yn, will continue";
				break;;
			[Nn]* )
				echo "User input $yn, Will unmount, stop service and exit";
				echo "unmounting and cleaning.."
				unmount_and_clean &>> $MERO_TEST_LOGFILE

				echo "About to stop Mero service"
				mero_service stop
				if [ $? -ne "0" ]
				then
					echo "Failed to stop Mero Service."
				fi

				exit;;
			* )
				echo "Please answer yes or no";;
		esac
	done

}

dgio_test()
{
	local fail_device1=1
	local fail_device2=9
	local fail_device3=2
	local unit_size=$((stride * 1024))
	local random_source_size=$(($unit_size * $random_dd_count))
	local rc=0

	echo "Starting dgmode testing ..."

	prog_file_pattern="$MERO_CORE_ROOT/m0t1fs/linux_kernel/st/m0t1fs_io_file_pattern"
	source_abcd="$dgmode_sandbox/source_abcd"
	source_random="$dgmode_sandbox/source_random"
	file_to_compare_sandbox="$dgmode_sandbox/file_to_compare_sandbox"
	file_to_compare_m0t1fs="$MERO_M0T1FS_MOUNT_DIR/file_to_compare_m0t1fs"
	file_in_dgmode1="$MERO_M0T1FS_MOUNT_DIR/file_in_dgmode1"
	file_in_dgmode2="$MERO_M0T1FS_MOUNT_DIR/file_in_dgmode2"
	file_in_dgmode3="$MERO_M0T1FS_MOUNT_DIR/file_in_dgmode3"

	rm -rf $dgmode_sandbox
	mkdir $dgmode_sandbox
	if [ $debug_level -eq $DEBUG_LEVEL_TEST ]
	then
		mkdir $dgmode_sandbox/tmp
	fi

	echo "Creating source files"
	dgio_source_files_create || {
		echo "Failed: source file creation..."
		return 1
	}

	echo "Creating files"
	dgio_files_write dd bs=$unit_size count=50 || {
		echo "Failed: file creation..."
		return 1
	}

	echo "Sending device1 failure"
	dgio_pool_mach_set_failure $fail_device1
	rc=$?
	if [ $rc -ne "0" ]
	then
		return $rc
	fi

	echo "dgmode test 1: Read after first failure"
	dgio_files_compare || {
		echo "Failed: read after first failure..."
		return 1
	}

	echo "Create a file after first failure"
	touch $file_in_dgmode1
	rc=$?
	if [ $rc -ne "0" ]
	then
		return $rc
	fi

	echo "dgmode test 2: IO and read after first failure"
	dgio_files_write dd bs=8821 count=5 seek=23 conv=notrunc || {
		echo "Failed: IO or read after first failure..."
		return 1
	}

	echo "Sending device2 failure"
	dgio_pool_mach_set_failure $fail_device2
	rc=$?
	if [ $rc -ne "0" ]
	then
		return $rc
	fi

	echo "Create a file after second failure"
	touch $file_in_dgmode2
	rc=$?
	if [ $rc -ne "0" ]
	then
		return $rc
	fi

	echo "dgmode test 3: Read after second failure"
	dgio_files_compare || {
		echo "Failed: read after second failure..."
		return 1
	}

	echo "dgmode test 4: IO and read after second failure"
	dgio_files_write dd bs=$unit_size count=60 || {
		echo "Failed: IO or read after second failure..."
		return 1
	}

	echo "dgmode test 5: Another IO after second failure"
	dgio_files_write dd bs=$unit_size count=40 || {
		echo "Failed: Another IO or read after second failure..."
		return 1
	}

	echo "Sending device3 failure"
	dgio_pool_mach_set_failure $fail_device3
	rc=$?
	if [ $rc -ne "0" ]
	then
		return $rc
	fi

	echo "dgmode test 6: Read after third failure"
	dgio_files_compare || {
		echo "Failed: read after third failure..."
		return 1
	}

	echo "Create a file after third failure"
	touch $file_in_dgmode3
	rc=$?
	if [ $rc -ne "0" ]
	then
		return $rc
	fi

	echo "dgmode test 7: IO and read after third failure"
	dgio_files_write dd bs=$unit_size count=50 || {
		echo "Failed: IO or read after third failure..."
		return 1
	}

	echo "dgmode test 8: Another IO and read after third failure"
	dgio_files_write dd bs=$unit_size count=10 || {
		echo "Failed: IO or read after third failure..."
		return 1
	}

	return $rc
}

main()
{
	NODE_UUID=`uuidgen`
	dgmode_sandbox="./sandbox"
	rc=0

	echo "debug_level $debug_level"
	echo "pattern $pattern"

	# Override this variable so as to use linux stob, for debugging
	if [ $debug_level -ne $DEBUG_LEVEL_OFF ]
	then
		MERO_STOB_DOMAIN="linux"
	fi
	echo "MERO_STOB_DOMAIN $MERO_STOB_DOMAIN"

	# Override these variables so as to test the ST framework without
	# involving mero service and m0t1fs
	if [ $debug_level -eq $DEBUG_LEVEL_TEST ]
	then
		MERO_TEST_LOGFILE="$dgmode_sandbox/log"
		MERO_M0T1FS_MOUNT_DIR="$dgmode_sandbox/tmp"
	fi

	# Start mero service and mount fs
	if [ $debug_level -ne $DEBUG_LEVEL_TEST ]
	then
		echo "About to start Mero service"
		mero_service start $stride $N $K $P
		if [ $? -ne "0" ]
		then
			echo "Failed to start Mero Service."
			return 1
		fi
		echo "mero service started"

		ios_eps=""
		for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
			ios_eps="$ios_eps -S ${lnet_nid}:${IOSEP[$i]}"
		done

		mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $N $K $P &>> $MERO_TEST_LOGFILE || {
			cat $MERO_TEST_LOGFILE
			return 1
		}
		mount
	fi

	# Perform dgio test by marking some devices as failed in between
	dgio_test || {
		echo "Failed: dgmode failed.."
		rc=1
	}

	# Unmount and stop mero service
	if [ $debug_level -ne $DEBUG_LEVEL_TEST ]
	then
		echo "unmounting and cleaning.."
		unmount_and_clean &>> $MERO_TEST_LOGFILE

		echo "About to stop Mero service"
		mero_service stop
		if [ $? -ne "0" ]
		then
			echo "Failed to stop Mero Service."
			return 1
		fi
	fi

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
