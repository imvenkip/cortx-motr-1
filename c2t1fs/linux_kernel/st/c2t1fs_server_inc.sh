colibri_service()
{
        prog_start=$COLIBRI_CORE_ROOT/colibri/colibri_setup
        prog_exec=$COLIBRI_CORE_ROOT/colibri/.libs/lt-colibri_setup

	. /etc/rc.d/init.d/functions

	start() {
		prepare

		# spawn servers
		for ((i=0; i < ${#EP[*]}; i++)) ; do
                        SRV="-s $COLIBRI_IOSERVICE_NAME"
		        if ((i == 0)) ; then
                            SRV="-s $COLIBRI_MDSERVICE_NAME $SRV"
                        fi
			rm -rf $COLIBRI_C2T1FS_TEST_DIR/d$i
			mkdir $COLIBRI_C2T1FS_TEST_DIR/d$i
			cmd="cd $COLIBRI_C2T1FS_TEST_DIR/d$i; \
			 $prog_start -r $PREPARE_STORAGE -T $COLIBRI_STOB_DOMAIN \
			 -D db -S stobs \
			 -e $XPT:${lnet_nid}:${EP[$i]} \
			 $SRV -m $MAX_RPC_MSG_SIZE \
			 -q $TM_MIN_RECV_QUEUE_LEN "
			echo $cmd
			(eval $cmd) &

			sleep 1
			status $prog_exec
			if [ $? -eq 0 ]; then
			        SRV=$(echo $SRV | sed 's/-s //g')
				echo "Colibri services ($SRV) started."
			else
				echo "Colibri service failed to start:"
				cat $COLIBRI_C2T1FS_TEST_DIR/servers_started
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
		echo "Colibri services stopped."
		;;
	    *)
		echo "Usage: $0 {start|stop}"
		return 2
	esac
	return $?
}
