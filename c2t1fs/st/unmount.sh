set -x

. common.sh

cd ../..
pwd

umount /mnt/c2t1fs
modunload
killall lt-server
echo ======================done=====================
