DBENCH_DIR=""

if [ -z $DBENCH_DIR ] ; then
	echo "please specify dbench dir";
	exit;
fi

cd ../..
pwd

rmmod loop

ulimit -c unlimited
insmod net/ksunrpc/ksunrpc.ko || exit
insmod c2t1fs/c2t1fs.ko
(./stob/ut/server /tmp/ 2222 &)
sleep 1
mkdir -p /mnt/c2t1fs
mount -t c2t1fs -o objid=12345 127.0.0.1:2222 /mnt/c2t1fs

#large file write & read
dd if=/dev/zero of=/mnt/c2t1fs/12345 bs=1M count=200
ls -l /mnt/c2t1fs/12345

umount /mnt/c2t1fs

# mount again and check its content
# 1024 * 1024 * 256 = 268435456
mount -t c2t1fs -o objid=12345,objsize=268435456 127.0.0.1:2222 /mnt/c2t1fs

#attach loop device over c2t1fs file
insmod c2t1fs/c2t1fs_loop.ko
sleep 1
losetup /dev/loop0 /mnt/c2t1fs/12345

mkfs.ext3 /dev/loop0
mkdir -p /mnt/loop
mount /dev/loop0 /mnt/loop

# please use your own dbench options here!
$DBENCH_DIR/dbench -D /mnt/loop/ 2 -t 10 -c $DBENCH_DIR/client.txt

umount /mnt/loop
losetup -d /dev/loop0
umount /mnt/c2t1fs

rmmod c2t1fs_loop
rmmod c2t1fs
rmmod ksunrpc

killall lt-server
echo ======================done=====================
