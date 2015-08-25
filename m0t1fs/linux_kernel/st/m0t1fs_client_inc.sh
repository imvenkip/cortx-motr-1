unit2id_map=(
	[    4]=1
	[    8]=2
	[   16]=3
	[   32]=4
	[   64]=5
	[  128]=6
	[  256]=7
	[  512]=8
	[ 1024]=9
	[ 2048]=10
	[ 4096]=11
	[ 8192]=12
	[16384]=13
	[32768]=14
)

touch_file()
{
	local file=$1
	local unitsz_id=${unit2id_map[$2]}

	if [ x$unitsz_id = x ]; then
		echo "Invalid unit_size: $2"
		return 1
	fi

	run "touch $file" &&
	run "setfattr -n lid -v $unitsz_id $file"

	return $?
}

mount_m0t1fs()
{
	if [ $# -ne 4 -a $# -ne 5 ]
	then
		echo "Usage: mount_m0t1fs <mount_dir> <N> <K> <p>"
		return 1
	fi

	local m0t1fs_mount_dir=$1
	local mountop=$5

	# Create mount directory
	sudo mkdir -p $m0t1fs_mount_dir || {
		echo "Failed to create mount directory."
		return 1
	}

	lsmod | grep -q m0mero || {
		echo "Failed to	mount m0t1fs file system. (m0mero not loaded)"
		return 1
	}

	echo "Mounting file system..."

	local cmd="sudo mount -t m0t1fs \
	    -o profile='$PROF_OPT',confd='$CONFD_EP',$mountop \
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
	for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
		# Removes the stob files created in stob domain since
		# there is no support for m0_stob_delete() and after
		# unmounting the client file system, from next mount,
		# fids are generated from same baseline which results
		# in failure of cob_create fops.
		local ios_index=`expr $i + 1`
		rm -rf $MERO_M0T1FS_TEST_DIR/d$ios_index/stobs/o/*
	done
}

unmount_m0t1fs()

{	if [ $# -ne 1 ]
	then
		echo "Usage: unmount_m0t1fs <mount_dir>"
		return 1
	fi

	local m0t1fs_mount_dir=$1
	echo "Unmounting file system ..."
	umount $m0t1fs_mount_dir &>/dev/null

	sleep 2

	echo "Cleaning up test directory..."
	rm -rf $m0t1fs_mount_dir &>/dev/null
}


bulkio_test()
{
	local fsname1="0:100125"
	local fsname2="0:600456"
	local fsname3="0:a0089b"
	local local_input=$MERO_M0T1FS_TEST_DIR/$fsname1
	local local_output=$MERO_M0T1FS_TEST_DIR/$fsname2
	local m0t1fs_mount_dir=$MERO_M0T1FS_MOUNT_DIR
	local m0t1fs_file=$m0t1fs_mount_dir/1:23456
	local unit_size=$1
	local io_counts=$2
	local mode=$3
	local mountopt=$4

	mount_m0t1fs $m0t1fs_mount_dir $NR_DATA $NR_PARITY $POOL_WIDTH "$mode,$mountopt" || return 1

	echo "Creating local input file of I/O size ..."
	run "dd if=/dev/urandom of=$local_input bs=$io_size count=$io_counts"
	if [ $? -ne 0 ]; then
		echo "Failed to create local input file."
		unmount_and_clean
		return 1
	fi

	echo "Writing data to m0t1fs file ..."
	touch_file $m0t1fs_file $unit_size &&
	run "dd if=$local_input of=$m0t1fs_file bs=$io_size count=$io_counts"
	if [ $? -ne 0 ]; then
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
	run "dd if=$m0t1fs_file of=$local_output bs=$io_size count=$io_counts"
	if [ $? -ne 0 ]; then
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

	run "rm -f $m0t1fs_file"

	unmount_and_clean

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

	echo "Test: io_combinations: (N,P,K) = ($2,$1,$3) $4 ..."

	pool_width=$1
	data_units=$2
	parity_units=$3
	mode=$4

	p=`expr $data_units + 2 '*' $parity_units`
	if [ $p -gt $pool_width ]
	then
		echo "Error: pool_width >=  data_units + 2 * parity_units."
		return 1
	fi

	# Since current I/O supports full stripe I/O,
	# I/O sizes are multiple of stripe size

	# stripe unit size in K
	for unit_size in 4 8 16 32 64 128 256 512 1024 2048 4096
	do
	    stripe_size=`expr $unit_size '*' $data_units`

	    for io_size in 1 2 4 8
	    do
		io_size=`expr $io_size '*' $stripe_size`
		io_size=${io_size}K
		echo -n "Test: I/O for stripe = ${stripe_size}K," \
		     "bs = $io_size, count = 1... "
		bulkio_test $unit_size 1 $mode "" &>> $MERO_TEST_LOGFILE
		if [ $? -ne "0" ]
		then
			return 1
		fi
		show_write_speed

		# Multiple I/Os
		echo -n "Test: I/O for stripe = ${stripe_size}K," \
		     "bs = $io_size, count = 2... "
		bulkio_test $unit_size 2 $mode "" &>> $MERO_TEST_LOGFILE
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

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH ||
		return 1

	echo "Create m0t1fs file..."
	m0t1fs_file=$MERO_M0T1FS_MOUNT_DIR/file.img
	touch_file $m0t1fs_file 1024 &&
	run "dd if=/dev/zero of=$m0t1fs_file bs=${NR_DATA}M count=20" ||
		return 1
	echo "Associate m0t1fs file m0loop device..."
	run "losetup /dev/m0loop0 $m0t1fs_file" || return 1
	echo "Make ext4 fs on m0loop block device..."
	run "mkfs.ext4 -b 4096 /dev/m0loop0" || return 1
	echo "Mount new ext4 fs..."
	ext4fs_mpoint=${MERO_M0T1FS_MOUNT_DIR}-ext4fs
	run "mkdir $ext4fs_mpoint" &&
	run "mount /dev/m0loop0 $ext4fs_mpoint" || return 1
	echo "Write, read and compare some file..."
	local_file1=$MERO_M0T1FS_TEST_DIR/file1
	local_file2=$MERO_M0T1FS_TEST_DIR/file2
	ext4fs_file=$ext4fs_mpoint/file
	run "dd if=/dev/urandom of=$local_file1 bs=${NR_DATA}M count=2" &&
	run "dd if=$local_file1 of=$ext4fs_file bs=${NR_DATA}M count=2" &&
	run "dd if=$ext4fs_file of=$local_file2 bs=${NR_DATA}M count=2" &&
	run "cmp $local_file1 $local_file2" || return 1
	echo "Clean up..."
	run "umount $ext4fs_mpoint" || return 1
	cmd="losetup -d /dev/m0loop0"
	# losetup -d may fail if not all m0loop buffers are flushed yet,
	# in this case we sleep for 5 secods and try again.
	echo $cmd && $cmd || { sleep 5 && $cmd; } || return 1
	run "umount $MERO_M0T1FS_MOUNT_DIR" || return 1
	run "rmmod m0loop" || return 1
	rm -r $MERO_M0T1FS_MOUNT_DIR $local_file1 $local_file2
	echo "Successfully passed m0loop ST tests."
	return 0
}

m0loop_st()
{
	echo -n "Running m0loop system tests"
	while true; do echo -n .; sleep 1; done &
	pid=$!
	m0loop_st_run
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
	local nr_files=$1
	local mode=$2
	local SOURCE_TXT=/tmp/source.txt
	local START_FID=15     # Added to skip root and other system fids.

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH "$mode,"verify"" || {
		return 1
	}
	for i in {a..z} {A..Z} ; do
		for c in `seq 1 4095`;
			do echo -n $i ;
		done;
		echo;
	done > $SOURCE_TXT

	NR_FILES=$[ $nr_files - $START_FID ]
	NR_FILES=$[ $NR_FILES * $NR_FILES ]
	echo "Test: Creating $NR_FILES files on m0t1fs..."
	for ((i=$START_FID; i<$nr_files; ++i)); do
		for ((j=$START_FID; j<$nr_files; ++j)); do
			touch $MERO_M0T1FS_MOUNT_DIR/$j:$i || break
			cp -v $SOURCE_TXT $MERO_M0T1FS_MOUNT_DIR/$j:$i || break
			dd if=$SOURCE_TXT of=$MERO_M0T1FS_MOUNT_DIR/$j:$i bs=8k conv=notrunc 2>/dev/null || break
			cp -v $MERO_M0T1FS_MOUNT_DIR/$j:$i /tmp/dest.txt || break
			diff -C 0 $SOURCE_TXT /tmp/dest.txt || {
				echo "file content differ!!!!!!!!!!!!!!!!! at $j:$i file. "
#				echo "Press Enter to go";
#				read;
				break;
			}
		done
	done
	unmount_and_clean
	echo -n "Test: file creation: "
	if (( $i == $nr_files && $j == $nr_files )); then
		echo "success."
	else
		echo "failed."
		return 1
	fi

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH $mode || {
		return 1
	}
	echo "Test: removing $NR_FILES files on m0t1fs..."
	for ((i=$START_FID; i<$nr_files; ++i)); do
		for ((j=$START_FID; j<$nr_files; ++j)); do
			echo "rm -vf $MERO_M0T1FS_MOUNT_DIR/$j:$i"
			rm -vf $MERO_M0T1FS_MOUNT_DIR/$j:$i || break
		done
	done

	unmount_and_clean
	echo -n "Test: file removal: "
	if (( $i == $nr_files && $j == $nr_files )); then
		echo "success."
	else
		echo "failed."
		return 1
	fi

	if [ "$mode" == "oostore" ]; then
		return 0
	fi

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH "verify" || {
                return 1
        }
	for ((i=0; i<$nr_files; ++i)); do
		#arbitrary file size. "1021" is a prime close to 1024.
		touch $MERO_M0T1FS_MOUNT_DIR/file$i || break
		dd if=$SOURCE_TXT of=/tmp/src bs=1021 count=`expr $i + 1` >/dev/null 2>&1 || break
		cp -v /tmp/src $MERO_M0T1FS_MOUNT_DIR/file$i      || break
		cp -v $MERO_M0T1FS_MOUNT_DIR/file$i /tmp/dest     || break
		diff -C 0 /tmp/src /tmp/dest || {
			echo "file content differ at file$i file. "
			break;
		}
	done

	if [ $i -eq $nr_files ]; then
		echo "success."
	else
		echo "failed."
		return 1
	fi

	for ((i=0; i<$nr_files; ++i)); do
		rm -vf $MERO_M0T1FS_MOUNT_DIR/file$i      || break
	done
	if [ $i -eq $nr_files ]; then
		echo "success."
	else
		echo "failed."
		return 1
	fi
	unmount_and_clean
	return 0
}

multi_client_test()
{
	local mount_dir_1=${MERO_M0T1FS_MOUNT_DIR}aa
	local mount_dir_2=${MERO_M0T1FS_MOUNT_DIR}bb
	local mount_dir_3=${MERO_M0T1FS_MOUNT_DIR}cc

	local rc

	mount_m0t1fs ${mount_dir_1} $NR_DATA $NR_PARITY $POOL_WIDTH "fid_start=65536" ||
		return 1
	mount | grep m0t1fs
	mount_m0t1fs ${mount_dir_2} $NR_DATA $NR_PARITY $POOL_WIDTH "fid_start=66536" || {
		unmount_m0t1fs ${mount_dir_1}
		return 1
	}
	mount | grep m0t1fs
	mount_m0t1fs ${mount_dir_3} $NR_DATA $NR_PARITY $POOL_WIDTH "fid_start=67536" || {
		unmount_m0t1fs ${mount_dir_1}
		unmount_m0t1fs ${mount_dir_2}
		return 1
	}
	echo "Three clients mounted:"
	mount | grep m0t1fs
	cp -av /bin/ls ${mount_dir_1}/obj1 || rc=1
	cp -av /bin/ls ${mount_dir_2}/obj2 || rc=1
	cp -av /bin/ls ${mount_dir_3}/obj3 || rc=1
	ls -liR ${mount_dir_1} || rc=1
	ls -liR ${mount_dir_2} || rc=1
	ls -liR ${mount_dir_3} || rc=1

	diff /bin/ls ${mount_dir_1}/obj1 || rc=1
	diff /bin/ls ${mount_dir_1}/obj2 || rc=1
	diff /bin/ls ${mount_dir_1}/obj3 || rc=1

	diff /bin/ls ${mount_dir_1}/obj1 || rc=1
	diff /bin/ls ${mount_dir_2}/obj2 || rc=1
	diff /bin/ls ${mount_dir_3}/obj3 || rc=1

	unmount_m0t1fs ${mount_dir_1}
	unmount_m0t1fs ${mount_dir_2}
	unmount_m0t1fs ${mount_dir_3}
	echo "First round done."
	mount_m0t1fs ${mount_dir_1} $NR_DATA $NR_PARITY $POOL_WIDTH "fid_start=65536" || {
		return 1
	}
	mount_m0t1fs ${mount_dir_2} $NR_DATA $NR_PARITY $POOL_WIDTH "fid_start=66536" || {
		unmount_m0t1fs ${mount_dir_1}
		return 1
	}
	mount_m0t1fs ${mount_dir_3} $NR_DATA $NR_PARITY $POOL_WIDTH "fid_start=67536" || {
		unmount_m0t1fs ${mount_dir_1}
		unmount_m0t1fs ${mount_dir_2}
		return 1
	}
	echo "Three clients mounted:"
	mount | grep m0t1fs
	ls -liR ${mount_dir_1} || rc=1
	ls -liR ${mount_dir_2} || rc=1
	ls -liR ${mount_dir_3} || rc=1

	diff /bin/ls ${mount_dir_1}/obj1 || rc=1
	diff /bin/ls ${mount_dir_2}/obj2 || rc=1
	diff /bin/ls ${mount_dir_3}/obj3 || rc=1

	unmount_m0t1fs ${mount_dir_1}
	unmount_m0t1fs ${mount_dir_2}
	unmount_m0t1fs ${mount_dir_3}
	echo "Second round done"
	mount | grep m0t1fs
	return $rc
}


rmw_test()
{
	local mode=$1
	local mountopt=$2

	echo "Test: RMW (with mountopt=\"${mountopt}\") ..."
	for unit_size in 4 8 16 32
	do
		for io in 1 2 3 4 5 15 16 17 32
		do
			io_size=${io}K
			echo -n "IORMW Test: I/O for unit ="\
			     "${unit_size}K, bs = $io_size, count = 1... "
			bulkio_test $unit_size 1 $mode $mountopt &>> $MERO_TEST_LOGFILE || return 1
			show_write_speed

			# Multiple I/O
			echo -n "IORMW Test: I/O for unit ="\
			   "${unit_size}K, bs = $io_size, count = 2... "
			bulkio_test $unit_size 2 $mode $mountopt &>> $MERO_TEST_LOGFILE || return 1
			show_write_speed
		done
	done

	echo "Test: IORMW: Success."

	return 0
}

###########################################
# This test is only valid in OOSTORE mode.
# Mero.cmd hash file by filename.
# In oostore mode directory support is not available,
# so creation of the file inside a sub directory test is removed.
###########################################
obf_test()
{
	local rc=0

	echo "Test: obf..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH "oostore" || {
		return 1
	}
	touch $MERO_M0T1FS_MOUNT_DIR/0:30000 || rc=1
	stat $MERO_M0T1FS_MOUNT_DIR/0:30000 || rc=1
	ls -la $MERO_M0T1FS_MOUNT_DIR/0:30000 || rc=1
	rm $MERO_M0T1FS_MOUNT_DIR/0:30000 || rc=1
	unmount_m0t1fs $MERO_M0T1FS_MOUNT_DIR
	if [ $rc -eq 0 ]; then
		echo "Success: Open-by-fid test."
	else
		echo "Failure: Open-by-fid test."
	fi
	return $rc
}

m0t1fs_crud()
{
	local rc=0

	local fsname1=$1
	local fsname2=$2
	local fsname3=$3
	local mode=$4
	touch $MERO_M0T1FS_MOUNT_DIR/$fsname1 || rc=1
	touch $MERO_M0T1FS_MOUNT_DIR/$fsname2 || rc=1
	touch $MERO_M0T1FS_MOUNT_DIR/$fsname3 || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname1 || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname2 || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname3 || rc=1
	chmod 567 $MERO_M0T1FS_MOUNT_DIR/$fsname1 || rc=1
	chmod 123 $MERO_M0T1FS_MOUNT_DIR/$fsname2 || rc=1
	chmod 345 $MERO_M0T1FS_MOUNT_DIR/$fsname3 || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname1 -c "%n: %a %s" || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname2 -c "%n: %a %s" || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname3 -c "%n: %a %s" || rc=1
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$fsname1 bs=4K   count=1 || rc=1
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$fsname2 bs=128K count=1 || rc=1
	touch_file $MERO_M0T1FS_MOUNT_DIR/$fsname3 64 &&
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$fsname3 bs=1M   count=1 || rc=1
	sync
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname1 -c "%n: %a %s" || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname2 -c "%n: %a %s" || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname3 -c "%n: %a %s" || rc=1
	echo 3 > /proc/sys/vm/drop_caches
	dd of=/dev/zero if=$MERO_M0T1FS_MOUNT_DIR/$fsname1 bs=4K   count=1 || rc=1
	dd of=/dev/zero if=$MERO_M0T1FS_MOUNT_DIR/$fsname2 bs=128K count=1 || rc=1
	dd of=/dev/zero if=$MERO_M0T1FS_MOUNT_DIR/$fsname3 bs=1M   count=1 || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname1 -c "%n: %a %s" || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname2 -c "%n: %a %s" || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname3 -c "%n: %a %s" || rc=1
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$fsname1 bs=4K   count=1 || rc=1
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$fsname2 bs=128K count=1 || rc=1
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$fsname3 bs=1M   count=1 || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname1 -c "%n: %a %s" || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname2 -c "%n: %a %s" || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname3 -c "%n: %a %s" || rc=1
	if [ "$mode" != "oostore" ]; then
		ls -l $MERO_M0T1FS_MOUNT_DIR || rc=1
	fi
	rm -f $MERO_M0T1FS_MOUNT_DIR/$fsname1 || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$fsname2 || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$fsname3 || rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname1 2>/dev/null && rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname2 2>/dev/null && rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/$fsname3 2>/dev/null && rc=1

	# Test for CROW
	dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$fsname1 bs=4K   count=1 || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$fsname1 || rc=1
	return $rc
}

m0t1fs_basic()
{
	local rc=0
	local fsname1="123456"
	local fsname2="890"
	local fsname3="xyz0"
	echo "Test: basic..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH || rc=1
	mount | grep m0t1fs
	m0t1fs_crud $fsname1 $fsname2 $fsname3 || rc=1
	unmount_and_clean
	return $rc
}

###############################################################
# The following readdir() test will send two readdir requests
# to mdservice 0, and then EOF is returned; So client readdir()
# switches to another mdservice, and does the same, until
# all mdservices are iterated.
# m0t1fs_large_dir mode fsname_prex
###############################################################
m0t1fs_large_dir()
{
	local rc=0
	local mode=$1
	local fsname_prex=$2
	local count=512

	echo "Test: larde_dir: mode=$1 fsname_prefix=$2..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH "$mode" || rc=1
	mount | grep m0t1fs                                 || rc=1
	for i in `seq 1 $count`; do
		touch $MERO_M0T1FS_MOUNT_DIR/$fsname_prex$i || rc=1
		stat  $MERO_M0T1FS_MOUNT_DIR/$fsname_prex$i -c "%n: %a %s" || rc=1
	done

	local dirs=`/bin/ls $MERO_M0T1FS_MOUNT_DIR -U`
	local dirs_count=`echo $dirs | wc -w`
	echo "readdir count: result $dirs_count, expected $count"
	if [ ! $dirs_count -eq $count ] ; then
		rc=1
	fi
	for i in `seq 1 $count`; do
		local match=`echo $dirs | grep -c "\<$fsname_prex$i\>"`
		if [ ! $match -eq 1 ] ; then
			echo "match $fsname_prex$i failed: $match"
			rc=1
		else
			rm -v $MERO_M0T1FS_MOUNT_DIR/$fsname_prex$i || rc=1
		fi
	done

	unmount_and_clean
	return $rc
}

m0t1fs_oostore_mode()
{
	local rc=0
	local fsname1="0:100125"
	local fsname2="0:600456"
	local fsname3="0:a0089b"
	local mode="oostore"

	echo "Test: oostore_mode..."
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH $mode || rc=1
	mount | grep m0t1fs                              || rc=1
	m0t1fs_crud $fsname1 $fsname2 $fsname3 $mode || rc=1
	touch $MERO_M0T1FS_MOUNT_DIR/123456 2>/dev/null && rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/123456 2>/dev/null && rc=1
	touch $MERO_M0T1FS_MOUNT_DIR/abcdef 2>/dev/null && rc=1
	stat  $MERO_M0T1FS_MOUNT_DIR/abcdef 2>/dev/null && rc=1
	unmount_and_clean

	return $rc
}

m0t1fs_oostore_mode_basic()
{
	local rc=0
	local fsname1="0:100125"
	local fsname2="0:100321"
	local file="1:23456"
	local file1="3:23457"
	local file2="2:23458"
	local file3="2:23459"
	local file4="3:23459"

	local SOURCE_TXT=/tmp/source.txt

	echo "Test: oostore_mode_basic..."

	for i in {a..z} {A..Z} ; do
		for c in `seq 1 4095`;
			do echo -n $i ;
		done;
		echo;
	done > $SOURCE_TXT

	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH "oostore" || rc=1
	mount | grep m0t1fs                                 || rc=1
	cp -v $SOURCE_TXT $MERO_M0T1FS_MOUNT_DIR/$fsname1   || rc=1
	cat $MERO_M0T1FS_MOUNT_DIR/$fsname1 > /tmp/$fsname1 || rc=1
	# sleep two seconds, so the {a,c,m}time are different
	sleep 2
	cp -v $SOURCE_TXT $MERO_M0T1FS_MOUNT_DIR/$fsname2   || rc=1
	cat $MERO_M0T1FS_MOUNT_DIR/$fsname2 > /tmp/$fsname2 || rc=1
	diff $SOURCE_TXT /tmp/$fsname1                      || rc=1
	diff $SOURCE_TXT /tmp/$fsname2                      || rc=1

	chmod 123 $MERO_M0T1FS_MOUNT_DIR/$fsname1           || rc=1
	chmod 456 $MERO_M0T1FS_MOUNT_DIR/$fsname2           || rc=1

	cat $MERO_M0T1FS_MOUNT_DIR/$fsname1 > /dev/null     || rc=1
	cat $MERO_M0T1FS_MOUNT_DIR/$fsname2 > /dev/null     || rc=1

	stat $MERO_M0T1FS_MOUNT_DIR/$fsname1                || rc=1
	stat $MERO_M0T1FS_MOUNT_DIR/$fsname2                || rc=1

	rm -f $MERO_M0T1FS_MOUNT_DIR/$fsname1               || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$fsname2               || rc=1

	# Recreate and remove the file
	touch $MERO_M0T1FS_MOUNT_DIR/$fsname1               || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$fsname1               || rc=1

	cp -v $SOURCE_TXT $MERO_M0T1FS_MOUNT_DIR/$file                   || rc=1
	cp -v $MERO_M0T1FS_MOUNT_DIR/$file $MERO_M0T1FS_MOUNT_DIR/$file1 || rc=1
	cp -v $MERO_M0T1FS_MOUNT_DIR/$file $MERO_M0T1FS_MOUNT_DIR/$file2 || rc=1
	cp -v $MERO_M0T1FS_MOUNT_DIR/$file $MERO_M0T1FS_MOUNT_DIR/$file3 || rc=1
	cp -v $MERO_M0T1FS_MOUNT_DIR/$file $MERO_M0T1FS_MOUNT_DIR/$file4 || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$file                               || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$file1                              || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$file2                              || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$file3                              || rc=1
	rm -f $MERO_M0T1FS_MOUNT_DIR/$file4                              || rc=1

	unmount_and_clean

	return $rc
}

m0t1fs_parallel_io_test()
{
	N=2
	K=1
	P=4
	LAYOUT_ID=8 #512K
	UNIT_SIZE=512 # KB
	NR_UNITS=3 # Write that many units on disk at a time.
	echo "Parallel IO test"
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH "$mode" || rc=1
	mount | grep m0t1fs                              || rc=1
	echo "Create files"
	for i in 0:100{0..4}000; do
		touch $MERO_M0T1FS_MOUNT_DIR/$i          || rc=1
	done

	for i in 0:100{0..4}000; do
		setfattr -n lid -v $LAYOUT_ID $MERO_M0T1FS_MOUNT_DIR/$i   || rc=1
	done
	echo "Spawn parallel dd's"
	for i in `seq 1 4`
	do
		fid="0:100"$i"000"
		dd if=/dev/zero of=$MERO_M0T1FS_MOUNT_DIR/$fid \
			bs=$((P * NR_UNITS * UNIT_SIZE / 1024))M count=100 &
		dd_pid[$i]=$!
	done
	echo "Wait for IO to complete"
	for i in `seq 1 4`
	do
		echo ${dd_pid[$i]}
		wait ${dd_pid[$i]}
	done
	for i in 0:100{0..4}000; do
		rm -f $MERO_M0T1FS_MOUNT_DIR/$i          || rc=1
	done
	unmount_and_clean

	return $rc
}

m0t1fs_system_tests()
{
	file_creation_test $MAX_NR_FILES || {
		echo "Failed: File creation test failed."
		return 1
	}

	file_creation_test $MAX_NR_FILES "oostore" || {
		echo "Failed: File creation test failed."
		return 1
	}

	m0t1fs_basic || {
		echo "Failed: m0t1fs basic test failed."
		return 1
	}
	m0t1fs_parallel_io_test || {
		echo "Failed: m0t1fs parallel io test failed."
		return 1
	}
	m0t1fs_oostore_mode || {
		echo "Failed: m0t1fs oostore mode test failed."
		return 1
	}

	m0t1fs_oostore_mode_basic || {
		echo "Failed: m0t1fs oostore mode basic test failed."
		return 1
	}

	m0t1fs_large_dir "" "mero-testfile-" || {
		echo "Failed: m0t1fs large dir test failed."
		return 1
	}

	obf_test || {
		echo "Failed: Open-by-fid test failed."
		return 1
	}

	io_combinations $POOL_WIDTH $NR_DATA $NR_PARITY || {
		echo "Failed: IO failed.."
		return 1
	}

	io_combinations $POOL_WIDTH $NR_DATA $NR_PARITY "oostore"|| {
		echo "Failed: IO failed oostore mode.."
		return 1
	}

	rmw_test "" "" || {
		echo "Failed: IO-RMW failed.."
		return 1
	}

	rmw_test "" "verify" || {
		echo "Failed: IO-RMW failed.."
		return 1
	}

	#m0loop_st || return 1

	return 0
}
