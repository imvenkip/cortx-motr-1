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
export M0_TRACE_IMMEDIATE_MASK="!all"
export M0_TRACE_LEVEL=call+
export M0_TRACE_PRINT_CONTEXT=short

# Note: m0t1fs_dgmode_io.sh refers to value of this variable.
# Hence, do not rename it.
MERO_STOB_DOMAIN="ad -d disks.conf"

MDS_DEVS=""
NR_MDS_DEVS=0
MDS_DEV_IDS=

IOS_DEVS=""
NR_IOS_DEVS=0
IOS_DEV_IDS=

DISK_FIDS=""
NR_DISK_FIDS=0
DISKV_FIDS=""
NR_DISKV_FIDS=0

DDEV_ID=1          #data devices
ADEV_ID=100        #addb devices
MDEV_ID=200        #meta-data devices

HA_EP=12345:34:1
CONFD_EP=12345:33:100
STATSEP=12345:33:800    #stats service runs on a single node.

MKFS_EP=12345:35:1  # MKFS EP

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

# Use separate client endpoints for m0repair and m0poolmach utilities
# to avoid network endpoint conflicts (-EADDRINUSE) in-case both the
# utilities are run at the same time, e.g. concurrent i/o with sns repair.
SNS_CLI_EP="12345:33:1000"
POOL_MACHINE_CLI_EP="12345:33:1001"

POOL_WIDTH=${#IOSEP[*]}
NR_PARITY=1
NR_DATA=$(expr $POOL_WIDTH - $NR_PARITY \* 2)
UNIT_SIZE=$(expr 1024 \* 1024)

#MAX_NR_FILES=250
MAX_NR_FILES=20 # XXX temporary workaround for performance issues
TM_MIN_RECV_QUEUE_LEN=16
MAX_RPC_MSG_SIZE=65536
XPT=lnet
MD_REDUNDANCY=1  # Meta-data redundancy, use greater than 1 after failure domain is available.

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
	echo "Cleaning up test directory $MERO_M0T1FS_TEST_DIR ..."
	rm -rf $MERO_M0T1FS_TEST_DIR	 &> /dev/null

	echo "Creating test directory $MERO_M0T1FS_TEST_DIR ..."
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
	modload_m0gf >& /dev/null
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
	modunload_m0gf
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
	local ioservices=("${!4}")
	local mdservices=("${!5}")
	local statservices=("${!6}")

	if [ -z "$ioservices" ]; then
		ioservices=("${IOSEP[@]}")
		for ((i = 0; i < ${#ioservices[*]}; i++)); do
			ioservices[$i]=${server_nid}:${ioservices[$i]}
		done
	fi

	if [ -z "$mdservices" ]; then
		mdservices=("${MDSEP[@]}")
		for ((i = 0; i < ${#mdservices[*]}; i++)); do
			mdservices[$i]=${server_nid}:${mdservices[$i]}
		done
	fi

	if [ -z "$statservices" ]; then
		statservices=("${STATSEP[@]}")
		for ((i = 0; i < ${#statservices[*]}; i++)); do
			statservices[$i]=${server_nid}:${statservices[$i]}
		done
	fi

	# prepare configuration data
	local RMS_ENDPOINT="\"${mdservices[0]}\""
	local STATS_ENDPOINT="\"${statservices[0]}\""
	local HA_ENDPOINT="\"${server_nid}:${HA_EP}\""
	local  ROOT='(0x7400000000000001, 0)'
	local  PROF='(0x7000000000000001, 0)'
	local    FS='(0x6600000000000001, 1)'
	local  NODE='(0x6e00000000000001, 2)'
	local  PROC='(0x7200000000000001, 3)'
	local    RM='(0x7300000000000001, 4)'
	local STATS='(0x7300000000000001, 5)'
	local HA_SVC_ID='(0x7300000000000001, 6)'
	local  RACKID='(0x6100000000000001, 6)'
	local  ENCLID='(0x6500000000000001, 7)'
	local  CTRLID='(0x6300000000000001, 8)'
	local  POOLID='(0x6f00000000000001, 9)'
	local  PVERID='(0x7600000000000001, 10)'
	#"pool_width" number of objv created for devv conf objects
	local  RACKVID="(0x6a00000000000001, $(($pool_width + 1)))"
	local  ENCLVID="(0x6a00000000000001, $(($pool_width + 2)))"
	local  CTRLVID="(0x6a00000000000001, $(($pool_width + 3)))"

	local i

	for ((i=0; i < ${#ioservices[*]}; i++)); do
	    local IOS_NAME="(0x7300000000000002, $i)"
	    local iosep="\"${ioservices[$i]}\""
	    local IOS_OBJ="{0x73| (($IOS_NAME), 2, [1: $iosep], ${IOS_DEV_IDS[$i]})}"

	    if ((i == 0)); then
	        IOS_NAMES="$IOS_NAME"
	        IOS_OBJS="$IOS_OBJ"
	    else
	        IOS_NAMES="$IOS_NAMES, $IOS_NAME"
		IOS_OBJS="$IOS_OBJS, \n  $IOS_OBJ"
	    fi
	done

	for ((i=0; i < ${#mdservices[*]}; i++)); do
	    local MDS_NAME="(0x7300000000000003, $i)"
	    local mdsep="\"${mdservices[$i]}\""
	    local MDS_OBJ="{0x73| (($MDS_NAME), 1, [1: $mdsep], ${MDS_DEV_IDS[$i]})}"

	    if ((i == 0)); then
	        MDS_NAMES="$MDS_NAME"
	        MDS_OBJS="$MDS_OBJ"
	    else
	        MDS_NAMES="$MDS_NAMES, $MDS_NAME"
		MDS_OBJS="$MDS_OBJS, \n  $MDS_OBJ"
	    fi
	done

	local RACK="{0x61| (($RACKID), [1: $ENCLID], [1: $PVERID])}"
	local ENCL="{0x65| (($ENCLID), [1: $CTRLID], [1: $PVERID])}"
	local CTRL="{0x63| (($CTRLID), $NODE, [$NR_DISK_FIDS: $DISK_FIDS], [1: $PVERID])}"

	local POOL="{0x6f| (($POOLID), 0, [1: $PVERID])}"
	local PVER="{0x76| (($PVERID), 0, $nr_data_units, $nr_parity_units, $pool_width, [3: 1, 2, 3], [0], [1: $RACKVID])}"
	local RACKV="{0x6a| (($RACKVID), $RACKID, [1: $ENCLVID])}"
	local ENCLV="{0x6a| (($ENCLVID), $ENCLID, [1: $CTRLVID])}"
	local CTRLV="{0x6a| (($CTRLVID), $CTRLID, [$NR_DISKV_FIDS: $DISKV_FIDS])}"

 # Here "15" configuration objects includes services excluding ios & mds,
 # pools, racks, enclosures, controllers and their versioned objects.
	echo -e "
 [$((${#ioservices[*]} + ${#mdservices[*]} + $NR_IOS_DEVS+ $NR_MDS_DEVS + 16)):
  {0x74| (($ROOT), 1, [1: $PROF])},
  {0x70| (($PROF), $FS)},
  {0x66| (($FS), (11, 22), $MD_REDUNDANCY,
	      [1: \"$pool_width $nr_data_units $nr_parity_units\"],
	      $POOLID,
	      [1: $NODE],
	      [1: $POOLID],
	      [1: $RACKID])},
  {0x6e| (($NODE), 16000, 2, 3, 2, $POOLID, [1: $PROC])},
  {0x72| (($PROC), 4000, 2,
	           [$((${#ioservices[*]} + ${#mdservices[*]} + 3)): $MDS_NAMES, $RM, $IOS_NAMES, $STATS, $HA_SVC_ID])},
  {0x73| (($RM), 4, [1: $RMS_ENDPOINT], [0])},
  {0x73| (($STATS), 5, [1: $STATS_ENDPOINT], [0])},
  {0x73| (($HA_SVC_ID), 6, [1: $HA_ENDPOINT], [0])},
  $MDS_OBJS,
  $IOS_OBJS,
  $IOS_DEVS,
  $MDS_DEVS,
  $RACK,
  $ENCL,
  $CTRL,
  $POOL,
  $PVER,
  $RACKV,
  $ENCLV,
  $CTRLV]"
}

function run()
{
	echo "# $*"
	eval $*
}
