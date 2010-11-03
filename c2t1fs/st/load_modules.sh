set +x
cd ../..

ulimit -c unlimited
insmod lib/linux_kernel/klibc2.ko
insmod addb/linux_kernel/kaddb.ko
insmod fop/linux_kernel/kfop.ko
insmod net/ksunrpc/ksunrpc.ko
insmod galois/linux_kernel/kgalois.ko
insmod sns/linux_kernel/ksns.ko
insmod c2t1fs/c2t1fs.ko

cd -
