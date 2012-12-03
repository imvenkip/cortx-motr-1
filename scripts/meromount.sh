#!/bin/bash
# This script creates number of mero servers as per end points
# given in $EP array.
# Stobs are created in $WORK_ARENA
# Each server has its separate directory.

# Transport
XPT=lnet

# load lnet if not yet loaded
modprobe lnet
lctl network up

# list of server end points
EP=(
    12345:33:101
    12345:33:102
)

NID=`lctl list_nids | head -1`

# mount data
MP=/mnt/m0
MEP=${NID}:12345:33:1
NR_DATA=2
POOL_WIDTH=4
UNIT_SIZE=4096
STOB_TYPE=linux
# KTRACE_FLAGS='trace_print_context=func trace_level=call+ trace_immediate_mask=8'
MAX_RPC_MSG_SIZE=163840
TM_RECV_QUEUE_MIN_LEN=16

WORK_ARENA=/usr/tmp

CONF='conf=local-conf:[2: '\
'("prof", {1| ("fs")}), '\
'("fs", {2| ((11, 22),'\
" [3: \"pool_width=$POOL_WIDTH\", \"nr_data_units=$NR_DATA\","\
" \"unit_size=$UNIT_SIZE\"],"\
' [1: "_"])})],profile=prof'

###################
# main

if [ ! -d build_kernel_modules ] ; then
	echo Invoke this script in the top of the Mero source directory
	exit 1
fi

MOUNTED=$(mount | grep $MP)
if [ -n "$MOUNTED" ] ; then
	echo Error $MP is already mounted
	exit 1
fi
SERVERS=$(pgrep -f colibri_setup)
if [ -n "$SERVERS" ] ; then
	echo Error colibri_setup processes already running
	exit 1
fi

set -x

# reload the Mero kernel module
rmmod kcolibri.ko galois.ko
insmod ../galois/src/linux_kernel/galois.ko

# Immediate trace is heavy, use sparingly
insmod build_kernel_modules/kcolibri.ko local_addr=$MEP \
    max_rpc_msg_size=$MAX_RPC_MSG_SIZE \
    tm_recv_queue_min_len=$TM_RECV_QUEUE_MIN_LEN $KTRACE_FLAGS

IOS="mds=${NID}:${EP[0]},"
HERE=$PWD

#if [ `ls -l $HERE/devices?.conf | wc -l` -ne ${#EP[*]} ]  ; then
#	echo "Please generate device configuration files"
#	rmmod kcolibri galois
#	exit 1
#fi

rm $WORK_ARENA/servers_started

# spawn servers
for ((i=0; i < ${#EP[*]}; i++)) ; do
	if ((i != 0)) ; then
		IOS="$IOS,"
	fi
	IOS="${IOS}ios=${NID}:${EP[$i]}"
	rm -rf $WORK_ARENA/d$i
	mkdir $WORK_ARENA/d$i
	(cd $WORK_ARENA/d$i
	 $HERE/colibri/colibri_setup -r -p -T ${STOB_TYPE}         \
	        -D $WORK_ARENA/d$i/db -S $WORK_ARENA/d$i/stobs     \
	        -e $XPT:${NID}:${EP[$i]} -s ioservice -s mdservice \
                -m $MAX_RPC_MSG_SIZE -q $TM_RECV_QUEUE_MIN_LEN     \
		&>>$WORK_ARENA/servers_started )&
done

layout/ut/ldemo $NR_DATA 1 $POOL_WIDTH $NR_DATA $NR_DATA

# Due to device stob pre-creation (balloc format) it normally takes ~0m28.166s
# for starting up a server, so wait till all colibri_setup services are started.
#echo "Please wait while services are starting..."

# Supress waiting being printed on screen
#set +x
#while [ `cat $WORK_ARENA/servers_started | wc -l` -ne ${#EP[*]} ]; do
#	sleep 1
#done
# Enable screen display again
#set -x

sleep 2

# mount the file system
mount -t c2t1fs -o "$CONF,$IOS" none $MP
mount | grep c2t1fs

# wait to terminate
echo Type quit or EOF to terminate
while read LINE; do
	if [ "$LINE" = "quit" ] ; then
		break
	fi
done

umount $MP
pkill -USR1 -f colibri_setup
