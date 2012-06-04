#!/bin/sh

usage()
{
	echo "Usage: `basename $0` <start|stop> [server_endpoint]"
	echo "Please provide the server endpoint address you want to use."
	echo "e.g. 192.168.172.130@tcp:12345:34:1"
	echo "If you want to use the default nid registered for lnet, then do"
	echo "`basename $0` start default"
}

if [ $# -lt 1 ]
then
	usage
        exit 1
fi

if [ "x$1" = "x-h" ]; then
	usage
	exit 0
fi

export C2_TRACE_IMMEDIATE_MASK=1

. `dirname $0`/c2t1fs_common_inc.sh
. `dirname $0`/c2t1fs_server_inc.sh

trap unprepare EXIT

main()
{
	if [ "x$1" = "xstart" ] || [ "x$1" = "xStart" ]; then
		if [ "x$2" = "xdefault" ] || [ "x$2" = "xDefault" ] || [ "x$2" = "x" ]; then
			lctl network up &>> /dev/null
			lnet_nid=`lctl list_nids | head -1`
			export COLIBRI_IOSERVICE_ENDPOINT="$lnet_nid:12345:34:1"
		fi
	fi

	prepare

	colibri_service $1
	if [ $? -ne "0" ]
	then
		echo "Failed to trigger Colibri Service."
		exit 1
	fi

}

main
