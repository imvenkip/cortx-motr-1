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

echo ======================done=====================
