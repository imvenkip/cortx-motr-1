#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_rsink.sh

NR_DATA=5
NR_PARITY=2
POOL_WIDTH=9
main()
{
	NODE_UUID=`uuidgen`
	mero_service start
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	m0t1fs_system_tests
	rc=$?

	mero_service stop --collect-addb
	if [ $? -ne "0" ]
	then
		echo "Failed to stop Mero Service."
		return 1
	fi

	if [ $rc -ne "0" ]
	then
		echo "Failed m0t1fs system tests."
		return 1
	fi

	# ADDB RPC sink ST usage ADDB client records generated
	# from IO done by "m0t1fs_system_tests".
	# ADDB dump file removed from rpcsink_addb_st after test.
        rpcsink_addb_st
	if [ $? -ne "0" ]
	then
                return 1
	fi

	echo "Test log available at $MERO_TEST_LOGFILE."

	return $rc
}

trap unprepare EXIT

main
