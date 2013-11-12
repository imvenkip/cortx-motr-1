mount_m0t1fs()
{
	if [ $# -ne 5 ]
	then
		echo "Usage: mount_m0t1fs <mount_dir> <unit_size (in Kbytes)> <N> <K> <p>"
		return 1
	fi

	local m0t1fs_mount_dir=$1
	local unit_size=`expr $2 \* 1024`
	local N=$3
	local K=$4
	local P=$5

	# Create mount directory
	sudo mkdir -p $m0t1fs_mount_dir || {
		echo "Failed to create mount directory."
		return 1
	}

	lsmod | grep -q m0mero || {
		echo "Failed to	mount m0t1fs file system. (m0mero not loaded)"
		return 1
	}

	# prepare configuration data
	MDS_ENDPOINT="\"${server_nid}:${EP[0]}\""
	RMS_ENDPOINT="\"${server_nid}:${EP[0]}\""
	for ((i=1; i < ${#EP[*]}; i++)); do
	    IOS_NAME="\"ios$i\""

	    if ((i == 1)); then
	        IOS_NAMES="$IOS_NAME"
	    else
	        IOS_NAMES="$IOS_NAMES, $IOS_NAME"
	    fi

	    local ep=\"${server_nid}:${EP[$i]}\"
	    IOS_OBJ="($IOS_NAME, {3| (2, [1: $ep], \"_\")})"
	    if ((i == 1)); then
	        IOS_OBJS="$IOS_OBJ"
	    else
		IOS_OBJS="$IOS_OBJS, $IOS_OBJ"
	    fi
	done

	local CONF="`cat <<EOF
[$((${#EP[*]} + 3)):
  ("prof", {1| ("fs")}),
  ("fs", {2| ((11, 22),
	      [4: "pool_width=$P",
		  "nr_data_units=$N",
		  "nr_parity_units=$K",
		  "unit_size=$unit_size"],
	      [$((${#EP[*]} + 1)): "mds", "dlm", $IOS_NAMES])}),
  ("mds", {3| (1, [1: $MDS_ENDPOINT], "_")}),
  ("dlm", {3| (4, [1: $RMS_ENDPOINT], "_")}),
  $IOS_OBJS]
EOF`"

	echo "Mounting file system..."

	cmd="sudo mount -t m0t1fs -o profile=prof,local_conf='$CONF' \
	    none $m0t1fs_mount_dir"
	echo $cmd
	eval $cmd || {
		echo "Failed to mount m0t1fs file system."
		return 1
	}
}

unmount_and_clean()
{
	m0t1fs_mount_dir=$MERO_M0T1FS_MOUNT_DIR
	echo "Unmounting file system ..."
	umount $m0t1fs_mount_dir &>/dev/null

	sleep 2

	echo "Cleaning up test directory..."
	rm -rf $m0t1fs_mount_dir &>/dev/null

	local i=0
	for ((i=0; i < ${#EP[*]}; i++)) ; do
		# Removes the stob files created in stob domain since
		# there is no support for m0_stob_delete() and after
		# unmounting the client file system, from next mount,
		# fids are generated from same baseline which results
		# in failure of cob_create fops.
		rm -rf $MERO_M0T1FS_TEST_DIR/d$i/stobs/o/*
	done
}

bulkio_test()
{
	local_input=$MERO_M0T1FS_TEST_DIR/file1.data
	local_output=$MERO_M0T1FS_TEST_DIR/file2.data
	m0t1fs_mount_dir=$MERO_M0T1FS_MOUNT_DIR
	m0t1fs_file=$m0t1fs_mount_dir/file.data
	unit_size=$1
	io_counts=$2

	mount_m0t1fs $m0t1fs_mount_dir $unit_size $NR_DATA $NR_PARITY $POOL_WIDTH || return 1

	echo "Creating local input file of I/O size ..."
	local cmd="dd if=/dev/urandom of=$local_input bs=$io_size count=$io_counts"
	echo $cmd
	if ! $cmd
	then
		echo "Failed to create local input file."
		unmount_and_clean
		return 1
	fi

	echo "Writing data to m0t1fs file ..."
	cmd="dd if=$local_input of=$m0t1fs_file bs=$io_size count=$io_counts"
	echo $cmd
	if ! $cmd
	then
		echo "Failed to write data on m0t1fs file."
		unmount_and_clean
		return 1
	fi

	echo -n "Reading data from m0t1fs file "
	if [ $io_counts -gt 1 ]; then
		trigger=`expr \( ${trigger:-0} + 1 \) % 2`
		# run 50% of such tests with different io_size
		if [ $trigger -eq 0 ]; then
			echo -n "with different io_size "
			io_suffix=${io_size//[^KM]}
			io_size=`expr ${io_size%[KM]} '*' $io_counts`$io_suffix
			io_counts=1
		fi
	fi
	echo "..."
	cmd="dd if=$m0t1fs_file of=$local_output bs=$io_size count=$io_counts"
	echo $cmd
	if ! $cmd
	then
		echo "Failed to read data from m0t1fs file."
		unmount_and_clean
		return 1
	fi

	echo "Comparing data written and data read from m0t1fs file ..."
	if ! cmp $local_input $local_output
	then
		echo -n "Failed: data written and data read from m0t1fs file "
		echo    "are not same."
		unmount_and_clean
		return 1
	fi

	echo "Successfully tested $io_counts I/O(s) of size $io_size."

	rm -f $m0t1fs_file

	unmount_and_clean &>> $MERO_TEST_LOGFILE

	return 0
}

show_write_speed()
{
	cat $MERO_TEST_LOGFILE | grep copied | tail -2 | head -1 | \
		awk -F, '{print $3}'
}

io_combinations()
{
	# This test runs for various stripe unit size values

	echo "Storage conf: pool_width=$1, data_units=$2, parity_units=$3"

	pool_width=$1
	data_units=$2
	parity_units=$3

	p=`expr $data_units + 2 '*' $parity_units`
	if [ $p -gt $pool_width ]
	then
		echo "Error: pool_width >=  data_units + 2 * parity_units."
		return 1
	fi

	# Since current I/O supports full stripe I/O,
	# I/O sizes are multiple of stripe size

	# stripe unit size in K
	for unit_size in 4 8 16 32 64 128 256 512 1024
	do
	    stripe_size=`expr $unit_size '*' $data_units`

	    for io_size in 1 2 4 8
	    do
		io_size=`expr $io_size '*' $stripe_size`
		io_size=${io_size}K
		echo -n "Test: I/O for stripe = ${stripe_size}K," \
		     "bs = $io_size, count = 1... "
		bulkio_test $unit_size 1 &>> $MERO_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi
		show_write_speed

		# Multiple I/Os
		echo -n "Test: I/O for stripe = ${stripe_size}K," \
		     "bs = $io_size, count = 2... "
		bulkio_test $unit_size 2 &>> $MERO_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi
		show_write_speed
	    done

	done
	return 0
}

m0loop_st_run()
{
	echo "Load m0loop module... "
	cmd="insmod `dirname $0`/../../../mero/m0loop.ko"
	echo $cmd && $cmd || return 1

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR 1024 $NR_DATA $NR_PARITY $POOL_WIDTH || return 1

	echo "Create m0t1fs file..."
	m0t1fs_file=$MERO_M0T1FS_MOUNT_DIR/file.img
	cmd="dd if=/dev/zero of=$m0t1fs_file bs=${NR_DATA}M count=20"
	echo $cmd && $cmd || return 1
	echo "Associate m0t1fs file m0loop device..."
	cmd="losetup /dev/m0loop0 $m0t1fs_file"
	echo $cmd && $cmd || return 1
	echo "Make ext4 fs on m0loop block device..."
	cmd="mkfs.ext4 -b 4096 /dev/m0loop0"
	echo $cmd && $cmd || return 1
	echo "Mount new ext4 fs..."
	ext4fs_mpoint=${MERO_M0T1FS_MOUNT_DIR}-ext4fs
	cmd="mkdir $ext4fs_mpoint"
	echo $cmd && $cmd || return 1
	cmd="mount /dev/m0loop0 $ext4fs_mpoint"
	echo $cmd && $cmd || return 1
	echo "Write, read and compare some file..."
	local_file1=$MERO_M0T1FS_TEST_DIR/file1
	cmd="dd if=/dev/urandom of=$local_file1 bs=${NR_DATA}M count=2"
	echo $cmd && $cmd || return 1
	ext4fs_file=$ext4fs_mpoint/file
	cmd="dd if=$local_file1 of=$ext4fs_file bs=${NR_DATA}M count=2"
	echo $cmd && $cmd || return 1
	local_file2=$MERO_M0T1FS_TEST_DIR/file2
	cmd="dd if=$ext4fs_file of=$local_file2 bs=${NR_DATA}M count=2"
	echo $cmd && $cmd || return 1
	cmd="cmp $local_file1 $local_file2"
	echo $cmd && $cmd || return 1
	echo "Clean up..."
	cmd="umount $ext4fs_mpoint"
	echo $cmd && $cmd || return 1
	cmd="losetup -d /dev/m0loop0"
	# losetup -d may fail if not all m0loop buffers are flushed yet,
	# in this case we sleep for 5 secods and try again.
	echo $cmd && $cmd || { sleep 5 && $cmd; } || return 1
	cmd="umount $MERO_M0T1FS_MOUNT_DIR"
	echo $cmd && $cmd || return 1
	cmd="rmmod m0loop"
	echo $cmd && $cmd || return 1
	rm -r $MERO_M0T1FS_MOUNT_DIR $local_file1 $local_file2
	echo "Successfully passed m0loop ST tests."
	return 0
}

m0loop_st()
{
	echo -n "Running m0loop system tests" | tee -a $MERO_TEST_LOGFILE
	echo >> $MERO_TEST_LOGFILE
	while true; do echo -n .; sleep 1; done &
	pid=$!
	m0loop_st_run &>> $MERO_TEST_LOGFILE
	status=$?
	exec 2> /dev/null; kill $pid; sleep 0.2; exec 2>&1
	[ $status -eq 0 ] || {
		echo " FAILED!"
		unmount_and_clean
		return 1
	}
	echo " Done: PASSED."
}

file_creation_test()
{
	nr_files=$1
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR 4 $NR_DATA $NR_PARITY $POOL_WIDTH &>> $MERO_TEST_LOGFILE || {
		cat $MERO_TEST_LOGFILE
		return 1
	}
	echo "Test: Creating $nr_files files on m0t1fs..." \
	    >> $MERO_TEST_LOGFILE
	for ((i=0; i<$nr_files; ++i)); do
		touch $MERO_M0T1FS_MOUNT_DIR/file$i >> $MERO_TEST_LOGFILE || break
		ls -li $MERO_M0T1FS_MOUNT_DIR/file$i >> $MERO_TEST_LOGFILE || break
	done
	unmount_and_clean &>> $MERO_TEST_LOGFILE
	echo -n "Test: file creation: " >> $MERO_TEST_LOGFILE
	if [ $i -eq $nr_files ]; then
		echo "success." >> $MERO_TEST_LOGFILE
	else
		echo "failed." >> $MERO_TEST_LOGFILE
		return 1
	fi

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR 4 $NR_DATA $NR_PARITY $POOL_WIDTH &>> $MERO_TEST_LOGFILE || {
		cat $MERO_TEST_LOGFILE
		return 1
	}
	echo "Test: removing $nr_files files on m0t1fs..." \
	    >> $MERO_TEST_LOGFILE
	for ((i=0; i<$nr_files; ++i)); do
		rm -vf $MERO_M0T1FS_MOUNT_DIR/file$i >> $MERO_TEST_LOGFILE || break
	done

	unmount_and_clean &>> $MERO_TEST_LOGFILE
	echo -n "Test: file removal: " >> $MERO_TEST_LOGFILE
	if [ $i -eq $nr_files ]; then
		echo "success." >> $MERO_TEST_LOGFILE
	else
		echo "failed." >> $MERO_TEST_LOGFILE
		return 1
	fi

	return 0
}

rmw_test()
{
	for unit_size in 4 8 16 32
	do
		for io in 1 2 3 4 5 15 16 17 32
		do
			io_size=${io}K
			echo -n "IORMW Test: I/O for unit ="\
			     "${unit_size}K, bs = $io_size, count = 1... "
			bulkio_test $unit_size 1 &>> $MERO_TEST_LOGFILE ||
				return 1
			show_write_speed

			# Multiple I/O
			echo -n "IORMW Test: I/O for unit ="\
			   "${unit_size}K, bs = $io_size, count = 2... "
			bulkio_test $unit_size 2 &>> \
			      $MERO_TEST_LOGFILE || return 1
			show_write_speed
		done
	done

	echo "Test: IORMW: Success." | tee -a $MERO_TEST_LOGFILE

	return 0
}

m0t1fs_system_tests()
{
	file_creation_test $MAX_NR_FILES || {
                echo "Failed: File creation test failed."
		return 1
	}

	io_combinations $POOL_WIDTH $NR_DATA $NR_PARITY || {
		echo "Failed: IO failed.."
		return 1
	}

	rmw_test || {
		echo "Failed: IO-RMW failed.."
		return 1
	}

	m0loop_st || return 1

	return 0
}
