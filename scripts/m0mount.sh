#!/bin/bash

# Script to startup local/remote services and mount Mero file system
#
# - The script requires the build path to be available on all the nodes.
#   (This can be easily shared via NFS from some one node.)
# - The script uses ssh and scp to operate remotely. It requires that the
#   remote service nodes have this node's public certificate in
#   their .ssh/authorized_keys file.  If services are defined on the local
#   host then it should be possible to ssh into the local host.

# Before running the script, make sure you configured
# the IP/IB addresses of your setup correctly here (see below).

# Here is some configuration info (as for the time of writing this)
# about Titan nodes in Fremont lab:
#
# +----------------------------------------------------------------+
# | Hostname   |   IP-address   |    IB-address   |  IPMI-address  |
# +------------+----------------+-----------------+----------------+
# | sjt00-c1   |  10.76.50.161  |  172.18.50.161  |  10.76.50.162  |
# | sjt00-c2   |  10.76.50.163  |  172.18.50.163  |  10.76.50.164  |
# | sjt02-c1   |  10.76.50.40   |  172.18.50.40   |  10.76.50.42   |
# | sjt02-c2   |  10.76.50.45   |  172.18.50.45   |  10.76.50.47   |
# +----------------------------------------------------------------+
#
# Use root/Xyratex to login to any node.
#
# To see the serial console of sjt02-c1 node, for example,
# use this command:
# $ ipmitool -I lanplus -H 10.76.50.42 -U admin -P admin sol activate
#
# To reset the power:
# $ ipmitool -H 10.76.50.42 -U admin -P admin power reset
#
# Provide a list of servers, represented by the 2-tuple of node IP and end
# point, in the SERVICES array.
#
#   e.g. 	sjt02-c2 172.18.50.45@o2ib:12345:33:101
#
# - There are an even number of records in the list.
# - The number of server records is the POOL_WIDTH.

# This example uses co-located remote ioservices.
#SERVICES=(
#	sjt02-c2 172.18.50.45@o2ib:12345:41:101
#	sjt02-c2 172.18.50.45@o2ib:12345:41:102
#	sjt02-c2 172.18.50.45@o2ib:12345:41:103
#	sjt02-c2 172.18.50.45@o2ib:12345:41:104
#)
# IMPORTANT! Keep the same nodes together in the list
# (the code depends on this).
#

usage()
{
	cat <<.
Usage:

The script should be run from mero directory:

$ cd ~/path/to/mero
$ sudo ~/path/to/m0mount.sh [-L] [-a] [-l] [-d NUM] [-p NUM] [-n NUM] [-u NUM] [-q]

Where:
-a: Use AD stobs
    configure the services to run on ad stobs.
    it automatically detects and make configuration files
    for the Titan discs.
    Before using ad option make sure the discs are online:
    $ sudo ~root/gem.sh dumpdrives
    Turn them on if needed:
    $ sudo ~root/gem.sh powerondrive all
    If 'local' option is set also - /dev/loopX discs
    should be prepeared for ad stobs beforehand.

-l: Use loop device for ad stob configuration

-L: Use local machine configuration.
    start the services on the local host only,
    it is convenient for debugging on a local devvm.
    The number of services is controlled by LOCAL_SERVICES_NR
    variable. The default number is $LOCAL_SERVICES_NR.

-h: Print this help.

-n NUM: Start 'NUM' number of local m0d. (default is $LOCAL_SERVICES_NR)

-d NUM: Use NUM number of data units. (default is $NR_DATA)

-p NUM: Use NUM as pool width. (default is $POOL_WIDTH)

-u NUM: Use NUM Unit size. (default is $UNIT_SIZE)

-q: Dont wait after mounting m0t1fs, exit immediately. (default is wait)

.
}

OPTIONS_STRING="aln:d:p:u:qhL"

# This example puts ioservices on 3 nodes, and uses 4 data blocks
SERVICES=(
	sjt02-c1 172.18.50.40@o2ib:12345:41:101
	sjt02-c1 172.18.50.40@o2ib:12345:41:102
	sjt02-c2 172.18.50.45@o2ib:12345:41:101
	sjt02-c2 172.18.50.45@o2ib:12345:41:102
	sjt00-c1 172.18.50.161@o2ib:12345:41:101
	sjt00-c1 172.18.50.161@o2ib:12345:41:102
)

declare -A NODE_UUID
NODE_UUID[sjt02-c1]=30ab1a00-8085-40d1-a557-8996e2369a7a
NODE_UUID[sjt02-c2]=6485c5f9-fbde-4e39-8c6a-bb9d46b3bf8a
NODE_UUID[sjt00-c1]=54b0a56a-56ba-41a8-9caf-3f3981cfdf69

THIS_HOST=$(hostname)
DISKS_PATTERN="/dev/disk/by-id/scsi-35*"

STOB=linux
LOCAL_SERVICES_NR=4
SERVICES_NR=$(expr ${#SERVICES[*]} / 2)
NR_DATA=3
POOL_WIDTH=5
UNIT_SIZE=262144
use_loop_device=0
setup_local_server_config=0
wait_after_mount=1

M0_TRACE_IMMEDIATE_MASK=0
M0_TRACE_LEVEL=debug+
M0_TRACE_PRINT_CONTEXT=full

# number of disks to split by for each service
# in ad-stob mode
DISKS_SH_NR=1 #`expr $POOL_WIDTH / $SERVICES_NR + 1`
# +1 for ADDB stob

# Local mount data
MP=/mnt/m0

# The file system TMID
FSTMID=1

# Remote work arena
WORK_ARENA=/var/tmp/m0

# transport related variables
XPT=lnet
# m0d flags
XPT_SETUP="-m 163840 -q 16"
# m0mero module params
XPT_PARAM_R="max_rpc_msg_size=163840 tm_recv_queue_min_len=1"	# remote host
XPT_PARAM_L="max_rpc_msg_size=163840 tm_recv_queue_min_len=48"	# local host

#KTRACE_FLAGS=m0_trace_immediate_mask=8

BROOT=$PWD   # globally visible build root

# track hosts that have been initialized in an associative array
declare -A SETUP

# track hosts on which servers have been started
declare -A STARTED

# A file whose sum we can check
SUMFILE=$BROOT/mero/.libs/m0d
LSUM=

###########
# functions

setup_local_params()
{
	modprobe lnet
	lctl network up &>> /dev/null
	LOCAL_NID=`lctl list_nids | head -1`
	LOCAL_EP_PREFIX=12345:41:10
	NODE_UUID[$THIS_HOST]=02e94b88-19ab-4166-b26b-91b51f22ad91

	unset SERVICES
	# Update each field of SERVICES array with local node values
	# Update hostname and end point addresses
	for ((i = 0; i < $LOCAL_SERVICES_NR; i++)); do
		SERVICES[((i*2))]=$THIS_HOST
		SERVICES[((i*2 +1))]="${LOCAL_NID}:${LOCAL_EP_PREFIX}"$((i+1))
	done

	if [ $use_loop_device -eq 1 ]; then
		DISKS_PATTERN="/dev/loop[0-$((LOCAL_SERVICES_NR*2 -1))]"
	fi
}

function l_run () {
	echo "# $*" >/dev/tty
	eval $*
}

function r_run () {
	H=$1
	shift
	echo "# ssh root@$H $*" >/dev/tty
	ssh root@$H $*
}

function setup_host () {
	H=$1
	EP=$2  # template for kernel end point address
	if [ "x${NODE_UUID[$H]}" == "x" ]; then
		echo ERROR: unknown uuid of the node $H
		return 1
	fi
	echo Setting up host $H
	# check for local host
	local RUN
	local XPT_PARAM
	if [ $H != $THIS_HOST ]; then
		RUN="r_run $H"
		XPT_PARAM=$XPT_PARAM_R
	else
		RUN=l_run
		XPT_PARAM=$XPT_PARAM_L
	fi
	# check if a mero process is running
	local SVRS=$($RUN pgrep -f m0d)
	if [ -n "$SVRS" ]; then
		echo $SVRS
		echo ERROR: m0d process already running on $H
		return 1
	fi
	if [ $H != $THIS_HOST ]; then
		# ensure that the build path is accessible
		local RSUM=$($RUN sum $SUMFILE)
		if [ "$RSUM" != "$LSUM" ]; then
			echo ERROR: Build tree not accessible on $H
			return 1
		fi
	fi
	# use the specified end point as a template for the file system addr
	KEP="${EP%:*}:$FSTMID"
	# enable lnet and load our kernel modules
	$RUN modprobe lnet
	$RUN lctl network up
	if [ $? -ne 0 ]; then
		echo  ERROR: Unable to configure LNet
		return 1
	fi
	$RUN rmmod m0mero galois 2>/dev/null
	$RUN insmod $BROOT/extra-libs/galois/src/linux_kernel/galois.ko
	if [ $? -ne 0 ]; then
		echo ERROR: Failed to load galois module on $H
		return 1
	fi
	$RUN insmod $BROOT/mero/m0mero.ko local_addr=$KEP $XPT_PARAM $KTRACE_FLAGS node_uuid=${NODE_UUID[$H]}
	if [ $? -ne 0 ]; then
		echo ERROR: Failed to load m0mero module on $H
		$RUN rmmod galois
		return 1
	fi
	return 0
}

function setup_hosts () {
	for ((i=0; i < ${#SERVICES[*]}; i += 2)); do
		H=${SERVICES[$i]}
		EP=${SERVICES[((i+1))]}
		if [ X${SETUP[$H]} = X ]; then
			setup_host $H $EP || return 1
			SETUP[$H]=$H
		fi
	done
	return 0
}

function teardown_host () {
	H=$1
	echo Tearing down host $H
	if [ $H != $THIS_HOST ]; then
		RUN="r_run $H"
	else
		RUN=l_run
	fi
	$RUN rmmod m0mero galois
	return 0
}

function teardown_hosts () {
	for H in ${SETUP[*]} ; do
		teardown_host $H
	done
}

function gen_disks_conf_files()
{
	local dev_id=$1
	local DISKS_SH=$WORK_ARENA/find_disks.sh
	local SF=/tmp/nh.$$

	cat <<EOF >$SF
#!/bin/bash

#This script helps to create a disks configuration file on Titan controllers.
#
#The file uses yaml format, as desired by the m0d program.
#The script uses the fdisk command and extracts only the unused disks
#present on the system (i.e without valid partition table).
#
#Below illustration describes a typical disks.conf entry,
#
#===========================================================
#Device:
#       - id: 0
#	  filename: /dev/sda
#       - id: 1
#	  filename: /dev/sdb
#===========================================================
#
# Disk id=0 is used for ADDB stob.
#

# number of disks to split by
DISKS_SH_NR=$DISKS_SH_NR

i=$dev_id; j=0; f=0;

echo "Device:" > disks.conf

devs=\`ls $DISKS_PATTERN | grep -v part\`

fdisk -l \$devs 2>&1 1>/dev/null | \
sed -n 's;^Disk \\(/dev/.*\\) doesn.*;\1;p' | \
while read dev; do
	if [ \$j -eq 0 ]; then
		echo "Device:" > disks\$f.conf
		echo "   - id: 0" >> disks\$f.conf
		echo "     filename: \$dev" >> disks\$f.conf
	else
		echo "   - id: \$i" | tee -a disks.conf >> disks\$f.conf
		echo "     filename: \$dev" | tee -a disks.conf >> disks\$f.conf
		i=\`expr \$i + 1\`
	fi
	j=\`expr \$j + 1\`
	[ \$j -eq \$DISKS_SH_NR ] && j=0 && f=\`expr \$f + 1\`
done

exit 0
EOF

	if [ $H != $THIS_HOST ]; then
		l_run scp $SF $H:$DISKS_SH
	else
		$RUN cp $SF $DISKS_SH
	fi
	if [ $? -ne 0 ]; then
		echo ERROR: Failed to copy script file to $H
		return 1
	fi

	$RUN "(cd $WORK_ARENA && sh $DISKS_SH)"
	if [ $? -ne 0 ]; then
		echo ERROR: Failed to get disks list on $H
		return 1
	fi
}

function start_server () {
	H=$1
	CEP=$2
	EP=$3
	I=$4
	local dcf_id=$5

	echo Starting server with end point $EP on host $H
	local RUN
	if [ $H != $THIS_HOST ]; then
		RUN="r_run $H"
	else
		RUN=l_run
	fi
	local SDIR=$WORK_ARENA/d$I
	local DDIR=$SDIR
	$RUN rm -rf $SDIR
	$RUN mkdir -p $SDIR
	local DF=$SDIR/m0d.sh
	local DISKS_SH_FILE=$WORK_ARENA/disks$dcf_id.conf
	local STOB_PARAMS="-T linux"
	if [ $STOB == "ad" -o $STOB == "-td" ]; then

		if ! $RUN [ -f $DISKS_SH_FILE ]; then
			local dev_id=1
			if [ $dcf_id -eq 0 ]; then
				# -1 to exclude addb-stob from numbering
				dev_id=$(($I * ($DISKS_SH_NR -1) +1))
			fi
			gen_disks_conf_files $dev_id || return 1
		fi

		if [ $STOB == "ad" ]; then
			$RUN cat $DISKS_SH_FILE
		else
			local disk=`$RUN "cat $DISKS_SH_FILE | grep filename"`
			disk=`echo $disk | head -1 | awk '{print $2}'`
			DDIR=/mnt/tdisk$dcf_id
			$RUN umount $DDIR >& /dev/null
			$RUN mkfs.ext4 -b 4096 -F $disk 2621440 || return 1
			$RUN mkdir -p $DDIR
			$RUN mount $disk $DDIR || return 1
		fi
		if [ $? -ne 0 ]; then
			echo "ERROR: can't find $DISKS_SH_FILE file"
			echo "Check the status of Titan disks:"
			$RUN ~root/gem.sh dumpdrives
			return 1
		fi
		if [ $STOB == "ad" ]; then
			STOB_PARAMS="-T ad -d $DISKS_SH_FILE"
		fi
	fi

	local SNAME="-s addb -s ioservice -s sns_cm"
	if [ $I -eq 0 ]; then
		SNAME="-s mdservice $SNAME"
	fi

	$RUN "cd $DDIR && \
M0_TRACE_IMMEDIATE_MASK=$M0_TRACE_IMMEDIATE_MASK \
M0_TRACE_LEVEL=$M0_TRACE_LEVEL \
M0_TRACE_PRINT_CONTEXT=$M0_TRACE_PRINT_CONTEXT \
$BROOT/mero/m0d -r -p \
$STOB_PARAMS -D $DDIR/db -S $DDIR/stobs -A $DDIR/stobs \
-w $POOL_WIDTH -G $XPT:$MDS_EP
-e $XPT:$EP $IOS_EPs $SNAME $XPT_SETUP" > ${SLOG}$I.log &
	if [ $? -ne 0 ]; then
		echo ERROR: Failed to start remote server on $H
		return 1
	fi

	STARTED[$H]=$H
}

function start_servers () {
	local devs_conf_cnt=0
	if [ $STOB == "ad" -o $STOB == "-td" ]; then
		for ((i=0; i < ${#SERVICES[*]}; i += 2)); do
			H=${SERVICES[$i]}
			local RUN
			[ $H == $THIS_HOST ] && RUN=l_run || RUN="r_run $H"
			$RUN rm -f $WORK_ARENA/disks*.conf
		done
	fi

	MDS_EP=${SERVICES[1]}
	IOS_EPs=" -i $XPT:$MDS_EP"
	for i in `seq 3 2 ${#SERVICES[*]}`; do
		IOS_EPs="$IOS_EPs -i $XPT:${SERVICES[$i]}"
	done

	SLOG=$WORK_ARENA/server
	for ((i=0; i < ${#SERVICES[*]}; i += 2)); do
		H=${SERVICES[$i]}
		SEP=${SERVICES[((i+1))]}	# server EP
		SEP1=${SEP%:*}
		TM=${SEP##*:}
		TM=$((TM + 100))
		CEP=$SEP1:$TM			# client EP
		# new Titan couple?
		[ $i -gt 0 ] && [ ${H%-*} != ${SERVICES[((i-2))]%-*} ] && \
			devs_conf_cnt=0
		start_server $H $CEP $SEP $((i / 2)) $devs_conf_cnt
		if [ $? -ne 0 ]; then
			return 1
		fi
		devs_conf_cnt=`expr $devs_conf_cnt + 1`
	done

	echo "Wait for the services to start up..."
	while true; do
		local STARTED_NR=`cat ${SLOG}*.log | grep CTRL | wc -l`
		echo "Started $STARTED_NR (of $SERVICES_NR) services..."
		[ $STARTED_NR -ge $SERVICES_NR ] && break
		l_run sleep 5
	done

	return 0
}

function stop_server () {
	H=$1
	echo Stopping servers on host $H
	local RUN
	if [ $H != $THIS_HOST ]; then
		RUN="r_run $H"
	else
		RUN=l_run
	fi
	$RUN pkill -INT -f m0d
}

function wait4server () {
	H=$1
	echo Wait for servers to finish on host $H
	local RUN
	if [ $H != $THIS_HOST ]; then
		RUN="r_run $H"
	else
		RUN=l_run
	fi
	$RUN 'while [ "`ps ax | grep -v grep | grep m0d`" ];
	      do echo -n .; sleep 2; done'
	echo
}

function stop_servers () {
	for H in ${STARTED[*]}; do
		stop_server $H
	done
	for H in ${STARTED[*]}; do
		wait4server $H
	done
}

function cleanup () {
	echo Cleaning up ...
	if [ "x_$IS_MOUNTED" == "x_yes" ]; then
		l_run umount $MP
		if [ $? -ne 0 ]; then
			cat > /dev/stderr << EOF
WARNING! Failed to unmount $MP
         Services won't be stopped.
         You should umount and stop the services manually..
EOF
			return 1
		fi
	fi
	stop_servers || return 1
	teardown_hosts
}

######
# main

main()
{
	if [ $setup_local_server_config -eq 1 ]; then
		setup_local_params
	fi

	SERVICES_NR=$(expr ${#SERVICES[*]} / 2)

	DISKS_SH_NR=`expr $POOL_WIDTH + 1`

	if [ ! $BROOT -ef $PWD ]; then
		echo ERROR: Run this script in the top of the Mero source directory
		exit 1
	fi
	LSUM=$(sum $SUMFILE)

	rmmod m0loop m0mero galois &> /dev/null

	# ldemo now needs kernel module loaded for some reason...
	l_run insmod $BROOT/extra-libs/galois/src/linux_kernel/galois.ko || {
		echo ERROR: Failed to load galois module
		exit 1
	}
	l_run modprobe lnet
	l_run insmod $BROOT/mero/m0mero.ko || {
		echo ERROR: Failed to load m0mero module
		rmmod galois
		exit 1
	}

	l_run utils/m0layout $NR_DATA 1 $POOL_WIDTH $NR_DATA $NR_DATA
	if [ $? -ne 0 ]; then
		echo ERROR: Parity configuration is incorrect
		exit 1
	fi

	setup_hosts && start_servers
	if [ $? -ne 0 ]; then
		exit 1
	fi

	# prepare configuration data
	MDS_ENDPOINT="\"${SERVICES[1]}\""
	IOS_NAMES='"ios1"'
	IOS_OBJS="($IOS_NAMES, {3| (2, [1: $MDS_ENDPOINT], \"_\")})"
	for i in `seq 3 2 ${#SERVICES[*]}`; do
		IOS_NAME="\"ios$(((i+1) / 2))\""
		IOS_NAMES="$IOS_NAMES, $IOS_NAME"
		IOS_OBJ="($IOS_NAME, {3| (2, [1: \"${SERVICES[$i]}\"], \"_\")})"
		IOS_OBJS="$IOS_OBJS, $IOS_OBJ"
	done

	CONF="`cat <<EOF
[$((SERVICES_NR + 3)):
  ("prof", {1| ("fs")}),
  ("fs", {2| ((11, 22),
              [3: "pool_width=$POOL_WIDTH",
                  "nr_data_units=$NR_DATA",
                  "unit_size=$UNIT_SIZE"],
              [$((SERVICES_NR + 1)): "mds", $IOS_NAMES])}),
  ("mds", {3| (1, [1: $MDS_ENDPOINT], "_")}),
  $IOS_OBJS]
EOF`"

	# mount the file system
	mkdir -p $MP
	l_run "mount -t m0t1fs -o profile=prof,local_conf='$CONF' none $MP" || {
		echo ERROR: Unable to mount the file system
		exit 1
	}
	mount | grep m0t1fs
	IS_MOUNTED=yes

	# wait to terminate
	if [ $wait_after_mount -eq 1 ]; then

		trap cleanup EXIT

		echo
		echo The mero file system may be accessed with another terminal at $MP
		echo Type quit or EOF in this terminal to unmount the file system and cleanup
		while read LINE; do
			if [ "$LINE" = "quit" ]; then
				break
			fi
		done
		echo
	fi
}

while getopts "$OPTIONS_STRING" OPTION; do
    case "$OPTION" in
        a)
            STOB="ad"
            ;;
        l)
            use_loop_device=1
            ;;
        L)
            setup_local_server_config=1
            ;;
        h)
            usage
            exit 0
            ;;
        n)
            LOCAL_SERVICES_NR="$OPTARG"
            ;;
        d)
            NR_DATA="$OPTARG"
            ;;
        p)
            POOL_WIDTH="$OPTARG"
            ;;
        u)
            UNIT_SIZE="$OPTARG"
            ;;
        q)
            wait_after_mount=0
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done

#set -x

main

# Local variables:
# sh-basic-offset: 8
# sh-indentation: 8
# tab-width: 8
# End:
