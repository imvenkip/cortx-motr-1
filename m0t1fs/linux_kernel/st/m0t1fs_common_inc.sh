M0_SRC_DIR=`readlink -f ${BASH_SOURCE[0]}`
M0_SRC_DIR=${M0_SRC_DIR%/*/*/*/*}

. $M0_SRC_DIR/scripts/functions  # sandbox_init, report_and_exit

[ -n "$SANDBOX_DIR" ] || SANDBOX_DIR=/var/mero/systest-$$
## XXX TODO: Replace `MERO_M0T1FS_TEST_DIR' with `SANDBOX_DIR' everywhere
## and delete the former.
MERO_M0T1FS_TEST_DIR=$SANDBOX_DIR
MERO_TEST_LOGFILE=$SANDBOX_DIR/mero_`date +"%Y-%m-%d_%T"`.log
MERO_M0T1FS_MOUNT_DIR=/tmp/test_m0t1fs_`date +"%d-%m-%Y_%T"`

MERO_MODULE=m0mero
MERO_CTL_MODULE=m0ctl #debugfs interface to control m0mero at runtime

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

IOS_DEVS=""
NR_IOS_DEVS=0
NR_IOS_SDEVS=0
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
MKFS_PORTAL=35

# list of io server end points: e.g., tmid in [900, 999).
IOSEP=(
    12345:33:900   # IOS1 EP
    12345:33:901   # IOS2 EP
    12345:33:902   # IOS3 EP
    12345:33:903   # IOS4 EP
)

IOS_PVER2_EP="12345:33:904"
IOS5_CMD=""       #IOS5 process commandline to spawn it again on Controller event.

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
SNS_QUIESCE_CLI_EP="12345:33:1002"

POOL_WIDTH=${#IOSEP[*]}
NR_PARITY=1
NR_DATA=$(expr $POOL_WIDTH - $NR_PARITY \* 2)
UNIT_SIZE=$(expr 1024 \* 1024)

#MAX_NR_FILES=250
MAX_NR_FILES=20 # XXX temporary workaround for performance issues
TM_MIN_RECV_QUEUE_LEN=16
MAX_RPC_MSG_SIZE=65536
XPT=lnet
PVERID='^v|1:10'

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

	mero_module_path=$M0_SRC_DIR/mero
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

prepare()
{
	sandbox_init || return $?
	modload_m0gf >& /dev/null
	echo 8 > /proc/sys/kernel/printk
	load_kernel_module || return $?
}

unprepare()
{
	sleep 2 # allow pending IO to complete
	if mount | grep -q m0t1fs; then
		umount $MERO_M0T1FS_MOUNT_DIR
		sleep 2
		rm -rf $MERO_M0T1FS_MOUNT_DIR
	fi

	if lsmod | grep -q m0mero; then
		unload_kernel_module
	fi
	modunload_m0gf
	## The absence of `sandbox_fini' is intentional.
}

PROF_OPT='<0x7000000000000001:0>'

. `dirname ${BASH_SOURCE[0]}`/common_service_fids_inc.sh

# On cent OS 7 loop device is created during losetup, so no need to call
# create_loop_device().
create_loop_device ()
{
	local dev_id=$1

	mknod -m660 /dev/loop$dev_id b 7 $dev_id
	chown root.disk /dev/loop$dev_id
	chmod 666 /dev/loop$dev_id
}

###############################
# globals: MDSEP[], IOSEP[], server_nid
###############################
function build_conf()
{
	local nr_data_units=$1
	local nr_parity_units=$2
	local pool_width=$3
	local multiple_pools=$4
	local ioservices=("${!5}")
	local mdservices=("${!6}")
	local PVER1_OBJ_COUNT=0
	local PVER1_OBJS=
	local node_count=1
	local pool_count=1
	local rack_count=1
	local PROC_FID_CONT='^r|1'
	local MD_REDUNDANCY=1

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

	if [ $multiple_pools -eq "1" ]; then
		MD_REDUNDANCY=2
	fi;
	# prepare configuration data
	local RMS_ENDPOINT="\"${mdservices[0]}\""
	local CONFD_ENDPOINT="\"${mdservices[0]%:*:*}:33:100\""
	local HA_ENDPOINT="\"${mdservices[0]%:*:*}:34:1\""
	local  ROOT='^t|1:0'
	local  PROF='^p|1:0'
	local    FS='^f|1:1'
	local  NODE='^n|1:2'
	local CONFD="$CONF_FID_CON:0"
	local    RM="$RMS_FID_CON:0"
	local HA_SVC_ID='^s|1:6'
	local  RACKID='^a|1:6'
	local  ENCLID='^e|1:7'
	local  CTRLID='^c|1:8'
	local  POOLID='^o|1:9'
	#"pool_width" number of objv created for devv conf objects
	local  RACKVID="^j|1:$(($pool_width + 1))"
	local  ENCLVID="^j|1:$(($pool_width + 2))"
	local  CTRLVID="^j|1:$(($pool_width + 3))"

	local NODES="$NODE"
	local POOLS="$POOLID"
	local RACKS="$RACKID"
	local PROC_NAMES
	local PROC_OBJS
	local M0D=0

	local i

	for ((i=0; i < ${#ioservices[*]}; i++, M0D++)); do
	    local IOS_NAME="$IOS_FID_CON:$i"
	    local ADDB_NAME="$ADDB_IO_FID_CON:$i"
	    local SNS_REP_NAME="$SNSR_FID_CON:$i"
	    local SNS_REB_NAME="$SNSB_FID_CON:$i"
	    local iosep="\"${ioservices[$i]}\""
	    local IOS_OBJ="{0x73| (($IOS_NAME), 2, [1: $iosep], ${IOS_DEV_IDS[$i]})}"
	    local ADDB_OBJ="{0x73| (($ADDB_NAME), 10, [1: $iosep], [0])}"
	    local SNS_REP_OBJ="{0x73| (($SNS_REP_NAME), 8, [1: $iosep], [0])}"
	    local SNS_REB_OBJ="{0x73| (($SNS_REB_NAME), 9, [1: $iosep], [0])}"

	    PROC_NAME="$PROC_FID_CONT:$M0D"
	    IOS_NAMES[$i]="$IOS_NAME, $ADDB_NAME, $SNS_REP_NAME, $SNS_REB_NAME"
	    PROC_OBJ="{0x72| (($PROC_NAME), [1:3], 0, 0, 0, 0, $iosep, [4: ${IOS_NAMES[$i]}])}"
	    IOS_OBJS="$IOS_OBJS${IOS_OBJS:+, }\n  $IOS_OBJ, \n  $ADDB_OBJ, \n $SNS_REP_OBJ, \n  $SNS_REB_OBJ"
	    PROC_OBJS="$PROC_OBJS${PROC_OBJS:+, }\n  $PROC_OBJ"
	    PROC_NAMES="$PROC_NAMES${PROC_NAMES:+, }$PROC_NAME"
	done

	for ((i=0; i < ${#mdservices[*]}; i++, M0D++)); do
	    local MDS_NAME="$MDS_FID_CON:$i"
	    local ADDB_NAME="$ADDB_MD_FID_CON:$i"
	    local mdsep="\"${mdservices[$i]}\""
	    local MDS_OBJ="{0x73| (($MDS_NAME), 1, [1: $mdsep], [0])}"
	    local ADDB_OBJ="{0x73| (($ADDB_NAME), 10, [1: $mdsep], [0])}"

	    PROC_NAME="$PROC_FID_CONT:$M0D"
	    MDS_NAMES[$i]="$MDS_NAME, $ADDB_NAME"
	    if ((i == 0)); then
		local RM_OBJ="{0x73| (($RM), 4, [1: $RMS_ENDPOINT], [0])}"
		MDS_OBJS="$MDS_OBJ, \n  $ADDB_OBJ, \n  $RM_OBJ"
		PROC_OBJ="{0x72| (($PROC_NAME), [1:3], 0, 0, 0, 0, $mdsep, [3: ${MDS_NAMES[$i]}, $RM])}"
	    else
		MDS_OBJS="$MDS_OBJS, \n  $MDS_OBJ, \n  $ADDB_OBJ"
		PROC_OBJ="{0x72| (($PROC_NAME), [1:3], 0, 0, 0, 0, $mdsep, [2: ${MDS_NAMES[$i]}])}"
	    fi
	    PROC_OBJS="$PROC_OBJS, \n $PROC_OBJ"
	    PROC_NAMES="$PROC_NAMES, $PROC_NAME"
	done

	PROC_NAME="$PROC_FID_CONT:$((M0D++))"
	PROC_OBJ="{0x72| (($PROC_NAME), [1:3], 0, 0, 0, 0, "${HA_ENDPOINT}", [1: $HA_SVC_ID])}"
	PROC_OBJS="$PROC_OBJS, \n $PROC_OBJ"
	PROC_NAMES="$PROC_NAMES, $PROC_NAME"
	PROC_NAME="$PROC_FID_CONT:$((M0D++))"
	PROC_OBJ="{0x72| (($PROC_NAME), [1:3], 0, 0, 0, 0, "${CONFD_ENDPOINT}", [1: $CONFD])}"
	PROC_OBJS="$PROC_OBJS, \n $PROC_OBJ"
	PROC_NAMES="$PROC_NAMES, $PROC_NAME"
	local RACK="{0x61| (($RACKID), [1: $ENCLID], [1: $PVERID])}"
	local ENCL="{0x65| (($ENCLID), [1: $CTRLID], [1: $PVERID])}"
	local CTRL="{0x63| (($CTRLID), $NODE, [$NR_DISK_FIDS: $DISK_FIDS], [1: $PVERID])}"

	local POOL="{0x6f| (($POOLID), 0, [1: $PVERID])}"
	local PVER="{0x76| (($PVERID), 0, $nr_data_units, $nr_parity_units, $pool_width, [5: 1, 0, 0, 0, $nr_parity_units], [1: $RACKVID])}"
	local RACKV="{0x6a| (($RACKVID), $RACKID, [1: $ENCLVID])}"
	local ENCLV="{0x6a| (($ENCLVID), $ENCLID, [1: $CTRLVID])}"
	local CTRLV="{0x6a| (($CTRLVID), $CTRLID, [$NR_DISKV_FIDS: $DISKV_FIDS])}"

	if ((multiple_pools == 1)); then
		# IDs for anther pool version to test assignment
		# of pools to new objects.
		local  NODEID1='^n|10:1'
		local  PROCID1='^r|10:1'
		local  IO_SVCID1='^s|10:1'
		local  ADDB_SVCID1='^s|10:2'
		local  REP_SVCID1='^s|10:3'
		local  REB_SVCID1='^s|10:4'
		local  SDEVID1='^d|10:1'
		local  SDEVID2='^d|10:2'
		local  SDEVID3='^d|10:3'
		local  RACKID1='^a|10:1'
		local  ENCLID1='^e|10:1'
		local  CTRLID1='^c|10:1'
		local  DISKID1='^k|10:1'
		local  DISKID2='^k|10:2'
		local  DISKID3='^k|10:3'
		local  POOLID1='^o|10:1'
		local  PVERID1='^v|10:1'
		local  RACKVID1='^j|10:1'
		local  ENCLVID1='^j|10:2'
		local  CTRLVID1='^j|10:3'
		local  DISKVID1='^j|10:4'
		local  DISKVID2='^j|10:5'
		local  DISKVID3='^j|10:6'
		local  IOS_EP="\"${server_nid}:$IOS_PVER2_EP\""
		# conf objects for anther pool version to test assignment
		# of pools to new objects.
		local NODE1="{0x6e| (($NODEID1), 16000, 2, 3, 2, $POOLID1, [1: $PROCID1])}"
		local PROC1="{0x72| (($PROCID1), [1:3], 0, 0, 0, 0, $IOS_EP, [4: $IO_SVCID1, $ADDB_SVCID1, $REP_SVCID1, $REB_SVCID1])}"
		local IO_SVC1="{0x73| (($IO_SVCID1), 2, [1: $IOS_EP], [3: $SDEVID1, $SDEVID2, $SDEVID3])}"
		local ADDB_SVC1="{0x73| (($ADDB_SVCID1), 10, [1: $IOS_EP], [0])}"
		local REP_SVC1="{0x73| (($REP_SVCID1), 8, [1: $IOS_EP], [0])}"
		local REB_SVC1="{0x73| (($REB_SVCID1), 9, [1: $IOS_EP], [0])}"
		local SDEV1="{0x64| (($SDEVID1), $((NR_IOS_SDEVS++)), 4, 1, 4096, 596000000000, 3, 4, \"/dev/loop5\")}"
		local SDEV2="{0x64| (($SDEVID2), $((NR_IOS_SDEVS++)), 4, 1, 4096, 596000000000, 3, 4, \"/dev/loop6\")}"
		local SDEV3="{0x64| (($SDEVID3), $((NR_IOS_SDEVS++)), 4, 1, 4096, 596000000000, 3, 4, \"/dev/loop7\")}"
		local RACK1="{0x61| (($RACKID1), [1: $ENCLID1], [1: $PVERID1])}"
		local ENCL1="{0x65| (($ENCLID1), [1: $CTRLID1], [1: $PVERID1])}"
		local CTRL1="{0x63| (($CTRLID1), $NODEID1, [3: $DISKID1, $DISKID2, $DISKID3], [1: $PVERID1])}"
                local DISK1="{0x6b| (($DISKID1), $SDEVID1, [1: $PVERID1])}"
                local DISK2="{0x6b| (($DISKID2), $SDEVID2, [1: $PVERID1])}"
                local DISK3="{0x6b| (($DISKID3), $SDEVID3, [1: $PVERID1])}"

		local POOL1="{0x6f| (($POOLID1), 0, [1: $PVERID1])}"
		local PVER1="{0x76| (($PVERID1), 0, 1, 1, 3, [5: 1, 0, 0, 0, 1], [1: $RACKVID1])}"
		local RACKV1="{0x6a| (($RACKVID1), $RACKID1, [1: $ENCLVID1])}"
		local ENCLV1="{0x6a| (($ENCLVID1), $ENCLID1, [1: $CTRLVID1])}"
		local CTRLV1="{0x6a| (($CTRLVID1), $CTRLID1, [3: $DISKVID1, $DISKVID2, $DISKVID3])}"
		local  DISKV1="{0x6a| (($DISKVID1), $DISKID1, [0])}"
		local  DISKV2="{0x6a| (($DISKVID2), $DISKID2, [0])}"
		local  DISKV3="{0x6a| (($DISKVID3), $DISKID3, [0])}"
		PVER1_OBJS=", \n$NODE1, \n$PROC1, \n$IO_SVC1, \n$ADDB_SVC1, \n$REP_SVC1, \n$REB_SVC1, \n$SDEV1, \n$SDEV2, \n$SDEV3, \n$RACK1, \n$ENCL1, \n$CTRL1, \n$DISK1, \n$DISK2, \n$DISK3, \n$POOL1, \n$PVER1, \n$RACKV1, \n$ENCLV1, \n$CTRLV1, \n$DISKV1, \n$DISKV2, \n$DISKV3"
		PVER1_OBJ_COUNT=23
		node_count=2
		rack_count=2
		pool_count=2

		NODES="$NODES, $NODEID1"
		POOLS="$POOLS, $POOLID1"
		RACKS="$RACKS, $RACKID1"
		# Total 23 objects for this anther pool version
	fi

 # Here "15" configuration objects includes services excluding ios & mds,
 # pools, racks, enclosures, controllers and their versioned objects.
	echo -e "
 [$(($((${#ioservices[*]} * 5)) + $((${#mdservices[*]} * 3)) + $NR_IOS_DEVS + 17 + $PVER1_OBJ_COUNT)):
  {0x74| (($ROOT), 1, [1: $PROF])},
  {0x70| (($PROF), $FS)},
  {0x66| (($FS), (11, 22), $MD_REDUNDANCY,
	      [1: \"$pool_width $nr_data_units $nr_parity_units\"],
	      $POOLID,
	      [$node_count: $NODES],
	      [$pool_count: $POOLS],
	      [$rack_count: $RACKS])},
  {0x6e| (($NODE), 16000, 2, 3, 2, $POOLID,
	[$M0D: ${PROC_NAMES[@]}])},
  $PROC_OBJS,
  {0x73| (($CONFD), 3, [1: $CONFD_ENDPOINT], [0])},
  {0x73| (($HA_SVC_ID), 6, [1: $HA_ENDPOINT], [0])},
  $MDS_OBJS,
  $IOS_OBJS,
  $IOS_DEVS,
  $RACK,
  $ENCL,
  $CTRL,
  $POOL,
  $PVER,
  $RACKV,
  $ENCLV,
  $CTRLV $PVER1_OBJS]"
}

# Uses console utility to generate proxy HA notification.
# input parameters:
# (i)   ha_fop to be sent
# (ii)  list of endpoints
# (iii) endpoint where reply is to be received.
dispatch_ha_events()
{
	local ha_fop=$1
        local eplist=($2)
        local self_endpoint=$3

        for ep in "${eplist[@]}"
        do
                # dispatch ha_fop
                cmd="$M0_SRC_DIR/console/bin/m0console \
                     -f $(opcode M0_HA_NOTE_SET_OPCODE) \
                     -s $ep -c $self_endpoint \
                     -d \"$ha_fop\""
                #echo $cmd
                (eval "$cmd")
        done
}

function run()
{
	echo "# $*"
	eval $*
}
