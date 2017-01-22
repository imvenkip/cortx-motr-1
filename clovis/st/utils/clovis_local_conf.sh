#!/bin/bash

# Get local address and other parameters to start services
modprobe lnet &>> /dev/null
lctl network up &>> /dev/null
LOCAL_NID=`lctl list_nids | head -1`
if [ X$LOCAL_NID == X ]; then
	echo "lnet is not up"
	exit
fi

if [ X$LOCAL_EP == X ]; then
	LOCAL_EP=$LOCAL_NID:12345:33:100
fi

if [ X$CONFD_EP == X ]; then
	CONFD_EP=$LOCAL_NID:12345:33:100
fi

if [ X$HA_EP == X ]; then
	HA_EP=$LOCAL_NID:12345:34:1
fi

if [ X$PROF_OPT == X ]; then
	PROF_OPT='<0x7000000000000001:0>'
fi

if [ X$PROC_FID == X ]; then
	PROC_FID='<0x7200000000000000:0>'
fi