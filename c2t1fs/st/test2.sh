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

dd if=/dev/urandom of=dummy.bin bs=196608 count=1

# tests:
# 1
dd if=dummy.bin of=/mnt/c2t1fs/12345 bs=12288 count=1
#dd if=dummy.bin of=dummy1.bin bs=12288 count=1
#dd if=/mnt/c2t1fs/12345 of=dummy2.bin bs=12288 count=1
dd if=dummy.bin         bs=12288 count=1 2>/dev/null | md5sum
dd if=/mnt/c2t1fs/12345 bs=12288 count=1 2>/dev/null | md5sum
# 2
dd if=dummy.bin of=/mnt/c2t1fs/12345 bs=36864 count=1
dd if=dummy.bin         bs=36864 count=1 2>/dev/null | md5sum
dd if=/mnt/c2t1fs/12345 bs=36864 count=1 2>/dev/null | md5sum
# 3
dd if=dummy.bin of=/mnt/c2t1fs/12345 bs=110592 count=1
dd if=dummy.bin         bs=110592 count=1 2>/dev/null | md5sum
dd if=/mnt/c2t1fs/12345 bs=110592 count=1 2>/dev/null | md5sum


rm dummy.bin
umount /mnt/c2t1fs
modunload
killall lt-server
echo ======================done=====================

#a=$(dd if=/dev/urandom bs=1M count=1 2>/dev/null | md5sum)
#if [ "$a" = "$a" ]; then echo passed; fi
