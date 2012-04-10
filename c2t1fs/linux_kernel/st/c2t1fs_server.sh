#!/bin/sh

if [ $# -lt "1" ]
then
	echo "Usage : $0 <start|stop>"
        exit 1
fi

echo 8 > /proc/sys/kernel/printk
export C2_TRACE_IMMEDIATE_MASK=1

. `dirname $0`/c2t1fs_common.sh

COLIBRI_NET_DOMAIN=bulk-sunrpc
COLIBRI_SERVICE_NAME=ioservice
COLIBRI_STOB_DOMAIN=linux
COLIBRI_DB_PATH=$COLIBRI_C2T1FS_TEST_DIR/db
COLIBRI_STOB_PATH=$COLIBRI_C2T1FS_TEST_DIR/stobs
GLOBAL_FID_LO=3         #Global fid = <0,3>


create_stobs()
{
        if [ "$#" -lt 1 ]
        then
                echo "Usage :$0 pool_width"
                return 1
        fi

        pool_width=$1
        stob_domain=$COLIBRI_STOB_DOMAIN
        db_path=$COLIBRI_DB_PATH
        stobid_lo=$GLOBAL_FID_LO

        echo "Cleaning up test directory ..."
        rm -rf $COLIBRI_C2T1FS_TEST_DIR  &> /dev/null

        echo "Creating test directory ..."
        mkdir $COLIBRI_C2T1FS_TEST_DIR &> /dev/null

        if [ $? -ne "0" ]
        then
                echo "Failed to create test directory."
                return 1
        fi

        return 0
}

colibri_service()
{
        prog=$COLIBRI_CORE_ROOT/colibri/colibri_setup
        exec=$COLIBRI_CORE_ROOT/colibri/.libs/lt-colibri_setup
        prog_args="-r -T $COLIBRI_STOB_DOMAIN -D $COLIBRI_DB_PATH -S $COLIBRI_STOB_PATH -e $COLIBRI_NET_DOMAIN:$COLIBRI_IOSERVICE_ENDPOINT -s $COLIBRI_SERVICE_NAME"

        . /etc/rc.d/init.d/functions

        start() {
                $prog $prog_args &
        }

        stop() {
                killproc $exec
        }

        case "$1" in
            start)
		create_stobs $POOL_WIDTH
		if [ $? -ne "0" ]
		then
			echo "Failed to create stobs."
		fi
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

colibri_service $1
if [ $? -ne "0" ]
then
	echo "Failed to trigger Colibri Service."
	exit 1
fi
