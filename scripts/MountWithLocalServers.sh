#!/bin/bash
# This script creates number of colibri servers as per end points
# given in $EP array.
# Stobs are created in $WORK_ARENA
# Each server has its separate directory.

# transport
XPT=lnet

# list of server end points
EP=(
    0@lo:12345:33:101
#    0@lo:12345:33:102
#    0@lo:12345:33:103
#    0@lo:12345:33:104
)
# mount data
MP=/mnt/c2
MEP=0@lo:12345:33:1
NR_DATA=2
POOL_WIDTH=4
UNIT_SIZE=4096
STOB_TYPE=linux

WORK_ARENA=/usr/tmp

###################
# main

if [ ! -d build_kernel_modules ] ; then
	echo Invoke this script in the top of the Colibri source directory
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

# load lnet if not yet loaded
modprobe lnet
lctl network up

# reload the kcolibri module
rmmod kcolibri.ko galois.ko
insmod ../galois/src/linux_kernel/galois.ko

# Immediate trace is heavy, use sparingly
# KTRACE_FLAGS='trace_print_context=func trace_level=call+ trace_immediate_mask=8'
insmod build_kernel_modules/kcolibri.ko local_addr=$MEP max_rpc_msg_size=163840 tm_recv_queue_min_len=16 $KTRACE_FLAGS

IOS=
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
	IOS="${IOS}ios=${EP[$i]}"
	rm -rf $WORK_ARENA/d$i
	mkdir $WORK_ARENA/d$i
	(cd $WORK_ARENA/d$i
	 $HERE/colibri/colibri_setup -r -T ${STOB_TYPE} -D $WORK_ARENA/d$i/db \
            -S $WORK_ARENA/d$i/stobs -e $XPT:${EP[$i]} -s ioservice -s sns_repair \
            -m 163840 -q 16 &>>$WORK_ARENA/servers_started )&
done

layout/ut/ldemo $NR_DATA 1 $POOL_WIDTH $NR_DATA $NR_DATA

# Due to device stob pre-creation (balloc format) it normally takes ~0m28.166s for
# starting up a server, so wait till all colibri_setup services are started.
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
STRIPE=nr_data_units=$NR_DATA,pool_width=$POOL_WIDTH,unit_size=$UNIT_SIZE
mount -t c2t1fs -o $IOS,$STRIPE none $MP
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
