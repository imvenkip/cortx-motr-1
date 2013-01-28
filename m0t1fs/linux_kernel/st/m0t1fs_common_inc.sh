if [ -z "$PS1" ]; then
	# non-interactive shell
	# MERO_CORE_ROOT should be absolute path
	if [ ${0:0:1} = "/" ]; then
		MERO_CORE_ROOT=`dirname $0`
	else
		MERO_CORE_ROOT=$PWD/`dirname $0`
	fi
	MERO_CORE_ROOT=${MERO_CORE_ROOT%/m0t1fs*}
else
	# interactive shell
	if [ -z "$MERO_CORE_ROOT" ]; then
		MERO_CORE_ROOT=$PWD
		echo "MERO_CORE_ROOT variable is set to $MERO_CORE_ROOT"
	fi
fi
MERO_M0T1FS_MOUNT_DIR=/tmp/test_m0t1fs_`date +"%d-%m-%Y_%T"`
MERO_M0T1FS_TEST_DIR=/tmp/test_m0t1fs_$$
#MERO_M0T1FS_TEST_DIR=/tmp/test_m0t1fs

MERO_MODULE=m0mero


# kernel space tracing parameters
MERO_MODULE_TRACE_MASK='!all'
MERO_TRACE_PRINT_CONTEXT=short
MERO_TRACE_LEVEL=call+


#user-space tracing parameters
export M0_TRACE_IMMEDIATE_MASK='!all' # put your subsystem here
#export M0_TRACE_LEVEL=debug+
export M0_TRACE_PRINT_CONTEXT=short

MERO_TEST_LOGFILE=`pwd`/mero_`date +"%Y-%m-%d_%T"`.log

MERO_ADDBSERVICE_NAME=addb
MERO_IOSERVICE_NAME=ioservice
MERO_MDSERVICE_NAME=mdservice
MERO_STOB_DOMAIN=linux

PREPARE_STORAGE="-p"
POOL_WIDTH=4
NR_DATA=2
NR_PARITY=1
MAX_NR_FILES=250
TM_MIN_RECV_QUEUE_LEN=16
# Maximum value needed to run current ST is 160k.
MAX_RPC_MSG_SIZE=163840
XPT=lnet

# list of server end points
EP=(
    12345:33:101
    12345:33:102
    12345:33:103
    12345:33:104
)

unload_kernel_module()
{
	mero_module=$MERO_MODULE
	rmmod $mero_module.ko &>> /dev/null
	if [ $? -ne "0" ]
	then
	    echo "Failed to remove $mero_module."
	    return 1
	fi
}

load_kernel_module()
{
	modprobe lnet &>> /dev/null
	lctl network up &>> /dev/null
	lnet_nid=`sudo lctl list_nids | head -1`
	server_nid=${server_nid:-$lnet_nid}

	# Client end point (m0mero module local_addr)
	LADDR="$lnet_nid:12345:33:1"

	mero_module_path=$MERO_CORE_ROOT/build_kernel_modules
	mero_module=$MERO_MODULE
	lsmod | grep $mero_module &> /dev/null
	if [ $? -eq "0" ]
	then
		echo "Module $mero_module already present."
		echo "Removing existing module for clean test."
		unload_kernel_module || return $?
	fi

        insmod $mero_module_path/$mero_module.ko \
               trace_immediate_mask=$MERO_MODULE_TRACE_MASK \
	       trace_print_context=$MERO_TRACE_PRINT_CONTEXT \
	       trace_level=$MERO_TRACE_LEVEL \
	       node_uuid=${NODE_UUID:-00000000-0000-0000-0000-000000000000} \
               local_addr=$LADDR \
	       tm_recv_queue_min_len=$TM_MIN_RECV_QUEUE_LEN \
	       max_rpc_msg_size=$MAX_RPC_MSG_SIZE
        if [ $? -ne "0" ]
        then
                echo "Failed to insert module \
                      $mero_module_path/$mero_module.ko"
                return 1
        fi
}

prepare_testdir()
{
	echo "Cleaning up test directory ..."
	rm -rf $MERO_M0T1FS_TEST_DIR	 &> /dev/null

	echo "Creating test directory ..."
	mkdir $MERO_M0T1FS_TEST_DIR &> /dev/null

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
	modload_galois >& /dev/null
	echo 8 > /proc/sys/kernel/printk
	load_kernel_module || return $?
}

unprepare()
{
	sleep 2 # allow pending IO to complete
	if mount | grep m0t1fs > /dev/null; then
		umount $MERO_M0T1FS_MOUNT_DIR
		sleep 2
		rm -rf $MERO_M0T1FS_MOUNT_DIR
	fi

	if lsmod | grep m0mero > /dev/null; then
		unload_kernel_module
	fi
	modunload_galois

	if [ -d $MERO_M0T1FS_TEST_DIR ]; then
		# don't cleanup core dumps
		[ "`find $MERO_M0T1FS_TEST_DIR -name 'core.*'`" ] ||
			rm -rf $MERO_M0T1FS_TEST_DIR
	fi
}
