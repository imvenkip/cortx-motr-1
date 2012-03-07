#!/bin/bash

logfile=`pwd`/bulkio_`date +"%d-%m-%Y_%T"`.log

#  This function test Bulk I/O
#  1. Insert c2t1fs module.
#  2. Mount c2t1fs file system.
#  3. Create local file1 with random data of specified I/O size.
#  4. Write local file data to c2t1fs file.
#  5. Read c2t1fs file to local file2.
#  6. Compare local file1 & file2.
#  7. Unmount file system.
#  8. Remove c2t1fs module.

bulkio_test()
{
        strip_size=$1
        io_size=$2
        io_counts=$3
        pool_width=$4
        data_units=$5
        parity_units=$6
        
        colibri_module=kcolibri
        colibri_module_path=$COLIBRI_CORE/build_kernel_modules
        c2t1fs_mount_dir=/tmp/test_c2t1fs_`date +"%d-%m-%Y_%T"`
        io_service=$COLIBRI_SERVER_END_POINT
        local_input=$C2T1FS_IO_TEST_DIR/file1.data
        local_output=$C2T1FS_IO_TEST_DIR/file2.data
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
        insmod $colibri_module_path/$colibri_module.ko
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
        
        echo "Unmounting file system ..."
        umount $c2t1fs_mount_dir &>/dev/null
        
        echo "Cleaning up test directory..."
        rm -rf $c2t1fs_mount_dir &>/dev/null
        
        echo "Removing colibro module..."
        rmmod kcolibri.ko &>/dev/null

        return 0 
}

# This test run for different different strip_size values 4K,8K,12K,16K,20K,24K,28K,32K

pool_width=$1
data_units=$2
parity_units=$3

p=`expr $data_units + 2 '*' $parity_units`
if [ $p -gt $pool_width ]
then
        echo "Error : pool_width >=  data_units + 2 * parity_units."
        exit 1
fi

# Since current I/O supports full stripe I/O,
# I/O sizes are multiple of stripe size

# stripe size is in K
for strip_size_multiplyer in 4 8 # 12 16 20 24 28 32 
do
    # Small I/Os KBs
    for ((io_size_multiplyer=1; io_size_multiplyer<=8; io_size_multiplyer++))
    do
        strip_size=`expr $strip_size_multiplyer '*' 1024`
        io_size=`expr $io_size_multiplyer '*' $strip_size` 
        # I/O size in K 
        io_size=`expr $io_size / 1024`K
        echo "Test : I/O for stripe_size = $strip_size, io_size = $io_size, Number of I/Os = 1 ..."
        bulkio_test $strip_size $io_size 1 $pool_width $data_units $parity_units &>> $logfile
        if [ $? -eq "0" ]
        then
                echo "Passed."
        else
                echo "failed."
                exit 1
        fi

        # Multiple I/Os
        echo "Test : I/O for stripe_size = $strip_size, io_size = $io_size, Number of I/Os = 2 ..."
        bulkio_test $strip_size $io_size 2 $pool_width $data_units $parity_units &>> $logfile
        if [ $? -eq "0" ]
        then
                echo "Passed."
        else
                echo "failed."
                exit 1
        fi
    done

    one_mb=`expr 1024 '*' 1024`
    size_multiplyer=256
    # Large I/Os KBs
    for ((io_size_multiplyer=1; io_size_multiplyer<=8; io_size_multiplyer++))
    do
        #Making I/O size in MBs by multiplying with size_multiplyer
        strip_size=`expr $strip_size_multiplyer '*' 1024`
        io_size=`expr $io_size_multiplyer '*' $strip_size '*' $size_multiplyer`
        # I/O size in M 
        io_size=`expr $io_size / $one_mb`M
        echo "Test : I/O for stripe_size = $strip_size, io_size = $io_size, Number of I/Os = 1 ..."
        bulkio_test $strip_size $io_size 1 $pool_width $data_units $parity_units &>> $logfile
        if [ $? -eq "0" ]
        then
                echo "Passed."
        else
                echo "failed."
                exit 1
        fi

        # Multiple I/Os
        echo "Test : I/O for stripe_size = $strip_size, io_size = $io_size, Number of I/Os = 2 ..."
        bulkio_test $strip_size $io_size 2 $pool_width $data_units $parity_units &>> $logfile
        if [ $? -eq "0" ]
        then
                echo "Passed."
        else
                echo "failed."
                exit 1
        fi
    done

done

echo "Test log available at $logfile ."

exit 0

