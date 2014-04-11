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
MERO_M0T1FS_TEST_DIR=/var/mero/systest-$$

MERO_MODULE=m0mero


# kernel space tracing parameters
MERO_MODULE_TRACE_MASK='!all'
MERO_TRACE_PRINT_CONTEXT=short
MERO_TRACE_LEVEL=call+

#user-space tracing parameters
export M0_TRACE_IMMEDIATE_MASK=all
export M0_TRACE_LEVEL=warn+
export M0_TRACE_PRINT_CONTEXT=short

MERO_TEST_LOGFILE=`pwd`/mero_`date +"%Y-%m-%d_%T"`.log

MERO_ADDBSERVICE_NAME=addb
MERO_IOSERVICE_NAME=ioservice
MERO_MDSERVICE_NAME=mdservice
MERO_SNSREPAIRSERVICE_NAME=sns_repair
MERO_SNSREBALANCESERVICE_NAME=sns_rebalance
MERO_STATSSERVICE_NAME=stats
MERO_RMSERVICE_NAME=rmservice
MERO_CONFD_NAME=confd

MERO_STOB_DOMAIN="ad -d disks.conf"

# list of server end points
EP=(
    12345:33:1001   # MDS  EP
    12345:33:1002   # IOS1 EP
    12345:33:1003   # IOS2 EP
    12345:33:1004   # IOS3 EP
    12345:33:1005   # IOS4 EP
)

SNS_CLI_EP="12345:33:991"

POOL_WIDTH=$(expr ${#EP[*]} - 1)
NR_PARITY=1
NR_DATA=$(expr $POOL_WIDTH - $NR_PARITY \* 2)
UNIT_SIZE=$(expr 1024 \* 1024)

#MAX_NR_FILES=250
MAX_NR_FILES=2 # XXX temporary workaround for performance issues
TM_MIN_RECV_QUEUE_LEN=16
# Maximum value needed to run current ST is 160k.
MAX_RPC_MSG_SIZE=163840
XPT=lnet

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
	# last component in this addr will be generated and filled in m0mero.
	LADDR="$lnet_nid:12345:33:"

	mero_module_path=$MERO_CORE_ROOT/mero
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
	mkdir -p $MERO_M0T1FS_TEST_DIR &> /dev/null

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
}

function build_conf () {
	local i

	# prepare configuration data
	MDS_ENDPOINT="\"${server_nid}:${EP[0]}\""
	RMS_ENDPOINT="\"${server_nid}:${EP[0]}\""
	PROF='(0x7000000000000001, 0)'
	PROF_OPT='0x7000000000000001:0'
	FS='(0x6600000000000001, 1)'
	MDS='(0x7300000000000001, 2)'
	RM='(0x7300000000000001, 3)'
	STATS='(0x7300000000000001, 4)'
	NODE='(0x6e00000000000001, 0)'
	for ((i=1; i < ${#EP[*]}; i++)); do
	    IOS_NAME="(0x7300000000000003, $i)"

	    if ((i == 1)); then
	        IOS_NAMES="$IOS_NAME"
	    else
	        IOS_NAMES="$IOS_NAMES, $IOS_NAME"
	    fi

	    local ep=\"${server_nid}:${EP[$i]}\"
	    IOS_OBJ="{0x73| (($IOS_NAME), 2, [1: $ep], $NODE)}"
	    if ((i == 1)); then
	        IOS_OBJS="$IOS_OBJ"
	    else
		IOS_OBJS="$IOS_OBJS, $IOS_OBJ"
	    fi
	done

	echo "[$((${#EP[*]} + 3)):
  {0x70| (($PROF), $FS)},
  {0x66| (($FS), (11, 22),
	      [4: \"pool_width=$POOL_WIDTH\",
		  \"nr_data_units=$NR_DATA\",
		  \"nr_parity_units=$NR_PARITY\",
		  \"unit_size=$UNIT_SIZE\"],
	      [$((${#EP[*]} + 1)): $MDS, $RM, $IOS_NAMES])},
  {0x73| (($MDS), 1, [1: $MDS_ENDPOINT], $NODE)},
  {0x73| (($RM), 4, [1: $RMS_ENDPOINT], $NODE)},
  $IOS_OBJS]"
}
