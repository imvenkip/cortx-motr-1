#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh

m0t1fs_test()
{
	NODE_UUID=`uuidgen`
	local multiple_pools=0
	mero_service start $multiple_pools
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	m0t1fs_system_tests
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
		echo "Failed m0t1fs system tests."
		return 1
	fi

	echo "System tests status: SUCCESS."
}

main()
{
	local rc=0
	echo "System tests start:"
	echo "Test log will be stored in $MERO_TEST_LOGFILE."

        sandbox_init

	set -o pipefail
	m0t1fs_test 2>&1 | tee -a $MERO_TEST_LOGFILE
	rc=${PIPESTATUS[0]}

	if [ $rc -eq 0 ]; then
		sandbox_fini
	else
		echo "Test log available at $MERO_TEST_LOGFILE."
	fi
	return $rc
}

trap unprepare EXIT
main
