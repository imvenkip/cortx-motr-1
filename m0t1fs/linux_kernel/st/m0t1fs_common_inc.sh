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
MERO_TEST_LOGFILE=/var/mero/mero_`date +"%Y-%m-%d_%T"`.log

MERO_MODULE=m0mero


# kernel space tracing parameters
MERO_MODULE_TRACE_MASK='!all'
MERO_TRACE_PRINT_CONTEXT=short
MERO_TRACE_LEVEL=call+

#user-space tracing parameters
export M0_TRACE_IMMEDIATE_MASK="cm,sns,snscm"
export M0_TRACE_IMMEDIATE_MASK="!all"
export M0_TRACE_LEVEL=call+
export M0_TRACE_PRINT_CONTEXT=short

MERO_STOB_DOMAIN="ad -d disks.conf"


CONFD_EP=12345:33:100

# list of io server end points: e.g., tmid in [900, 999).
IOSEP=(
    12345:33:900   # IOS1 EP
    12345:33:901   # IOS2 EP
    12345:33:902   # IOS3 EP
    12345:33:903   # IOS4 EP
)

# list of io server end points tmid in [800, 899)
MDSEP=(
    12345:33:800   # MDS1 EP
    12345:33:801   # MDS2 EP
    12345:33:802   # MDS3 EP
)


SNS_CLI_EP="12345:33:1000"

POOL_WIDTH=${#IOSEP[*]}
NR_PARITY=1
NR_DATA=$(expr $POOL_WIDTH - $NR_PARITY \* 2)
UNIT_SIZE=$(expr 1024 \* 1024)

#MAX_NR_FILES=250
MAX_NR_FILES=20 # XXX temporary workaround for performance issues
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
	CONFD_EP=$lnet_nid:$CONFD_EP

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


PROF_OPT='<0x7000000000000001:0>'

###############################
# globals: MDSEP[], IOSEP[], server_nid
###############################
function build_conf()
{
	local nr_data_units=$1
	local nr_parity_units=$2
	local pool_width=$3

	# prepare configuration data
	local RMS_ENDPOINT="\"${server_nid}:${MDSEP[0]}\""
	local  PROF='(0x7000000000000001, 0)'
	local    FS='(0x6600000000000001, 1)'
	local    RM='(0x7300000000000001, 3)'
	local STATS='(0x7300000000000001, 4)'
	local  NODE='(0x6e00000000000001, 0)'

	local i

	for ((i=0; i < ${#IOSEP[*]}; i++)); do
	    local IOS_NAME="(0x7300000000000003, $i)"
	    local iosep="\"${server_nid}:${IOSEP[$i]}\""
	    local IOS_OBJ="{0x73| (($IOS_NAME), 2, [1: $iosep], $NODE)}"

	    if ((i == 0)); then
	        IOS_NAMES="$IOS_NAME"
	        IOS_OBJS="$IOS_OBJ"
	    else
	        IOS_NAMES="$IOS_NAMES, $IOS_NAME"
		IOS_OBJS="$IOS_OBJS, \n  $IOS_OBJ"
	    fi
	done

	for ((i=0; i < ${#MDSEP[*]}; i++)); do
	    local MDS_NAME="(0x7300000000000002, $i)"
	    local mdsep="\"${server_nid}:${MDSEP[$i]}\""
	    local MDS_OBJ="{0x73| (($MDS_NAME), 1, [1: $mdsep], $NODE)}"

	    if ((i == 0)); then
	        MDS_NAMES="$MDS_NAME"
	        MDS_OBJS="$MDS_OBJ"
	    else
	        MDS_NAMES="$MDS_NAMES, $MDS_NAME"
		MDS_OBJS="$MDS_OBJS, \n  $MDS_OBJ"
	    fi
	done


	echo -e "
 [$((${#IOSEP[*]} + ${#MDSEP[*]} + 3)):
  {0x70| (($PROF), $FS)},
  {0x66| (($FS), (11, 22),
	      [1: \"$pool_width $nr_data_units $nr_parity_units\"],
	      [$((${#IOSEP[*]} + ${#MDSEP[*]} + 1)): $MDS_NAMES, $RM, $IOS_NAMES])},
  {0x73| (($RM), 4, [1: $RMS_ENDPOINT], $NODE)},
  $MDS_OBJS,
  $IOS_OBJS]"
}

function run()
{
	echo "# $*"
	eval $*
}
