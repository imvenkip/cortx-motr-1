# COLIBRI_CORE_ROOT should be absolute path
if [ ${0:0:1} = "/" ]; then
	COLIBRI_CORE_ROOT=`dirname $0`
else
	COLIBRI_CORE_ROOT=$PWD/`dirname $0`
fi
COLIBRI_CORE_ROOT=${COLIBRI_CORE_ROOT%/c2t1fs*}
COLIBRI_C2T1FS_MOUNT_DIR=/tmp/test_c2t1fs_`date +"%d-%m-%Y_%T"`
#COLIBRI_C2T1FS_TEST_DIR=/tmp/test_c2t1fs_$$
COLIBRI_C2T1FS_TEST_DIR=/tmp/test_c2t1fs
COLIBRI_MODULE=kcolibri

COLIBRI_MODULE_TRACE_MASK=0
COLIBRI_TRACE_PRINT_CONTEXT=none
COLIBRI_TRACE_LEVEL=call+
export C2_TRACE_IMMEDIATE_MASK=all # put your subsystem here
export C2_TRACE_LEVEL=debug+
export C2_TRACE_PRINT_CONTEXT=full

COLIBRI_TEST_LOGFILE=`pwd`/bulkio_`date +"%Y-%m-%d_%T"`.log

COLIBRI_IOSERVICE_NAME=ioservice
COLIBRI_MDSERVICE_NAME=mdservice
COLIBRI_STOB_DOMAIN=linux

PREPARE_STORAGE="-p"
POOL_WIDTH=4
NR_DATA=2
NR_PARITY=1
MAX_NR_FILES=250
TM_MIN_RECV_QUEUE_LEN=16
# Maximum value needed to run current ST is 160k.
MAX_RPC_MSG_SIZE=163840
SERVICES=""
STRIPE="pool_width=$POOL_WIDTH,nr_data_units=$NR_DATA"
XPT=lnet
lnet_nid=0@lo

# Client end point (kcolibri module local_addr)
LADDR="$lnet_nid:12345:33:1"

# list of server end points
EP=(
    12345:33:101
    12345:33:102
    12345:33:103
    12345:33:104
)

unload_kernel_module()
{
	colibri_module=$COLIBRI_MODULE
	rmmod $colibri_module.ko &>> /dev/null
	if [ $? -ne "0" ]
	then
	    echo "Failed to remove $colibri_module."
	    return 1
	fi
}

load_kernel_module()
{
	modprobe lnet

	colibri_module_path=$COLIBRI_CORE_ROOT/build_kernel_modules
	colibri_module=$COLIBRI_MODULE
	lsmod | grep $colibri_module &> /dev/null
	if [ $? -eq "0" ]
	then
		echo "Module $colibri_module already present."
		echo "Removing existing module for clean test."
		unload_kernel_module || return $?
	fi

        insmod $colibri_module_path/$colibri_module.ko \
               trace_immediate_mask=$COLIBRI_MODULE_TRACE_MASK \
	       trace_print_context=$COLIBRI_TRACE_PRINT_CONTEXT \
	       trace_level=$COLIBRI_TRACE_LEVEL \
               local_addr=$LADDR \
	       tm_recv_queue_min_len=$TM_MIN_RECV_QUEUE_LEN \
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
	if mount | grep c2t1fs > /dev/null; then
		umount $COLIBRI_C2T1FS_MOUNT_DIR
		sleep 2
		rm -rf $COLIBRI_C2T1FS_MOUNT_DIR
	fi

	if lsmod | grep kcolibri > /dev/null; then
		unload_kernel_module
	fi
	modunload_galois

	rm -rf $COLIBRI_C2T1FS_TEST_DIR
}
