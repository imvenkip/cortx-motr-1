colibri_module=kcolibri

unload_kernel_module()
{
	rmmod $colibri_module.ko
	if [ $? -ne "0" ]
	then
	    echo "Failed:failed to remove $colibri_module."
	    return 1
	fi
}

load_kernel_module()
{
	colibri_module_path=$COLIBRI_CORE_ROOT/build_kernel_modules
	lsmod | grep $colibri_module &> /dev/null
	if [ $? -eq "0" ]
	then
		echo "Module $colibri_module already present."
		echo "Removing existing module to for clean test."
		unload_kernel_module || return $?
	fi

	insmod $colibri_module_path/$colibri_module.ko \
	       c2_trace_immediate_mask=$COLIBRI_MODULE_TRACE_MASK \
	       local_addr=$COLIBRI_C2T1FS_ENDPOINT
	if [ $? -ne "0" ]
	then
		echo "Failed to insert module \
		      $colibri_module_path/$colibri_module.ko"
		return 1
	fi
}

bulkio_test()
{
	stripe_size=$1
	io_size=$2
	io_counts=$3
	pool_width=$4
	data_units=$5
	parity_units=$6

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
	if [ "x$rc" != "x0" ]; then
		echo "Mounting file system..."
		echo "Mount options: -t c2t1fs -o ios=$io_service,unit_size=$stripe_size,"            \
		     "pool_width=$pool_width,nr_data_units=$data_units,nr_parity_units=$parity_units" \
		     " none $c2t1fs_mount_dir"
		mount -t c2t1fs -o ios=$io_service,unit_size=$stripe_size,\
pool_width=$pool_width,nr_data_units=$data_units,nr_parity_units=$parity_units \
none $c2t1fs_mount_dir
		if [ $? -ne "0" ]
		then
			echo "Failed to	mount c2t1fs file system."
			unload_kernel_module
			return 1
		fi
		echo "Successfully mounted c2t1fs file system."
	else
		echo "Failed to	mount c2t1fs file system. (kcolibri not loaded)"
		return 1
	fi

	echo "Creating local input file of I/O size ..."
	dd if=/dev/urandom of=$local_input bs=$io_size count=$io_counts
	if [ $? -ne "0" ]
	then
		echo "Failed to create local input file."
		umount $c2t1fs_mount_dir
		unload_kernel_module
		return 1
	fi
	echo "Created local input file of I/O size."

	echo "Writing data of $io_counts*$io_size c2t1fs file ..."
	dd if=$local_input of=$c2t1fs_file bs=$io_size count=$io_counts
	if [ $? -ne "0" ]
	then
		echo "Failed to write data on c2t1fs file."
		umount $c2t1fs_mount_dir
		unload_kernel_module
		return 1
	fi
	echo "Successfully wrote data of $io_counts*$io_size to c2t1fs file."

	echo "Reading data of $io_counts*$io_size from c2t1fs file ..."
	dd if=$c2t1fs_file of=$local_output bs=$io_size count=$io_counts
	if [ $? -ne "0" ]
	then
		echo "Failed to read data from c2t1fs file."
		umount $c2t1fs_mount_dir
		unload_kernel_module
		return 1
	fi
	echo "Successfully read data of $io_counts*$io_size from c2t1fs file."

	echo "Comparing data written and data read from c2t1fs file ..."
	diff $local_input $local_output &> /dev/null
	if [ $? -ne "0" ]
	then
		echo "Failed, data written and data read from c2t1fs file" \
		     "are not same."
		umount $c2t1fs_mount_dir
		unload_kernel_module
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
	for stripe_size_multiplyer in 4 12 20 28
	do
	    # Small I/Os KBs
	    for ((io_size_multiplyer=1; io_size_multiplyer<=8; \
		  io_size_multiplyer++))
	    do
		stripe_size=`expr $stripe_size_multiplyer '*' 1024`
		io_size=`expr $io_size_multiplyer '*' $stripe_size`
		# I/O size in K
		io_size=`expr $io_size / 1024`K
		echo "Test: I/O for stripe_size = $stripe_size," \
		     "io_size = $io_size, Number of I/Os = 1."
		bulkio_test $stripe_size $io_size 1 $pool_width $data_units \
			    $parity_units &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi

		# Multiple I/Os
		echo "Test: I/O for stripe_size = $stripe_size," \
		     "io_size = $io_size, Number of I/Os = 2."
		bulkio_test $stripe_size $io_size 2 $pool_width $data_units \
			    $parity_units &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi
	    done

	    one_mb=`expr 1024 '*' 1024`
	    size_multiplyer=256
	    # Large I/Os MBs
	    for ((io_size_multiplyer=1; io_size_multiplyer<=8; \
		  io_size_multiplyer++))
	    do
		#Making I/O size in MBs by multiplying with size_multiplyer
		stripe_size=`expr $stripe_size_multiplyer '*' 1024`
		io_size=`expr $io_size_multiplyer '*' $stripe_size '*' \
						      $size_multiplyer`
		# I/O size in M
		io_size=`expr $io_size / $one_mb`M
		echo "Test: I/O for stripe_size = $stripe_size," \
		     "io_size = $io_size, Number of I/Os = 1."
		bulkio_test $stripe_size $io_size 1 $pool_width $data_units \
			    $parity_units &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi

		# Multiple I/Os
		echo "Test: I/O for stripe_size = $stripe_size," \
		     "io_size = $io_size, Number of I/Os = 2."
		bulkio_test $stripe_size $io_size 2 $pool_width $data_units \
			    $parity_units &>> $COLIBRI_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi

	    done

	done
	echo "Test log available at $COLIBRI_TEST_LOGFILE."
	return 0
}
