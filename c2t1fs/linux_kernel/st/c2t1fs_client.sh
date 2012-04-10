#!/bin/sh

if [ $# -lt "0" ]
then
	echo "Usage : $0"
        exit 1
fi

echo 8 > /proc/sys/kernel/printk

. `dirname $0`/c2t1fs_common.sh

bulkio_test()
{
        strip_size=$1
        io_size=$2
        io_counts=$3
        pool_width=$4
        data_units=$5
        parity_units=$6

        colibri_module=kcolibri
        c2t1fs_mount_dir=$COLIBRI_C2T1FS_MOUNT_DIR
        colibri_module_path=$COLIBRI_CORE_ROOT/build_kernel_modules
        io_service=$COLIBRI_IOSERVICE_ENDPOINT
        local_input=$COLIBRI_C2T1FS_TEST_DIR/file1.data
        local_output=$COLIBRI_C2T1FS_TEST_DIR/file2.data
        c2t1fs_file=$c2t1fs_mount_dir/file.data

        lsmod | grep $colibri_module &> /dev/null
        if [ $? -eq "0" ]
        then
                echo "Module $colibri_module already present."
                echo "Removing existing module to for clean test."
                rmmod $colibri_module.ko
                if [ $? -ne "0" ]
                then
                        echo "Failed:failed to remove existing module $colibri_module ."
                fi
        fi

        echo "Inserting $colibri_module module..."
        insmod $colibri_module_path/$colibri_module.ko c2_trace_immediate_mask=0x01 local_addr=$COLIBRI_C2T1FS_ENDPOINT
        if [ $? -ne "0" ]
        then
                echo "Failed to insert module $colibri_module_path/$colibri_module.ko"
                return 1
        fi

        #Create mount directory
        mkdir $c2t1fs_mount_dir
        if [ $? -ne "0" ]
        then
                echo "Failed to create mount directory."
                rmmod $colibri_module.ko
                return 1
        fi

        echo "Mounting file system..."
        mount -t c2t1fs -o ios=$io_service,unit_size=$strip_size,pool_width=$pool_width,nr_data_units=$data_units,nr_parity_units=$parity_units none $c2t1fs_mount_dir
        if [ $? -ne "0" ]
        then
                echo "Failed to  mount c2t1fs file system."
                rmmod $colibri_module.ko
                return 1
        fi
        echo "Successfully mounted c2t1fs file system."

        echo "Creating local input file of I/O size ..."
        dd if=/dev/urandom of=$local_input bs=$io_size count=$io_counts
        if [ $? -ne "0" ]
        then
                echo "Failed to create local input file."
                umount $c2t1fs_mount_dir
                rmmod $colibri_module.ko
                return 1
        fi
        echo "Created local input file of I/O size."

        echo "Writing data of specified size and count to c2t1fs file ..."
        dd if=$local_input of=$c2t1fs_file bs=$io_size count=$io_counts
        if [ $? -ne "0" ]
        then
                echo "Failed to write data on c2t1fs file."
                umount $c2t1fs_mount_dir
                rmmod $colibri_module.ko
                return 1
        fi
        echo "Successfully written data of specified size and count to c2t1fs file."

        echo "Reading data of specified size and count from c2t1fs file ..."
        dd if=$c2t1fs_file of=$local_output bs=$io_size count=$io_counts
        if [ $? -ne "0" ]
        then
                echo "Failed to read data from c2t1fs file."
                umount $c2t1fs_mount_dir
                rmmod $colibri_module.ko
                return 1
        fi
        echo "Successfully read data of specified size and count from c2t1fs file."

        echo "Comparing data written and data read from c2t1fs file ..."
        diff $local_input $local_output &> /dev/null
        if [ $? -ne "0" ]
        then
                echo "Failed, data written and data read from c2t1fs file are not same."
                umount $c2t1fs_mount_dir
                rmmod $colibri_module.ko
                return 1
        fi
        echo "Successfully test $io_counts I/O of size $io_size ."

        rm -f $c2t1fs_file

        echo "Unmounting file system ..."
        umount $c2t1fs_mount_dir &>/dev/null

        echo "Cleaning up test directory..."
        rm -rf $c2t1fs_mount_dir &>/dev/null

        echo "Removing colibri module..."
        rmmod kcolibri.ko &>/dev/null

        # Removes the stob files created in stob domain since
        # there is no support for c2_stob_delete() and after unmounting
        # the client file system, from next mount, fids are generated
        # from same baseline which results in failure of cob_create fops.
        rm -rf $COLIBRI_STOB_PATH/o/*

        return 0
}

io_combinations()
{
        # This test run for different different strip_size values 4K,8K,12K,16K,20K,24K,28K,32K

        pool_width=$1
        data_units=$2
        parity_units=$3

        p=`expr $data_units + 2 '*' $parity_units`
        if [ $p -gt $pool_width ]
        then
                echo "Error : pool_width >=  data_units + 2 * parity_units."
                return 1
        fi

        # Since current I/O supports full stripe I/O,
        # I/O sizes are multiple of stripe size

        # stripe size is in K
        for strip_size_multiplyer in 4 12 20 28
        do
            # Small I/Os KBs
            for ((io_size_multiplyer=1; io_size_multiplyer<=8; io_size_multiplyer++))
            do
                strip_size=`expr $strip_size_multiplyer '*' 1024`
                io_size=`expr $io_size_multiplyer '*' $strip_size`
                # I/O size in K
                io_size=`expr $io_size / 1024`K
                echo "Test : I/O for stripe_size = $strip_size, io_size = $io_size, Number of I/Os = 1."
                bulkio_test $strip_size $io_size 1 $pool_width $data_units $parity_units &>> $COLIBRI_TEST_LOGFILE
                if [ $? -ne "0" ]
                then
                        return 1
                fi

                # Multiple I/Os
                echo "Test : I/O for stripe_size = $strip_size, io_size = $io_size, Number of I/Os = 2."
                bulkio_test $strip_size $io_size 2 $pool_width $data_units $parity_units &>> $COLIBRI_TEST_LOGFILE
                if [ $? -ne "0" ]
                then
                        return 1
                fi
            done

            one_mb=`expr 1024 '*' 1024`
            size_multiplyer=256
            # Large I/Os MBs
            for ((io_size_multiplyer=1; io_size_multiplyer<=8; io_size_multiplyer++))
            do
                #Making I/O size in MBs by multiplying with size_multiplyer
                strip_size=`expr $strip_size_multiplyer '*' 1024`
                io_size=`expr $io_size_multiplyer '*' $strip_size '*' $size_multiplyer`
                # I/O size in M
                io_size=`expr $io_size / $one_mb`M
                echo "Test : I/O for stripe_size = $strip_size, io_size = $io_size, Number of I/Os = 1."
                bulkio_test $strip_size $io_size 1 $pool_width $data_units $parity_units &>> $COLIBRI_TEST_LOGFILE
                if [ $? -ne "0" ]
                then
                        return 1
                fi


                # Multiple I/Os
                echo "Test : I/O for stripe_size = $strip_size, io_size = $io_size, Number of I/Os = 2."
                bulkio_test $strip_size $io_size 2 $pool_width $data_units $parity_units &>> $COLIBRI_TEST_LOGFILE
                if [ $? -ne "0" ]
                then
                        return 1
                fi


            done

        done
        echo "Test log available at $COLIBRI_TEST_LOGFILE."
        return 0
}

main()
{
	mkdir $COLIBRI_C2T1FS_TEST_DIR

        io_combinations $POOL_WIDTH 1 1
        if [ $? -ne "0" ]
        then
                echo "Failed : IO failed.."
        fi
        return 0
}

insmod $COLIBRI_CORE_ROOT/../galois/src/linux_kernel/galois.ko
main
rmmod galois
