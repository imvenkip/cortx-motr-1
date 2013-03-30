mount_m0t1fs()
{
	if [ $# -ne 2 ]
	then
		echo "Usage: mount_m0t1fs <mount_dir> <unit_size (in Kbytes)>"
		return 1
	fi

	local m0t1fs_mount_dir=$1
	local stride_size=`expr $2 \* 1024`

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
	IOS_NAMES='"ios1"'
	IOS_OBJS="($IOS_NAMES, {3| (2, [1: $MDS_ENDPOINT], \"_\")})"
	for ((i=1; i < ${#EP[*]}; i++)); do
	    IOS_NAME="\"ios$((i+1))\""
	    IOS_NAMES="$IOS_NAMES, $IOS_NAME"
	    local ep=\"${server_nid}:${EP[$i]}\"
	    IOS_OBJ="($IOS_NAME, {3| (2, [1: $ep], \"_\")})"
	    IOS_OBJS="$IOS_OBJS, $IOS_OBJ"
	done

	local CONF="`cat <<EOF
[$((${#EP[*]} + 3)):
  ("prof", {1| ("fs")}),
  ("fs", {2| ((11, 22),
	      [3: "pool_width=$POOL_WIDTH",
		  "nr_data_units=$NR_DATA",
		  "unit_size=$stride_size"],
	      [$((${#EP[*]} + 1)): "mds", $IOS_NAMES])}),
  ("mds", {3| (1, [1: $MDS_ENDPOINT], "_")}),
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
	stride_size=$1
	io_counts=$2

	mount_m0t1fs $m0t1fs_mount_dir $stride_size || return 1

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

	# stripe unit (stride) size in K
	for stride_size in 4 12 20 28
	do
	    stripe_size=`expr $stride_size '*' $data_units`

	    # Small I/Os (KBs)
	    for io_size in 1 2 3 4 5 6 7 8
	    do
		io_size=`expr $io_size '*' $stripe_size`
		io_size=${io_size}K
		echo -n "Test: I/O for stripe = ${stripe_size}K," \
		     "bs = $io_size, count = 1... "
		bulkio_test $stride_size 1 &>> $MERO_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi
		show_write_speed

		# Multiple I/Os
		echo -n "Test: I/O for stripe = ${stripe_size}K," \
		     "bs = $io_size, count = 2... "
		bulkio_test $stride_size 2 &>> $MERO_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi
		show_write_speed
	    done

	    # Large I/Os (MBs)
	    for io_size in 1 2 4 5 8
	    do
		stripe_1M_mult=`expr 1024 / $stripe_size`
		[ $stripe_1M_mult -ge 1 ] || stripe_1M_mult=1
		io_size=`expr $io_size '*' $stripe_size '*' $stripe_1M_mult`
		io_size=${io_size}K
		echo -n "Test: I/O for stripe = ${stripe_size}K," \
		     "bs = $io_size, count = 1... "
		bulkio_test $stride_size 1 &>> $MERO_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi
		show_write_speed

		# Multiple I/Os
		echo -n "Test: I/O for stripe = ${stripe_size}K," \
		     "bs = $io_size, count = 2... "
		bulkio_test $stride_size 2 &>> $MERO_TEST_LOGFILE
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

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR 4 || return 1

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
	echo $cmd && $cmd || return 1
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
	echo -n "Running m0loop system tests"
	while true; do echo -n .; sleep 1; done &
	pid=$!
	m0loop_st_run &>> $MERO_TEST_LOGFILE
	status=$?
	exec 2> /dev/null; kill $pid; sleep 0.2; exec 2>&1
	[ $status -eq 0 ] || {
		unmount_and_clean
		return 1
	}
	echo " Done: PASSED."
}

file_creation_test()
{
	nr_files=$1
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR 4 &>> $MERO_TEST_LOGFILE || {
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
		echo "Creation Success." >> $MERO_TEST_LOGFILE
	else
		echo "Creation Failed." >> $MERO_TEST_LOGFILE
		return 1
	fi

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR 4 &>> $MERO_TEST_LOGFILE || {
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
		echo "Removal Success." >> $MERO_TEST_LOGFILE
		return 0
	else
		echo "Removal Failed." >> $MERO_TEST_LOGFILE
		return 1
	fi

	return 1
}

rmw_test()
{
	max_stride_size=32
	max_count=2

	for ((stride_size=4; stride_size<=$max_stride_size; stride_size*=2))
	do
		for io in 1 2 3 4 5 15 16 17 32 64 128
		do
			io_size=${io}K
			echo -n "IORMW Test: I/O for stride ="\
			     "${stride_size}K, bs = $io_size, count = 1... "
			bulkio_test $stride_size 1 &>> $MERO_TEST_LOGFILE ||
				return 1
			show_write_speed

			for((j=2; j<=$max_count; j*=2))
			do
			# Multiple I/O
			    echo -n "IORMW Test: I/O for stride ="\
			       "${stride_size}K, bs = $io_size, count = $j... "
			    bulkio_test $stride_size $j &>> \
			          $MERO_TEST_LOGFILE || return 1
			    show_write_speed
			done
		done
	done

	for ((stride_size=4; stride_size<=$max_stride_size; stride_size*=2))
	do
		# Small I/O
		for ((io=1; io<=$max_count; io*=2))
		do
			io_size=$i
			echo -n "IORMW Small IO Test: I/O for stride ="\
			     "${stride_size}K, bs = $io_size, count = 1... "
			bulkio_test $stride_size 1 &>> $MERO_TEST_LOGFILE ||
				return 1
			show_write_speed

			for((j=2; j<=$max_count; j*=2))
			do
				# Multiple I/O
				echo -n "IORMW Small IO Test: I/O for stride"\
				        "= ${stride_size}K, bs = $io_size,"\
				        "count = $j... "
				bulkio_test $stride_size $j &>> \
					$MERO_TEST_LOGFILE || return 1
				show_write_speed
			done
		done
	done

	for ((stride_size=4; stride_size<=$max_stride_size; stride_size*=2))
	do
		# Large I/O
		for ((io=8; io<=16; io*=2))
		do
			io_size=${io}M
			echo -n "IORMW Large IO Test: I/O for stride ="\
			     "${stride_size}K, bs = $io_size, count = 1... "
			bulkio_test $stride_size 1 &>> $MERO_TEST_LOGFILE ||
				return 1
			show_write_speed
			for((j=2; j<=$max_count; j*=2))
			do
				# Multiple I/O
				echo -n "IORMW Large IO Test: I/O for stride"\
				        "= ${stride_size}K, bs = $io_size,"\
				        "count = $j... "
				bulkio_test $stride_size $j &>> \
					$MERO_TEST_LOGFILE || return 1
				show_write_speed
			done
		done
	done

	for ((stride_size=4; stride_size<=$max_stride_size; stride_size*=2))
	do
		# I/O With large count
		for i in 10 20 30 40
		do
			io_size=1M
			echo -n "IORMW Large Count Test: I/O for stride ="\
			     "${stride_size}K, bs = $io_size, count = $i... "
			bulkio_test $stride_size $i &>> \
				$MERO_TEST_LOGFILE || return 1
			show_write_speed
		done

		for ((stride_size=4; stride_size<=$max_stride_size; stride_size*=2))
		do
			# I/O With large Block
			for i in 16M
			do
				io_size=$i
				echo -n "IORMW Large Block Test: I/O for stride ="\
				"${stride_size}K, bs = $io_size, count = 1... "
				bulkio_test $stride_size 1 &>> \
				$MERO_TEST_LOGFILE || return 1
				show_write_speed
			done

		done

	done

	echo "Test: IORMW: Success." | tee $MERO_TEST_LOGFILE

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
