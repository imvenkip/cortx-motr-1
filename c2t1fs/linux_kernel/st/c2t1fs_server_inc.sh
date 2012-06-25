COLIBRI_NET_DOMAIN=lnet
COLIBRI_SERVICE_NAME=ioservice
COLIBRI_STOB_DOMAIN=linux
COLIBRI_DB_PATH=$COLIBRI_C2T1FS_TEST_DIR/db
COLIBRI_STOB_PATH=$COLIBRI_C2T1FS_TEST_DIR/stobs

colibri_service()
{
        prog=$COLIBRI_CORE_ROOT/colibri/colibri_setup
        exec=$COLIBRI_CORE_ROOT/colibri/.libs/lt-colibri_setup
        prog_args="-r -T $COLIBRI_STOB_DOMAIN -D $COLIBRI_DB_PATH
		   -S $COLIBRI_STOB_PATH
		   -e $COLIBRI_NET_DOMAIN:$COLIBRI_IOSERVICE_ENDPOINT
		   -s $COLIBRI_SERVICE_NAME
		   -q $TM_MIN_RECV_QUEUE_LEN
                   -m $MAX_RPC_MSG_SIZE"

	. /etc/rc.d/init.d/functions

	start() {
		prepare
		$prog $prog_args &
		sleep 1
		status $exec
		if [ $? -eq 0 ]; then
			echo "Colibri service started."
		else
			echo "Colibri service failed to start."
		fi
	}

	stop() {
		killproc $exec
		unprepare
	}

	case "$1" in
	    start)
		$1
		;;
	    stop)
		$1
		echo "Colibri service stopped."
		;;
	    *)
		echo $"Usage: $0 {start|stop}"
		return 2
	esac
	return $?
}
