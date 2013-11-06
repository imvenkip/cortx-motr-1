mkloopdevs()
{
	local i=$1
	local dir=$2
	local adisk=addb-disk.img
	local ddisk=data-disk.img

	cd $dir || return 1

	dd if=/dev/zero of=$adisk bs=1M seek=1M count=1 || return 1
	if ((i > 0)); then
		dd if=/dev/zero of=$ddisk bs=1M seek=1M count=1 ||
			return 1
	fi

	cat > disks.conf << EOF
Device:
   - id: 0
     filename: $adisk
EOF
	if ((i > 0)); then
		cat >> disks.conf << EOF
   - id: $i
     filename: $ddisk
EOF
	fi

	return $?
}

mero_service()
{
	local P=$POOL_WIDTH
	if [ $# -eq 2 ]
	then
		P=$2
	fi
        prog_start="$MERO_CORE_ROOT/mero/m0d"
        prog_exec="$MERO_CORE_ROOT/mero/.libs/lt-m0d"

	if echo $(basename $0) | grep -q 'altogether'; then
		prog_start="${prog_start}-altogether"
		prog_exec="${prog_exec}-altogether"
	fi

	. /etc/rc.d/init.d/functions

	start() {
		prepare
		for ((i=1; i < ${#EP[*]}; i++)) ; do
			ios_eps="$ios_eps -i $XPT:${lnet_nid}:${EP[$i]} "
		done

		# spawn servers
		for ((i=0; i < ${#EP[*]}; i++)) ; do
			SNAME="-s $MERO_ADDBSERVICE_NAME"
			if ((i == 0)); then
				SNAME="-s $MERO_MDSERVICE_NAME -s $MERO_RMSERVICE_NAME $SNAME"
			else
				SNAME="-s $MERO_IOSERVICE_NAME -s $MERO_SNSREPAIRSERVICE_NAME \
				      -s $MERO_SNSREBALANCESERVICE_NAME $SNAME"
			fi

			rm -rf $MERO_M0T1FS_TEST_DIR/d$i
			mkdir $MERO_M0T1FS_TEST_DIR/d$i

			mkloopdevs $i $MERO_M0T1FS_TEST_DIR/d$i || return 1

			ulimit -c unlimited
			cmd="cd $MERO_M0T1FS_TEST_DIR/d$i && exec \
			$prog_start -r $PREPARE_STORAGE \
			 -T $MERO_STOB_DOMAIN \
			 -D db -S stobs -A addb-stobs \
			 -w $P \
			 -G $XPT:${lnet_nid}:${EP[0]} \
			 -e $XPT:${lnet_nid}:${EP[$i]} \
			 $ios_eps \
			 $SNAME -m $MAX_RPC_MSG_SIZE \
			 -q $TM_MIN_RECV_QUEUE_LEN |& tee -a m0d.log"
			echo $cmd
			(eval "$cmd") &

			# wait till the server start completes
			local m0d_log=$MERO_M0T1FS_TEST_DIR/d$i/m0d.log
			touch $m0d_log
			while ! grep CTRL $m0d_log > /dev/null; do
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
		       echo ----- $pid stopping--------
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
		       echo ----- $pid stopped --------
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
