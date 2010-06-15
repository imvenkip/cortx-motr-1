cd ../..
pwd

killall lt-server
umount /mnt/c2t1fs
rmmod loop
rmmod c2t1fs
rmmod ksunrpc

ulimit -c unlimited
insmod net/ksunrpc/ksunrpc.ko 
insmod c2t1fs/c2t1fs.ko
(./stob/ut/server /tmp/ 2222 &)
sleep 1
mkdir -p /mnt/c2t1fs

# 1024 * 1024 * 256 = 268435456
mount -t c2t1fs -o objid=12345,objsize=268435456 127.0.0.1:2222 /mnt/c2t1fs

echo "wwwwwwwwwwwwwwwww"
for bs in 1 2 4 8 16 32 64 128 256 1024; do
	dd if=/dev/zero of=/mnt/c2t1fs/12345 bs=${bs}k count=$((268435456 / bs / 1024 ))
done

echo "rrrrrrrrrrrrrrrrr"
for bs in 1024 512 256 128 64 32 16 8 4 2 1; do
	dd of=/dev/null if=/mnt/c2t1fs/12345 bs=${bs}k count=$((268435456 / bs / 1024 ))
done

umount /mnt/c2t1fs


rmmod c2t1fs
rmmod ksunrpc

echo "press Enter"
read
killall lt-server
echo ======================done=====================
