mero_service()
{
        prog_start="$MERO_CORE_ROOT/mero/m0d"
        prog_exec="$MERO_CORE_ROOT/mero/.libs/lt-m0d"

	if echo $(basename $0) | grep -q 'altogether'; then
		prog_start="${prog_start}-altogether"
		prog_exec="${prog_exec}-altogether"
	fi

	. /etc/rc.d/init.d/functions

	start() {
		prepare
		for ((i=0; i < ${#EP[*]}; i++)) ; do
			ios_eps="$ios_eps -i $XPT:${lnet_nid}:${EP[$i]} "
		done

		# spawn servers
		for ((i=0; i < ${#EP[*]}; i++)) ; do
			SNAME="-s $MERO_IOSERVICE_NAME -s $MERO_CMSERVICE_NAME"
			SNAME="-s $MERO_ADDBSERVICE_NAME $SNAME"
			if ((i == 0)); then
				SNAME="-s $MERO_MDSERVICE_NAME $SNAME"
			fi

			rm -rf $MERO_M0T1FS_TEST_DIR/d$i
			mkdir $MERO_M0T1FS_TEST_DIR/d$i
			ulimit -c unlimited
			cmd="cd $MERO_M0T1FS_TEST_DIR/d$i; \
			$prog_start -r $PREPARE_STORAGE \
			 -T $MERO_STOB_DOMAIN \
			 -D db -S stobs -A addb-stobs \
			 -w $POOL_WIDTH \
			 -G $XPT:${lnet_nid}:${EP[0]} \
			 -e $XPT:${lnet_nid}:${EP[$i]} \
			 $ios_eps \
			 -L $XPT:${lnet_nid}:${EPC2M[$i]} \
			 $SNAME -m $MAX_RPC_MSG_SIZE \
			 -q $TM_MIN_RECV_QUEUE_LEN"
			echo $cmd
			(eval $cmd) &

			sleep 2
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
			   usleep 100000
			   if checkpid $pid && sleep 1 &&
			      checkpid $pid && sleep $delay &&
			      checkpid $pid ; then
                                kill -KILL $pid >/dev/null 2>&1
				usleep 100000
			   fi
		        fi
		       echo ----- $pid stopped --------
		done

		unprepare
	}

	case "$1" in
	    start)
		$1
		;;
	    stop)
		$1
		echo "Mero services stopped."
		;;
	    *)
		echo "Usage: $0 {start|stop}"
		return 2
	esac
	return $?
}
