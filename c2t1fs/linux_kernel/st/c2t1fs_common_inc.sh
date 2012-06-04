COLIBRI_CORE_ROOT=`dirname $0`/../../..
COLIBRI_C2T1FS_MOUNT_DIR=/tmp/test_c2t1fs_`date +"%d-%m-%Y_%T"`
COLIBRI_C2T1FS_TEST_DIR=/tmp/test_c2t1fs_$$
COLIBRI_MODULE=$COLIBRI_CORE_ROOT/build_kernel_modules/kcolibri.ko
COLIBRI_MODULE_TRACE_MASK=0x00
COLIBRI_TEST_LOGFILE=`pwd`/bulkio_`date +"%Y-%m-%d_%T"`.log
POOL_WIDTH=3
TM_MIN_RECV_QUEUE_LEN=2
MAX_RPC_MSG_SIZE=0

prepare_testdir()
{
	stob_domain=$COLIBRI_STOB_DOMAIN
	db_path=$COLIBRI_DB_PATH

	echo "Cleaning up test directory ..."
	rm -rf $COLIBRI_C2T1FS_TEST_DIR	 &> /dev/null

	echo "Creating test directory ..."
	mkdir $COLIBRI_C2T1FS_TEST_DIR &> /dev/null

	if [ $? -ne "0" ]
	then
		echo "Failed to create test directory."
		return 1
	fi

	return 0
}

prepare()
{
	prepare_testdir || return $?
	modload_galois
	echo 8 > /proc/sys/kernel/printk
	load_kernel_module || return $?
}

unprepare()
{
	c2t1fs_mount_dir=$COLIBRI_C2T1FS_MOUNT_DIR
	rc=`cat /proc/filesystems | grep c2t1fs | wc -l > /dev/null`
	if [ "x$rc" = "x1" ]; then
		umount $c2t1fs_mount_dir &>> /dev/null
	fi

	rc=`ps -ef | grep colibri_setup | grep -v grep | wc -l`
	if [ "x$rc" != "x0" ]; then
		colibri_service stop
		sleep 5 # Give some time to stop service properly.
	fi

	unload_kernel_module
	modunload_galois
}

export  COLIBRI_CORE_ROOT         \
	COLIBRI_C2T1FS_MOUNT_DIR  \
	COLIBRI_C2T1FS_TEST_DIR   \
	COLIBRI_MODULE            \
	COLIBRI_MODULE_TRACE_MASK \
	COLIBRI_TEST_LOGFILE      \
	POOL_WIDTH                \
	TM_MIN_RECV_QUEUE_LEN     \
	MAX_RPC_MSG_SIZE
