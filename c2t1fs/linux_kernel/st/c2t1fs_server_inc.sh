COLIBRI_NET_DOMAIN=bulk-sunrpc
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
		   -q $TM_MIN_RECV_QUEUE_LEN"

        . /etc/rc.d/init.d/functions

        start() {
                $prog $prog_args &
        }

        stop() {
                killproc $exec
        }

        case "$1" in
            start)
		prepare_testdir || return $?
                $1
		echo "Colibri service started."
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

