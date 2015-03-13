conf_ios_device_setup()
{
	local _DDEV_ID=$1
	local _id_count=$2
	local id_count_out=$3
	local _ids="$4"
	local ids_out=$5
	local ddev_id="(0x6400000000000001, $_DDEV_ID)"
	local ddisk_id="(0x6b00000000000001, $_DDEV_ID)"
	local ddiskv_id="(0x6a00000000000001, $_DDEV_ID)"

	if (($_id_count == 0))
	then
		eval $ids_out="'$ddev_id'"
	else
		eval $ids_out="'$_ids, $ddev_id'"
	fi

	eval $id_count_out=`expr $_id_count + 1`

	#dev conf obj
	local ddev_obj="{0x64| (($ddev_id), 4, 1, 4096, 596000000000, 3, 4, \"`pwd`/$_DDEV_ID$ddisk\")}"
	#disk conf obj
	local ddisk_obj="{0x6b| (($ddisk_id), $ddev_id)}"
	if (($NR_DISK_FIDS == 0))
	then
		DISK_FIDS="$ddisk_id"
	else
		DISK_FIDS="$DISK_FIDS, $ddisk_id"
	fi
	NR_DISK_FIDS=`expr $NR_DISK_FIDS + 1`

	#diskv conf obj
	local ddiskv_obj="{0x6a| (($ddiskv_id), $ddisk_id, [0])}"
	if (($NR_DISKV_FIDS == 0))
	then
		DISKV_FIDS="$ddiskv_id"
	else
		DISKV_FIDS="$DISKV_FIDS, $ddiskv_id"
	fi
	NR_DISKV_FIDS=`expr $NR_DISKV_FIDS + 1`

	if (($NR_IOS_DEVS == 0))
	then
		IOS_DEVS="$ddev_obj, \n $ddisk_obj, \n $ddiskv_obj"
	else
		IOS_DEVS="$IOS_DEVS, \n $ddev_obj, \n $ddisk_obj, \n $ddiskv_obj"
	fi
	NR_IOS_DEVS=`expr $NR_IOS_DEVS + 3`
}

mkiosloopdevs()
{
	local ios=$1
	local nr_devs=$2
	local dir=$3
	local adisk=addb-disk.img
	local ddisk=data-disk.img
	local dev_end=0
	local adev_id="(0x6400000000000001, $ADEV_ID)"
	local ids=""
	local id_count=0

	cd $dir || return 1

	dd if=/dev/zero of=$ADEV_ID$adisk bs=1M seek=1M count=1 || return 1
	cat > disks.conf << EOF
Device:
   - id: $ADEV_ID
     filename: `pwd`/$ADEV_ID$adisk
EOF

	ADEV_ID=`expr $ADEV_ID + 1`
	dev_end=$(($DDEV_ID + $nr_devs))
	for (( ; DDEV_ID < $dev_end; DDEV_ID++)) ; do
		conf_ios_device_setup $DDEV_ID $id_count id_count "$ids" ids

		dd if=/dev/zero of=$DDEV_ID$ddisk bs=1M seek=1M count=1 ||
			return 1
		cat >> disks.conf << EOF
   - id: $DDEV_ID
     filename: `pwd`/$DDEV_ID$ddisk
EOF
	done

	IOS_DEV_IDS[`expr $ios - 1`]="[$id_count: $ids]"

	cd - >/dev/null
	return $?
}

conf_mds_devices_setup()
{
	local mds=$1
	local adev_id="(0x6400000000000001, $ADEV_ID)"
	local ddev_id="(0x6400000000000001, $MDEV_ID)"
	local adev_obj="{0x64| (($adev_id), 4, 1, 4096, 596000000000, 3, 4, \"`pwd`/$ADEV_ID$adisk\")}"
	local ddev_obj="{0x64| (($ddev_id), 4, 1, 4096, 596000000000, 3, 4, \"`pwd`/$MDEV_ID$ddisk\")}"

	MDS_DEV_IDS[`expr $mds - 1`]="[2: $adev_id, $ddev_id]"
	if (($NR_MDS_DEVS == 0))
	then
		MDS_DEVS="$adev_obj, \n $ddev_obj"
	else
		MDS_DEVS="$MDS_DEVS, \n $adev_obj, \n $ddev_obj"
	fi
	NR_MDS_DEVS=`expr $NR_MDS_DEVS + 2`
	ADEV_ID=`expr $ADEV_ID + 1`
	MDEV_ID=`expr $MDEV_ID + 1`
}

mkmdsloopdevs()
{
	local mds=$1
	local dir=$2
	local adisk=addb-disk.img
	local ddisk=data-disk.img

	cd $dir || return 1
	conf_mds_devices_setup $mds

	dd if=/dev/zero of=$ADEV_ID$adisk bs=1M seek=1M count=1 || return 1
	dd if=/dev/zero of=$MDEV_ID$ddisk bs=1M seek=1M count=1 || return 1
	cat > disks.conf << EOF
Device:
   - id: $ADEV_ID
     filename: `pwd`/$ADEV_ID$adisk
   - id: $MDEV_ID
     filename: `pwd`/$MDEV_ID$ddisk
EOF
	cd - >/dev/null
	return $?
}

servers_stop()
{
	prog=$1

	. /etc/rc.d/init.d/functions

	# shutdown services. mds should be stopped last, because
	# other ioservices may have connections to mdservice.
	pids=$(__pids_pidof $prog)
	echo === pids of services: $pids ===
	echo "Shutting down services one by one. mdservice is the last."
	delay=5
	for pid in $pids; do
	       echo -n "----- $pid stopping--------"
	       if checkpid $pid 2>&1; then
		   # TERM first, then KILL if not dead
		   kill -TERM $pid >/dev/null 2>&1
		   sleep 5
		   if checkpid $pid && sleep 5 &&
		      checkpid $pid && sleep $delay &&
		      checkpid $pid ; then
		       kill -KILL $pid >/dev/null 2>&1
		       usleep 100000
		    fi
		fi
	       echo "----- $pid stopped --------"
	done
}

mero_service()
{
	local stride
	local unit_size=$UNIT_SIZE
	local N=$NR_DATA
	local K=$NR_PARITY
	local P=$POOL_WIDTH
	if [ $# -eq 5 ]
	then
		stride=$2
		N=$3
		K=$4
		P=$5
		unit_size=$((stride * 1024))
	fi

        prog_mkfs="$MERO_CORE_ROOT/utils/mkfs/m0mkfs"
        prog_start="$MERO_CORE_ROOT/mero/m0d"
        prog_exec="$MERO_CORE_ROOT/mero/.libs/lt-m0d"

	if echo $(basename $0) | grep -q 'altogether'; then
		prog_start="${prog_start}-altogether"
		prog_exec="${prog_exec}-altogether"
	fi

	. /etc/rc.d/init.d/functions

	start() {
		local i
		# Use one process fid for all processes for now.
		# Confd database contains only one process, too.
		# Actually, every process should have its own entry
		# in confd database.
		local proc_fid=\''<'$PROC_FID_CNTR:$PROC_FID_KEY'>'\'

		prepare

		# start confd
		DIR=$MERO_M0T1FS_TEST_DIR/confd
		rm -rf $DIR
		mkdir -p $DIR
		ulimit -c unlimited

		local nr_ios=${#IOSEP[*]}
		local nr_dev_per_ios=$(($P / $nr_ios))
		local remainder=$(($P % $nr_ios))

		#create mds devices
		for ((i=0; i < ${#MDSEP[*]}; i++)) ; do
			local mds=`expr $i + 1`
			DIR=$MERO_M0T1FS_TEST_DIR/mds$mds
			rm -rf $DIR
			mkdir -p $DIR

			mkmdsloopdevs $mds $DIR || return 1
		done

		#create ios devices
		for ((i=0; i < $nr_ios; i++)) ; do
			local ios=`expr $i + 1`
			local nr_dev=$nr_dev_per_ios
			DIR=$MERO_M0T1FS_TEST_DIR/ios$ios
			rm -rf $DIR
			mkdir -p $DIR

			if (($i < $remainder))
			then
				nr_dev=$(($nr_dev_per_ios + 1))
			fi

			mkiosloopdevs $ios $nr_dev $DIR || return 1
		done

		DIR=$MERO_M0T1FS_TEST_DIR/confd
		build_conf $N $K $P | tee $DIR/conf.xc

		common_opts="-D db -S stobs -A linuxstob:addb-stobs \
			     -w $P -m $MAX_RPC_MSG_SIZE \
			     -q $TM_MIN_RECV_QUEUE_LEN -P '$PROF_OPT' "

		# mkfs for confd server
		opts="$common_opts -T linux -e $XPT:${lnet_nid}:$MKFS_EP \
		      -s 'confd:<$CONF_FID_CON:1>' -c $DIR/conf.xc"
		cmd="cd $DIR && exec $prog_mkfs -F $opts |& tee -a m0d.log"

		echo $cmd
		(eval "$cmd")

		# spawn confd
		opts="$common_opts -f $proc_fid -T linux -e $XPT:$CONFD_EP \
		      -s 'confd:<$CONF_FID_CON:2>' -c $DIR/conf.xc"
		cmd="cd $DIR && exec $prog_start $opts |& tee -a m0d.log"
		echo $cmd
		(eval "$cmd") &

                # Wait until confd service starts.
                # This will be handeled by MERO-988.
                sleep 5
		# mkfs for ha agent
		DIR=$MERO_M0T1FS_TEST_DIR/ha
		rm -rf $DIR
		mkdir -p $DIR
		opts="$common_opts -T linux -e $XPT:${lnet_nid}:$MKFS_EP \
		      -C $CONFD_EP"
		cmd="cd $DIR && exec $prog_mkfs -F $opts |& tee -a m0d.log"
		echo $cmd
		(eval "$cmd")

		# spawn ha agent
		opts="$common_opts -f $proc_fid -T linux \
		      -e $XPT:${lnet_nid}:$HA_EP -C $CONFD_EP"
		cmd="cd $DIR && exec $prog_start $opts |& tee -a m0d.log"
		echo $cmd
		(eval "$cmd") &

		#mds mkfs
		for ((i=0; i < ${#MDSEP[*]}; i++)) ; do
			local mds=`expr $i + 1`
			DIR=$MERO_M0T1FS_TEST_DIR/mds$mds

			SNAME="-s 'mdservice:<$MDS_FID_CON:$i>' \
                               -s 'rmservice:<$RMS_FID_CON:$i>' \
                               -s 'addb2:<$ADD2_FID_CON:$i>' \
                               -s 'stats:<$STS_FID_CON:$i>'"

			ulimit -c unlimited
			cmd="cd $DIR && exec \
			$prog_mkfs -F -T $MERO_STOB_DOMAIN \
			$common_opts -e $XPT:${lnet_nid}:$MKFS_EP \
			$SNAME -C $CONFD_EP |& tee -a m0d.log"
			echo $cmd
			eval "$cmd"
		done

		#ios mkfs
		for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
			local ios=`expr $i + 1`
			DIR=$MERO_M0T1FS_TEST_DIR/ios$ios

			SNAME="-s 'ioservice:<$IOS_FID_CON:$i>' \
                               -s 'sns_repair:<$SNSR_FID_CON:$i>' \
                               -s 'sns_rebalance:<$SNSB_FID_CON:$i>' \
                               -s 'addb2:<$ADD2_FID_CON:$i>'"

			ulimit -c unlimited
			cmd="cd $DIR && exec \
			$prog_mkfs -F -T $MERO_STOB_DOMAIN \
			$common_opts -e $XPT:${lnet_nid}:$MKFS_EP \
			$SNAME -C $CONFD_EP |& tee -a m0d.log"
			echo $cmd
			eval "$cmd"
		done

		# spawn ha agent
		opts="$common_opts -T linux -e $XPT:${lnet_nid}:$HA_EP \
		      -C $CONFD_EP -f $proc_fid"
		DIR=$MERO_M0T1FS_TEST_DIR/ha
		cmd="cd $DIR && exec $prog_start $opts |& tee -a m0d.log"
		echo $cmd
		(eval "$cmd") &

		# spawn mds
		for ((i=0; i < ${#MDSEP[*]}; i++)) ; do
			local mds=`expr $i + 1`
			DIR=$MERO_M0T1FS_TEST_DIR/mds$mds

			SNAME="-s 'mdservice:<$MDS_FID_CON:$i>' \
                               -s 'rmservice:<$RMS_FID_CON:$i>' \
                               -s 'addb2:<$ADD2_FID_CON:$i>' \
                               -s 'stats:<$STS_FID_CON:$i>'"

			ulimit -c unlimited

			cmd="cd $DIR && exec \
			$prog_start -T $MERO_STOB_DOMAIN \
			$common_opts -e $XPT:${lnet_nid}:${MDSEP[$i]} \
                        -f $proc_fid \
			-C $CONFD_EP $SNAME |& tee -a m0d.log"
			echo $cmd

			local m0d_log=$DIR/m0d.log
			touch $m0d_log
			(eval "$cmd") &

		done

		# spawn ios
		for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
			local ios=`expr $i + 1`
			DIR=$MERO_M0T1FS_TEST_DIR/ios$ios

			SNAME="-s 'ioservice:<$IOS_FID_CON:$i>' \
                               -s 'sns_repair:<$SNSR_FID_CON:$i>' \
                               -s 'sns_rebalance:<$SNSB_FID_CON:$i>' \
                               -s 'addb2:<$ADD2_FID_CON:$i>'"

			ulimit -c unlimited

			cmd="cd $DIR && exec \
			$prog_start -T $MERO_STOB_DOMAIN \
			$common_opts -e $XPT:${lnet_nid}:${IOSEP[$i]} \
                        -f $proc_fid \
			-C $CONFD_EP $SNAME |& tee -a m0d.log"
			echo $cmd

			local m0d_log=$DIR/m0d.log
			touch $m0d_log
			(eval "$cmd") &

		done

		# Wait for confd to start
		local confd_log=$MERO_M0T1FS_TEST_DIR/confd/m0d.log
		while ! grep CTRL $confd_log > /dev/null; do
			sleep 2
		done
		echo "Mero confd started."


		# Wait for HA agent to start
		while ! grep CTRL $MERO_M0T1FS_TEST_DIR/ha/m0d.log > /dev/null;
		do
			sleep 2
		done
		echo "Mero HA agent started."

		# Wait for mds to start
		for ((i=0; i < ${#MDSEP[*]}; i++)) ; do
			local mds=`expr $i + 1`
			DIR=$MERO_M0T1FS_TEST_DIR/mds$mds
			local m0d_log=$DIR/m0d.log
			while ! grep CTRL $m0d_log > /dev/null; do
				sleep 2
			done
		done
		echo "Mero mdservices started."

		# Wait for ios to start
		for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
			local ios=`expr $i + 1`
			DIR=$MERO_M0T1FS_TEST_DIR/ios$ios
			local m0d_log=$DIR/m0d.log
			while ! grep CTRL $m0d_log > /dev/null; do
				sleep 2
			done
		done
		echo "Mero ioservices started."
	}

	stop() {
		servers_stop $prog_exec
		unprepare
	}

	case "$1" in
	    start)
		$1
		;;
	    stop)
		function=$1
		shift
		$function $@
		echo "Mero services stopped."
		;;
	    *)
		echo "Usage: $0 {start|stop}"
		return 2
	esac
	return $?
}
