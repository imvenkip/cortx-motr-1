#!/bin/bash

# Get local address and other parameters to start services
modprobe lnet &>> /dev/null
lctl network up &>> /dev/null
LOCAL_NID=`lctl list_nids | head -1`
if [ X$LOCAL_NID == X ]; then
	echo "lnet is not up"
	exit
fi

LOCAL_EP=$LOCAL_NID:12345:44:101
CONFD_EP=10.201.10.58@tcp:12345:44:101
HA_EP=10.201.10.58@tcp:12345:45:1
PROF_OPT='<0x7000000000000001:0>'
PROC_FID='<0x7200000000000000:0>'
