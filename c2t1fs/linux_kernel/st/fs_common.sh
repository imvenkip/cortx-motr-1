#!/bin/bash

set -x
. common.sh

fsmount()
{
# get the first non-loopback IP address
IPAddr=$(/sbin/ifconfig | grep "inet addr" | grep -v "127.0.0.1" | awk -F: '{print $2}' | awk '{print $1}' | head -n 1)
Port0="1111"
Port1="1112"
Port2="1113"
Port3="1114"
Port4="1115"
echo "server0 address is $IPAddr:$Port0"
echo "server1 address is $IPAddr:$Port1"
echo "server2 address is $IPAddr:$Port2"
echo "server3 address is $IPAddr:$Port3"
echo "server4 address is $IPAddr:$Port4"

modunload
modload

# Run servers on different ports with different stobs:
cd /tmp
rm -rf 0 1 2 3 4
mkdir -p 0 1 2 3 4 
cd -

(./stob/ut/server -d/tmp/0 -p$Port0 &)
(./stob/ut/server -d/tmp/1 -p$Port1 &)
(./stob/ut/server -d/tmp/2 -p$Port2 &)
(./stob/ut/server -d/tmp/3 -p$Port3 &)
(./stob/ut/server -d/tmp/4 -p$Port4 &)
sleep 3

mkdir -p /mnt/c2t1fs

mount -t c2t1fs -o layout-data=3,layout-parity=1,objid=12345,objsize=196608,\
ds=$IPAddr:$Port0,ds=$IPAddr:$Port1,ds=$IPAddr:$Port2,ds=$IPAddr:$Port3,ds=$IPAddr:$Port4\
 $IPAddr:$Port1 /mnt/c2t1fs
}

fsumount()
{
umount /mnt/c2t1fs
modunload
killall lt-server
}

#arg0 max_count - maximal count per read/write
#arg1 max_bs    - maximal block size
fsrwtest()
{
max_count_=$1
max_bs_=$2
echo "fsrwtest max_count=$max_count, max_bs=$max_bs"

dd if=/dev/urandom of=dummy.bin bs=$(($max_bs_*$max_count_)) count=1

#for count_ in {1..$max_count_}
for (( count_ = 1; count_ <= $max_count_; count_++ ))
do
     #for bs_ in {12288,24576,$max_bs_}
     for (( bs_ = 12288; bs_ <= $max_bs_; bs_ += 12288))
     do
        dd if=dummy.bin of=/mnt/c2t1fs/12345 bs=$bs_ count=$count_
        left=$(dd if=dummy.bin         bs=$bs_ count=$count_ 2>/dev/null | md5sum)
	#sleep 2
        right=$(dd if=/mnt/c2t1fs/12345 bs=$bs_ count=$count_ 2>/dev/null | md5sum)
	#sleep 2
	
	if [ "$left" == "$right" ]
	then
	    echo "test {$count_,$bs_} passed."
	else
	    echo "test {$count_,$bs_} failed."
	    echo =========== FFFFF FFFFF  F  F     FFFFF FFFF  FF ===========
	    echo =========== F     F   F  F  F     F     F   F FF ===========
	    echo =========== FFF   FFFFF  F  F     FFF   F   F FF ===========
	    echo =========== F     F   F  F  F     F     F   F    ===========
	    echo =========== F     F   F  F  FFFFF FFFFF FFFF  FF ===========
	fi
     done
 done
rm dummy.bin
}
