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
	server_nid=$1
	lnet_nid=`lctl list_nids | head -1`
	LADDR="$lnet_nid:12345:33:1"

	for ((i=0; i < ${#EP[*]}; i++)) ; do
		if ((i != 0)) ; then
                    IOS="$IOS,"
                fi
                IOS="${IOS}ios=${server_nid}:${EP[$i]}"
	done

	prepare

	echo "Prepare done, starting tests..."

	c2t1fs_system_tests
	if [ $? -ne "0" ]
	then
		return 1
	fi

        return 0
}

trap unprepare EXIT

main $1
