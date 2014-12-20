mkiosloopdevs()
{
	local ios=$1
	local nr_devs=$2
	local dir=$3
	local adisk=addb-disk.img
	local ddisk=data-disk.img
	local dev_start=1
	local dev_end=0

	cd $dir || return 1

	dd if=/dev/zero of=$adisk bs=1M seek=1M count=1 || return 1
	cat > disks.conf << EOF
Device:
   - id: 0
     filename: `pwd`/$adisk
EOF
	if (($ios > 0))
	then
		dev_start=$(($nr_devs * $ios))
		dev_end=$(($dev_start - $nr_devs))
	fi
	for ((j=$dev_start; j > $dev_end; j--)) ; do
		dd if=/dev/zero of=$j$ddisk bs=1M seek=1M count=1 ||
			return 1
		cat >> disks.conf << EOF
   - id: $j
     filename: `pwd`/$j$ddisk
EOF
	done

	cd - >/dev/null
	return $?
}

mkmdsloopdevs()
{
	local mds=$1
	local dir=$2
	local adisk=addb-disk.img
	local ddisk=data-disk.img

	cd $dir || return 1

	dd if=/dev/zero of=$adisk bs=1M seek=1M count=1 || return 1
	dd if=/dev/zero of=$ddisk bs=1M seek=1M count=1 || return 1
	cat > disks.conf << EOF
Device:
   - id: 0
     filename: `pwd`/$adisk
   - id: $mds
     filename: `pwd`/$ddisk
EOF

	cd - >/dev/null
	return $?
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

		prepare

		# start confd
		DIR=$MERO_M0T1FS_TEST_DIR/confd
		rm -rf $DIR
		mkdir $DIR
		ulimit -c unlimited

		build_conf $N $K $P | tee $DIR/conf.xc

		opts="-T linux -D db -S stobs -A linuxstob:addb-stobs \
		      -w $P -e $XPT:$CONFD_EP -s confd -c $DIR/conf.xc \
		      -m $MAX_RPC_MSG_SIZE -q $TM_MIN_RECV_QUEUE_LEN"
		cmd="cd $DIR && exec $prog_mkfs -F $opts |& tee -a m0d.log"
		echo $cmd
		(eval "$cmd")
		cmd="cd $DIR && exec $prog_start $opts |& tee -a m0d.log"
		echo $cmd
		(eval "$cmd") &

		# wait till the server start completes
		local m0d_log=$DIR/m0d.log
		touch $m0d_log
		sleep 2
		while status $prog_exec > /dev/null && \
		      ! grep CTRL $m0d_log > /dev/null; do
			sleep 2
		done

		status $prog_exec
		if [ $? -eq 0 ]; then
			echo "Mero confd service started."
		else
			echo "Mero confd service failed to start."
			return 1
		fi

		local nr_ios=${#IOSEP[*]}
		local nr_dev_per_ios=$(($P / $nr_ios))
		if (($P % $nr_ios > 0))
		then
			nr_dev_per_ios=$(($nr_dev_per_ios + 1))
		fi

		# spawn mds servers
		for ((i=0; i < ${#MDSEP[*]}; i++)) ; do
			local mds=`expr $i + 1`
			DIR=$MERO_M0T1FS_TEST_DIR/mds$mds
			rm -rf $DIR
			mkdir $DIR

			(mkmdsloopdevs $mds $DIR) || return 1

			SNAME="-s mdservice -s rmservice -s addb -s stats"
			ulimit -c unlimited
			cmd="cd $DIR && exec \
			$prog_mkfs -F -T $MERO_STOB_DOMAIN \
			 -D db -S stobs -A linuxstob:addb-stobs \
			 -w $P -e $XPT:${lnet_nid}:${MDSEP[$i]} \
			 $SNAME |& tee -a m0d.log"
			echo $cmd
			eval "$cmd"

			cmd="cd $DIR && exec \
			$prog_start -T $MERO_STOB_DOMAIN \
			 -D db -S stobs -A linuxstob:addb-stobs -w $P \
			 -e $XPT:${lnet_nid}:${MDSEP[$i]} \
			 -m $MAX_RPC_MSG_SIZE -q $TM_MIN_RECV_QUEUE_LEN \
			 -P '$PROF_OPT' -C $CONFD_EP $SNAME |& tee -a m0d.log"
			echo $cmd

			# wait till the server start completes
			local m0d_log=$DIR/m0d.log
			touch $m0d_log
			(eval "$cmd") &

			sleep 5
			while status $prog_exec > /dev/null && \
			      ! grep CTRL $m0d_log > /dev/null; do
				sleep 2
			done

			status $prog_exec
			if [ $? -eq 0 ]; then
				SNAME=$(echo $SNAME | sed 's/-s //g')
				echo "Mero services ($SNAME) started."
			else
				echo "Mero service failed to start."
				return 1
			fi
		done


		# spawn io servers
		for ((i=0; i < ${#IOSEP[*]}; i++)) ; do
			local ios=`expr $i + 1`
			DIR=$MERO_M0T1FS_TEST_DIR/ios$ios
			rm -rf $DIR
			mkdir -p $DIR
			(mkiosloopdevs $ios $nr_dev_per_ios $DIR) || return 1

			SNAME="-s ioservice -s sns_repair -s sns_rebalance -s addb"

			ulimit -c unlimited
			cmd="cd $DIR && exec \
			$prog_mkfs -F -T $MERO_STOB_DOMAIN \
			 -D db -S stobs -A linuxstob:addb-stobs \
			 -w $P -e $XPT:${lnet_nid}:${IOSEP[$i]} \
			 $SNAME |& tee -a m0d.log"
			echo $cmd
			eval "$cmd"

			cmd="cd $DIR && exec \
			$prog_start -T $MERO_STOB_DOMAIN \
			 -D db -S stobs -A linuxstob:addb-stobs \
			 -w $P -e $XPT:${lnet_nid}:${IOSEP[$i]} \
			 -m $MAX_RPC_MSG_SIZE -q $TM_MIN_RECV_QUEUE_LEN \
			 -P '$PROF_OPT' -C $CONFD_EP $SNAME |& tee -a m0d.log"
			echo $cmd

			# wait till the server start completes
			local m0d_log=$DIR/m0d.log
			touch $m0d_log
			(eval "$cmd") &

			sleep 5
			while status $prog_exec > /dev/null && \
			      ! grep CTRL $m0d_log > /dev/null; do
				sleep 2
			done

			status $prog_exec
			if [ $? -eq 0 ]; then
				SNAME=$(echo $SNAME | sed 's/-s //g')
				echo "Mero services ($SNAME) started."
			else
				echo "Mero service failed to start."
				return 1
			fi
		done
	}

	stop() {

		# shutdown services. mds should be stopped last, because
		# other ioservices may have connections to mdservice.
		pids=$(__pids_pidof $prog_exec)
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

		# ADDB RPC sink ST usage ADDB client records generated
		# by IO done by "m0t1fs_system_tests".
		# It collects ADDB records from addb_stobs from all services
		# to $ADDB_DUMP_FILE and later used by RPC sink ST.
		if [ "$1" = "--collect-addb" ]
		then
			collect_addb_from_all_services
		fi

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
		echo "Usage: $0 {start|stop [--collect-addb]}"
		return 2
	esac
	return $?
}
