#!/bin/sh

usage()
{
	echo "Usage: `basename $0` [client_endpoint]"
	echo "Please provide the client endpoint address you want to use."
	echo "e.g. 192.168.172.130@tcp:12345:34:1"
	echo "If you want to use the default nid registered for lnet, then do"
	echo "`basename $0` default"
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
	if [ "x$1" = "xdefault" ] || [ "x$1" = "xDefault" ] || [ "x$2" = "x" ]; then
		lctl network up &>> /dev/null
		lnet_nid=`lctl list_nids | head -1`
		export COLIBRI_C2T1FS_ENDPOINT="$lnet_nid:12345:34:6"
	fi

	prepare

	io_combinations $POOL_WIDTH 1 1
        if [ $? -ne "0" ]
        then
                echo "Failed : IO failed.."
        fi
        return 0
}

trap unprepare EXIT

main
