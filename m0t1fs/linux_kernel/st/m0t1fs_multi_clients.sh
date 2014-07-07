#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_rsink.sh

multi_clients()
{
	NODE_UUID=`uuidgen`
	mero_service start
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	multi_client_test
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
	return $rc
}

main()
{
	echo "Starting multi clients testing:"
	echo "Test log will be stored in $MERO_TEST_LOGFILE."

	multi_clients 2>&1 | tee -a $MERO_TEST_LOGFILE
	rc=$?

	echo "Test log available at $MERO_TEST_LOGFILE."
	return $rc
}

trap unprepare EXIT
main

# this msg is used by Jenkins as a test success criteria;
# it should appear on STDOUT
if [ $? -eq 0 ] ; then
    echo "multi-client: test status: SUCCESS"
fi
