#!/bin/sh

usage()
{
	echo "Usage: `basename $0` server_nid"
	echo "Please provide the server nid you want to use."
	echo "e.g. 192.168.172.130@tcp"
}

if [ $# -lt 1 ]
then
	usage
        exit 1
fi

if [ "x$1" = "x-h" ];
then
	usage
	exit 0
fi

. `dirname $0`/common.sh
. `dirname $0`/c2t1fs_common_inc.sh
. `dirname $0`/c2t1fs_client_inc.sh

main()
{
	modprobe lnet &>> /dev/null
	lctl network up &>> /dev/null
	lnet_nid=`lctl list_nids | head -1`
	export COLIBRI_C2T1FS_ENDPOINT="$lnet_nid:12345:34:6"
	export COLIBRI_IOSERVICE_ENDPOINT="$1:12345:34:1"

	echo "Colibri ioservice endpoint = $COLIBRI_IOSERVICE_ENDPOINT"
	echo "Colibri c2t1fs endpoint = $COLIBRI_C2T1FS_ENDPOINT"

	prepare

	io_combinations $POOL_WIDTH 1 1
        if [ $? -ne "0" ]
        then
                echo "Failed : IO failed.."
        fi
        return 0
}

trap unprepare EXIT

main $1
