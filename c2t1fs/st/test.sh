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
mkdir -p /tmp/test

(./stob/ut/server -d/tmp/test -p$Port 2>/tmp/test/stderr-$$.log &)
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
dd if=/dev/zero of=/mnt/c2t1fs/12345 bs=1M count=256
ls -l /mnt/c2t1fs/12345
dd if=/mnt/c2t1fs/12345 bs=1M count=256 2>/dev/null | md5sum
dd if=/dev/zero bs=1M count=256 2>/dev/null | md5sum

umount /mnt/c2t1fs

# mount again and check its content
# 1024 * 1024 * 256 = 268435456
# !!!! please note:  the file size is 256MB  !!!!
mount -t c2t1fs -o objid=12345,objsize=268435456,ds=$IPAddr:$Port $IPAddr:$Port /mnt/c2t1fs
dd if=/mnt/c2t1fs/12345 bs=1M count=256 2>/dev/null | md5sum

#attach loop device over c2t1fs file
sleep 1
losetup /dev/loop0 /mnt/c2t1fs/12345

mkfs.ext3 /dev/loop0 || abort "mkfs failed"
mkdir -p /mnt/loop
mount /dev/loop0 /mnt/loop || abort "mount failed"

# read & write the loop device file system.
dd if=/dev/zero of=/mnt/loop/10M bs=1M count=10 oflag=direct
ls -l /mnt/loop/10M
dd if=/mnt/loop/10M bs=1M count=10 2>/dev/null | md5sum
dd if=/dev/zero bs=1M count=10 2>/dev/null | md5sum
umount /mnt/loop

# again, read & write the loop device file system.
mount /dev/loop0 /mnt/loop  || abort "mount again failed"
dd if=/mnt/loop/10M bs=1M count=10 2>/dev/null | md5sum
umount /mnt/loop

losetup -d /dev/loop0
umount /mnt/c2t1fs

###### mount c2t1fs and loop again
mount -t c2t1fs -o objid=12345,objsize=268435456,ds=$IPAddr:$Port $IPAddr:$Port /mnt/c2t1fs
losetup /dev/loop0 /mnt/c2t1fs/12345
mount /dev/loop0 /mnt/loop
dd if=/mnt/loop/10M bs=1M count=10 2>/dev/null | md5sum
umount /mnt/loop
losetup -d /dev/loop0
umount /mnt/c2t1fs

modunload

killall lt-server
echo ======================done=====================
