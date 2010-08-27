set -x

cd ../..
pwd

# get the first non-loopback IP address
IPAddr=$(/sbin/ifconfig | grep "inet addr" | grep -v "127.0.0.1" | awk -F: '{print $2}' | awk '{print $1}' | head -n 1)
Port="2222"
echo "server address is $IPAddr:$Port"
rmmod loop

ulimit -c unlimited
insmod lib/linux_kernel/klibc2.ko
insmod addb/linux_kernel/kaddb.ko
insmod fop/linux_kernel/kfop.ko
insmod net/ksunrpc/ksunrpc.ko
insmod c2t1fs/c2t1fs.ko
lsmod | grep -c "c2t1fs" || exit

(./stob/ut/server -d/tmp/ -p$Port &)
sleep 1
mkdir -p /mnt/c2t1fs
mount -t c2t1fs -o objid=12345 $IPAddr:$Port /mnt/c2t1fs

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
mount -t c2t1fs -o objid=12345,objsize=268435456 $IPAddr:$Port /mnt/c2t1fs
dd if=/mnt/c2t1fs/12345 bs=1M count=200 2>/dev/null | md5sum

#attach loop device over c2t1fs file
insmod c2t1fs/c2t1fs_loop.ko
sleep 1
losetup /dev/loop0 /mnt/c2t1fs/12345

mkfs.ext3 /dev/loop0
mkdir -p /mnt/loop
mount /dev/loop0 /mnt/loop

# read & write the loop device file system.
dd if=/dev/zero of=/mnt/loop/10M bs=1M count=10 oflag=direct
ls -l /mnt/loop/10M
dd if=/mnt/loop/10M bs=1M count=10 2>/dev/null | md5sum
dd if=/dev/zero bs=1M count=10 2>/dev/null | md5sum
umount /mnt/loop

# again, read & write the loop device file system.
mount /dev/loop0 /mnt/loop
dd if=/mnt/loop/10M bs=1M count=10 2>/dev/null | md5sum
umount /mnt/loop

losetup -d /dev/loop0
umount /mnt/c2t1fs

###### mount c2t1fs and loop again
mount -t c2t1fs -o objid=12345,objsize=268435456 $IPAddr:$Port /mnt/c2t1fs
losetup /dev/loop0 /mnt/c2t1fs/12345
mount /dev/loop0 /mnt/loop
dd if=/mnt/loop/10M bs=1M count=10 2>/dev/null | md5sum
umount /mnt/loop
losetup -d /dev/loop0
umount /mnt/c2t1fs


rmmod c2t1fs_loop
rmmod c2t1fs
rmmod ksunrpc
rmmod kfop
rmmod kaddb
rmmod klibc2

killall lt-server
echo ======================done=====================
