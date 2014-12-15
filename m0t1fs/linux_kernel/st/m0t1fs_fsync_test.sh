#!/bin/sh

#set -x

. `dirname $0`/common.sh
. `dirname $0`/m0t1fs_common_inc.sh
. `dirname $0`/m0t1fs_client_inc.sh
. `dirname $0`/m0t1fs_server_inc.sh
. `dirname $0`/m0t1fs_sns_common_inc.sh

fsync_test()
{
	`dirname $0`/m0t1fs_fsync_test_helper $MERO_M0T1FS_MOUNT_DIR
	return $?
}

main()
{
	NODE_UUID=`uuidgen`
	echo "About to start Mero service"
	mero_service start
	if [ $? -ne "0" ]
	then
		echo "Failed to start Mero Service."
		return 1
	fi

	rc=0
	echo "mero service started"

	mkdir -p $MERO_M0T1FS_MOUNT_DIR
	mount_m0t1fs $MERO_M0T1FS_MOUNT_DIR $NR_DATA $NR_PARITY $POOL_WIDTH "oostore" || return 1

	fsync_test || {
		echo "Failed: Fsync test failed.."
		rc=1
	}

	unmount_m0t1fs $MERO_M0T1FS_MOUNT_DIR &>> $MERO_TEST_LOGFILE

	mero_service stop
	if [ $? -ne "0" ]
	then
		echo "Failed to stop Mero Service."
		return 1
	fi

	echo "Test log available at $MERO_TEST_LOGFILE."

	return $rc
}

trap unprepare EXIT
main
