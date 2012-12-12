#!/bin/bash

# Script to startup local/remote services and mount Mero file system
#
# - The script requires the build path to be available on all the nodes.
#   (This can be easily shared via NFS from some one node.)
# - The script uses ssh and scp to operate remotely. It requires that the
#   remote service nodes have this node's public certificate in
#   their .ssh/authorized_keys file.  If services are defined on the local
#   host then it should be possible to ssh into the local host.

# Usage:
#
# The script should be run from mero/core directory:
#
# $ cd ~/path/to/mero/core
# $ sudo ~/path/to/m0mount.sh [-ad] # this script
#
# -ad option configure the services to run on ad stobs.
#     it automatically detects and make configuration files
#     for the Titan discs (by default - 10 discs per service,
#     see DISKS_SH_NR below).
#
#     Before using -ad option make sure the discs are online:
#     $ sudo ~root/gem.sh dumpdrives
#
#     Turn them on if needed:
#     $ sudo ~root/gem.sh powerondrive all
#
# But before running the script, make sure you configured
# the IP/IB addresses of your setup correctly here (see below).

# Here is some configuration info (as for the time of writing this)
# about Titan nodes in Fremont lab:
#
# +----------------------------------------------------------------+
# | Hostname   |   IP-address   |    IB-address   |  IPMI-address  |
# +------------+----------------+-----------------+----------------+
# | sjt00-c1   |  10.76.50.161  |  172.18.50.161  |  10.76.50.162  |
# | sjt00-m0   |  10.76.50.163  |  172.18.50.163  |  10.76.50.164  |
# | sjt02-c1   |  10.76.50.40   |  172.18.50.40   |  10.76.50.42   |
# | sjt02-m0   |  10.76.50.45   |  172.18.50.45   |  10.76.50.47   |
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
#   e.g. 	sjt02-m0 172.18.50.45@o2ib:12345:33:101
#
# - There are an even number of records in the list.
# - The number of server records is the POOL_WIDTH.

# This example uses co-located remote ioservices.
#SERVICES=(
#	sjt02-m0 172.18.50.45@o2ib:12345:41:101
#	sjt02-m0 172.18.50.45@o2ib:12345:41:102
#	sjt02-m0 172.18.50.45@o2ib:12345:41:103
#	sjt02-m0 172.18.50.45@o2ib:12345:41:104
#)
# IMPORTANT! Keep nodes together in the list (the code depends on this).
#

# This example puts ioservices on 3 nodes, and uses 4 data blocks
SERVICES=(
	sjt02-c1 172.18.50.40@o2ib:12345:41:101
	sjt02-c1 172.18.50.40@o2ib:12345:41:102
	sjt02-m0 172.18.50.45@o2ib:12345:41:101
	sjt02-m0 172.18.50.45@o2ib:12345:41:102
	sjt00-c1 172.18.50.161@o2ib:12345:41:101
	sjt00-c1 172.18.50.161@o2ib:12345:41:102
)

LOCAL_EP=(
	12345:33:101
	12345:33:102
)

UNIT_SIZE=262144
SERVICES_NR=$(expr ${#SERVICES[*]} / 2)
POOL_WIDTH=$(expr $SERVICES_NR \* 1)
NR_DATA=$(expr $POOL_WIDTH - 2)

M0_TRACE_IMMEDIATE_MASK=0
M0_TRACE_LEVEL=debug+
M0_TRACE_PRINT_CONTEXT=full

STOB=${1:-linux}

# number of disks to split by for each service
# in ad-stob mode
DISKS_SH_NR=`expr $POOL_WIDTH / $SERVICES_NR`

# Local mount data
MP=/mnt/m0

# The file system TMID
FSTMID=1

# Remote work arena
WORK_ARENA=/usr/tmp/m0

# transport related variables
XPT=lnet
# m0d flags
XPT_SETUP="-m 163840 -q 16"
# m0mero module params
XPT_PARAM_R="max_rpc_msg_size=163840 tm_recv_queue_min_len=1"	# remote host
XPT_PARAM_L="max_rpc_msg_size=163840 tm_recv_queue_min_len=48"	# local host

#KTRACE_FLAGS=m0_trace_immediate_mask=8

BROOT=${PWD%/*}   # globally visible build root
THIS_HOST=$(hostname)

# track hosts that have been initialized in an associative array
declare -A SETUP

# track hosts on which servers have been started
declare -A STARTED

# A file whose sum we can check
SUMFILE=$BROOT/core/mero/.libs/m0d
LSUM=

###########
# functions

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
	$RUN insmod $BROOT/galois/src/linux_kernel/galois.ko
	if [ $? -ne 0 ]; then
		echo ERROR: Failed to load galois module on $H
		return 1
	fi
	$RUN insmod $BROOT/core/build_kernel_modules/m0mero.ko local_addr=$KEP $XPT_PARAM $KTRACE_FLAGS
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
#Device:
#       - id: 1
#	  filename: /dev/sda

# number of disks to split by
DISKS_SH_NR=$DISKS_SH_NR

i=$dev_id; j=0; f=0;

echo "Device:" > disks.conf

devs=\`ls /dev/disk/by-id/scsi-35* | grep -v part\`

fdisk -l \$devs 2>&1 1>/dev/null | \
sed -n 's;^Disk \\(/dev/.*\\) doesn.*;\1;p' | \
while read dev; do
	[ \$j -eq 0 ] && echo "Device:" > disks\$f.conf
	echo "       - id: \$i" >> disks.conf
	echo "       - id: \$i" >> disks\$f.conf
	echo "         filename: \$dev" >> disks.conf
	echo "         filename: \$dev" >> disks\$f.conf
	i=\`expr \$i + 1\`
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
	EP=$2
	I=$3
	local dcf_id=$4

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
	if [ $STOB == "-ad" -o $STOB == "-td" ]; then

		if ! $RUN [ -f $DISKS_SH_FILE ]; then
			local dev_id=1
			if [ $dcf_id -eq 0 ]; then
				dev_id=`expr $I \* $DISKS_SH_NR + 1`
			fi
			gen_disks_conf_files $dev_id || return 1
		fi

		if [ $STOB == "-ad" ]; then
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
		if [ $STOB == "-ad" ]; then
			STOB_PARAMS="-T ad -d $DISKS_SH_FILE"
		fi
	fi

	local SNAME="-s ioservice"
	if [ $I -eq 0 ]; then
		SNAME="-s mdservice $SNAME"
	fi

	$RUN "cd $DDIR && \
M0_TRACE_IMMEDIATE_MASK=$M0_TRACE_IMMEDIATE_MASK \
M0_TRACE_LEVEL=$M0_TRACE_LEVEL \
M0_TRACE_PRINT_CONTEXT=$M0_TRACE_PRINT_CONTEXT \
$BROOT/core/mero/m0d -r -p \
$STOB_PARAMS -D $DDIR/db -S $DDIR/stobs \
-e $XPT:$EP $SNAME $XPT_SETUP" > ${SLOG}$I.log &
	if [ $? -ne 0 ]; then
		echo ERROR: Failed to start remote server on $H
		return 1
	fi

	STARTED[$H]=$H
}

function start_servers () {
	local devs_conf_cnt=0
	if [ $STOB == "-ad" -o $STOB == "-td" ]; then
		for ((i=0; i < ${#SERVICES[*]}; i += 2)); do
			H=${SERVICES[$i]}
			local RUN
			[ $H == $THIS_HOST ] && RUN=l_run || RUN="r_run $H"
			$RUN rm -f $WORK_ARENA/disks*.conf
		done
	fi
	SLOG=$WORK_ARENA/server
	for ((i=0; i < ${#SERVICES[*]}; i += 2)); do
		H=${SERVICES[$i]}
		EP=${SERVICES[((i+1))]}
		# new Titan couple?
		[ $i -gt 0 ] && [ ${H%-*} != ${SERVICES[((i-2))]%-*} ] && \
			devs_conf_cnt=0
		start_server $H $EP $((i / 2)) $devs_conf_cnt
		if [ $? -ne 0 ]; then
			return 1
		fi
		devs_conf_cnt=`expr $devs_conf_cnt + 1`
	done

	echo "Wait for the services to start up..."
	while true; do
		local STARTED_NR=`cat ${SLOG}*.log | grep CTRL | wc -l`
		echo "Started $STARTED_NR services..."
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
	$RUN pkill -USR1 -f m0d
}

function stop_servers () {
	for H in ${STARTED[*]}; do
		stop_server $H
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
	sleep 5
	teardown_hosts
}

update_params()
{
	modprobe lnet
	lctl network up &>> /dev/null
	NID=`lctl list_nids | head -1`

	rm -rf $WORK_ARENA/*
	unset SERVICES

	# Update each field of SERVICES array with local node values
	# Update hostname and end point addresses
	LOCAL_SERVICES_NR=$(expr ${#LOCAL_EP[*]} \* 2)

	for ((i = 0, j = 0; i < $LOCAL_SERVICES_NR; i += 2, ++j)); do
		SERVICES[$i]=$THIS_HOST
		SERVICES[((i+1))]="${NID}:${LOCAL_EP[$j]}"
	done

	SERVICES_NR=$(expr ${#LOCAL_EP[*]})
	POOL_WIDTH=4
	NR_DATA=2
}

######
# main

if [ ! -d build_kernel_modules -o ! $BROOT/core -ef $PWD ]; then
	echo ERROR: Run this script in the top of the Mero source directory
	exit 1
fi
LSUM=$(sum $SUMFILE)

if [ "x$1" = "xlocal" ]; then
	update_params
fi

l_run utils/m0layout $NR_DATA 1 $POOL_WIDTH $NR_DATA $NR_DATA
if [ $? -ne 0 ]; then
	echo ERROR: Parity configuration is incorrect
	exit 1
fi

trap cleanup EXIT

setup_hosts && start_servers
if [ $? -ne 0 ]; then
	exit 1
fi

# compute mount paramters
IOS="mds=${SERVICES[1]},ios=${SERVICES[1]}"
for i in `seq 3 2 ${#SERVICES[*]}`; do
	IOS="${IOS},ios=${SERVICES[$i]}"
done

# mount the file system
mkdir -p $MP

CONF='profile=prof,local_conf=[2: '\
'("prof", {1| ("fs")}), '\
'("fs", {2| ((11, 22),'\
" [3: \"pool_width=$POOL_WIDTH\", \"nr_data_units=$NR_DATA\","\
" \"unit_size=$UNIT_SIZE\"],"\
' [1: "_"])})]'

l_run mount -t m0t1fs -o "'$CONF,$IOS'" none $MP
if [ $? -ne 0 ]; then
	echo ERROR: Unable to mount the file system
	exit 1
fi
mount | grep m0t1fs
IS_MOUNTED=yes

# wait to terminate
echo
echo The mero file system may be accessed with another terminal at $MP
echo Type quit or EOF in this terminal to unmount the file system and cleanup
while read LINE; do
	if [ "$LINE" = "quit" ]; then
		break
	fi
done
echo
