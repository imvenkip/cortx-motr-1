colibri_service()
{
        prog=$COLIBRI_CORE_ROOT/colibri/colibri_setup
        exec=$COLIBRI_CORE_ROOT/colibri/.libs/lt-colibri_setup

	. /etc/rc.d/init.d/functions

	start() {
		prepare

		IOS=""
		# spawn servers
		for ((i=0; i < ${#EP[*]}; i++)) ; do
			if ((i != 0)) ; then
				IOS="$IOS,"
		        fi
		        IOS="${IOS}ios=${lnet_nid}:${EP[$i]}"
			rm -rf $COLIBRI_C2T1FS_TEST_DIR/d$i
			mkdir $COLIBRI_C2T1FS_TEST_DIR/d$i
			(cd $COLIBRI_C2T1FS_TEST_DIR/d$i
			 $prog -r -T $COLIBRI_STOB_DOMAIN \
			 -D $COLIBRI_C2T1FS_TEST_DIR/d$i/db \
		         -S $COLIBRI_C2T1FS_TEST_DIR/d$i/stobs \
			 -e $XPT:${lnet_nid}:${EP[$i]} \
			 -s $COLIBRI_SERVICE_NAME -m $MAX_RPC_MSG_SIZE \
			 -q $TM_MIN_RECV_QUEUE_LEN \
			     &>>$COLIBRI_C2T1FS_TEST_DIR/servers_started )&

			sleep 1
			status $exec
			if [ $? -eq 0 ]; then
				echo "Colibri service started."
			else
				echo "Colibri service failed to start."
				return 1
			fi
		done
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
