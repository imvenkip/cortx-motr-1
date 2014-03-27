#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_rsink.sh

main()
{
	NODE_UUID=`uuidgen`
	mero_service start
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	multi_client_test $NR_CLIENTS
	rc=$?

	# mero_service stop --collect-addb
	mero_service stop
	if [ $? -ne "0" ]
	then
		echo "Failed to stop Mero Service."
		return 1
	fi

	if [ $rc -ne "0" ]
	then
		echo "Failed m0t1fs multi-clients tests."
		return 1
	fi

	echo "m0t1fs multi-clients tests status: SUCCESS."
	echo "Test log available at $MERO_TEST_LOGFILE."

	return $rc
}

trap unprepare EXIT
main
