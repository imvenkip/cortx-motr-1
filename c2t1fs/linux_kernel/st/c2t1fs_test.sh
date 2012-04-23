#!/bin/sh

. `dirname $0`/c2t1fs_common_inc.sh
. `dirname $0`/c2t1fs_client_inc.sh
. `dirname $0`/c2t1fs_server_inc.sh

COLIBRI_IOSERVICE_ENDPOINT="127.0.0.1:23124:1"
COLIBRI_C2T1FS_ENDPOINT="127.0.0.1:23125:1"

main()
{
        colibri_service start
        if [ $? -ne "0" ]
        then
                echo "Failed to start Colibri Service."
                return 1
        fi

        sleep 5 #Give time to start service properly.

        io_combinations $POOL_WIDTH 1 1
        if [ $? -ne "0" ]
        then
                echo "Failed : IO failed.."
        fi

        colibri_service stop
        if [ $? -ne "0" ]
        then
                echo "Failed to stop Colibri Service."
                return 1
        fi

        return 0
}

insmod $COLIBRI_CORE_ROOT/../galois/src/linux_kernel/galois.ko
main
rmmod galois
