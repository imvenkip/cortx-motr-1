#!/bin/sh

usage()
{
	echo "Usage: `basename $0` <start|stop> [server_nid]"
	echo "Please provide the server endpoint address you want to use."
	echo "e.g. 192.168.172.130@tcp"
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

. `dirname $0`/common.sh
. `dirname $0`/c2t1fs_common_inc.sh
. `dirname $0`/c2t1fs_server_inc.sh

main()
{
	if [ "x$1" = "xstart" ] || [ "x$1" = "xStart" ]; then
		modprobe lnet &>> /dev/null
		lctl network up &>> /dev/null
		lnet_nid=`lctl list_nids | head -1`
		export COLIBRI_C2T1FS_ENDPOINT="$lnet_nid:12345:34:6"

		if [ "x$2" = "xdefault" ] || [ "x$2" = "xDefault" ] ||
		   [ "x$2" = "x" ]; then
			export COLIBRI_IOSERVICE_ENDPOINT="$lnet_nid:12345:34:1"
		else
			export COLIBRI_IOSERVICE_ENDPOINT="$2:12345:34:1"
		fi

		echo "Colibri ioservice endpoint = $COLIBRI_IOSERVICE_ENDPOINT"
		echo "Colibri ioservice endpoint = $COLIBRI_C2T1FS_ENDPOINT"

		prepare
	fi

	colibri_service $1
	if [ $? -ne "0" ]
	then
		echo "Failed to trigger Colibri Service."
		exit 1
	fi

}

main $1 $2
