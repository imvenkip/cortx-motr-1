#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/c2t1fs_common_inc.sh
. `dirname $0`/c2t1fs_client_inc.sh
. `dirname $0`/c2t1fs_server_inc.sh

main()
{
	colibri_service start
	if [ $? -ne "0" ]
	then
		echo "Failed to start Colibri Service."
		return 1
	fi

	for ((i=0; i < ${#EP[*]}; i++)) ; do
                SERVICES="${SERVICES},ios=${lnet_nid}:${EP[$i]}"
	done

	c2t1fs_system_tests

	colibri_service stop
	if [ $? -ne "0" ]
	then
		echo "Failed to stop Colibri Service."
		return 1
	fi

	echo "Test log available at $COLIBRI_TEST_LOGFILE."

	return 0
}

trap unprepare EXIT

main
