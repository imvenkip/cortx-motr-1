#!/bin/bash
# This script creates number of mero servers as per end points
# given in $EP array.
# Stobs are created in $WORK_ARENA
# Each server has its separate directory.

{
    cat <<EOF
+------------------------+
| THE SCRIPT IS DISABLED |
+------------------------+

${0##*/} script doesn't work with the version of m0t1fs client,
released as part of "conf-reqh" milestone. The script uses 'mds=' and 'ios='
options of m0t1fs, which don't exist any more.

Updating ${0##*/} would take some efforts, and these efforts
would be wasted if the script is never used.  I suspect the script to be
obsolete, because grepping its name in origin/master branch of our git
repository returns nothing.

Please let me know if you need this script, and I'll put it back into
action ASAP.

--
All the best,
vvv
<valery_vorotyntsev@xyratex.com>
EOF
} >&2
exit 1

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
MP=/mnt/m0
MEP=0@lo:12345:33:1
NR_DATA=2
POOL_WIDTH=4
UNIT_SIZE=4096
STOB_TYPE=linux

WORK_ARENA=/usr/tmp

###################
# main

if [ ! -d mero ] ; then
	echo Invoke this script in the top of the Mero source directory
	exit 1
fi

MOUNTED=$(mount | grep $MP)
if [ -n "$MOUNTED" ] ; then
	echo Error $MP is already mounted
	exit 1
fi
SERVERS=$(pgrep m0d)
if [ -n "$SERVERS" ] ; then
	echo Error m0d processes already running
	exit 1
fi

set -x

# load lnet if not yet loaded
modprobe lnet
lctl network up

# reload the m0mero module
rmmod m0mero.ko galois.ko
insmod ../galois/src/linux_kernel/galois.ko

# Immediate trace is heavy, use sparingly
# KTRACE_FLAGS='trace_print_context=func trace_level=call+ trace_immediate_mask=8'
insmod mero/m0mero.ko local_addr=$MEP max_rpc_msg_size=163840 tm_recv_queue_min_len=16 $KTRACE_FLAGS

IOS=
HERE=$PWD

#if [ `ls -l $HERE/devices?.conf | wc -l` -ne ${#EP[*]} ]  ; then
#	echo "Please generate device configuration files"
#	rmmod m0mero galois
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
	$HERE/mero/m0d -T ${STOB_TYPE} -D $WORK_ARENA/d$i/db \
	    -S $WORK_ARENA/d$i/stobs -e $XPT:${EP[$i]} -s addb -s ioservice \
	    -s sns_repair -m 163840 -q 16 &>>$WORK_ARENA/servers_started )&
done

utils/m0layout $NR_DATA 1 $POOL_WIDTH $NR_DATA $NR_DATA

# Due to device stob pre-creation (balloc format) it normally takes ~0m28.166s for
# starting up a server, so wait till all m0d services are started.
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
mount -t m0t1fs -o $IOS,$STRIPE none $MP
mount | grep m0t1fs

# wait to terminate
echo Type quit or EOF to terminate
while read LINE; do
	if [ "$LINE" = "quit" ] ; then
		break
	fi
done

umount $MP
pkill -USR1 -f m0d
