#!/bin/sh

if [ $# -lt "0" ]
then
	echo "Usage : $0"
        exit 1
fi

. `dirname $0`/common.sh
. `dirname $0`/c2t1fs_common_inc.sh
. `dirname $0`/c2t1fs_client_inc.sh

main()
{
	io_combinations $POOL_WIDTH 1 1
        if [ $? -ne "0" ]
        then
                echo "Failed : IO failed.."
        fi
        return 0
}

trap unprepare EXIT

prepare
main
