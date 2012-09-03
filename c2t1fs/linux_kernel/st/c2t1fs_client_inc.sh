colibri_module=kcolibri

bulkio_test()
{
	local stride_size=`expr $1 '*' 1024`
	io_counts=$2

	c2t1fs_mount_dir=$COLIBRI_C2T1FS_MOUNT_DIR
	io_service=$COLIBRI_IOSERVICE_ENDPOINT
	local_input=$COLIBRI_C2T1FS_TEST_DIR/file1.data
	local_output=$COLIBRI_C2T1FS_TEST_DIR/file2.data
	c2t1fs_file=$c2t1fs_mount_dir/file.data

	# Create mount directory
	mkdir $c2t1fs_mount_dir
	if [ $? -ne "0" ]
	then
		echo "Failed to create mount directory."
		return 1
	fi

	rc=`lsmod | grep kcolibri | wc -l`
	if [ "x$rc" == "x0" ]; then
		echo "Failed to	mount c2t1fs file system. (kcolibri not loaded)"
		return 1
	fi

	echo "Mounting file system..."
	cmd="mount -t c2t1fs -o ios=$io_service,unit_size=$stride_size,\
pool_width=$pool_width,nr_data_units=$data_units,nr_parity_units=$parity_units \
none $c2t1fs_mount_dir"
	echo $cmd
	if ! $cmd
	then
		echo "Failed to	mount c2t1fs file system."
		return 1
	fi

	echo "Creating local input file of I/O size ..."
	local cmd="dd if=/dev/urandom of=$local_input bs=$io_size count=$io_counts"
	echo $cmd
	if ! $cmd
	then
		echo "Failed to create local input file."
		return 1
	fi

	echo "Writing data to c2t1fs file ..."
	cmd="dd if=$local_input of=$c2t1fs_file bs=$io_size count=$io_counts"
	echo $cmd
	if ! $cmd
	then
		echo "Failed to write data on c2t1fs file."
		return 1
	fi

	echo -n "Reading data from c2t1fs file "
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
	cmd="dd if=$c2t1fs_file of=$local_output bs=$io_size count=$io_counts"
	echo $cmd
	if ! $cmd
	then
		echo "Failed to read data from c2t1fs file."
		return 1
	fi

	echo "Comparing data written and data read from c2t1fs file ..."
	if ! cmp $local_input $local_output
	then
		echo -n "Failed: data written and data read from c2t1fs file "
		echo    "are not same."
		return 1
	fi

	echo "Successfully tested $io_counts I/O(s) of size $io_size."

	rm -f $c2t1fs_file

	echo "Unmounting file system ..."
	umount $c2t1fs_mount_dir &>/dev/null

	echo "Cleaning up test directory..."
	rm -rf $c2t1fs_mount_dir &>/dev/null

	# Removes the stob files created in stob domain since
	# there is no support for c2_stob_delete() and after unmounting
	# the client file system, from next mount, fids are generated
	# from same baseline which results in failure of cob_create fops.
	rm -rf $COLIBRI_STOB_PATH/o/*

	return 0
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

	# stripe unit (stride) size in K
	for stride_size in 4 12 20 28
	do
	    stripe_size=`expr $stride_size '*' $data_units`

	    # Small I/Os (KBs)
	    for io_size in 1 2 3 4 5 6 7 8
	    do
		io_size=`expr $io_size '*' $stripe_size`
		io_size=${io_size}K
		echo "Test: I/O for stripe_size = ${stripe_size}K," \
		     "io_size = $io_size, Number of I/Os = 1."
		bulkio_test $stride_size 1 &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi

		# Multiple I/Os
		echo "Test: I/O for stripe_size = ${stripe_size}K," \
		     "io_size = $io_size, Number of I/Os = 2."
		bulkio_test $stride_size 2 &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi
	    done

	    # Large I/Os (MBs)
	    for io_size in 1 2 4 5 8
	    do
		stripe_1M_mult=`expr 1024 / $stripe_size`
		[ $stripe_1M_mult -ge 1 ] || stripe_1M_mult=1
		io_size=`expr $io_size '*' $stripe_size '*' $stripe_1M_mult`
		io_size=${io_size}K
		echo "Test: I/O for stripe_size = ${stripe_size}K," \
		     "io_size = $io_size, Number of I/Os = 1."
		bulkio_test $stride_size 1 &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi

		# Multiple I/Os
		echo "Test: I/O for stripe_size = ${stripe_size}K," \
		     "io_size = $io_size, Number of I/Os = 2."
		bulkio_test $stride_size 2 &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi

	    done

	done
	echo "Test log available at $COLIBRI_TEST_LOGFILE."
	return 0
}

c2loop_st_run()
{
	echo "Load c2loop module... "
	cmd="insmod `dirname $0`/../../../build_kernel_modules/c2loop.ko"
	echo $cmd && $cmd || return 1
	echo "Mount c2t1fs file system..."
	mkdir $COLIBRI_C2T1FS_MOUNT_DIR
	cmd="mount -t c2t1fs -o ios=$COLIBRI_IOSERVICE_ENDPOINT,\
unit_size=4096,pool_width=$POOL_WIDTH,nr_data_units=1,nr_parity_units=1 \
none $COLIBRI_C2T1FS_MOUNT_DIR"
	echo $cmd && $cmd || return 1
	echo "Create c2t1fs file..."
	c2t1fs_file=$COLIBRI_C2T1FS_MOUNT_DIR/file.img
	cmd="dd if=/dev/zero of=$c2t1fs_file bs=1M count=20"
	echo $cmd && $cmd || return 1
	echo "Associate c2t1fs file c2loop device..."
	cmd="losetup /dev/c2loop0 $c2t1fs_file"
	echo $cmd && $cmd || return 1
	echo "Make ext4 fs on c2loop block device..."
	cmd="mkfs.ext4 -b 4096 /dev/c2loop0"
	echo $cmd && $cmd || return 1
	echo "Mount new ext4 fs..."
	ext4fs_mpoint=${COLIBRI_C2T1FS_MOUNT_DIR}-ext4fs
	cmd="mkdir $ext4fs_mpoint"
	echo $cmd && $cmd || return 1
	cmd="mount /dev/c2loop0 $ext4fs_mpoint"
	echo $cmd && $cmd || return 1
	echo "Write, read and compare some file..."
	local_file1=$COLIBRI_C2T1FS_TEST_DIR/file1
	cmd="dd if=/dev/urandom of=$local_file1 bs=1M count=2"
	echo $cmd && $cmd || return 1
	ext4fs_file=$ext4fs_mpoint/file
	cmd="dd if=$local_file1 of=$ext4fs_file bs=1M count=2"
	echo $cmd && $cmd || return 1
	local_file2=$COLIBRI_C2T1FS_TEST_DIR/file2
	cmd="dd if=$ext4fs_file of=$local_file2 bs=1M count=2"
	echo $cmd && $cmd || return 1
	cmd="cmp $local_file1 $local_file2"
	echo $cmd && $cmd || return 1
	echo "Clean up..."
	cmd="umount $ext4fs_mpoint"
	echo $cmd && $cmd || return 1
	cmd="losetup -d /dev/c2loop0"
	echo $cmd && $cmd || return 1
	cmd="umount $COLIBRI_C2T1FS_MOUNT_DIR"
	echo $cmd && $cmd || return 1
	rm -r $COLIBRI_C2T1FS_MOUNT_DIR $local_file1 $local_file2
	cmd="rmmod c2loop"
	echo $cmd && $cmd || return 1

	echo "Successfully passed c2loop ST tests."
	return 0
}

c2loop_st()
{
	echo -n "Running c2loop system tests"
	while true; do echo -n .; sleep 1; done &
	pid=$!
	c2loop_st_run &>> $COLIBRI_TEST_LOGFILE
	status=$?
	exec 2> /dev/null; kill $pid; sleep 0.2; exec 2>1
	[ $status -eq 0 ] || return 1
	echo " Done: PASSED."
}
