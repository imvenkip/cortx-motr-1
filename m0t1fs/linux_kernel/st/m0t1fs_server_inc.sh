mero_service()
{
        prog_start="$MERO_CORE_ROOT/mero/m0d"
        prog_exec="$MERO_CORE_ROOT/mero/.libs/lt-m0d"

	. /etc/rc.d/init.d/functions

	start() {
		prepare

		# spawn servers
		for ((i=0; i < ${#EP[*]}; i++)) ; do
			SNAME="-s $MERO_IOSERVICE_NAME"
			if ((i == 0)) ; then
				SNAME=\
"-s confd -s $MERO_MDSERVICE_NAME $SNAME"
			fi

			rm -rf $MERO_M0T1FS_TEST_DIR/d$i
			mkdir $MERO_M0T1FS_TEST_DIR/d$i
			ulimit -c unlimited
			cmd="cd $MERO_M0T1FS_TEST_DIR/d$i; \
			$prog_start -r $PREPARE_STORAGE \
			 -T $MERO_STOB_DOMAIN \
			 -D db -S stobs \
			 -e $XPT:${lnet_nid}:${EP[$i]} \
			 $SNAME -m $MAX_RPC_MSG_SIZE \
			 -q $TM_MIN_RECV_QUEUE_LEN"
			echo $cmd
			(eval $cmd) &

			sleep 1
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
		killproc $prog_exec
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
