#!/bin/sh

if [ $# -lt "0" ]
then
	echo "Usage : $0"
        exit 1
fi

echo 8 > /proc/sys/kernel/printk

. `dirname $0`/c2t1fs_common_inc.sh
. `dirname $0`/c2t1fs_client_inc.sh

main()
{
        prepare_test_dir || return $?

        load_kernel_module

        io_combinations $POOL_WIDTH 1 1
        if [ $? -ne "0" ]
        then
                echo "Failed : IO failed.."
        fi
        return 0
}

modprobe_lnet
insmod $COLIBRI_CORE_ROOT/../galois/src/linux_kernel/galois.ko
main
rmmod galois
