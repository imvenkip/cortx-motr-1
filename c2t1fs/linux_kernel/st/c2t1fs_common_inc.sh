COLIBRI_CORE_ROOT=`dirname $0`/../../..
COLIBRI_C2T1FS_MOUNT_DIR=/tmp/test_c2t1fs_`date +"%d-%m-%Y_%T"`
COLIBRI_C2T1FS_TEST_DIR=/tmp/test_c2t1fs_$$
COLIBRI_MODULE=kcolibri
COLIBRI_MODULE_TRACE_MASK=0x00
COLIBRI_TEST_LOGFILE=`pwd`/bulkio_`date +"%Y-%m-%d_%T"`.log
POOL_WIDTH=3
TM_MIN_RECV_QUEUE_LEN=2
MAX_RPC_MSG_SIZE=0

unload_kernel_module()
{
	colibri_module=$COLIBRI_MODULE
	rmmod $colibri_module.ko &>> /dev/null
	if [ $? -ne "0" ]
	then
	    echo "Failed:failed to remove $colibri_module."
	    return 1
	fi
}

load_kernel_module()
{
	colibri_module_path=$COLIBRI_CORE_ROOT/build_kernel_modules
	colibri_module=$COLIBRI_MODULE
	lsmod | grep $colibri_module &> /dev/null
	if [ $? -eq "0" ]
	then
		echo "Module $colibri_module already present."
		echo "Removing existing module to for clean test."
		unload_kernel_module || return $?
	fi

        insmod $colibri_module_path/$colibri_module.ko            \
               c2_trace_immediate_mask=$COLIBRI_MODULE_TRACE_MASK \
               local_addr=$COLIBRI_C2T1FS_ENDPOINT                \
	       tm_recv_queue_min_len=$TM_MIN_RECV_QUEUE_LEN       \
	       max_rpc_msg_size=$MAX_RPC_MSG_SIZE
        if [ $? -ne "0" ]
        then
                echo "Failed to insert module \
                      $colibri_module_path/$colibri_module.ko"
                return 1
        fi
}

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
