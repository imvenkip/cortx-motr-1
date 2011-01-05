set -x

. common.sh

cd ../..
pwd

# get the first non-loopback IP address
IPAddr=$(/sbin/ifconfig | grep "inet addr" | grep -v "127.0.0.1" | awk -F: '{print $2}' | awk '{print $1}' | head -n 1)
Port="2222"
echo "server address is $IPAddr:$Port"
rmmod loop

ulimit -c unlimited

modunload
modload

rm -rf /tmp/test/
mkdir -p /tmp/test/

(./stob/ut/server -d/tmp/test -p$Port &)
sleep 5
mkdir -p /mnt/c2t1fs
mount -t c2t1fs -o objid=12345,ds=$IPAddr:$Port $IPAddr:$Port /mnt/c2t1fs

#small file write & read
ls -l /mnt/c2t1fs/12345
cat c2t1fs/main.c > /mnt/c2t1fs/12345
ls -l /mnt/c2t1fs/12345
cat c2t1fs/main.c | md5sum
cat /mnt/c2t1fs/12345 | md5sum

#large file write & read
dd if=/dev/zero of=/mnt/c2t1fs/12345 bs=1M count=200
ls -l /mnt/c2t1fs/12345
dd if=/mnt/c2t1fs/12345 bs=1M count=200 2>/dev/null | md5sum
dd if=/dev/zero bs=1M count=200 2>/dev/null | md5sum

umount /mnt/c2t1fs

# mount again and check its content
# 1024 * 1024 * 256 = 268435456
mount -t c2t1fs -o objid=12345,objsize=268435456,ds=$IPAddr:$Port $IPAddr:$Port /mnt/c2t1fs
dd if=/mnt/c2t1fs/12345 bs=1M count=200 2>/dev/null | md5sum
umount /mnt/c2t1fs

modunload

killall lt-server
echo ======================done=====================
