DBENCH_DIR=""

if [ -z $DBENCH_DIR ] ; then
	echo "please specify dbench dir";
	exit;
fi

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

rm -rf /tmp/test/
mkdir -p /tmp/test/

(./stob/ut/server -d/tmp/test -p$Port &)
sleep 5
mkdir -p /mnt/c2t1fs
mount -t c2t1fs -o objid=12345,ds=$IPAddr:$Port $IPAddr:$Port /mnt/c2t1fs

#large file write & read
dd if=/dev/zero of=/mnt/c2t1fs/12345 bs=1M count=200
ls -l /mnt/c2t1fs/12345

umount /mnt/c2t1fs

# mount again and check its content
# 1024 * 1024 * 256 = 268435456
mount -t c2t1fs -o objid=12345,objsize=268435456,ds=$IPAddr:$Port $IPAddr:$Port /mnt/c2t1fs

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
rmmod kfop
rmmod kaddb
rmmod klibc2

killall lt-server
echo ======================done=====================
