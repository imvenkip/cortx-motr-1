#!/bin/bash

if [ "$#" -lt 1 ]
then
        echo "Usage :$0 <pool_width>"
        exit 0
fi

stobutilpath=$COLIBRI_CORE/stob/ut/stobutil

stob_domain=$STOB_DOMAIN
pool_width=$1

db_path=$C2T1FS_IO_TEST_DIR/db
stob_path=$C2T1FS_IO_TEST_DIR/stobs
#Global fid = <0,3>
stobid_lo=3

echo "Cleaning up stob directory ..."
rm -rf $stob_path &> /dev/null

echo "Cleaning up database directory ..."
rm -rf $db_path  &> /dev/null

echo "Creating test directory ..."
mkdir $C2T1FS_IO_TEST_DIR &> /dev/null

echo "Setting up stobs for I/O ..."
for ((  stobid_hi = 1 ;  stobid_hi <= $pool_width;  stobid_hi++  ))
do

        stobid=$stobid_hi:$stobid_lo
        $stobutilpath -c -t $stob_domain -d $db_path -p $stob_path -s $stobid 
        if [ $? -ne "0" ]
        then
                echo "Stob $stobid creation failed."
                exit 1
        else
                echo "Stob $stobid created."
        fi

done

exit 0
