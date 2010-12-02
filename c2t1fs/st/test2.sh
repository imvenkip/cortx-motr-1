# Using simple layout N/K = 3/1 (with 1 parity and 1 spare unit).

set -x

. common.sh

cd ../..
pwd

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

ulimit -c unlimited
modunload
modload

# Run servers on different ports with different stobs:
cd /tmp
mkdir -p 0 1 2 3 4 
cd -

(./stob/ut/server -d/tmp/0 -p$Port0 &)
(./stob/ut/server -d/tmp/1 -p$Port1 &)
(./stob/ut/server -d/tmp/2 -p$Port2 &)
(./stob/ut/server -d/tmp/3 -p$Port3 &)
(./stob/ut/server -d/tmp/4 -p$Port4 &)
sleep 1

mkdir -p /mnt/c2t1fs

mount -t c2t1fs -o layout-data=3,layout-parity=1,objid=12345,objsize=196608,\
ds=$IPAddr:$Port0,ds=$IPAddr:$Port1,ds=$IPAddr:$Port2,ds=$IPAddr:$Port3,ds=$IPAddr:$Port4\
 $IPAddr:$Port1 /mnt/c2t1fs

# max_count_=3
# max_bs_=49152

# dd if=/dev/urandom of=dummy.bin bs=$(($max_bs_*$max_count_)) count=1

# for count_ in {1,2,$max_count_}
# do
#      for bs_ in {12288,24576,$max_bs_}
#      do
#         dd if=dummy.bin of=/mnt/c2t1fs/12345 bs=$bs_ count=$count_
#         left=$(dd if=dummy.bin         bs=$bs_ count=$count_ 2>/dev/null | md5sum)
# 	sleep 2
#         right=$(dd if=/mnt/c2t1fs/12345 bs=$bs_ count=$count_ 2>/dev/null | md5sum)
# 	sleep 2
	
# 	if [ "$left" == "$right" ]
# 	then
# 	    echo "test {$count_,$bs_} passed."
# 	else
# 	    echo "test {$count_,$bs_} failed."
# 	fi
#      done
#  done
# rm dummy.bin

# umount /mnt/c2t1fs
# modunload
# killall lt-server
echo ======================done=====================
