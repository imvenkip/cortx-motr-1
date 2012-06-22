colibri_module=kcolibri

bulkio_test()
{
	local stripe_size=`expr $1 '*' 1024`
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
	cmd="mount -t c2t1fs -o ios=$io_service,unit_size=$stripe_size,\
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

	echo "Reading data from c2t1fs file ..."
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
		echo "Failed, data written and data read from c2t1fs file" \
		     "are not same."
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
	# This test runs for various stripe_size values

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

	# stripe size is in K
	for stripe_size in 4 12 20 28
	do
	    # Small I/Os KBs
	    for io_size in 1 2 3 4 5 6 7 8
	    do
		io_size=`expr $io_size '*' $stripe_size`
		io_size=${io_size}K
		echo "Test: I/O for stripe_size = ${stripe_size}K," \
		     "io_size = $io_size, Number of I/Os = 1."
		bulkio_test $stripe_size 1 &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi

		# Multiple I/Os
		echo "Test: I/O for stripe_size = ${stripe_size}K," \
		     "io_size = $io_size, Number of I/Os = 2."
		bulkio_test $stripe_size 2 &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi
	    done

	    # Large I/Os MBs
	    for io_size in 1 2 4 5 8
	    do
		stripe_mult=`expr 1024 / $stripe_size`
		[ $stripe_mult -ge 1 ] || stripe_mult=1
		io_size=`expr $io_size '*' $stripe_size '*' $stripe_mult`
		io_size=${io_size}K
		echo "Test: I/O for stripe_size = ${stripe_size}K," \
		     "io_size = $io_size, Number of I/Os = 1."
		bulkio_test $stripe_size 1 &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi

		# Multiple I/Os
		echo "Test: I/O for stripe_size = ${stripe_size}K," \
		     "io_size = $io_size, Number of I/Os = 2."
		bulkio_test $stripe_size 2 &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi

	    done

	done
	echo "Test log available at $COLIBRI_TEST_LOGFILE."
	return 0
}
