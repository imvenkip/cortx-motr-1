#!/bin/sh

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

	sleep 5 #Give time to start service properly.

	io_combinations $POOL_WIDTH 1 1
	if [ $? -ne "0" ]
	then
		echo "Failed : IO failed.."
	fi

	colibri_service stop
	if [ $? -ne "0" ]
	then
		echo "Failed to stop Colibri Service."
		return 1
	fi

	return 0
}

trap unprepare EXIT

prepare
main
