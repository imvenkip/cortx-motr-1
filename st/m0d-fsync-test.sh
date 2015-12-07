#!/bin/bash

SANDBOX_DIR=${SANDBOX_DIR:-/var/mero/sandbox.fsync-st}
M0_SRC_DIR=`readlink -f $0`
M0_SRC_DIR=${M0_SRC_DIR%/*/*}
cd $M0_SRC_DIR

echo "Installing Mero services"
sudo scripts/install-mero-service -u
sudo scripts/install-mero-service -l
sudo utils/m0setup -v -P 12 -N 2 -K 1 -i 3 -d /var/mero/img -s 8 -c
sudo utils/m0setup -v -P 12 -N 2 -K 1 -i 3 -d /var/mero/img -s 8

# update Mero configuration: turn on fdatasync with '-I' option
sudo sed -i "s/.*MERO_M0D_EXTRA_OPTS.*/MERO_M0D_EXTRA_OPTS='-I'/" \
     /etc/sysconfig/mero

# update Mero configuration: set specific dir for test artifacts
sudo sed -i "s@.*MERO_LOG_DIR.*@MERO_LOG_DIR=${SANDBOX_DIR}/log@" \
     /etc/sysconfig/mero
sudo sed -i "s@.*MERO_M0D_DATA_DIR.*@MERO_M0D_DATA_DIR=${SANDBOX_DIR}/mero@" \
     /etc/sysconfig/mero

echo "Start Mero services"
sudo systemctl start mero-mkfs
sudo systemctl start mero-singlenode

echo "Perform fsync test"
for i in 0:1{0..9}0000; do touch /mnt/m0t1fs/$i & done
wait
for i in 0:1{0..9}0000; do setfattr -n lid -v 8 /mnt/m0t1fs/$i & done
wait
for i in 0:1{0..9}0000; do dd if=/dev/zero of=/mnt/m0t1fs/$i \
			      bs=8M count=20 conv=fsync & done
wait


echo "Tear down Mero services"
sudo systemctl stop mero-singlenode
sudo scripts/install-mero-service -u
