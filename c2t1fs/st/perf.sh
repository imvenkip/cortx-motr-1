cd ../..
pwd
# get the first non-loopback IP address
IPAddr=$(/sbin/ifconfig | grep "inet addr" | grep -v "127.0.0.1" | awk -F: '{print $2}' | awk '{print $1}' | head -n 1)
Port="2222"
echo "server address is $IPAddr:$Port"


killall lt-server
umount /mnt/c2t1fs
rmmod loop
rmmod c2t1fs
rmmod ksunrpc
rmmod kfop
rmmod kaddb
rmmod klibc2

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

# size of file to write, in MB
MB=256

mount -t c2t1fs -o objid=12345,objsize=$((MB * 1024 * 1024)),ds=$IPAddr:$Port $IPAddr:$Port /mnt/c2t1fs || exit 1

echo "wwwwwwwwwwwwwwwww"
for bs in 1 2 4 8 16 32 64 128 256 1024; do
	echo "w block size ${bs}k"
	dd if=/dev/zero of=/mnt/c2t1fs/12345 bs=${bs}k count=$((MB * 1024 / bs))
done

echo "rrrrrrrrrrrrrrrrr"
for bs in 1024 512 256 128 64 32 16 8 4 2 1; do
	echo "r block size ${bs}k"
	dd of=/dev/null if=/mnt/c2t1fs/12345 bs=${bs}k count=$((MB * 1024 / bs))
done

umount /mnt/c2t1fs


rmmod c2t1fs_loop
rmmod c2t1fs
rmmod ksunrpc
rmmod kfop
rmmod kaddb
rmmod klibc2

echo "press Enter"
read
killall lt-server
echo ======================done=====================
