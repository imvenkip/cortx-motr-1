#!/bin/sh

if [ $# -lt "1" ]
then
	echo "Usage : $0 <start|stop>"
        exit 1
fi

echo 8 > /proc/sys/kernel/printk
export C2_TRACE_IMMEDIATE_MASK=1

. `dirname $0`/c2t1fs_common_inc.sh
. `dirname $0`/c2t1fs_server_inc.sh

colibri_service $1
if [ $? -ne "0" ]
then
	echo "Failed to trigger Colibri Service."
	exit 1
fi
